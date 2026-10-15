"""macgl_test.py - the smallest check of osdep.MacGL: a Tk window whose frame OpenGL clears to red, a green
Tk label beside it; a screenshot then shows whether the OpenGL picture reaches the screen.

    VX_MACGL=child|subview python tools/macgl_test.py SECONDS
"""
import ctypes, os, sys, time, tkinter as tk
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'frontend'))
import osdep    # noqa: E402

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 6
root = tk.Tk(); root.title('macgl test'); root.geometry('500x340+200+150'); root.configure(bg='blue')
tk.Label(root, text='TK LABEL', bg='green', fg='white', font=('Helvetica', 20)).pack(side='top', fill='x')
fr = tk.Frame(root, bg='yellow', width=480, height=260); fr.pack(fill='both', expand=True, padx=10, pady=10)
root.update()
c = osdep.MacGL(fr)
print(c.diag(), flush=True)
gl = c.gl
gl.glClearColor.argtypes = [ctypes.c_float] * 4
n = [0]
def frame():
    c.ctx.makeCurrentContext()
    gl.glViewport(0, 0, fr.winfo_width(), fr.winfo_height())
    gl.glClearColor(1.0, 0.0, 0.0, 1.0); gl.glClear(0x4000)
    c.swap(); n[0] += 1
    root.after(50, frame)
root.after(100, frame)
root.after(int(secs * 1000), root.destroy)
root.mainloop()
print('frames', n[0])
