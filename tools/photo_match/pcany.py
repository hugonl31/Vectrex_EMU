"""the PC app from source, minimised, focus kept: any cart, button presses, one F12 snapshot (window size).
argv: out.png rom button presses [wait_ms]; env LOOK, BEAM=1"""
import sys, os, argparse, glob, shutil, ctypes
S = os.path.dirname(os.path.abspath(__file__))
out, rom, btn, presses = os.path.join(S, sys.argv[1]), sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
wait = int(sys.argv[5]) if len(sys.argv) > 5 else 1500
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.chdir(r'G:\AI\Vectrex_EMU')
_FG = ctypes.windll.user32.GetForegroundWindow()
import vectrex_emu as V
BTN = [0]
_fs = V.Core.frame_send
def frame_send(self, jx, jy, b, flags, p2=None): return _fs(self, jx, jy, b | BTN[0], flags, p2)
V.Core.frame_send = frame_send
args = argparse.Namespace(rom=rom, height_mm=0, scale=1.0, tk=False, fullscreen=False, ideal=False, demo=0, net_host=None, net_join=None, net_online=False)
V.vecrender.set_dpi_aware()
V.App.save_settings = lambda self, *a, **k: None      # (tests never save the settings)
app = V.App(args); app.root.iconify()
def _back():
    try: ctypes.windll.user32.SetForegroundWindow(_FG)
    except Exception: pass
for ms in (50, 300, 700, 1500): app.root.after(ms, _back)
if os.environ.get('LOOK'): app.ren.look = os.environ['LOOK']
if os.environ.get('BEAM'): app.ren.beam_trace = True
app.ren.wob_on = False; app.ren.bend_on = False; app.skip_boot = True; app.ren.art_on = False
def _vrr():
    app.vrr = True
    if app.ren and hasattr(app.ren, 'present'): app.ren.swap_interval(0)
app.root.after(200, _vrr)
app.root.after(300, lambda: app.restart(os.path.abspath(rom)))
def finish():
    try: app.quit()
    except Exception: app.root.destroy()
def snap():
    fold = app.rec_folder(); before = set(glob.glob(os.path.join(fold, '*.png')))
    app.rec_opts['rec_snap'] = 'window'; app.snapshot()
    def fetch(n=0):
        new = set(glob.glob(os.path.join(fold, '*.png'))) - before
        if new: f = max(new, key=os.path.getmtime); app.root.after(400, lambda: (shutil.move(f, out), print('ok', out), finish()))
        elif n < 40: app.root.after(250, lambda: fetch(n + 1))
        else: print('no snapshot'); finish()
    fetch()
def press(k):
    if k >= presses: app.root.after(wait, snap); return
    BTN[0] = btn
    app.root.after(400, lambda: (BTN.__setitem__(0, 0), app.root.after(600, lambda: press(k + 1))))
app.root.after(5000, lambda: press(0))
app.root.mainloop()
