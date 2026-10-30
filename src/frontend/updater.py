"""The one-click update (the owner, 2026-10-29): the newest release's zip for this system downloaded and unpacked to a
temporary folder; a small script waits for the emulator to close, puts the new program in place of the old one, adds
the new release's files that are missing in system, overlays, roms and uvmc2 (the player's ROMs, overlays, SD cards and
BIOS are never overwritten), writes fresh settings - keeping only the overlays' places and sizes, the player name and
its key, the SD folders and the recent games (the old settings stay as a backup next to them) - and starts the new
version.

Not when run from the sources, nor when the program's folder cannot be written (Program Files): then the release page."""
import json, os, shutil, subprocess, sys, tempfile, time, urllib.request, zipfile

IS_WIN, IS_MAC = sys.platform == 'win32', sys.platform == 'darwin'
ASSET = 'Vectrex_EMU-windows.zip' if IS_WIN else 'Vectrex_EMU-mac-app.zip' if IS_MAC else 'Vectrex_EMU-linux-bin.zip'
PROGRAM = 'Vectrex_EMU.exe' if IS_WIN else 'Vectrex_EMU.app' if IS_MAC else 'Vectrex_EMU'
FOLDERS = ('system', 'overlays', 'roms', 'uvmc2', 'pitrex')     # (next to the program: only what is missing is added)
# what a fresh settings file keeps: where the overlays sit and how large (per game and the global default), who the
# player is online, where the SD cards are, the recent games - not the look, the sound, the controls
KEEP = ('overlay_adjust', 'overlay_adjust2', 'overlay_adjust_all', 'overlay_adjust_game', 'overlay_for',
        'gamer_name', 'gamer_ok', 'player_key', 'uvm2_sd_dir', 'pitrex_sd_dir', 'recent', 'last_rom', 'setup_done',
        'downloads_agreed', 'update_check')


def can_update(root):
    """None when this program can update itself, else why not (a sentence)"""
    if not getattr(sys, 'frozen', False):
        return 'Run from the sources: update with git pull (or download the new version).'
    try:
        t = os.path.join(root, '.update_test'); open(t, 'w').close(); os.remove(t)
    except OSError:
        return "This program's folder cannot be written (it is in Program Files?): download the new version instead."
    if IS_MAC and not os.path.isdir(os.path.join(root, PROGRAM)):
        return 'Vectrex_EMU.app was not found next to its folders: download the new version instead.'
    return None


def asset_url(release):
    """this system's zip in a release (the GitHub API's JSON), or None"""
    for a in release.get('assets', []) or []:
        if a.get('name') == ASSET:
            return a.get('browser_download_url'), int(a.get('size') or 0)
    return None


def download(url, size, dest, progress, user_agent):
    """the zip to dest; progress(done, total) now and then"""
    req = urllib.request.Request(url, headers={'User-Agent': user_agent})
    done = 0
    with urllib.request.urlopen(req, timeout=30) as r, open(dest, 'wb') as f:
        total = int(r.headers.get('Content-Length') or size or 0)
        while True:
            b = r.read(1 << 16)
            if not b:
                break
            f.write(b); done += len(b)
            progress(done, total)
    if total and done != total:
        raise OSError('the download ended early (%d of %d bytes)' % (done, total))


def fresh_settings(settings_f):
    """the new settings: only what KEEP names, from the old file (empty when there is none)"""
    try:
        old = json.load(open(settings_f, encoding='utf-8'))
    except Exception:   # noqa
        return {}
    return {k: old[k] for k in KEEP if k in old}


def prepare(zip_path, root, settings_f, pid, tag, exe_path=None):
    """unpacks the new release next to the zip and writes the script that installs it once this process (pid) has
    ended; returns the command that starts that script (detached)"""
    work = os.path.dirname(zip_path)
    new = os.path.join(work, 'new')
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(new)
    if IS_MAC:                                      # (zipfile drops the execute bits: the app's binaries get them back)
        for d, _, fs in os.walk(os.path.join(new, PROGRAM)):
            for f in fs:
                p = os.path.join(d, f)
                if '/MacOS/' in p or f.endswith(('.so', '.dylib')) or 'vectrex_core' in f:
                    os.chmod(p, 0o755)
    elif not IS_WIN:
        for f in (PROGRAM,):
            if os.path.exists(os.path.join(new, f)):
                os.chmod(os.path.join(new, f), 0o755)
    if not os.path.exists(os.path.join(new, PROGRAM)):
        raise OSError('the download has no %s' % PROGRAM)
    st = fresh_settings(settings_f)
    stf = os.path.join(work, 'settings.json')
    json.dump(st, open(stf, 'w', encoding='utf-8'), indent=1)
    backup = os.path.join(os.path.dirname(settings_f), 'settings_before_%s.json' % tag)
    log = os.path.join(work, 'update.log')
    if IS_WIN:
        root, settings_f, work, new, stf = (os.path.normpath(x) for x in (root, settings_f, work, new, stf))
        backup = os.path.normpath(backup)
        exe = os.path.normpath(exe_path or os.path.join(root, PROGRAM))   # (the program as it is run: renamed or not)
        log = os.path.normpath(log)
        lines = ['@echo off', 'setlocal',
                 r'set S=%SystemRoot%\System32',          # (Windows' own tools by their full path: not another find)
                 ':wait', r'"%%S%%\tasklist.exe" /FI "PID eq %d" /NH 2>nul | "%%S%%\find.exe" " %d " >nul && ('
                 r'"%%S%%\PING.EXE" -n 2 127.0.0.1 >nul & goto wait)' % (pid, pid),
                 r'"%S%\PING.EXE" -n 2 127.0.0.1 >nul']
        lines += ['set n=0', ':copy',                # (the old exe can stay locked a moment after it ended: tried again)
                  'copy /y "%s" "%s" >nul 2>>"%s" && goto copied' % (os.path.join(new, PROGRAM), exe, log),
                  'set /a n+=1', r'if %n% lss 20 ("%S%\PING.EXE" -n 2 127.0.0.1 >nul & goto copy)',
                  'echo the program could not be replaced >>"%s"' % log, 'goto start', ':copied']
        for f in FOLDERS:                             # (/XC /XN /XO: only files that are not there yet)
            lines.append(r'if exist "%s" "%%S%%\robocopy.exe" "%s" "%s" /E /XC /XN /XO /NFL /NDL /NJH /NJS /NP >nul' % (
                os.path.join(new, f), os.path.join(new, f), os.path.join(root, f)))
        if os.path.exists(settings_f):
            lines.append('copy /y "%s" "%s" >nul' % (settings_f, backup))
        lines += ['copy /y "%s" "%s" >nul' % (stf, settings_f), ':start',
                  'start "" "%s"' % exe,
                  'rmdir /s /q "%s"' % work,
                  '(goto) 2>nul & del "%~f0"']                # (the script removes itself last)
        script = os.path.join(tempfile.gettempdir(), 'vectrex_emu_update_%d.cmd' % os.getpid())
        open(script, 'w', newline='\r\n').write('\n'.join(lines) + '\n')
        return ['cmd.exe', '/c', script], script
    q = lambda s: "'" + s.replace("'", "'\\''") + "'"
    lines = ['#!/bin/sh',                             # (until the emulator has ended - Linux: a zombie counts as ended)
             'while kill -0 %d 2>/dev/null; do [ -r /proc/%d/stat ] && grep -q "^%d ([^)]*) Z" /proc/%d/stat && break; '
             'sleep 0.5; done' % (pid, pid, pid, pid), 'sleep 0.5']
    if IS_MAC:
        app = os.path.join(root, PROGRAM)
        lines += ['rm -rf %s.old && mv %s %s.old && ditto %s %s && rm -rf %s.old' % (
            q(app), q(app), q(app), q(os.path.join(new, PROGRAM)), q(app), q(app)),
                  'xattr -dr com.apple.quarantine %s 2>/dev/null' % q(app)]
    else:
        exe = exe_path or os.path.join(root, PROGRAM)
        lines += ['cp -f %s %s.new && mv -f %s.new %s && chmod 755 %s' % (
            q(os.path.join(new, PROGRAM)), q(exe), q(exe), q(exe), q(exe))]
    for f in FOLDERS:                                 # (cp -n: never over a file that is there)
        src = os.path.join(new, f)
        lines.append('[ -d %s ] && mkdir -p %s && cp -R -n %s/. %s/ 2>/dev/null' % (q(src), q(os.path.join(root, f)), q(src),
                                                                                   q(os.path.join(root, f))))
    if os.path.exists(settings_f):
        lines.append('cp -f %s %s' % (q(settings_f), q(backup)))
    lines.append('cp -f %s %s' % (q(stf), q(settings_f)))
    lines.append(('open %s' % q(os.path.join(root, PROGRAM))) if IS_MAC else
                 '(cd %s && nohup %s >/dev/null 2>&1 &)' % (q(root), q(exe)))
    lines.append('rm -rf %s' % q(work))
    lines.append('rm -f "$0"')
    script = os.path.join(tempfile.gettempdir(), 'vectrex_emu_update_%d.sh' % os.getpid())
    open(script, 'w').write('\n'.join(lines) + '\n'); os.chmod(script, 0o755)
    return ['/bin/sh', script], script


def start(cmd):
    """the install script, detached: it outlives this process"""
    if IS_WIN:
        NEW_GROUP, NO_WINDOW = 0x00000200, 0x08000000   # (not DETACHED_PROCESS: cmd without a console ends at once;
        subprocess.Popen(cmd, creationflags=NEW_GROUP | NO_WINDOW, close_fds=True)   #  it outlives the emulator anyway)
    else:
        subprocess.Popen(cmd, start_new_session=True, close_fds=True,
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def work_folder():
    return tempfile.mkdtemp(prefix='vectrex_emu_update_')


def _vkey(s):
    import re
    n = re.findall(r'\d+', s or '')
    return tuple(int(x) for x in n[:3]) if n else None


def highlights(body, mine, limit=7, width=74, always=False):
    """the new release's main points for the message (the owner, 2026-10-29: what it brings, so you want it): from the
    release notes (GitHub's body), every '**New in vX...**' block newer than this version (mine), a line per point - its
    bold title and the start of what follows, cut to fit the window. Fixes left out; at most limit lines, then a last
    one saying there is more. always: the newest block even when it is not newer (tests)"""
    import re
    clean = lambda t: re.sub(r'\[([^\]]+)\]\([^)]*\)', r'\1', t).replace('**', '').replace('`', '').replace('*', '').strip()
    out, take, mk = [], False, _vkey(mine)
    bullet = None
    def flush():
        if bullet is None:
            return
        m = re.match(r'\*\*(.+?)\*\*\s*(.*)', bullet)
        if not m:
            return
        title, rest = clean(m.group(1)), clean(m.group(2))
        if title.lower().startswith('fix'):
            return
        rest = re.sub(r'^\([^)]*\)\s*', '', rest).lstrip('-: ').strip()
        line = title
        if rest and len(title) < width - 12:
            room = width - len(title) - 3
            clause = re.split(r'[:;.(]', rest)[0].strip()
            if len(clause) > room:
                clause = clause[:room - 3].rsplit(' ', 1)[0].rstrip(',') + '...'
            if clause:
                line = title + ' - ' + clause
        out.append(line[:width])
    newest = True
    for raw in (body or '').replace('\r', '').split('\n'):
        s = raw.strip()
        h = re.match(r'^\*\*New in (.+?)\*\*', s)
        if h or (s.startswith('**') and not s.startswith('* ')):
            flush(); bullet = None
            vs = [_vkey(v) for v in re.findall(r'v\d+\.\d+(?:\.\d+)?', h.group(1))] if h else []
            v = max([x for x in vs if x] or [None]) if vs else None
            take = bool(v) and ((mk is not None and v > mk) or (always and newest))
            if h:
                newest = False
            continue
        if not take:
            continue
        if s.startswith('* '):
            flush(); bullet = s[2:]
        elif bullet is not None and s:
            bullet += ' ' + s
    flush()
    if len(out) > limit:
        out = out[:limit - 1] + ['...and more on GitHub']
    return out


NEWS_URL = 'https://raw.githubusercontent.com/hugonl31/Vectrex_EMU/%s/WHATS_NEW.md'   # (at the release's tag)


def fetch_news(tag, user_agent):
    """WHATS_NEW.md as it is in that release (the short list), or '' when it cannot be read"""
    try:
        req = urllib.request.Request(NEWS_URL % tag, headers={'User-Agent': user_agent})
        with urllib.request.urlopen(req, timeout=8) as r:
            return r.read().decode('utf-8', 'replace')
    except Exception:   # noqa
        return ''


def whats_new(text, mine, limit=7, always=False):
    """the short list for the message from WHATS_NEW.md: the lines of every version newer than this one (mine), newest
    first; at most limit lines, the last one then saying there is more. always: the newest version's even when it is
    not newer (tests)"""
    import re
    out, take, mk, first = [], False, _vkey(mine), True
    for raw in (text or '').replace('\r', '').split('\n'):
        s = raw.strip()
        m = re.match(r'^##\s+(v\d+\.\d+(?:\.\d+)?)', s)
        if m:
            v = _vkey(m.group(1))
            take = bool(v) and ((mk is not None and v > mk) or (always and first))
            first = False
            continue
        if take and s.startswith(('- ', '* ')):
            out.append(s[2:].strip())
    if len(out) > limit:
        out = out[:limit - 1] + ['...and more on GitHub']
    return out
