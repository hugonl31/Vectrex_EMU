"""getter.py - Vectrex > Get games & extras: one click fetches the original GCE games, free homebrew games that have an
overlay, the PiTrex's SD card files and the speech ROM, unpacked and named as the emulator lists them (the overlays come
with the emulator). Each item has a chain of sources, tried in turn: what never changes (the games, the speech ROM) from
the project's own copy first, the PiTrex from the newest file on Malban's page first; then the other sites and copies
(archive.org sets, GitHub, the Wayback Machine). All gone: the row says where to get it by hand, and Add file... takes
that download (a zip, zips in a zip, or the files) and names it the same way.
Nothing of it comes with the emulator: the user downloads it and takes the responsibility for it (DISCLAIMER: shown once,
with every file and its sources; accepted, the downloads go without asking). Files already there are kept."""
import io, json, os, re, threading, urllib.parse, urllib.request, webbrowser, zipfile
import tkinter as tk

V = None                                # (the app's module: set by vectrex_emu)
V_APP = None                            # (the app, for the PiTrex folder)

DISCLAIMER = ("These files are not part of Vectrex_EMU and do not come with it. The maintainer of Vectrex_EMU on GitHub "
              "does not take responsibility for the sharing of these files. You, the user, download them yourself from "
              "the sites named, and you take the responsibility for doing so and for respecting their copyrights, which "
              "belong to their owners. (The original Vectrex games are free for non-commercial use by Smith Engineering's "
              "permission; the homebrew games, the overlays, the PiTrex files and the speech ROM belong to their authors.)")
UA = {'User-Agent': 'Vectrex_EMU'}

# ---------------------------------------------------------------- the games' names
# the original games as the ROM browser shows them; another set's name (archive.org's, TOSEC's, No-Intro's: "Armor..Attack
# (NA)", "Armor Attack (1982)(GCE)", "Armor..Attack (World)") is matched on its letters and digits, its (...) / [...] off
GCE_GAMES = ['3D Crazy Coaster', '3D Mine Storm', '3D Narrow Escape', 'Armor Attack', 'Bedlam', 'Berzerk',
             'Blitz! Action Football', 'Clean Sweep', 'Cosmic Chasm', 'Dark Tower (Proto)', 'Fortress of Narzod',
             'Heads-Up Action Soccer', 'HyperChase Auto Race', 'Lunar Lander (Proto)', 'Mail Plane (Proto)', 'Mine Storm',
             'Mine Storm II', 'Mine Storm III (Proto)', 'Mr. Boston Clean Sweep', "Pitcher's Duel (Proto)", 'Polar Rescue',
             'Pole Position', 'Rip Off', 'Rocket Sledge (Proto)', 'Scramble', 'Solar Quest', 'Space Wars', 'Spike',
             'Spinball', 'Star Castle', 'Star Ship', 'Star Trek - The Motion Picture', 'StarHawk', 'Tour De France (Proto)',
             'Web Wars', 'ZZAP (Proto)']
ALIASES = {'zzapscrollingspacegame': 'ZZAP (Proto)', 'minestorm3': 'Mine Storm III (Proto)', 'hyperchase': 'HyperChase Auto Race',
           'blitz': 'Blitz! Action Football', 'blitzfootball': 'Blitz! Action Football', 'headsup': 'Heads-Up Action Soccer',
           'headsupsoccer': 'Heads-Up Action Soccer', 'startrek': 'Star Trek - The Motion Picture'}


def _key(s):
    return re.sub(r'[^a-z0-9]', '', s.lower())


KNOWN = {_key(re.sub(r'\s*\(Proto\)', '', n)): n for n in GCE_GAMES}
KNOWN.update(ALIASES)

# homebrew games that have an overlay (overlays/) and are free: public domain, or shared by their authors (Telengard on
# Malban's GitHub; Vectorblade and the Neon Hawk demo by the MiSTer project, with their authors' permission - fetched
# from there only). Others with an overlay are sold (Protector & YASI, Harmagedon) or have no free source.
HOMEBREW = ['Moon Lander', 'Nebula Commander', 'Omega Chase', 'Patriots', "Spike Hoppin'", 'Thrust', 'Vectrexians',
            'Web Warp', 'Telengard', 'Vectorblade', 'Neon Hawk (Demo)']
HB_KNOWN = {_key(re.sub(r'\s*\(Demo\)', '', n)): n for n in HOMEBREW}
HB_KNOWN.update({'neonhawkdemo': 'Neon Hawk (Demo)', 'telengard256kflash': 'Telengard', 'vectrexthrust': 'Thrust'})


def hb_name(stem):
    """a homebrew file's name as ours (one of HOMEBREW), else None"""
    low = stem.lower()
    if re.search(r'\[(b|h|a|f|o|t|p)\d*\]', low):
        return None
    t = re.sub(r'^zzz-unk-', '', stem, flags=re.I)
    t = re.sub(r'\s*[\(\[][^\)\]]*[\)\]]', '', t)
    t = re.sub(r'\s+by\s+.*$', '', t, flags=re.I)
    t = re.sub(r'\s+v?\d+(\.\d+)+$', '', t.strip())
    return HB_KNOWN.get(_key(t))


def game_name(stem):
    """a game file's name as ours: None for what is not one to take (bad, hacked, overdumped copies; homebrew demos)"""
    low = stem.lower()
    if re.search(r'\[(b|h|a|f|o|t|p)\d*\]', low) or '(pd)' in low:
        return None
    if 'boston' in low:
        return 'Mr. Boston Clean Sweep'
    title = re.sub(r'\s*[\(\[][^\)\]]*[\)\]]', '', stem).strip()
    k = _key(title)
    if k in KNOWN:
        return KNOWN[k]
    return (title + (' (Proto)' if 'proto' in low else '')) if title else None


# ---------------------------------------------------------------- what is there
# the games' fingerprints (see _fp: their bytes without the padding at the end), so a copy of one under any name, in
# any subfolder, counts as there
KNOWN_SUMS = {
    'a81c9e4ca6c68e42': '3D Crazy Coaster',
    'e3b8594d71d33cc9': '3D Mine Storm',
    '8988075388910d62': '3D Narrow Escape',
    'eb76f860130f8cc8': 'Armor Attack',
    '6efd76b38d603797': 'Bedlam',
    '6e27b509020260e3': 'Berzerk',
    'd4fd15ceed0d4eff': 'Blitz! Action Football',
    '67f8513958c04e93': 'Clean Sweep',
    '56a7f277bf09f944': 'Cosmic Chasm',
    'fbfd58464d42986d': 'Dark Tower (Proto)',
    'bc27411caba93e4d': 'Fortress of Narzod',
    '0406c13952fb0bd6': 'Heads-Up Action Soccer',
    'a246f1d94a5b062b': 'HyperChase Auto Race',
    '4318d453257b3599': 'Lunar Lander (Proto)',
    '05f56e708f752ca8': 'Mail Plane (Proto)',
    '5f756a350f7c163f': 'Mine Storm',
    '39dbec9632885095': 'Mine Storm II',
    'b456780dfbb2a5ec': 'Mine Storm III (Proto)',
    'bc244985be4108c1': 'Moon Lander',
    '765af69f6a231590': 'Mr. Boston Clean Sweep',
    'd30fade000f73db3': 'Nebula Commander',
    '42427dc02a6a25c0': 'Neon Hawk (Demo)',
    'c390f580f841b270': 'Omega Chase',
    '228dc86174f12aca': 'Patriots',
    '4de711490c7edfe1': "Pitcher's Duel (Proto)",
    'a501ef26d670d50b': 'Polar Rescue',
    'b0c0b12cbea918d8': 'Pole Position',
    '22f7a490f6f7aaa3': 'Rip Off',
    '526b347bc02a47c8': 'Rocket Sledge (Proto)',
    '38e38b5c60466146': 'Scramble',
    'b76f14c824344c02': 'Solar Quest',
    '70c26d3974219ef4': 'Space Wars',
    '9a2f8d06286c4315': 'Spike',
    '775d6e5fd438246a': "Spike Hoppin'",
    '2e8285061530ccad': 'Spinball',
    '5444ef96ef9fd758': 'Star Castle',
    'd182eac5a54ffc64': 'Star Ship',
    '3a2ee61316fb316d': 'Star Trek - The Motion Picture',
    '86c6e6039ecdf971': 'StarHawk',
    'ffc1e68c7b7fc721': 'Telengard',
    'a29a9974706b5cff': 'Telengard',
    '451ba51be3f94b3a': 'Thrust',
    '4f461b0b51c82433': 'Tour De France (Proto)',
    '95d4704dd59fe584': 'Vectorblade',
    '9b40748fb9e395f7': 'Vectrexians',
    '8e984cbb6c85d9bc': 'Web Warp',
    '922621cf8e0fa4d8': 'Web Wars',
    '1db76a96327738be': 'ZZAP (Proto)',
}


def _fp(data):
    import hashlib
    return hashlib.sha1(data.rstrip(b'\xff').rstrip(b'\x00')).hexdigest()[:16]


def present():
    """the known games (GCE_GAMES, HOMEBREW) the roms folder has: by our name, by another set's name for it, or by its
    bytes (KNOWN_SUMS) whatever its name"""
    d = os.path.join(V.ROOT, 'roms')
    have = set()
    for dp, dn, fn in (os.walk(d) if os.path.isdir(d) else ()):
        for f in fn:
            s, e = os.path.splitext(f)
            if e.lower() not in ('.bin', '.vec', '.gam', '.rom'):
                continue
            for n in (s, game_name(s), hb_name(s)):
                if n in GCE_GAMES or n in HOMEBREW:
                    have.add(n)
            try:
                if os.path.getsize(os.path.join(dp, f)) <= 1 << 20:
                    with open(os.path.join(dp, f), 'rb') as fh:
                        n = KNOWN_SUMS.get(_fp(fh.read()))
                    if n:
                        have.add(n)
            except OSError:
                pass
    return have


def games_have():
    """(the original games in the roms folder, how many there are)"""
    return len(present() & set(GCE_GAMES)), len(GCE_GAMES)


def homebrew_have():
    return len(present() & set(HOMEBREW)), len(HOMEBREW)


def state(key, app):
    """(complete, text) for the setup window and this one"""
    if key == 'homebrew':
        n, of = homebrew_have()
        return n >= of, '%d of %d free homebrew games' % (n, of)
    if key == 'games':
        n, of = games_have()
        return n >= of, '%d of the %d original games' % (n, of)
    if key == 'pitrex':
        e = os.path.exists(os.path.join(app.px_dir, 'pitrex.img'))
        return e, 'found' if e else 'not there'
    e = bool(V.sp_rom())
    return e, 'found' if e else 'not there'


# ---------------------------------------------------------------- the sources: each a chain of addresses
def _get(url, timeout=30):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=timeout) as r:
        return r.read()


def _ia_search(q, rows=5):
    try:
        r = json.loads(_get('https://archive.org/advancedsearch.php?' + urllib.parse.urlencode(
            {'q': q, 'fl[]': 'identifier', 'sort[]': 'addeddate desc', 'rows': rows, 'output': 'json'})))
        return [d['identifier'] for d in r['response']['docs']]
    except Exception:   # noqa
        return []


def _ia_zip(ident, pick=None):
    """an archive.org item's largest original zip (its name matching `pick`, not extras or patches)"""
    try:
        md = json.loads(_get('https://archive.org/metadata/' + ident))
        zips = [f for f in md.get('files', []) if f.get('name', '').lower().endswith('.zip') and f.get('source') == 'original'
                and not re.search(r'extra|patch', f['name'], re.I) and (not pick or re.search(pick, f['name'], re.I))]
        if zips:
            f = max(zips, key=lambda f: int(f.get('size') or 0))
            return 'https://archive.org/download/%s/%s' % (ident, urllib.parse.quote(f['name']))
    except Exception:   # noqa
        pass
    return None


def _wayback(prefix, pattern):
    """the Wayback Machine's copies of files under `prefix` whose name matches `pattern`, newest first"""
    try:
        rows = _get('https://web.archive.org/cdx/search/cdx?' + urllib.parse.urlencode(
            {'url': prefix, 'matchType': 'prefix', 'fl': 'original,timestamp,length', 'filter': 'statuscode:200'}), 60).decode()
        hits = []
        for ln in rows.splitlines():
            p = ln.split(' ')
            if len(p) == 3 and re.search(pattern, p[0], re.I) and p[2].isdigit() and int(p[2]) > 1000:
                hits.append((p[1], p[0]))
        return ['https://web.archive.org/web/%sid_/%s' % (ts, url) for ts, url in sorted(hits, reverse=True)]
    except Exception:   # noqa
        return []


# the project's own copies (the owner's Google Drive, shared by link), tried first: the games and the speech ROM never
# change. The PiTrex's files do: its own sites first
DRIVE_GAMES = 'https://drive.usercontent.google.com/download?export=download&confirm=t&id=1dcRdUxgMg2yX0ux-izPzcttY0SislQgF'
DRIVE_SPEECH = 'https://drive.usercontent.google.com/download?export=download&confirm=t&id=1L_fWaxPa4KLWnE7-RNCDqmI1clzSXxPP'
# the PiTrex's files as they were last (they get updates): only when its sites fail, on the user's 'Use the older
# version for now'
DRIVE_PITREX = 'https://drive.usercontent.google.com/download?export=download&confirm=t&id=17XNGNgabHEQ0eNc7zzuPhTPxmZ7G4Rri'
DRIVE_PITREX_NAME = 'PiTrex_sd_Jul29_25'
DRIVE_HOMEBREW = 'https://drive.usercontent.google.com/download?export=download&confirm=t&id=1N2lCNS8VSlBQeCqKp40nYoQVegHyhub7'


def chain_games():
    if DRIVE_GAMES:                                                  # the project's own copy: it never changes
        yield DRIVE_GAMES
    yield _ia_zip('vectrex-champion-collection', 'collection')       # the Vectrex Champion Collection: all 36, clean
    for ident in _ia_search('title:(vectrex champion collection)'):  # (moved: its newest copy)
        yield _ia_zip(ident, 'collection')
    yield 'https://archive.org/download/vectrex-champion-collection/Vectrex%20Champion%20Collection.zip'
    yield _ia_zip('gce-vectrex-games-complete')                      # other sets (the games one zip each in some)
    yield _ia_zip('GCE_Vectrex_TOSEC_2012_04_23')
    yield _ia_zip('vectrex_202604')


def chain_homebrew():
    if DRIVE_HOMEBREW:                                               # the project's own copy (they do not change)
        yield DRIVE_HOMEBREW
    m = 'https://raw.githubusercontent.com/Videodr0me/Vectrex_MiSTer/master/games/'
    yield m + 'Vectorblade/Vectorblade%20(v1.13).bin'                # (shared there with the authors' permission:
    yield m + 'NeonHawk/Neon%20Hawk%20Demo%20(v1.0).bin'             #  from there only)
    yield 'https://raw.githubusercontent.com/malbanGit/Telengard/master/Telengard_256kFlash.bin'
    yield _ia_zip('gce-vectrex-games-complete')                      # the public domain ones
    yield _ia_zip('GCE_Vectrex_TOSEC_2012_04_23')
    yield m + 'Telengard/Telengard%20(v1.03).bin'


def chain_pitrex():
    try:                                                             # the newest PiTrex_sd_... on Malban's page
        page = _get('http://vide.malban.de/pitrex/pitrex-baremetal-download').decode('utf8', 'replace')
        ids = []
        for m in re.finditer(r'download_id=(\d+)', page):
            if re.search(r'PiTrex_sd_\w+\.zip', page[m.start():m.start() + 1500], re.I):
                ids.append(int(m.group(1)))
        for i in sorted(set(ids), reverse=True):
            yield 'http://vide.malban.de/?sdm_process_download=1&download_id=%d' % i
    except Exception:   # noqa
        pass
    yield 'http://vide.malban.de/?sdm_process_download=1&download_id=7074'
    yield 'http://vectrex.malban.de/pitrex/PiTrex_sd_Jul29_25.zip'
    for u in _wayback('vectrex.malban.de/pitrex/', r'(pitrex_sd_|sd_drive_)[^/]*\.zip$'):   # (older copies)
        yield u


def chain_speech():
    if DRIVE_SPEECH:                                                 # the project's own copy: it never changes
        yield DRIVE_SPEECH
    yield 'http://spatula-city.org/~im14u2c/sp0256-al2/al2.bin'
    for u in _wayback('spatula-city.org/~im14u2c/sp0256-al2/al2.bin', r'al2\.bin$'):
        yield u


# (key, title, site, its page (to get it by hand), where it goes, size shown, the chain of addresses)
SOURCES = [
    ('games', 'GCE Vectrex games', 'archive.org', 'https://archive.org/details/vectrex-champion-collection', 'roms',
     '0.2 MB', chain_games),
    ('homebrew', 'Homebrew games (free)', 'several', 'https://github.com/Videodr0me/Vectrex_MiSTer/tree/master/games', 'roms',
     '1 MB', chain_homebrew),
    ('pitrex', 'PiTrex SD card', 'Malban', 'http://vide.malban.de/pitrex/pitrex-baremetal-download', 'pitrex',
     '190 MB', chain_pitrex),
    ('speech', 'Speech ROM (al2.bin)', 'Joe Zbiciak', 'http://spatula-city.org/~im14u2c/sp0256-al2/', 'system',
     '2 KB', chain_speech),
]
SRC = {s[0]: s for s in SOURCES}


def fetch(url, progress):
    """the file at url, in memory; progress(done, total) as it comes"""
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
        total = int(r.headers.get('Content-Length') or 0)
        buf = io.BytesIO()
        done = 0
        while True:
            b = r.read(1 << 16)
            if not b:
                break
            buf.write(b); done += len(b); progress(done, total)
    return buf.getvalue()


def _usable(key, data):
    """a download that is what it should be (not an error page, not some other zip)"""
    if key == 'speech':
        return len(data) == 2048
    if not zipfile.is_zipfile(io.BytesIO(data)):
        return False
    names = [n.lower() for n in zipfile.ZipFile(io.BytesIO(data)).namelist()]
    if key == 'pitrex':
        return any(n.endswith('pitrex.img') for n in names)
    return any(n.endswith(('.bin', '.vec', '.zip')) for n in names)


def _usable_any(key, data):
    """a homebrew game comes as a zip or as its own file"""
    if key == 'homebrew' and not zipfile.is_zipfile(io.BytesIO(data)):
        return len(data) >= 1024 and not data[:64].lstrip().startswith((b'<', b'{'))
    return _usable(key, data)


def get_item(key, progress, say, px_dir):
    """the item through its chain: each address until one works (the games: until all of them are there).
    (added, kept); IOError when none did"""
    tried, added, kept, ok = set(), 0, 0, False
    for url in SRC[key][6]():
        if not url or url in tried:
            continue
        tried.add(url)
        say('trying %s...' % urllib.parse.urlsplit(url).netloc)
        try:
            data = fetch(url, progress)
        except Exception:   # noqa
            continue
        if not _usable_any(key, data):
            continue
        a, k = unpack(key, data, px_dir, urllib.parse.unquote(url.rsplit('/', 1)[-1]))
        added += a; kept += k; ok = True
        if key == 'games' and games_have()[0] < len(GCE_GAMES):
            continue                                    # (some still missing: the next source)
        if key == 'homebrew' and homebrew_have()[0] < len(HOMEBREW):
            continue
        break
    if not ok:
        raise IOError('none of its sources worked')
    return added, kept


# ---------------------------------------------------------------- unpacked and named
def _sums(data):
    """a ROM's fingerprints: its bytes, and its bytes without the padding at the end (dumps differ in it)"""
    import hashlib
    return {hashlib.sha1(data).hexdigest(), hashlib.sha1(data.rstrip(b'\xff').rstrip(b'\x00')).hexdigest()}


_OWN = None                             # (the roms folder's games while a download is unpacked: see _owned)


def _owned():
    """the games already in the roms folder (its subfolders too, under any name): (fingerprints, our names)"""
    sums, names = set(), set()
    d = os.path.join(V.ROOT, 'roms')
    for dp, dn, fn in (os.walk(d) if os.path.isdir(d) else ()):
        for f in fn:
            st_, e = os.path.splitext(f)
            if e.lower() not in ('.bin', '.vec', '.gam', '.rom'):
                continue
            try:
                if os.path.getsize(os.path.join(dp, f)) <= 1 << 20:
                    with open(os.path.join(dp, f), 'rb') as fh:
                        sums |= _sums(fh.read())
            except OSError:
                pass
            for n in (st_, game_name(st_), hb_name(st_)):
                if n:
                    names.add(n)
    return sums, names


def _write_new(path, data):
    """a file written only if it is not there yet (yours are kept): 1 if written. A game the roms folder has already -
    the same bytes under another name, or another copy of the same game - is not written a second time"""
    if os.path.exists(path):
        return 0
    if _OWN is not None and os.path.dirname(path) == os.path.join(V.ROOT, 'roms'):
        sums, names = _OWN
        name = os.path.splitext(os.path.basename(path))[0]
        if name in names or _sums(data) & sums:
            return 0
        names.add(name); sums |= _sums(data)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)
    return 1


def place(key, name, data, px_dir):
    """one file of a download (its path inside it, '/'): where it goes, named ours; None to leave it"""
    base = name.rsplit('/', 1)[-1]
    stem, ext = os.path.splitext(base)
    if key == 'games':
        g = game_name(stem) if ext.lower() in ('.vec', '.bin', '.gam', '.rom') else None
        return os.path.join(V.ROOT, 'roms', g + '.bin') if g else None
    if key == 'homebrew':
        g = hb_name(stem) if ext.lower() in ('.vec', '.bin', '.gam', '.rom') else None
        return os.path.join(V.ROOT, 'roms', g + '.bin') if g else None
    if key == 'speech':
        return os.path.join(V.ROOT, 'system', 'al2.bin') if len(data) == 2048 else None
    parts = [p for p in name.split('/') if p not in ('', '.', '..')]
    return os.path.join(px_dir, *parts)


def unpack(key, data, px_dir, name='download', depth=0):
    """a download (a zip - zips in it too, for the games - or one file named `name`) where the emulator looks:
    (files added, files kept)"""
    global _OWN
    if depth == 0 and key in ('games', 'homebrew'):
        _OWN = _owned()
    added = kept = 0
    if zipfile.is_zipfile(io.BytesIO(data)) and not (key == 'pitrex' and depth):
        z = zipfile.ZipFile(io.BytesIO(data))
        names = [i.filename.replace('\\', '/') for i in z.infolist() if not i.is_dir()]
        tops = {n.split('/', 1)[0] for n in names}
        strip = (tops.pop() + '/') if len(tops) == 1 and all('/' in n for n in names) else ''   # (one folder round it all)
        for info in z.infolist():
            if info.is_dir():
                continue
            n = info.filename.replace('\\', '/')
            n = n[len(strip):] if strip and n.startswith(strip) else n
            d = z.read(info)
            if key in ('games', 'homebrew') and n.lower().endswith('.zip') and depth < 2:
                a, k = unpack(key, d, px_dir, n, depth + 1)          # (a game in its own zip)
            else:
                out = place(key, n, d, px_dir)
                a = _write_new(out, d) if out else 0
                k = (1 - a) if out else 0
            added += a; kept += k
    else:
        out = place(key, name, data, px_dir)
        if out:
            w = _write_new(out, data); added += w; kept += 1 - w
    return added, kept


# ---------------------------------------------------------------- the window
def _agreed_all():
    """Download all's disclaimer: agreed to before (it asks once; a single download asks every time)"""
    try:
        return bool(json.load(open(V_APP.settings_f)).get('downloads_agreed'))
    except Exception:   # noqa
        return False


def _remember_agreed():
    try:
        try:
            d = json.load(open(V_APP.settings_f))
        except Exception:   # noqa
            d = {}
        d['downloads_agreed'] = True
        with open(V_APP.settings_f, 'w') as f:
            json.dump(d, f, indent=1)
    except Exception:   # noqa
        pass


WHAT = [('GCE Vectrex games', "the project's copy (Google Drive), archive.org"),
        ('Free homebrew games', "the project's copy (Google Drive), archive.org, GitHub (Malban, MiSTer)"),
        ('PiTrex SD card files', "Malban's site, the Wayback Machine, the project's copy (Google Drive)"),
        ('Speech ROM (al2.bin)', "the project's copy (Google Drive), Joe Zbiciak's site, the Wayback Machine")]


def agreed(parent):
    """the disclaimer, shown once with every file and its sources: True if accepted (now or before - then every
    download goes without asking)"""
    import deck
    if _agreed_all():
        return True
    lines = '\n'.join('- %s: %s' % w for w in WHAT)
    if deck.dialog('warning', 'Downloads: the disclaimer', DISCLAIMER + '\n\nThe files and where they come from:\n' + lines +
                   '\n\nAccept it once, and these downloads go without asking again.', parent=parent, yesno=True,
                   buttons=('Accept', 'Cancel')):
        _remember_agreed()
        return True
    return False


def window(app, only=None, parent=None):
    """the Get games & extras window: what is there (missing: red), per item Download and Add file... (a download made by
    hand), and Download all (what is missing); the disclaimer once, before the first download"""
    global V_APP
    import deck
    from tkinter import filedialog
    a = V_APP = app
    if getattr(a, 'getter_win', None) and a.getter_win.winfo_exists():
        a.getter_win.lift()
        return
    t = tk.Toplevel(parent or a.root, bg=deck.BG)
    a.getter_win = t
    t.title('Get games & extras'); t.transient(parent or a.root); t.resizable(False, False)
    t.lift(); t.focus_force()
    f9, f8, f10b = deck._font(a, 9), deck._font(a, 8), deck._font(a, 10, True)
    top = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); top.pack(fill='x')
    tk.Label(top, text='GET GAMES & EXTRAS', fg=deck.HI, bg=deck.TOPBG, font=f10b, anchor='w').pack(fill='x', padx=14, pady=(8, 0))
    tk.Label(top, text='One click: the newest versions downloaded, unpacked and named for the ROM browser. Files you have are kept.',
             fg=deck.DIM, bg=deck.TOPBG, font=f9, anchor='w').pack(fill='x', padx=14, pady=(0, 8))
    bx = tk.Frame(t, bg=deck.PANEL, highlightthickness=1, highlightbackground=deck.EDGE); bx.pack(fill='x', padx=10, pady=(8, 0))
    grid = tk.Frame(bx, bg=deck.PANEL); grid.pack(fill='x', padx=10, pady=6)
    status, helps, buttons = {}, {}, []
    st = dict(busy=False, frac=0.0, msgs={}, done=True, failed=set(), old_shown=False)

    def show_state(key, extra=''):
        ok, txt = state(key, a)
        status[key].configure(text=txt + extra, fg=deck.G if ok else deck.RED)

    def by_hand(k):
        helps[k].configure(text='Not found on its sites. Get it yourself from %s (click here), then press Add file... and '
                                'pick the download: it goes into %s, named for the ROM browser.' % (SRC[k][3], SRC[k][4]))
        if k == 'pitrex' and DRIVE_PITREX and not st['old_shown']:
            st['old_shown'] = True
            old.grid(row=2 * [s[0] for s in SOURCES].index('pitrex') + 1, column=6, sticky='e', padx=(6, 0))

    def add_file(key):
        fs = filedialog.askopenfilenames(parent=t, title='Add your download: ' + SRC[key][1],
                                         filetypes=[('Downloads', '*.zip *.bin *.vec *.gam *.rom *.img'), ('All files', '*.*')])
        added = 0
        for f in fs:
            try:
                with open(f, 'rb') as fh:
                    added += unpack(key, fh.read(), a.px_dir, os.path.basename(f))[0]
            except Exception:   # noqa
                pass
        if fs:
            show_state(key, '  (%d added)' % added)

    def run(keys):
        for key in keys:
            try:
                st['frac'] = 0.0
                added, kept = get_item(key, lambda d, n: st.update(frac=(d / n) if n else 0.0),
                                       lambda m, key=key: st['msgs'].__setitem__(key, m), a.px_dir)
                st['msgs'][key] = 'done: %d added' % added + (', %d kept' % kept if kept else '')
            except Exception:   # noqa (no network, the sites changed: got by hand)
                st['msgs'][key] = 'not found: get it yourself'
                st['failed'].add(key)
        st['done'] = True

    def use_old():
        """the PiTrex's files as they were last (the project's copy), until its sites have the new ones"""
        if st['busy'] or not agreed(t):
            return
        def go_old():
            try:
                st['msgs']['pitrex'] = 'the older version...'
                data = fetch(DRIVE_PITREX, lambda d, n: st.update(frac=(d / n) if n else 0.0))
                if not _usable('pitrex', data):
                    raise IOError('not a PiTrex SD card')
                added, kept = unpack('pitrex', data, a.px_dir)
                st['msgs']['pitrex'] = 'done: %d added (%s)' % (added, DRIVE_PITREX_NAME)
                st['failed'].discard('pitrex')
            except Exception:   # noqa
                st['msgs']['pitrex'] = 'not found: get it yourself'
            st['done'] = True
        begin()
        threading.Thread(target=go_old, daemon=True).start()
        poll()

    def poll():
        if not t.winfo_exists():
            return
        for k, m in list(st['msgs'].items()):
            if k in st['failed'] or not m.startswith('done'):
                status[k].configure(text=m, fg=deck.RED if k in st['failed'] else deck.G)
            else:
                show_state(k, '  (' + m + ')')
        for k in list(st['failed']):
            by_hand(k)
        cv.delete('all')
        cv.create_rectangle(0, 0, 260 * st['frac'], 12, fill=deck.G, width=0)
        if st['done']:
            st['busy'] = False
            for b in buttons:
                b.configure(state='normal')
            return
        t.after(100, poll)

    def begin():
        st.update(busy=True, done=False, frac=0.0, msgs={})
        for b in buttons:
            b.configure(state='disabled')

    def start(keys):
        if st['busy'] or not keys:
            return
        st['failed'] -= set(keys)
        for k in keys:
            helps[k].configure(text='')
        begin()
        threading.Thread(target=run, args=(keys,), daemon=True).start()
        poll()

    def one(key):
        if not st['busy'] and agreed(t):
            start([key])

    def download_all():                                 # (all that is missing - else all)
        if st['busy']:
            return
        if not agreed(t):
            return
        keys = [k for k in (only or [s[0] for s in SOURCES]) if not state(k, a)[0]] or list(only or [s[0] for s in SOURCES])
        start(keys)

    for i, (key, title, where, page, dest, size, chain) in enumerate(SOURCES):
        r = 2 * i
        tk.Label(grid, text=title, fg=deck.HI, bg=deck.PANEL, font=f9, width=22, anchor='w').grid(row=r, column=1, sticky='w')
        tk.Label(grid, text=where, fg=deck.DIM, bg=deck.PANEL, font=f9, width=12, anchor='w').grid(row=r, column=2, sticky='w')
        tk.Label(grid, text=size, fg=deck.DIM, bg=deck.PANEL, font=f9, width=7, anchor='e').grid(row=r, column=3, sticky='e')
        s = tk.Label(grid, text='', font=f9, width=32, anchor='w', bg=deck.PANEL)
        s.grid(row=r, column=4, sticky='w', padx=(10, 0))
        status[key] = s
        b = deck._button(grid, 'Download', lambda k=key: one(k), a, small=True)
        b.grid(row=r, column=5, sticky='e', padx=(6, 0)); buttons.append(b)
        b = deck._button(grid, 'Add file...', lambda k=key: add_file(k), a, small=True)
        b.grid(row=r, column=6, sticky='e', padx=(6, 0)); buttons.append(b)
        h = tk.Label(grid, text='', fg=deck.WARN, bg=deck.PANEL, font=f8, anchor='w', justify='left', wraplength=640, cursor='hand2')
        h.grid(row=r + 1, column=1, columnspan=5, sticky='w')
        h.bind('<Button-1>', lambda e, p=page: webbrowser.open(p))
        helps[key] = h
        show_state(key)
    old = deck._button(grid, 'Use the older version for now', use_old, a, small=True)
    tk.Label(t, text='The overlays come with the emulator.', fg=deck.DIM, bg=deck.BG, font=f8, anchor='w').pack(fill='x', padx=14, pady=(8, 0))
    tk.Label(t, text=DISCLAIMER, fg=deck.DIM, bg=deck.BG, font=f8, justify='left', wraplength=860,
             anchor='w').pack(fill='x', padx=14, pady=(6, 2))
    bar = tk.Frame(t, bg=deck.BG); bar.pack(fill='x', padx=10, pady=10)
    go = deck._button(bar, 'Download all', download_all, a, on=True)
    go.pack(side='left'); buttons.append(go)
    cv = tk.Canvas(bar, width=260, height=10, bg=deck.PANEL, highlightthickness=1, highlightbackground=deck.EDGE)
    cv.pack(side='left', padx=(12, 0))
    deck._button(bar, 'Close', t.destroy, a).pack(side='right')
    t.bind('<Escape>', lambda e: t.destroy())
    deck.center(t, a.root)                              # (in the middle of the emulator: seen at once)
    if only:                                            # (from the setup's Download everything missing: at once)
        t.after(200, download_all)
