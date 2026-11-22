"""the PC app from source, minimised: Dot Test pages 1-4 as F12 snapshots (window size); LOOK env; out prefix argv[1]"""
import sys, os, argparse, glob, shutil
S = os.path.dirname(os.path.abspath(__file__))
pre = os.path.join(S, sys.argv[1])
VR = os.environ.get('VR')
if VR: sys.path.insert(0, os.path.join(S, VR + 'vr'))
sys.path.insert(1 if VR else 0, r'G:\AI\Vectrex_EMU\src\frontend')
os.chdir(r'G:\AI\Vectrex_EMU')
import ctypes
_FG = ctypes.windll.user32.GetForegroundWindow()   # (the owner's window: focus goes back to it)
import vectrex_emu as V
BTN = [0]
_fs = V.Core.frame_send
def frame_send(self, jx, jy, btn, flags, p2=None):
    return _fs(self, jx, jy, btn | BTN[0], flags, p2)
V.Core.frame_send = frame_send
args = argparse.Namespace(rom=r'testcart\dot_test.bin', height_mm=0, scale=1.0, tk=False, fullscreen=False, ideal=False, demo=0,
                          net_host=None, net_join=None, net_online=False)
V.vecrender.set_dpi_aware()
V.App.save_settings = lambda self, *a, **k: None      # (tests never save the settings)
app = V.App(args)
app.root.iconify()
def _back():
    try: ctypes.windll.user32.SetForegroundWindow(_FG)
    except Exception: pass
for _ms in (50, 300, 700, 1500): app.root.after(_ms, _back)
if os.environ.get('LOOK'): app.ren.look = os.environ['LOOK']
if os.environ.get('BEAM'): app.ren.beam_trace = True
app.ren.wob_on = False; app.ren.bend_on = False; app.skip_boot = True      # (tests: no wobble, no bending, no intro)
def _vrr():
    app.vrr = True
    if app.ren and hasattr(app.ren, 'present'): app.ren.swap_interval(0)   # (G-Sync: whole frames; not saved)
app.root.after(200, _vrr)
app.root.after(600, app.root.iconify)
app.root.after(300, lambda: app.restart(os.path.abspath(r'testcart\dot_test.bin')))
PAGES = [int(c) for c in os.environ.get('PAGES', '1234')]
def page(p):
    if p > 4 or p not in PAGES and not any(q > p for q in PAGES):
        try: app.quit()
        except Exception: app.root.destroy()
        return
    BTN[0] = 1 << (p - 1)
    app.root.after(400, lambda: (BTN.__setitem__(0, 0), app.root.after(1500, lambda: snap(p))))
def snap(p):
    fold = app.rec_folder(); before = set(glob.glob(os.path.join(fold, '*.png')))
    app.rec_opts['rec_snap'] = 'window'
    app.snapshot()
    def fetch(n=0):
        new = set(glob.glob(os.path.join(fold, '*.png'))) - before
        if new:
            f = max(new, key=os.path.getmtime)
            app.root.after(400, lambda: (shutil.move(f, '%s_p%d.png' % (pre, p)), print('page', p), page(p + 1)))
        elif n < 40: app.root.after(250, lambda: fetch(n + 1))
        else: print('no snapshot', p); page(p + 1)
    fetch()
app.root.after(3000, lambda: page(1))
app.root.mainloop()
