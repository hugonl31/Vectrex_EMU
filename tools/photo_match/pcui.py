"""pcui.py - the PC app from source, its window captured as it is on screen (the README's interface pictures): the window
with a game and its deck, the game library, the Config window. Never saves settings; its own TEMP.
argv: out_prefix rom what[,what...] [wait_ms]   what: main | library | boxes | config
env: NOSETUP=1 (defaults, no setup window), OVERLAY=1 (the game's overlay on), PRESS=ms[,ms...] (button 4 pressed then), LOOK,
ROOM=0.5|1|2 (View > Room light: Dark, Evening, Daylight), JOY=from-to:x,y (ms; player 1's stick held, -1..1),
LIBART=1.0 (the library's pictures at that brightness - the README's screenshots; the app keeps its 80 %),
LIBROOT=<folder> (the app's folder for roms and thumbs, e.g. build\Vectrex_EMU with the owner's pictures), HIDE=name;name (games left out of the list),
LIBUI=1.25 (the library's own colours - text, buttons, panels - that much brighter: with LIBART=1.0 the whole loader at 100 %)"""
import sys, os, glob, ctypes, threading, subprocess, tempfile
S = os.path.dirname(os.path.abspath(__file__))
_root = os.environ.get('TEST_TEMP_ROOT') or os.path.join(os.path.dirname(os.path.abspath(__file__)), '_temp')
os.makedirs(_root, exist_ok=True)                         # (never the owner's TEMP: the core and the app write stamps there -
import tempfile; _tmp = tempfile.mkdtemp(prefix='pcui_', dir=_root)   #  vectrex_emu_power_off.txt, read at his next start)
os.environ['TEMP'] = os.environ['TMP'] = os.environ['TMPDIR'] = _tmp; tempfile.tempdir = _tmp
def _dead():
    subprocess.call('taskkill /F /T /PID %d' % os.getpid(), shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); os._exit(3)
_wd = threading.Timer(float(os.environ.get('TEST_MAX_S', '120')), _dead); _wd.daemon = True; _wd.start()
out, rom, whats = sys.argv[1], sys.argv[2], sys.argv[3].split(',')
wait = int(sys.argv[4]) if len(sys.argv) > 4 else 6000
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.chdir(r'G:\AI\Vectrex_EMU')
_FG = ctypes.windll.user32.GetForegroundWindow()
import argparse
import vectrex_emu as V
from PIL import ImageGrab
BTN = [0]; JOY = [None]
_fs = V.Core.frame_send
def frame_send(self, jx, jy, b, flags, p2=None):
    if JOY[0]: jx, jy = (max(-128, min(127, int(round(v * 127)))) for v in JOY[0])
    return _fs(self, jx, jy, b | BTN[0], flags, p2)
V.Core.frame_send = frame_send
args = argparse.Namespace(rom=rom, height_mm=0, scale=1.0, tk=False, fullscreen=False, ideal=False, demo=0, net_host=None, net_join=None, net_online=False)
V.vecrender.set_dpi_aware()
if os.environ.get('LIBART'): V.vecrender.LIB_ART = float(os.environ['LIBART'])
if os.environ.get('LIBUI'):
    _uk = float(os.environ['LIBUI']); _lib0 = V.vecrender.GLBloomRenderer._library
    def _lib1(self, b, _p=V.vecrender.palette):
        r0 = _p.rgb; done = set()                    # (once: the text's colours come through rgb() again)
        def r1(c):
            if tuple(c) in done: return tuple(c)
            o = tuple(min(1.0, v * _uk) for v in r0(c)); done.add(o); return o
        _p.rgb = r1
        try: return _lib0(self, b)
        finally: _p.rgb = r0
    V.vecrender.GLBloomRenderer._library = _lib1
if os.environ.get('LIBROOT'): V.ROOT = os.path.abspath(os.environ['LIBROOT'])
if os.environ.get('HIDE'):
    _hide = {x.lower() for x in os.environ['HIDE'].split(';')}; _scan = V.library.Library.scan
    def _scan2(self, *a, **k):
        r = _scan(self, *a, **k); self.games = [g for g in self.games if g['name'].lower() not in _hide]; return r
    V.library.Library.scan = _scan2
V.SETTINGS_F = os.path.join(_tmp, 'settings.json')       # (a fresh settings file of its own: the defaults, a first start)
if os.environ.get('SETTINGS'): import shutil as _sh; _sh.copy(os.environ['SETTINGS'], V.SETTINGS_F)
elif os.environ.get('NOSETUP'): open(V.SETTINGS_F, 'w').write('{"setup_done": true, "start_on": true}')   # (the defaults the setup offers)
V.App.save_settings = lambda self, *a, **k: None      # (never the owner's settings)
import time as _time
def _warm(self, off_for=0.0):
    self.crt_on_t = _time.perf_counter() - 100.0; self.crt_fade = 0.3; self.crt_bulge = 0.0
V.App.crt_on = _warm
app = V.App(args)
app.skip_boot = True
if os.environ.get('CLEAN'): app.ren.wob_on = False; app.ren.bend_on = False
if os.environ.get('LOOK'): app.ren.look = os.environ['LOOK']
app.ren.art_on = bool(os.environ.get('OVERLAY'))
if os.environ.get('ROOM'): app.root.after(250, lambda: app.set_room(float(os.environ['ROOM'])))
def _vrr():                                          # (as pcany: the game runs at the monitor's pace)
    app.vrr = True
    if app.ren and hasattr(app.ren, 'present'): app.ren.swap_interval(0)
if not os.environ.get('NOVRR'): app.root.after(200, _vrr)
app.root.after(300, lambda: app.restart(os.path.abspath(rom)))
for t in (int(x) for x in filter(None, os.environ.get('PRESS', '').split(','))):   # (ms, a comma list: one press each)
    app.root.after(t, lambda: BTN.__setitem__(0, 8)); app.root.after(t + 300, lambda: BTN.__setitem__(0, 0))
for _it in filter(None, os.environ.get('JOY', '').split(';')):
    _r, _v = _it.split(':'); _a, _b = (int(x) for x in _r.split('-')); _xy = tuple(float(x) for x in _v.split(','))
    app.root.after(_a, lambda xy=_xy: JOY.__setitem__(0, xy)); app.root.after(_b, lambda: JOY.__setitem__(0, None))
def grab(win, name):
    win.update(); x, y, w, h = win.winfo_rootx(), win.winfo_rooty(), win.winfo_width(), win.winfo_height()
    im = ImageGrab.grab(bbox=(x, y, x + w, y + h), all_screens=True); p = '%s_%s.png' % (out, name); im.save(p); print('ok', p, im.size, flush=True)
def step(k=0):
    if k >= len(whats):
        try: ctypes.windll.user32.SetForegroundWindow(_FG)
        except Exception: pass
        app.root.after(300, lambda: (app.quit() if hasattr(app, 'quit') else app.root.destroy())); return
    w = whats[k]
    if w == 'main':
        grab(app.root, 'main'); app.root.after(300, lambda: step(k + 1))
    elif w in ('library', 'boxes'):
        app.open_library()
        def g():
            if getattr(app, 'lib', None) is not None: app.lib.set('look', 'boxes' if w == 'boxes' else 'carts')
            app.root.after(1500, lambda: (grab(app.root, w), step(k + 1)))
        app.root.after(1500, g)
    elif w == 'setup':                              # (a first start's setup window, if it is open)
        tops = [t for t in app.root.winfo_children() if t.winfo_class() == 'Toplevel' and t.winfo_viewable()]
        if tops: grab(tops[-1], 'setup')
        else: print('no setup window', flush=True)
        app.root.after(300, lambda: step(k + 1))
    elif w == 'shot':                               # (the picture: F12, 4x)
        fold = app.rec_folder(); before = set(glob.glob(os.path.join(fold, '*.png')))
        app.rec_opts['rec_snap'] = 'max'; app.snapshot()
        def fetch(n=0):
            new = set(glob.glob(os.path.join(fold, '*.png'))) - before
            if new:
                f = max(new, key=os.path.getmtime); import shutil as _sh
                app.root.after(600, lambda: (_sh.move(f, out + '_shot.png'), print('ok', out + '_shot.png', flush=True), step(k + 1)))
            elif n < 60: app.root.after(250, lambda: fetch(n + 1))
            else: print('no snapshot', flush=True); step(k + 1)
        fetch()
    elif w == 'config':
        app.open_deck()
        def g():
            tops = [t for t in app.root.winfo_children() if t.winfo_class() == 'Toplevel' and t.winfo_viewable()]
            if tops: grab(tops[-1], 'config'); tops[-1].destroy()
            else: print('no config window', flush=True)
            step(k + 1)
        app.root.after(2500, g)
app.root.after(wait, step)
app.root.mainloop()
