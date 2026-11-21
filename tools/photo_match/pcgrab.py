import sys, os, argparse
S = os.path.dirname(os.path.abspath(__file__))
which, out = sys.argv[1], os.path.join(S, sys.argv[2])
sys.path.insert(0, os.path.join(S, which + 'vr'))
sys.path.insert(1, r'G:\AI\Vectrex_EMU\src\frontend')
os.chdir(r'G:\AI\Vectrex_EMU')
import vecrender
import ctypes
_FG = ctypes.windll.user32.GetForegroundWindow()   # (the owner's window: focus goes back to it)
import vectrex_emu as V
from PIL import ImageGrab
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
app.ren.wob_on = False; app.ren.bend_on = False; app.skip_boot = True      # (tests: no wobble, no bending, no intro)
def _vrr():
    app.vrr = True
    if app.ren and hasattr(app.ren, 'present'): app.ren.swap_interval(0)   # (G-Sync: whole frames; not saved)
app.root.after(200, _vrr)
app.root.after(600, app.root.iconify)   # (minimised: the owner keeps working; F12 draws off screen)
app.root.after(300, lambda: app.restart(os.path.abspath(r'testcart\dot_test.bin')))
def grab():
    import glob, shutil
    fold = app.rec_folder(); before = set(glob.glob(os.path.join(fold, '*.png')))
    app.rec_opts['rec_snap'] = os.environ.get('SNAP', 'window')   # (the window's size: as the owner sees it; not saved)
    app.snapshot()                                     # (F12)
    def fetch(n=0):
        new = set(glob.glob(os.path.join(fold, '*.png'))) - before
        if new:
            f = max(new, key=os.path.getmtime); app.root.after(500, lambda: (shutil.move(f, out), print('snap', out, vecrender.__file__, app.ren.look), finish()))
        elif n < 40: app.root.after(250, lambda: fetch(n + 1))
        else: print('no snapshot'); finish()
    fetch()
def finish():
    try: app.quit()
    except Exception: app.root.destroy()
app.root.after(int(os.environ.get('GRAB_MS', '5000')), grab)
app.root.mainloop()
