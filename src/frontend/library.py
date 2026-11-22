"""library.py - the game library over the picture (L, the cartridge in the deck): as the iPhone / Android app's.

The owner, 2026-11-22: the PC's loader like the phones' - the tiles' look (cartridges, boxes, overlays), how many a row or
titles only, a search, the categories; it fills the picture's place above the control deck. The pictures: the apps' own
folder, system/vectrex_emu/thumbs/{boxes,carts,overlays}/<game>.png (nothing is downloaded: there, or the name's tile).

The app holds one Library; while it is open App.browser is its dict (lib=True), so everything that asks "is the loader
open" works as before. The renderer draws state() (vecrender._library); the pictures are read by a thread (the PNG reader
is Python: 60 - 90 ms each) and handed over there to become textures."""
import json, os, threading, queue, time, math
import tkinter as tk

ASPECT = {'carts': 0.96, 'boxes': 1.39, 'overlays': 207.0 / 162.0}      # a tile's height / width (the apps')
LOOKS = (('CARTRIDGES', 'carts'), ('BOXES', 'boxes'), ('OVERLAYS', 'overlays'))
CATS = ('All', 'Recent', 'Games', 'UVMC2', 'PiTrex', 'Tests')
COLS = (0, 2, 3, 4, 5, 6)                                               # tiles a row (0: titles only)


class Library:
    def __init__(self, app, V):
        self.a, self.V = app, V
        self.games = []
        self.look, self.cols, self.cat, self.text = 'boxes', 4, 'All', ''
        try:
            d = json.load(open(V.SETTINGS_F))
            if d.get('lib_look') in ASPECT: self.look = d['lib_look']
            if d.get('lib_cols') in COLS: self.cols = d['lib_cols']
            if d.get('lib_cat') in CATS: self.cat = d['lib_cat']
        except Exception:   # noqa: no settings yet
            pass
        self.sel, self.scroll = 0, 0.0
        self.entry = None
        self.want = queue.Queue(); self.done = {}; self.asked = set(); self.missing = set()
        self.files = {}                                                 # look -> {lower name: path}
        threading.Thread(target=self._reader, daemon=True).start()

    # ---------------------------------------------------------------- the games
    def scan(self):
        V, a = self.V, self.a
        top = os.path.join(V.ROOT, 'roms'); out = []
        ext_ok = tuple(a.ROM_EXT) + (('.um2',) if getattr(a, 'show_um2', True) else ()) + (('.img', '.pit') if V.PITREX_UI else ())
        for dp, dn, fn in os.walk(top):
            dn[:] = [d for d in dn if d.lower() != 'overlays']
            rel = os.path.relpath(dp, top)
            for f in fn:
                e = os.path.splitext(f)[1].lower()
                if e not in ext_ok:
                    continue
                kind = 'UVMC2' if e == '.um2' else 'PiTrex' if e in ('.img', '.pit') else 'Tests' if rel.split(os.sep)[0].lower() == 'tests' else 'Games'
                out.append(dict(name=os.path.splitext(f)[0], path=os.path.join(dp, f), kind=kind))
        boot = os.path.join(getattr(a, 'px_dir', '') or '', 'pitrex.img')
        if V.PITREX_UI and os.path.isfile(boot):
            out.append(dict(name='PiTrex', path=boot, kind='PiTrex'))
        out.sort(key=lambda g: g['name'].lower())
        self.games = out
        th = os.path.join(V.ROOT, 'system', 'vectrex_emu', 'thumbs'); self.files = {}
        for _, lk in LOOKS:
            d = os.path.join(th, lk); m = {}
            try:
                for f in os.listdir(d):
                    if f.lower().endswith('.png'):
                        m[f[:-4].lower()] = os.path.join(d, f)
            except OSError:
                pass
            self.files[lk] = m

    def shown(self):
        g = self.games
        if self.cat == 'Recent':
            by = {os.path.abspath(x['path']).lower(): x for x in g}
            g = [by[os.path.abspath(p).lower()] for p in getattr(self.a, 'recent', []) if os.path.abspath(p).lower() in by]
        elif self.cat == 'All':
            g = [x for x in g if x['kind'] != 'Tests']                  # (the tests: under Tests only, as the apps)
        else:
            g = [x for x in g if x['kind'] == self.cat]
        t = self.text.strip().lower()
        if t:
            g = [x for x in g if t in x['name'].lower()]
        return g

    def tile(self, name):
        """the picture's file for a game in the look chosen (its name, else its name without a (...) part) or None"""
        m = self.files.get(self.look) or {}
        return m.get(name.lower()) or m.get(name.split(' (')[0].lower())

    def _reader(self):
        import vart
        while True:
            p = self.want.get()
            try:
                w, h, px = vart.load_png(p)
                self.done[p] = (w, h, px)
            except Exception:   # noqa: not a picture
                self.missing.add(p)

    # ---------------------------------------------------------------- open, close
    def open(self, select=None):
        self.scan()
        g = self.shown()
        if select:
            hit = [i for i, x in enumerate(g) if os.path.abspath(x['path']).lower() == os.path.abspath(select).lower()]
            self.sel = hit[0] if hit else min(self.sel, max(0, len(g) - 1))
        self.sel = max(0, min(self.sel, max(0, len(g) - 1)))
        self._entry()
        self.keep_in_view()
        return dict(lib=True, roms=[], sel=0, top=0, dir=os.path.join(self.V.ROOT, 'roms'), stack=[])

    def closed(self):
        if self.entry is not None:
            try: self.entry.destroy()
            except tk.TclError: pass
            self.entry = None
        try: self.a.root.focus_set()
        except tk.TclError: pass

    def save(self, d):
        d['lib_look'] = self.look; d['lib_cols'] = self.cols; d['lib_cat'] = self.cat

    # ---------------------------------------------------------------- the layout (window px, y down)
    def geom(self):
        a = self.a; r = a.ren
        rw, rh = r.real_size
        bx = a.rec_box(False)                                           # (the picture's part of the window: above the deck)
        x0, x1 = bx[0], bx[0] + bx[2]; y1 = rh - bx[1]; y0 = y1 - bx[3]
        W = x1 - x0; u = W / 60.0; k = 0.15 * u                         # (k: the stroke font's unit - a letter 6 k)
        m = 1.2 * u
        g = dict(rect=(x0, y0, x1, y1), u=u, k=k)
        g['close'] = (x1 - m - 2.6 * u, y0 + 0.7 * u, x1 - m, y0 + 3.3 * u)
        g['search'] = (x1 - m - 3.2 * u - 19 * u, y0 + 0.7 * u, x1 - m - 3.2 * u, y0 + 3.3 * u)
        g['title'] = (x0 + m, y0 + 1.25 * u)
        y = y0 + 4.2 * u; x = x0 + m; g['looks'] = []
        for name, lk in LOOKS:
            g['looks'].append(((x, y, x + 8.6 * u, y + 2.6 * u), name, lk)); x += 8.9 * u
        x += 0.9 * u; g['per'] = (x, y + 0.85 * u); x += 5.6 * u; g['cols'] = []
        for c in COLS:
            w = 4.0 * u if c == 0 else 3.0 * u
            g['cols'].append(((x, y, x + w, y + 2.6 * u), 'TEXT' if c == 0 else str(c), c)); x += w + 0.3 * u
        y += 3.4 * u; x = x0 + m; g['cats'] = []
        for c in CATS:
            w = (len(c) + 3) * 6 * k
            g['cats'].append(((x, y, x + w, y + 2.6 * u), c.upper(), c)); x += w + 0.6 * u
        g['help'] = (x0 + m, y + 3.3 * u)                                # (the keys: a small line under the categories)
        g['head'] = y + 4.9 * u                                          # (the tiles below this)
        gx0, gx1, gy0, gy1 = x0 + m, x1 - m, g['head'] + 0.4 * u, y1 - 0.4 * u
        g['grid'] = (gx0, gy0, gx1, gy1)
        if self.cols:
            gap = 1.0 * u; tw = (gx1 - gx0 - gap * (self.cols - 1)) / self.cols; th = tw * ASPECT[self.look]
            g['tile'] = (tw, th, gap, th + 2.6 * u)                       # (width, height, gap, a row's height with its name)
        else:
            g['tile'] = (gx1 - gx0, 0.0, 0.0, 2.4 * u)
        return g

    def rows(self, n):
        c = self.cols or 1
        return (n + c - 1) // c

    def keep_in_view(self):
        if not (self.a.ren and hasattr(self.a.ren, 'map')):
            return
        g = self.geom(); gx0, gy0, gx1, gy1 = g['grid']; rowh = g['tile'][3]
        n = len(self.shown()); c = self.cols or 1
        r = self.sel // c
        top, bot = r * rowh, (r + 1) * rowh
        if top < self.scroll: self.scroll = top
        if bot > self.scroll + (gy1 - gy0): self.scroll = bot - (gy1 - gy0)
        self.clamp(g, n)

    def clamp(self, g, n):
        gx0, gy0, gx1, gy1 = g['grid']
        self.scroll = max(0.0, min(self.scroll, max(0.0, self.rows(n) * g['tile'][3] - (gy1 - gy0))))

    # ---------------------------------------------------------------- what the renderer draws
    def state(self):
        a = self.a
        if not (a.ren and hasattr(a.ren, 'map')):
            return None
        g = self.geom(); games = self.shown(); n = len(games)
        self.sel = max(0, min(self.sel, max(0, n - 1))); self.clamp(g, n)
        gx0, gy0, gx1, gy1 = g['grid']; tw, th, gap, rowh = g['tile']; c = self.cols or 1
        tiles = []
        r0 = int(self.scroll // rowh); r1 = int((self.scroll + (gy1 - gy0)) // rowh) + 1
        for i in range(r0 * c, min(n, r1 * c)):
            r, col = divmod(i, c)
            x = gx0 + col * (tw + gap); y = gy0 + r * rowh - self.scroll
            p = self.tile(games[i]['name']) if self.cols else None
            if p and p not in self.asked and p not in self.missing:
                self.asked.add(p); self.want.put(p)
            tiles.append(dict(x=x, y=y, w=tw, h=th, name=games[i]['name'], img=p if p and p not in self.missing else None,
                              sel=i == self.sel, kind=games[i]['kind']))
        new = {}
        for p in list(self.done):
            new[p] = self.done.pop(p)
        self._place_entry(g)
        pad = bool(a.browser and a.browser.get('pad'))
        return dict(lib=True, g=g, tiles=tiles, new=new, text_only=not self.cols, n=n, look=self.look, cols=self.cols, cat=self.cat,
                    title='GAMES  (%d)' % n, scroll=(self.scroll, self.rows(n) * rowh),
                    empty=('NO GAMES FOUND.\nPUT YOUR ROMS IN THE FOLDER\nROMS NEXT TO THE PROGRAM.' if not self.games else 'NOTHING HERE.'),
                    help='STICK: CHOOSE   BUTTON 4: PLAY   BUTTON 1: CLOSE' if pad else 'ARROWS: CHOOSE   ENTER: PLAY   /: SEARCH   ESC: CLOSE')

    # ---------------------------------------------------------------- the search field: a real entry over the picture
    def _entry(self):
        if self.entry is not None:
            return
        a = self.a
        import deck, palette
        e = tk.Entry(a.screen, bg=deck.PANEL if hasattr(deck, 'PANEL') else '#0b1511', fg='#bbffee', insertbackground='#bbffee', relief='flat',
                     highlightthickness=1, highlightbackground='#2f5544', highlightcolor='#88ccbb', font=(self.V.MONO, 11))
        e.insert(0, self.text)
        e.bindtags((e.bindtags()[1], e.bindtags()[0]) + tuple(e.bindtags()[2:]))   # (the class first: it types; then ours stops the app's own keys)
        def key(ev):
            if ev.keysym == 'Escape':
                if e.get(): e.delete(0, 'end'); self.typed()
                else: a.root.focus_set()
                return 'break'
            if ev.keysym in ('Return', 'KP_Enter'):
                a.root.focus_set(); self.load(); return 'break'
            if ev.keysym in ('Down', 'Tab'):
                a.root.focus_set(); return 'break'
            self.typed(); return 'break'
        e.bind('<KeyPress>', key); e.bind('<KeyRelease>', lambda ev: (self.typed(), 'break')[1])
        self.entry = e

    def _place_entry(self, g):
        e = self.entry
        if e is None:
            return
        k = self.a.px() or 1.0
        x0, y0, x1, y1 = g['search']
        box = (int(x0 / k), int(y0 / k), int((x1 - x0) / k), int((y1 - y0) / k))
        if getattr(self, '_ebox', None) != box:
            self._ebox = box
            try: e.place(x=box[0], y=box[1], width=box[2], height=box[3])
            except tk.TclError: pass

    def typed(self):
        e = self.entry
        if e is None:
            return
        t = e.get()
        if t != self.text:
            self.text = t; self.sel = 0; self.scroll = 0.0

    def focus_search(self):
        if self.entry is not None:
            self.entry.focus_set(); self.entry.icursor('end')

    # ---------------------------------------------------------------- moving, choosing
    def move(self, dx, dy):
        n = len(self.shown())
        if not n:
            return
        c = self.cols or 1
        if not self.cols and dx:                                        # (titles only: left / right a page)
            g = self.geom(); dy = dx * max(1, int((g['grid'][3] - g['grid'][1]) // g['tile'][3]) - 1); dx = 0
        self.sel = max(0, min(n - 1, self.sel + dx + dy * c))
        self.keep_in_view()

    def page(self, d):
        g = self.geom()
        self.move(0, d * max(1, int((g['grid'][3] - g['grid'][1]) // g['tile'][3])))

    def home(self, end=False):
        self.sel = max(0, len(self.shown()) - 1) if end else 0
        self.keep_in_view()

    def wheel(self, d):
        """the list scrolled by d rows of the old list (3 a notch): a third of a tile's row each"""
        g = self.geom()
        self.scroll += d * g['tile'][3] / (6.0 if self.cols else 1.0)
        self.clamp(g, len(self.shown()))

    def load(self):
        g = self.shown()
        if not g:
            return
        p = g[max(0, min(self.sel, len(g) - 1))]['path']
        self.a.close_browser()
        self.a.restart(p)

    def set(self, what, v):
        if what == 'look': self.look = v
        elif what == 'cols': self.cols = v
        elif what == 'cat': self.cat = v; self.sel = 0; self.scroll = 0.0
        self.keep_in_view()
        self.a.save_settings()

    def next_cat(self, d=1):
        self.set('cat', CATS[(CATS.index(self.cat) + d) % len(CATS)])

    def click(self, e):
        g = self.geom()
        ins = lambda b: b[0] <= e.x <= b[2] and b[1] <= e.y <= b[3]
        try: self.a.root.focus_set()
        except tk.TclError: pass
        if ins(g['close']):
            return self.a.close_browser()
        for box, _n, v in g['looks']:
            if ins(box): return self.set('look', v)
        for box, _n, v in g['cols']:
            if ins(box): return self.set('cols', v)
        for box, _n, v in g['cats']:
            if ins(box): return self.set('cat', v)
        gx0, gy0, gx1, gy1 = g['grid']
        if gx0 <= e.x <= gx1 and gy0 <= e.y <= gy1:
            tw, th, gap, rowh = g['tile']; c = self.cols or 1
            col = int((e.x - gx0) // (tw + gap)) if self.cols else 0
            if col >= c or (self.cols and (e.x - gx0) - col * (tw + gap) > tw):
                return
            i = int((e.y - gy0 + self.scroll) // rowh) * c + col
            if 0 <= i < len(self.shown()):
                self.sel = i
                return self.load()                                      # (a click plays, as a tap on the phone)
        x0, y0, x1, y1 = g['rect']
        if not (x0 <= e.x <= x1 and y0 <= e.y <= y1):                   # (outside: the deck's cartridge - closed)
            return self.a.close_browser()
