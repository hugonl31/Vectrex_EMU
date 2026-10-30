#!/usr/bin/env python3
"""vxbot.py - the author's Telegram bot on the meeting point's machine.

Commands (only from TELEGRAM_CHAT, everyone else is ignored):
  /status          services up?, switches, today's numbers, the commands
  /live on|off     a message for every hosted game (game name, public/private, a public game's gamer - never a code)
  /daily on|off    the daily summary (vxdaily.py, 11:00)
  /debug on|off    a message for every debug report (vxreport.py)
  /today           the daily summary now (last 24 hours)
  /report R-XXXXXX that report's zip again
Plus: a message when vxrelay or vxreport stops, and when it runs again.

The switches live in STATE (world-readable: vxreport.py and vxdaily.py read them). Token and chat in
/etc/vxreport.env. Install: the unit
vxbot.service (root: reads the journal and the reports; ProtectSystem=strict, only /var/lib/vxbot writable) with EnvironmentFile=/etc/vxreport.env, StateDirectory=vxbot, StateDirectoryMode=0755.
"""
import os, re, json, time, queue, threading, subprocess, urllib.request, urllib.parse, secrets

TOKEN, CHAT = os.environ.get('TELEGRAM_TOKEN', ''), os.environ.get('TELEGRAM_CHAT', '')
STATE = os.environ.get('VXBOT_STATE', '/var/lib/vxbot/state.json')
REPORTS = os.environ.get('VXREPORT_DIR', '/var/lib/private/vxreports')
API = 'https://api.telegram.org/bot%s/' % TOKEN
HOST = re.compile(r'^(\w+) host ([\d.:a-f\[\]]+):\d+ (.*?)( \(public: (.*)\))?$')
DEFAULT = {'live': True, 'daily': True, 'debug': True}
out = queue.Queue()


def load():
    try:
        return dict(DEFAULT, **json.load(open(STATE)))
    except (OSError, ValueError):
        return dict(DEFAULT)


def save(st):
    tmp = STATE + '.tmp'
    with open(tmp, 'w') as f:
        json.dump(st, f)
    os.chmod(tmp, 0o644); os.replace(tmp, STATE)


def call(method, data=None, files=None, timeout=30):
    if files:
        b = '----vx' + secrets.token_hex(8); parts = []
        for k, v in (data or {}).items():
            parts.append(('--%s\r\nContent-Disposition: form-data; name="%s"\r\n\r\n%s\r\n' % (b, k, v)).encode())
        for k, (name, blob) in files.items():
            parts.append(('--%s\r\nContent-Disposition: form-data; name="%s"; filename="%s"\r\n'
                          'Content-Type: application/zip\r\n\r\n' % (b, k, name)).encode() + blob + b'\r\n')
        parts.append(('--%s--\r\n' % b).encode())
        req = urllib.request.Request(API + method, data=b''.join(parts), headers={'Content-Type': 'multipart/form-data; boundary=' + b})
    else:
        req = urllib.request.Request(API + method, data=urllib.parse.urlencode(data or {}).encode())
    return json.loads(urllib.request.urlopen(req, timeout=timeout).read())


def sender():
    """one message at a time, at most one a second (Telegram's limit for a chat); a burst of live games is summed up"""
    while True:
        item = out.get()
        if isinstance(item, str) and item.startswith('Hosted'):
            more = []
            time.sleep(1.0)
            while not out.empty() and len(more) < 50:
                more.append(out.get())
            hosted = [item] + [m for m in more if isinstance(m, str) and m.startswith('Hosted')]
            rest = [m for m in more if not (isinstance(m, str) and m.startswith('Hosted'))]
            item = hosted[0] if len(hosted) == 1 else '%d games hosted:\n' % len(hosted) + '\n'.join(h[len('Hosted '):] for h in hosted[:20])
            for m in rest:
                out.put(m)
        try:
            if isinstance(item, tuple):
                call('sendDocument', {'chat_id': CHAT, 'caption': item[0]}, {'document': (item[1], item[2])})
            else:
                call('sendMessage', {'chat_id': CHAT, 'text': item[:4000]})
        except Exception as e:   # noqa
            print('send failed: %s' % type(e).__name__, flush=True)
        time.sleep(1.1)


def live():
    """vxrelay's log as it is written: every hosted game, when live is on"""
    while True:
        p = subprocess.Popen(['journalctl', '-u', 'vxrelay', '-f', '-n', '0', '-o', 'cat'], stdout=subprocess.PIPE, text=True)
        for ln in p.stdout:
            m = HOST.match(ln.strip())
            if m and load()['live']:
                game = os.path.splitext(m.group(3))[0] or '?'
                out.put('Hosted %s: %s' % ('public' if m.group(4) else 'private',
                                           game + (', by %s' % m.group(5) if m.group(4) else '')))
        time.sleep(5)


def active(u):
    return subprocess.run(['systemctl', 'is-active', u], capture_output=True, text=True).stdout.strip()


def watchdog():
    """a message when the meeting point or the report receiver stops, and when it runs again"""
    was = {}
    while True:
        for u, name in (('vxrelay', 'Meeting point'), ('vxreport', 'Report receiver')):
            a = active(u)
            if u in was and (a == 'active') != (was[u] == 'active'):
                out.put('%s %s' % (name, 'is running again' if a == 'active' else 'STOPPED (%s)' % a))
            was[u] = a
        time.sleep(60)


def today_text():
    env = dict(os.environ, VXDAILY_PRINT='1')
    return subprocess.run(['python3', '/usr/local/bin/vxdaily.py'], capture_output=True, text=True, env=env).stdout.strip() or 'no summary'


def status_text():
    st = load()
    onoff = lambda k: 'on' if st[k] else 'off'
    up = lambda u: 'running' if active(u) == 'active' else 'DOWN (%s)' % active(u)
    disk = os.statvfs('/'); free = disk.f_bavail * disk.f_frsize / 2 ** 30
    return '\n'.join([
        'Meeting point: %s' % up('vxrelay'),
        'Report receiver: %s' % up('vxreport'),
        'Daily summary timer: %s' % up('vxdaily.timer'),
        'Live %s, daily %s, debug %s' % (onoff('live'), onoff('daily'), onoff('debug')),
        'Disk free: %.1f GB' % free,
        '',
        '/status  this',
        '/live on|off  a message per hosted game',
        '/daily on|off  the 11:00 summary',
        '/debug on|off  a message per debug report',
        '/today  the summary now',
        '/report R-XXXXXX  that report again'])


def command(text):
    w = text.strip().split()
    if not w:
        return None
    cmd = w[0].split('@')[0].lower(); arg = w[1].lower() if len(w) > 1 else ''
    if cmd in ('/status', '/start', '/help'):
        return status_text()
    if cmd in ('/live', '/daily', '/debug'):
        k = cmd[1:]; st = load()
        if arg in ('on', 'off'):
            st[k] = arg == 'on'; save(st)
        return '%s is %s' % (k.capitalize(), 'on' if st[k] else 'off')
    if cmd == '/today':
        return today_text()
    if cmd == '/report':
        num = (w[1] if len(w) > 1 else '').upper()
        if not re.match(r'^R-[A-Z0-9]{6}$', num):
            return 'Use /report R-XXXXXX'
        f = os.path.join(REPORTS, num + '.zip')
        try:
            out.put(('Report %s' % num, num + '.zip', open(f, 'rb').read())); return None
        except OSError:
            return 'No report %s' % num
    return 'Unknown command. /status shows them all.'


def main():
    if not os.path.exists(STATE):
        save(dict(DEFAULT))
    for f in (sender, live, watchdog):
        threading.Thread(target=f, daemon=True).start()
    off = None
    while True:
        try:
            r = call('getUpdates', {'timeout': 50, **({'offset': off} if off else {})}, timeout=60)
        except Exception as e:   # noqa
            print('poll failed: %s' % type(e).__name__, flush=True); time.sleep(10); continue
        for u in r.get('result', []):
            off = u['update_id'] + 1
            msg = u.get('message') or {}
            if str(msg.get('chat', {}).get('id')) != CHAT or not msg.get('text', '').startswith('/'):
                continue                                # (only the author's chat)
            try:
                reply = command(msg['text'])
            except Exception as e:   # noqa
                reply = 'Error: %s' % type(e).__name__
            if reply:
                out.put(reply)


if __name__ == '__main__':
    main()
