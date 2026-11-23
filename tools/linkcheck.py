"""tools/linkcheck.py - the download links of Vectrex > Get games & extras (src/frontend/getter.py) checked: EVERY source
of every item is tried (not only the first that works) and its download put through getter.check, the same check the
emulator makes before it unpacks anything. Big files (the PiTrex's 190 MB): the first MB (the speed) and the zip's
listing from its last MB (a Range request) - the whole file with --full (the weekly GitHub run: the first Monday of a
month, --full auto). Prints a table: item | source | status | speed | what it is.

  status  ok       works, and is what it should be
          slow     works, under 100 KB/s
          changed  answers, but with another file (the emulator refuses it and tries the next source)
          dead     no answer, an error, a time-out
          blocked  the host turns this computer away (403 / 429 / a quota page: Google Drive and archive.org do so
                   to data centres) - after one retry; not counted as a problem

Exit 1 when an item has NO working source or a game is on none of them (an error); a single source dead or changed
is a warning. --md FILE writes the table for a GitHub issue; on GitHub ($GITHUB_OUTPUT) it sets problems=true|false
(any error or warning).

  python tools/linkcheck.py [--full|--full auto] [--md report.md] [--pause 20]"""
import argparse, datetime, io, os, sys, time, types, urllib.error, urllib.parse, urllib.request, zipfile

try:
    import tkinter  # noqa: F401  (getter's window; not needed here)
except ImportError:
    sys.modules['tkinter'] = types.ModuleType('tkinter')
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'frontend'))
import getter as g  # noqa: E402

BIG = 8 << 20               # bigger: the first MB and the listing only (the whole file with --full)
PART = 1 << 20
TIMEOUT = 60                # each read
DEADLINE = 900              # a whole download
SLOW_BPS = 100e3
BLOCKY = ('drive.usercontent.google.com', 'drive.google.com', 'archive.org')     # (and their subdomains)
# captures known to be broken (left by the emulator too: they fail the check); shown, not counted
KNOWN_BAD = {
    'https://web.archive.org/web/20220630101702id_/http://vectrex.malban.de/pitrex/SD_DRIVE_22th_December_2021.zip':
        'a cut-off capture (1 MB of 54)',
}
PAUSE = 20


def blocky(url):
    h = urllib.parse.urlsplit(url).netloc.lower()
    return any(h == b or h.endswith('.' + b) for b in BLOCKY)


def short(url):
    """a source as the table shows it"""
    for k, v in vars(g).items():
        if k.startswith('DRIVE_') and v == url:
            return 'Google Drive: the project\'s copy' + (' (older version, on request)' if k == 'DRIVE_PITREX' else '')
    if url.startswith(g.MIRROR):
        return 'archive.org: the project\'s copy (%s)' % url[len(g.MIRROR):]
    u = urllib.parse.urlsplit(url)
    if u.netloc == 'web.archive.org':
        p = url.split('id_/', 1)
        return 'Wayback %s: %s' % (url.split('/web/', 1)[1][:8], urllib.parse.unquote(p[-1].rsplit('/', 1)[-1])[:40])
    q = urllib.parse.parse_qs(u.query)
    if 'download_id' in q:
        return '%s download %s' % (u.netloc, q['download_id'][0])
    if 'archive.org' in u.netloc and '/download/' in u.path:
        ident, _, f = urllib.parse.unquote(u.path.split('/download/', 1)[1]).partition('/')
        return ('archive.org %s: %s' % (ident, f))[:80]
    if u.netloc == 'raw.githubusercontent.com':
        p = u.path.strip('/').split('/')
        return 'GitHub %s/%s: %s' % (p[0], p[1], urllib.parse.unquote(p[-1]))
    return (u.netloc + urllib.parse.unquote(u.path))[:70]


# ---------------------------------------------------------------- the chains' own lookups (archive.org, Malban's page,
# the Wayback Machine) watched: one that fails becomes a row
ROWS = []                   # the current item's lookup rows: (source, status, detail)
_orig_get, _orig_ia_zip, _orig_wayback = g._get, g._ia_zip, g._wayback


def _open(url, headers=None, timeout=TIMEOUT):
    """urlopen with one retry after a pause when a host that blocks data centres says 403 / 429 / 503"""
    rq = lambda: urllib.request.Request(url, headers=dict(g.UA, **(headers or {})))   # noqa: E731
    try:
        return urllib.request.urlopen(rq(), timeout=timeout)
    except urllib.error.HTTPError as e:
        if not (blocky(url) and e.code in (403, 429, 503)):
            raise
        time.sleep(PAUSE)
        return urllib.request.urlopen(rq(), timeout=timeout)


def _get(url, timeout=30):
    try:
        with _open(url, timeout=timeout) as r:
            return r.read()
    except Exception as e:
        ROWS.append(('lookup ' + short(url), _fail_status(url, e), _err(e)))
        raise


def _ia_zip(ident, pick=None):
    n = len(ROWS)
    u = _orig_ia_zip(ident, pick)
    if not u and len(ROWS) == n:       # (the item answered, without the zip)
        ROWS.append(('archive.org item ' + ident, 'changed', 'no zip%s in it any more' % (' matching ' + pick if pick else '')))
    return u


def _wayback(prefix, pattern):
    n = len(ROWS)
    r = _orig_wayback(prefix, pattern)
    if not r and len(ROWS) == n:
        ROWS.append(('Wayback list ' + prefix[:50], 'note', 'no captures listed'))
    return r


g._get, g._ia_zip, g._wayback = _get, _ia_zip, _wayback


def _err(e):
    if isinstance(e, urllib.error.HTTPError):
        return 'HTTP %d' % e.code
    return (type(e).__name__ + ': ' + str(getattr(e, 'reason', e)))[:80]


def _fail_status(url, e):
    if isinstance(e, urllib.error.HTTPError) and e.code in (403, 429, 503) and blocky(url):
        return 'blocked'
    return 'dead'


# ---------------------------------------------------------------- a zip's listing from its last MB
class Tail(io.RawIOBase):
    """a file of `size` bytes of which only the end is here (zeros before it): enough for zipfile's listing"""
    def __init__(self, size, tail):
        self.size, self.tail, self.start, self.pos = size, tail, size - len(tail), 0

    def seekable(self):
        return True

    def readable(self):
        return True

    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else self.size + off
        return self.pos

    def tell(self):
        return self.pos

    def readinto(self, b):
        n = min(len(b), self.size - self.pos)
        if n <= 0:
            return 0
        out = bytearray(n)
        lo = max(self.pos, self.start)
        if lo < self.pos + n:
            out[lo - self.pos:] = self.tail[lo - self.start:self.pos + n - self.start]
        b[:n] = out
        self.pos += n
        return n


def listing(url, size):
    """the zip's names from a Range request for its end, None when the server sends no part"""
    with _open(url, {'Range': 'bytes=-%d' % PART}) as r:
        if r.status != 206:
            return None
        tail = r.read(PART + 1)
        cr = r.headers.get('Content-Range', '')
        total = int(cr.rsplit('/', 1)[1]) if '/' in cr and cr.rsplit('/', 1)[1].isdigit() else size
    return zipfile.ZipFile(Tail(total, tail)).namelist()


# ---------------------------------------------------------------- one source
CACHE = {}


def transfer(url, full):
    """the download: dict(status (None: it came), data (whole) or head (its first MB), size, bps, secs, name, err)"""
    if url in CACHE:
        return CACHE[url]
    res = dict(status=None, data=None, head=b'', size=0, bps=0.0, secs=0.0, name='', err='', html=False)
    t0 = time.time()
    try:
        r = _open(url)
    except Exception as e:
        res.update(status=_fail_status(url, e), err=_err(e))
        CACHE[url] = res
        return res
    with r:
        size = int(r.headers.get('Content-Length') or 0)
        res['size'] = size
        res['name'] = urllib.parse.unquote(urllib.parse.urlsplit(r.geturl()).path.rsplit('/', 1)[-1])
        cd = r.headers.get('Content-Disposition') or ''
        if 'filename=' in cd:
            res['name'] = cd.split('filename=', 1)[1].strip('"\'; ')
        res['html'] = 'text/html' in (r.headers.get('Content-Type') or '')
        whole = full or (size and size <= BIG)
        limit = None if whole else (PART if size else BIG + 1)
        buf = io.BytesIO()
        try:
            while True:
                b = r.read(1 << 16)
                if not b:
                    break
                buf.write(b)
                if limit and buf.tell() >= limit:
                    break
                if time.time() - t0 > DEADLINE:
                    raise TimeoutError('over %d s' % DEADLINE)
        except Exception as e:   # noqa
            res.update(status='dead', err=_err(e))
            CACHE[url] = res
            return res
        got = buf.getvalue()
    res['secs'] = max(time.time() - t0, 1e-3)
    res['bps'] = len(got) / res['secs']
    if whole or (not size and len(got) <= BIG):
        res['data'] = got
        res['size'] = res['size'] or len(got)
    else:
        res['head'] = got[:PART]
    CACHE[url] = res
    return res


def names_found(key, names):
    """the item's games in a zip's listing, by their names (a big set without --full)"""
    want = set(g.GCE_GAMES if key == 'games' else g.HOMEBREW)
    out = set()
    for n in names:
        stem, ext = os.path.splitext(n.replace('\\', '/').rsplit('/', 1)[-1])
        if ext.lower() in ('.bin', '.vec', '.gam', '.rom', '.zip'):
            for m in (g.game_name(stem), g.hb_name(stem)):
                if m in want:
                    out.add(m)
    return out


def check_source(key, url, full):
    """(status, speed, detail, the games it has by fingerprint)"""
    if url in KNOWN_BAD:
        return 'known bad', '', KNOWN_BAD[url], set()
    full_here = full and not (urllib.parse.urlsplit(url).netloc == 'web.archive.org')   # (captures do not change)
    r = transfer(url, full_here)
    speed = ''
    if r['bps']:                                # (small files: mostly the time to answer - hence the seconds)
        speed = ('%.1f MB/s' % (r['bps'] / 1e6)) if r['bps'] >= 1e6 else ('%.0f KB/s' % (r['bps'] / 1e3))
        speed += ', %.1f s' % r['secs']
    if r['status']:
        return r['status'], speed, r['err'], set()
    if r['html'] and blocky(url):
        return 'blocked', speed, 'an HTML page instead of the file (a quota or sign-in page)', set()
    has = set()
    if r['data'] is not None:
        ok, what = g.check(key, url, r['data'])
        if ok and key in ('games', 'homebrew'):
            has = g.found(key, r['data'])
        if ok and key == 'pitrex' and r['name']:
            what = r['name'] + ': ' + what
    else:                                       # (big: the first MB, the size, the listing)
        head, size = r['head'], r['size']
        ok, what = True, '%.0f MB' % (size / 1e6)
        if url in g.PINNED and size != g.PINNED[url][0]:
            ok, what = False, 'another size (%d bytes, not %d)' % (size, g.PINNED[url][0])
        elif head[:4] != b'PK\x03\x04':
            ok, what = False, 'not a zip'
        else:
            try:
                names = listing(url, size)
            except Exception as e:   # noqa
                names, what = None, what + ', listing failed (%s)' % _err(e)
            if names is not None:
                if key == 'pitrex':
                    miss = g.pitrex_missing(names)
                    ok = not miss
                    what = '%s%s, %d files' % ((r['name'] + ': ') if r['name'] else '', what, len(names)) if ok else 'lacks ' + ', '.join(miss)
                else:
                    f = names_found(key, names)
                    ok = len(f) >= g.MIN_FOUND[key]
                    what = '%d of the %d games by name' % (len(f), len(g.GCE_GAMES if key == 'games' else g.HOMEBREW))
            elif 'listing failed' not in what:
                what += ', no Range support: no listing'
            what += ' (the whole file: monthly)' if url in g.PINNED else ''
    if not ok:
        return 'changed', speed, what, set()
    if key == 'pitrex' and r['name'] and g.DRIVE_PITREX_NAME.lower() not in r['name'].lower() \
            and url not in g.PINNED and 'web.archive.org' not in url and r['name'].lower().startswith('pitrex_sd_'):
        what += ' - newer than the project\'s copy (%s)' % g.DRIVE_PITREX_NAME
    measured = len(r['data']) if r['data'] is not None else len(r['head'])
    if measured >= 256 << 10 and r['bps'] < SLOW_BPS:
        return 'slow', speed, what, has
    return 'ok', speed, what, has


def page_row(url):
    """an item's page (where to get it by hand): it answers"""
    try:
        t0 = time.time()
        with _open(url) as r:
            n = len(r.read(1 << 16))
        return 'ok', '%.0f KB/s' % (n / max(time.time() - t0, 1e-3) / 1e3), 'HTTP %d' % r.status
    except Exception as e:   # noqa
        return _fail_status(url, e), '', _err(e)


# ---------------------------------------------------------------- all of it
def run(full):
    rows, errors, warnings = [], [], []
    for key, title, site, page, dest, size_txt, chain in g.SOURCES:
        ROWS.clear()
        urls = []
        for u in chain():
            if u and u not in urls:
                urls.append(u)
        if key == 'pitrex' and g.DRIVE_PITREX not in urls:
            urls.append(g.DRIVE_PITREX)         # (the older version, on the user's request)
        item_rows, working, blocked, has = [], 0, 0, set()
        for src, st, det in ROWS:
            item_rows.append((title, src, st, '', det))
        for u in urls:
            st, sp, det, h = check_source(key, u, full)
            item_rows.append((title, short(u), st, sp, det))
            if st in ('ok', 'slow'):
                working += 1
                has |= h
            blocked += st == 'blocked'
            print('  %-8s %-9s %s' % (key, st, short(u)), file=sys.stderr, flush=True)
        st, sp, det = page_row(page)
        item_rows.append((title, 'page (by hand) ' + short(page), st, sp, det))
        for _, src, st, _, det in item_rows:
            if st in ('dead', 'changed'):
                warnings.append('%s: %s %s (%s)' % (title, src, st, det))
        if not working:
            if blocked:                         # (blocked here; at home they may well work)
                warnings.append('%s: no source worked from here, %d blocked - not checked' % (title, blocked))
            else:
                errors.append('%s: NO working source' % title)
        if key in ('games', 'homebrew') and working:
            want = set(g.GCE_GAMES if key == 'games' else g.HOMEBREW)
            miss = sorted(want - has)           # (by fingerprint: the sets downloaded whole)
            if miss and not blocked:
                errors.append('%s: on no working source: %s' % (title, ', '.join(miss)))
            elif not miss:
                item_rows.append((title, '(all its sources)', 'ok', '', 'all %d games there by fingerprint' % len(want)))
        rows += item_rows
    return rows, errors, warnings


def table(rows):
    hd = ('item', 'source', 'status', 'speed', 'what it is')
    w = [max(len(str(r[i])) for r in rows + [hd]) for i in range(4)]
    out = [' | '.join(str(x).ljust(w[i]) for i, x in enumerate(hd[:4])) + ' | ' + hd[4]]
    out.append('-+-'.join('-' * n for n in w) + '-+-' + '-' * 10)
    for r in rows:
        out.append(' | '.join(str(x).ljust(w[i]) for i, x in enumerate(r[:4])) + ' | ' + r[4])
    return '\n'.join(out)


def markdown(rows, errors, warnings, full):
    esc = lambda s: str(s).replace('|', '\\|')   # noqa: E731
    md = ['The weekly check of the download links of **Vectrex > Get games & extras** (`tools/linkcheck.py`, '
          'sources in `src/frontend/getter.py`), %s UTC%s.' % (datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d %H:%M'),
                                                              ', whole files' if full else ''), '']
    if errors:
        md += ['**Errors** (an item or a game that cannot be downloaded):', ''] + ['- ' + e for e in errors] + ['']
    if warnings:
        md += ['**Warnings:**', ''] + ['- ' + w for w in warnings] + ['']
    md += ['| item | source | status | speed | what it is |', '|---|---|---|---|---|']
    md += ['| %s |' % ' | '.join(esc(x) for x in r) for r in rows]
    md += ['', '*changed*: the file is not what it should be (the emulator refuses it and tries the next source); '
               '*blocked*: the host turned the GitHub runner away (Google Drive and archive.org do so to data centres) - '
               'not a problem by itself.']
    return '\n'.join(md) + '\n'


def main():
    global PAUSE
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--full', nargs='?', const='yes', default='', help="download big files whole; 'auto': in a month's first week")
    ap.add_argument('--md', help='write the report (markdown) here')
    ap.add_argument('--pause', type=float, default=PAUSE, help='seconds before the one retry of a 403/429')
    a = ap.parse_args()
    PAUSE = a.pause
    full = a.full == 'yes' or (a.full == 'auto' and datetime.date.today().day <= 7)
    t0 = time.time()
    rows, errors, warnings = run(full)
    print(table(rows))
    print()
    for e in errors:
        print('ERROR   ', e)
    for w in warnings:
        print('warning ', w)
    print('%s, %.0f s%s' % ('errors' if errors else 'warnings' if warnings else 'all well', time.time() - t0,
                            ' (whole files)' if full else ''))
    md = markdown(rows, errors, warnings, full)
    if a.md:
        with open(a.md, 'w', encoding='utf8') as f:
            f.write(md)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf8') as f:
            f.write(md)
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf8') as f:
            f.write('problems=%s\n' % ('true' if errors or warnings else 'false'))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
