"""deck.py - the Config window: one window over the running game with every display, sound and hardware
setting in strips (Tube, Room light, Case, Overlay, Sound, Unit, Circuit, Display, Cartridge). Every switch and
slider acts on the game at once; Quality and this game's own settings on top; hold H (or the Before button)
to see the look from before the deck opened. Keys: Tab / Esc close. The game keeps its controllers and keys while
the window is open (the owner, 2026-10-29: a controller no longer works the window - too many rows for it - and the
game can be played, and checked, with it open).
Also the first-run setup (setup_window)."""
import json, math, os, time
import tkinter as tk
import darkmenu
from tkinter import filedialog

V = None                                # (the app's module: set by vectrex_emu)
G, BG, DIM, HI, ROWBG, WARN = '#6a8', '#05080a', '#4a7a62', '#8cb', '#16302a', '#e93'
RED = '#e55'                            # (missing: the setup window, Get games & extras)
TOPBG, PANEL, BODY, EDGE, EDGE2 = '#0a1210', '#0b1511', '#0f1c17', '#1d3329', '#2f5544'   # (the areas: bars, boxes, the strip)


def _font(app, size, bold=False):
    return (V.MONO, size, 'bold') if bold else (V.MONO, size)


class FlatButton(tk.Label):
    """a button drawn by Tk itself: Windows' own button control was erased white before Tk painted it - white blocks
    for a moment whenever a tab or a window appeared. Hover, click (on release, over it), disabled, invoke: as tk.Button"""
    def __init__(self, parent, text='', command=None, **kw):
        tk.Label.__init__(self, parent, text=text, **kw)
        self._cmd = command
        self.bind('<Enter>', self._enter, add='+')
        self.bind('<Leave>', self._leave, add='+')
        self.bind('<ButtonRelease-1>', self._release, add='+')

    def _live(self):
        return str(tk.Label.cget(self, 'state')) != 'disabled'

    def _enter(self, e=None):
        if self._live(): tk.Label.configure(self, state='active')

    def _leave(self, e=None):
        if self._live(): tk.Label.configure(self, state='normal')

    def _release(self, e):
        if self._live() and self._cmd and 0 <= e.x < self.winfo_width() and 0 <= e.y < self.winfo_height():
            self._cmd()

    def configure(self, cnf=None, **kw):
        if 'command' in kw:
            self._cmd = kw.pop('command')
        return tk.Label.configure(self, cnf, **kw)
    config = configure

    def invoke(self):
        return self._cmd() if (self._cmd and self._live()) else None


def _button(parent, text, cmd, app, on=False, small=False):
    b = FlatButton(parent, text=text, command=cmd, font=_font(app, 9), relief='flat', bd=1, padx=(6 if small else 8) + 2,
                   pady=3, highlightthickness=1, highlightbackground=G, cursor='hand2')
    _paint(b, on)
    return b


def composited(t):
    """Windows: the window and all its controls painted into one buffer and shown when finished (WS_EX_COMPOSITED) -
    Tk's controls are windows of their own, painted one by one: a tab or window showed in blocks, white buttons first
    (their background erased before Tk draws them). Not for the main window (OpenGL)"""
    if not V.IS_WIN:
        return
    try:
        import ctypes
        u = ctypes.windll.user32
        t.update_idletasks()
        for h in (t.winfo_id(), u.GetParent(t.winfo_id())):
            if h:
                st = u.GetWindowLongW(h, -20)
                u.SetWindowLongW(h, -20, st | 0x02000000)                  # (GWL_EXSTYLE |= WS_EX_COMPOSITED)
    except Exception:   # noqa
        pass


def grab_screen(x, y, w, h):
    """a picture of the screen's rectangle - Windows: copied from the screen directly (GDI, ~17 ms for a Config
    window; PIL's ImageGrab copies the whole desktop first: ~65 ms); elsewhere PIL's ImageGrab"""
    from PIL import Image
    if V.IS_WIN:
        import ctypes, ctypes.wintypes as W
        u, g = ctypes.windll.user32, ctypes.windll.gdi32
        class BIH(ctypes.Structure):
            _fields_ = [('biSize', W.DWORD), ('biWidth', W.LONG), ('biHeight', W.LONG), ('biPlanes', W.WORD),
                        ('biBitCount', W.WORD), ('biCompression', W.DWORD), ('biSizeImage', W.DWORD),
                        ('biXPelsPerMeter', W.LONG), ('biYPelsPerMeter', W.LONG), ('biClrUsed', W.DWORD),
                        ('biClrImportant', W.DWORD)]
        u.GetDC.restype = W.HDC; u.GetDC.argtypes = [W.HWND]; u.ReleaseDC.argtypes = [W.HWND, W.HDC]
        g.CreateCompatibleDC.restype = W.HDC; g.CreateCompatibleDC.argtypes = [W.HDC]
        g.CreateCompatibleBitmap.restype = W.HBITMAP; g.CreateCompatibleBitmap.argtypes = [W.HDC, ctypes.c_int, ctypes.c_int]
        g.SelectObject.argtypes = [W.HDC, W.HGDIOBJ]; g.DeleteObject.argtypes = [W.HGDIOBJ]; g.DeleteDC.argtypes = [W.HDC]
        g.BitBlt.argtypes = [W.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.HDC, ctypes.c_int, ctypes.c_int, W.DWORD]
        g.GetDIBits.argtypes = [W.HDC, W.HBITMAP, W.UINT, W.UINT, ctypes.c_void_p, ctypes.c_void_p, W.UINT]
        sdc = u.GetDC(None); mdc = g.CreateCompatibleDC(sdc); bm = g.CreateCompatibleBitmap(sdc, w, h)
        try:
            g.SelectObject(mdc, bm)
            g.BitBlt(mdc, 0, 0, w, h, sdc, x, y, 0x00CC0020 | 0x40000000)     # (SRCCOPY | CAPTUREBLT)
            bi = BIH(); bi.biSize = ctypes.sizeof(BIH); bi.biWidth = w; bi.biHeight = -h; bi.biPlanes = 1; bi.biBitCount = 32
            buf = ctypes.create_string_buffer(w * h * 4)
            g.GetDIBits(mdc, bm, 0, h, buf, ctypes.byref(bi), 0)
        finally:
            g.DeleteObject(bm); g.DeleteDC(mdc); u.ReleaseDC(None, sdc)
        return Image.frombuffer('RGB', (w, h), buf, 'raw', 'BGRX', 0, 1)
    from PIL import ImageGrab
    return ImageGrab.grab((x, y, x + w, y + h), all_screens=True)


def no_transitions(t):
    """Windows: no fade or zoom when window t shows or hides (DWMWA_TRANSITIONS_FORCEDISABLED)"""
    if not V.IS_WIN:
        return
    try:
        import ctypes
        t.update_idletasks()
        h = ctypes.windll.user32.GetParent(t.winfo_id()) or t.winfo_id()
        v = ctypes.c_int(1)
        ctypes.windll.dwmapi.DwmSetWindowAttribute(h, 3, ctypes.byref(v), 4)
    except Exception:   # noqa
        pass


def paint_now(t):
    """window t painted completely, now: Windows asked to repaint all of it at once (its WM_PAINTs), then Tk's drawing
    run (Tk draws on its idle pass) - twice, for what the first pass moved"""
    try:
        t.update()
        if V.IS_WIN:
            import ctypes
            u = ctypes.windll.user32
            h = u.GetParent(t.winfo_id()) or t.winfo_id()
            u.RedrawWindow(h, None, None, 0x0001 | 0x0080 | 0x0100)    # (INVALIDATE ALLCHILDREN UPDATENOW)
        t.update_idletasks(); t.update()
    except Exception:   # noqa
        pass


def hold_paint(t, on):
    """Windows: the window not painted while it changes (WM_SETREDRAW), then painted once, all of it, at once"""
    if not V.IS_WIN:
        return
    try:
        import ctypes
        u = ctypes.windll.user32
        h = u.GetParent(t.winfo_id()) or t.winfo_id()
        u.SendMessageW(h, 0x000B, 0 if on else 1, 0)                       # (WM_SETREDRAW)
        if not on:
            u.RedrawWindow(h, None, None, 0x0001 | 0x0004 | 0x0080 | 0x0100)   # (INVALIDATE ERASE ALLCHILDREN UPDATENOW)
    except Exception:   # noqa
        pass


def no_close_button(t):
    """Windows: no close button in the window's title bar (its own Close button is below)"""
    if not V.IS_WIN:
        return
    try:
        import ctypes
        t.update_idletasks()
        h = ctypes.windll.user32.GetParent(t.winfo_id())
        st = ctypes.windll.user32.GetWindowLongW(h, -16)
        ctypes.windll.user32.SetWindowLongW(h, -16, st & ~0x00080000)       # (WS_SYSMENU off)
        ctypes.windll.user32.SetWindowPos(h, 0, 0, 0, 0, 0, 0x27)          # (frame redrawn: NOMOVE NOSIZE NOZORDER FRAMECHANGED)
    except Exception:   # noqa
        pass


def dialog(kind, title, text, parent=None, yesno=False, buttons=('Yes', 'No')):
    """a message in the dark style (Windows' own message boxes are light): kind 'info', 'warning', 'error' or
    'question'; yesno: Yes / No buttons (or `buttons`: other words; then Return does not answer), returns True for Yes.
    Waits until it is closed."""
    root = parent or tk._default_root
    t = tk.Toplevel(root, bg=BG); t.withdraw()
    t.title(title); t.resizable(False, False)
    if root is not None:
        t.transient(root)
    head = {'info': 'NOTE', 'warning': 'WARNING', 'error': 'ERROR', 'question': 'QUESTION'}.get(kind, '')
    bx = tk.Frame(t, bg=PANEL, highlightthickness=1, highlightbackground=EDGE); bx.pack(fill='x', padx=10, pady=(10, 0))
    tk.Label(bx, text=head, fg=WARN if kind in ('warning', 'error') else DIM, bg=PANEL, font=(V.MONO, 8), anchor='w').pack(fill='x', padx=10, pady=(4, 0))
    tk.Label(bx, text=text, fg=G, bg=PANEL, font=(V.MONO, 10), justify='left', anchor='w', wraplength=640).pack(padx=10, pady=(2, 10), anchor='w')
    foot = tk.Frame(t, bg=TOPBG, highlightthickness=1, highlightbackground=EDGE); foot.pack(fill='x', pady=(10, 0))
    res = [False]
    def done(v):
        res[0] = v
        try: t.grab_release()
        except tk.TclError: pass
        t.destroy()
    if yesno:
        _button(foot, buttons[1], lambda: done(False), None, small=True).pack(side='right', padx=(6, 12), pady=6)
        _button(foot, buttons[0], lambda: done(True), None, on=True, small=True).pack(side='right')
        if buttons == ('Yes', 'No'):
            t.bind('<Return>', lambda e: done(True))
        t.bind('<Escape>', lambda e: done(False))
    else:
        _button(foot, 'OK', lambda: done(True), None, on=True, small=True).pack(side='right', padx=12, pady=6)
        for k in ('<Return>', '<Escape>'):
            t.bind(k, lambda e: done(True))
    t.protocol('WM_DELETE_WINDOW', lambda: done(False))
    t.update_idletasks()
    if root is not None:
        x = root.winfo_rootx() + (root.winfo_width() - t.winfo_reqwidth()) // 2
        y = root.winfo_rooty() + (root.winfo_height() - t.winfo_reqheight()) // 3
        t.geometry('+%d+%d' % (max(0, x), max(0, y)))
    reveal(t, lambda: no_close_button(t)); t.focus_force()
    try: t.grab_set()
    except tk.TclError: pass
    t.wait_window()
    return res[0]


def work_area(w):
    """the usable desktop (x0, y0, x1, y1) on the monitor of window w: without the taskbar (Windows: the monitor's
    work area; elsewhere the screen less a margin)"""
    if V.IS_WIN:
        try:
            import ctypes, ctypes.wintypes as W
            class MI(ctypes.Structure):
                _fields_ = [('cbSize', W.DWORD), ('rcMonitor', W.RECT), ('rcWork', W.RECT), ('dwFlags', W.DWORD)]
            u = ctypes.windll.user32
            hwnd = u.GetParent(w.winfo_id()) or w.winfo_id()
            mon = u.MonitorFromWindow(hwnd, 2)                  # (MONITOR_DEFAULTTONEAREST)
            mi = MI(); mi.cbSize = ctypes.sizeof(MI)
            if u.GetMonitorInfoW(mon, ctypes.byref(mi)):
                r = mi.rcWork
                return r.left, r.top, r.right, r.bottom
        except Exception:   # noqa
            pass
    return 0, 0, w.winfo_screenwidth(), w.winfo_screenheight() - 60


class Scroller:
    """a scrolling area for a window's middle: what is put in .holder scrolls (the slim dark scrollbar, the mouse
    wheel) when fit() gets less height than it needs - a 1080p screen at 125 / 150 % - and is shown whole otherwise"""
    def __init__(self, parent, bg):
        self.frame = tk.Frame(parent, bg=bg)
        self.cv = tk.Canvas(self.frame, bg=bg, highlightthickness=0, bd=0)
        self.holder = tk.Frame(self.cv, bg=bg)
        self.win = self.cv.create_window(0, 0, window=self.holder, anchor='nw')
        self.sbar = darkmenu.DScroll(self.frame, command=self.cv.yview, bg=bg)
        self.cv.configure(yscrollcommand=self.sbar.set)
        self.cv.pack(side='left', fill='both', expand=True)
        self.cv.bind('<Configure>', lambda e: self.cv.itemconfigure(self.win, width=e.width))
        self.on = False

    def fit(self, max_h):
        self.holder.update_idletasks()
        w, h = self.holder.winfo_reqwidth(), max(self.holder.winfo_reqheight(), getattr(self, 'min_h', 0))
        on = h > max_h
        if on and not self.on:
            self.sbar.pack(side='right', fill='y', padx=(0, 3), pady=3)
        elif self.on and not on:
            self.sbar.pack_forget()
        self.on = on
        self.cv.configure(width=w, height=min(h, max(150, int(max_h))), scrollregion=(0, 0, w, h))
        if not on:
            self.cv.yview_moveto(0)

    def fit_window(self, t, margin=50):
        """as tall as window t can be on its screen (its other parts and the title bar kept)"""
        t.update_idletasks()
        x0, y0, x1, y1 = work_area(t)
        ui = t.winfo_fpixels('1i') / 96.0                        # (the title bar grows with the scaling)
        self.fit((y1 - y0) - (t.winfo_reqheight() - self.cv.winfo_reqheight()) - int(margin * ui))

    def wheel(self, e):
        if self.on:
            self.cv.yview_scroll(-1 if (getattr(e, 'delta', 0) > 0 or getattr(e, 'num', 0) == 4) else 1, 'units')

    def bind_wheel(self, t):
        for seq in ('<MouseWheel>', '<Button-4>', '<Button-5>'):
            t.bind(seq, self.wheel, add='+')

    def see(self, w):
        """scroll so that widget w (in holder) is in view"""
        if not self.on:
            return
        try:
            self.holder.update_idletasks()
            y = w.winfo_rooty() - self.holder.winfo_rooty(); h = w.winfo_height()
            total = max(1, self.holder.winfo_reqheight()); top = self.cv.canvasy(0); vis = self.cv.winfo_height()
            if y < top:
                self.cv.yview_moveto(y / float(total))
            elif y + h > top + vis:
                self.cv.yview_moveto((y + h - vis) / float(total))
        except tk.TclError:
            pass


def on_screen(t):
    """window t moved (if it must) so all of it is on its screen's desktop"""
    t.update_idletasks()
    x0, y0, x1, y1 = work_area(t)
    w, h = t.winfo_reqwidth(), t.winfo_reqheight()
    x, y = t.winfo_x(), t.winfo_y()
    nx = max(x0, min(x, x1 - w)); ny = max(y0, min(y, y1 - h - 40))
    if (nx, ny) != (x, y):
        t.geometry('+%d+%d' % (nx, ny))


def reveal(t, before=None):
    """show a built window at once, fully painted: mapped invisible (alpha 0), drawn, then made visible - shown
    plainly, Windows drew an empty frame first and the widgets a moment later. before(): done while it is still
    invisible (no_close_button changes the frame)"""
    try: t.attributes('-alpha', 0.0)
    except tk.TclError: pass
    t.deiconify()
    try:
        if before: before()
        t.update()
    except tk.TclError:
        pass
    try: t.attributes('-alpha', 1.0)
    except tk.TclError: pass


def center(t, over=None, before=None, focus=True):
    """a window placed in the middle of the emulator's (seen at once), drawn there before it shows"""
    over = over or getattr(V, 'APP_ROOT', None) or tk._default_root
    try:
        t.withdraw(); t.update_idletasks()
        x = over.winfo_rootx() + (over.winfo_width() - t.winfo_reqwidth()) // 2
        y = over.winfo_rooty() + (over.winfo_height() - t.winfo_reqheight()) // 3
        t.geometry('+%d+%d' % (max(0, x), max(0, y)))
        reveal(t, before); t.lift()
        if focus: t.focus_force()
    except tk.TclError:
        pass


def remember(t, name, app):
    """window t's place kept as it moves (app.win_pos, saved with the settings): it opens there next time"""
    if not hasattr(app, 'win_pos'):
        app.win_pos = {}
    def moved(e):
        if e.widget is t:
            try:
                if t.winfo_ismapped():
                    app.win_pos[name] = (t.winfo_x(), t.winfo_y())
            except tk.TclError:
                pass
    try:
        t.bind('<Configure>', moved, add='+')
    except tk.TclError:                                 # (the emulator closed meanwhile)
        pass


def saved_place(t, name, app):
    """(x, y) where window t was last time - if still on a screen - else None"""
    pos = (getattr(app, 'win_pos', None) or {}).get(name)
    if not pos:
        return None
    try:
        vx, vy = t.winfo_vrootx(), t.winfo_vrooty()
        vw, vh = max(t.winfo_screenwidth(), t.winfo_vrootwidth()), max(t.winfo_screenheight(), t.winfo_vrootheight())
        x, y = int(pos[0]), int(pos[1])
        if vx - 50 <= x <= vx + vw - 100 and vy - 10 <= y <= vy + vh - 60:
            return x, y
    except (tk.TclError, ValueError, TypeError, IndexError):
        pass
    return None


def place(t, name, app, before=None, focus=True):
    """window t where it was last time (if still on a screen), else in the middle of the emulator - and its place kept
    from now on (the owner, 2026-10-27: every window remembers where it was). focus=False: shown without taking the
    keyboard (a window that opens by itself - the chat: the player may be in another program). Never while another
    program has the keyboard: a window that opens by itself then shows without taking it"""
    if focus:
        try: focus = app.root.focus_get() is not None
        except (tk.TclError, KeyError): pass
    pos = saved_place(t, name, app)
    if pos:
        try:
            t.withdraw(); t.update_idletasks()
            t.geometry('+%d+%d' % pos)
            reveal(t, before); t.lift()
            if focus: t.focus_force()
        except tk.TclError:
            pass
    else:
        center(t, app.root, before, focus)
    remember(t, name, app)


def _rel(p):
    try:
        r = os.path.relpath(p, V.ROOT)
        return p if r.startswith('..') else r
    except ValueError:                                  # (another drive)
        return p


# ---- hover info: each row's text from the Help (help_text.py), the Vectrex unit's parts with the circuit behind them
STRIP_TOPIC = {'Tube': 'tube', 'Room light': 'light', 'Case': 'case', 'Overlay': 'overlay', 'Sound': 'sound',
               'Unit': 'unit', 'Circuit': 'unit', 'Display': 'display', 'Ports': 'addons', 'Head': 'headtrack'}
TIPBG, TIPFG = '#16261f', '#bfe'
TAB_CHG_FG = '#d9a35e'          # (a tab with a setting changed from its default: its name orange - the owner: the text only)
EXTRA = {   # (what used to be a note in the Config window, now in its row's i; 'GROUP/label' or 'label')
    'Case  (C)': 'Shown without an overlay; the Room light tab lights it.',
    'Controller port 2': "A game won't start with a speech module plugged in? Choose Joystick.",
    'ULTIMATE VECTREX MULTICART 2/Open the SD folder': 'Games that need files read them from it - Doom: doom/DOOM.WAD, Quake: quake/pak0.pak.',
    'ULTIMATE VECTREX MULTICART 2/Choose SD folder...': 'Games that need files read them from it - Doom: doom/DOOM.WAD, Quake: quake/pak0.pak.',
    'PITREX/Open the SD folder': 'Arcade games need your own ROMs: pitrex/roms (MAME), pitrex/roms/aae.',
    'PITREX/Choose SD folder...': 'Arcade games need your own ROMs: pitrex/roms (MAME), pitrex/roms/aae.',
    'WEAK GRAPHICS/30 pictures a second': 'Quality Low sets it.',
}
ALIAS = {'left / right': 'position and size', 'up / down': 'position and size', 'size': 'position and size'}   # (rows named unlike their Help item)


def _norm(s):
    """a label or a Help item's name, comparable: lower case, no '...', no (keys) or (notes), no punctuation"""
    import re
    s = re.sub(r'\(.*?\)', ' ', s.lower().replace('...', ' ').replace("'", ''))
    return ' '.join(re.sub(r'[^a-z0-9/ -]', ' ', s).split())


def help_info(label, topic=None):
    """the Help's text for a Config row (its item in `topic` first, then any topic) - None if it has none"""
    import help_text
    want = _norm(label)
    for pre in ('overlay ', 'scratches '):                      # (the strip names it already: Overlay tint strength)
        if want.startswith(pre) and len(want) >= len(pre) + 4:
            want = want[len(pre):]
    want = ALIAS.get(want, want)
    if len(want) < 3:
        return None
    best, score = None, 0
    for key, title, blocks in help_text.TOPICS:
        if key in ('start', 'system', 'carts', 'accuracy'):     # (the general topics: not about a setting)
            continue
        own = key == topic
        for b in blocks:
            if b[0] != 'item':
                continue
            for part in [_norm(x) for x in b[1].split(' / ')] + [_norm(b[1])]:
                if not part:
                    continue
                if part == want:
                    sc = 4
                elif part.startswith(want) and len(want) >= 4:                       # (a shortened label)
                    sc = 3
                elif want.startswith(part + ' ') and len(part) >= 0.6 * len(want):   # (a label with a few more words)
                    sc = 3
                elif part.endswith(' ' + want) and len(want) >= 10:                  # (the strip names what comes first)
                    sc = 3
                elif part.split()[:2] == want.split()[:2] and len(want.split()) >= 2 and len(part.split()) >= 2:
                    sc = 2
                else:
                    continue
                if not own and sc < 4:                          # (another strip's item: only by its exact name)
                    continue
                sc += 0.5 if own else 0
                if sc > score:
                    best, score = b[2], sc
    return best.replace('*', '') if best else None


class Tip:
    """an info box below widget w while the pointer rests on it (on every widget in `also` too)"""
    def __init__(self, w, text, also=()):
        self.w, self.text, self.win, self.job = w, text, None, None
        for x in (w,) + tuple(also):
            x.bind('<Enter>', self.enter, add='+'); x.bind('<Leave>', self.leave, add='+')
            x.bind('<ButtonPress>', self.leave, add='+')

    def enter(self, e=None):
        self.leave()
        try: self.job = self.w.after(450, self.show)
        except tk.TclError: self.job = None                 # (its widget gone: a tab rebuilt under the pointer)

    def leave(self, e=None):
        if self.job is not None:
            try: self.w.after_cancel(self.job)
            except tk.TclError: pass
            self.job = None
        if self.win is not None:
            try: self.win.destroy()
            except tk.TclError: pass
            self.win = None

    def show(self):
        self.job = None
        try:
            t = self.win = tk.Toplevel(self.w, bg=EDGE2)
            t.withdraw()                                    # (shown once in its place)
            t.overrideredirect(True)
            try: t.attributes('-topmost', True)
            except tk.TclError: pass
            paras = self.text.split('\n\n')
            box = tk.Frame(t, bg=TIPBG); box.pack(padx=1, pady=1)
            for i, p in enumerate(paras):
                tk.Label(box, text=p, bg=TIPBG, fg=TIPFG if i == 0 else HI, font=(V.MONO, 9), justify='left', anchor='w',
                         wraplength=470).pack(anchor='w', padx=9, pady=(6 if i == 0 else 4, 6 if i == len(paras) - 1 else 0))
            t.update_idletasks()
            x, y = self.w.winfo_rootx(), self.w.winfo_rooty() + self.w.winfo_height() + 4
            x0, y0, x1, y1 = work_area(self.w)
            if y + t.winfo_reqheight() > y1:                     # (no room below: above it)
                y = self.w.winfo_rooty() - t.winfo_reqheight() - 4
            x = max(x0, min(x, x1 - t.winfo_reqwidth()))
            t.geometry('+%d+%d' % (x, max(y0, y)))
            t.deiconify()
        except tk.TclError:
            self.win = None


def info_mark(parent, bg, text):
    """the small circled i of a row that has hover info (an empty space of its size when text is None: rows line up)"""
    c = tk.Canvas(parent, width=14, height=14, bg=bg, highlightthickness=0, bd=0)
    if text:
        c.create_oval(1, 1, 13, 13, outline=G)
        c.create_text(7, 7, text='i', fill=G, font=(V.MONO, 7, 'bold'))
        c.configure(cursor='question_arrow')
    return c


def _paint(b, on):
    b.configure(fg=BG if on else G, bg=G if on else BG, activebackground=HI if on else '#123',
                activeforeground=BG if on else HI)


class Deck:
    STRIPS = ('Tube', 'Overlay', 'Case', 'Room light', 'Head', 'Unit', 'Circuit', 'Ports', 'Sound', 'Display')   # (the screen from the inside out, the Vectrex, the PC - 2026-10-29)
    UNIT_STRIPS = ('Unit', 'Circuit')                   # (the Vectrex unit: split in two since 2026-10-28, the trim pots)

    def __init__(self, app):
        self.V = V
        self.app = app
        a = app
        self.before = dict(look=a.game_now()['look'], unit=dict(a.unit))   # (Before: the look as it was)
        self.comparing = None
        self.strip = getattr(a, 'deck_strip', 'Tube')
        if self.strip == 'Vectrex unit': self.strip = 'Unit'   # (its name before the split)
        if self.strip == 'Cartridge': self.strip = 'Ports'     # (its name before 2026-10-29)
        if self.strip not in self.STRIPS: self.strip = 'Tube'
        self.rows = []; self.focus = 0
        self.pad_prev = set(); self.pad_t = 0.0
        self.save_job = None
        t = self.t = tk.Toplevel(a.root, bg=BG)
        t.withdraw()                                            # (shown once it is built and in its place: no flash)
        t.title('Config'); t.transient(a.root); t.resizable(False, False)
        try: t.attributes('-toolwindow', True)
        except tk.TclError: pass
        a.inp.block = bool(a.browser)                           # (the game runs on behind it, with its input)
        f9, f10b = _font(a, 9), _font(a, 10, True)
        self.bgc = BG
        top = tk.Frame(t, bg=TOPBG, highlightthickness=1, highlightbackground=EDGE); top.pack(fill='x')
        tk.Label(top, text='Tab: close   hold H: compare with before   the game keeps its controls', fg=DIM, bg=TOPBG, font=f9).pack(side='left', padx=12, pady=6)
        # the whole emulator and this game: one panel
        pan = tk.Frame(t, bg=PANEL, highlightthickness=1, highlightbackground=EDGE); pan.pack(fill='x', padx=10, pady=(8, 0))
        q = tk.Frame(pan, bg=PANEL); q.pack(fill='x', padx=10, pady=(6, 0))
        tk.Label(q, text='QUALITY', fg=DIM, bg=PANEL, font=f9, width=10, anchor='w').pack(side='left')
        self.qbtn = {}
        for key in ('basic', 'low', 'medium', 'high', 'ultra', 'custom'):
            b = _button(q, a.QUALITY_NAMES[key], lambda k=key: self.set_quality(k), a, small=True); b.pack(side='left', padx=2)
            self.qbtn[key] = b
            tip = help_info(key.capitalize(), 'deck')
            if tip: Tip(b, tip)
        _button(q, 'Optimize', lambda: a.test_pc(self.t), a, small=True).pack(side='left', padx=(14, 2))
        self.qhint = tk.Label(q, fg=WARN, bg=PANEL, font=f9, anchor='w')   # (on the Quality row: nothing moves when it
        self.qhint.pack(side='left', padx=(12, 0))                          #  comes or goes)
        tk.Frame(pan, bg=EDGE, height=1).pack(fill='x', padx=10, pady=4)
        gm = tk.Frame(pan, bg=PANEL); gm.pack(fill='x', padx=10)
        tk.Label(gm, text='THIS GAME', fg=DIM, bg=PANEL, font=f9, width=10, anchor='w').pack(side='left')
        self.gbtn = {}
        for key, name, cmd in (('save', 'Save', a.save_game), ('revert', 'Revert', a.revert_game),
                               ('remove', 'Remove', a.remove_game), ('default', 'Make default', a.make_default)):
            b = _button(gm, name, lambda c=cmd: (c(), self.sync_all()), a, small=True); b.pack(side='left', padx=2)
            self.gbtn[key] = b
            tip = help_info(name, 'deck')
            if tip: Tip(b, tip)
        self.ghint = tk.Label(pan, fg=DIM, bg=PANEL, font=f9, anchor='w'); self.ghint.pack(fill='x', padx=(10 + 10 * 8, 10), pady=(0, 6))
        # the strips: tabs on top of the strip's area (the chosen one in the area's colour)
        sb = tk.Frame(t, bg=BG); sb.pack(fill='x', padx=10, pady=(10, 0))
        self.sbtn = {}
        for st in self.STRIPS:
            b = tk.Label(sb, text=st, font=f9, padx=8, pady=3, cursor='hand2', highlightthickness=1)
            b.bind('<Button-1>', lambda e, st=st: self.switch(st)); b.pack(side='left', padx=(0, 2))
            self.sbtn[st] = b
        self.body = tk.Frame(t, bg=BODY, highlightthickness=1, highlightbackground=EDGE2); self.body.pack(fill='x', padx=10)
        self.sc = Scroller(self.body, BODY); self.sc.frame.pack(fill='both', expand=True)   # (a short screen: the strip scrolls)
        self.sc.bind_wheel(t)
        self.inner = None; self.cols = None; self.split = None
        foot = tk.Frame(t, bg=TOPBG, highlightthickness=1, highlightbackground=EDGE); foot.pack(fill='x', pady=(10, 0))
        self.reset_b = _button(foot, 'Reset this tab', self.reset_strip, a, small=True); self.reset_b.pack(side='left', padx=(12, 0), pady=6)
        _button(foot, 'Reset all', self.reset_all, a, small=True).pack(side='left', padx=(8, 0), pady=6)
        bb = _button(foot, 'Hold: before', None, a, small=True); bb.pack(side='left', padx=8)
        bb.bind('<ButtonPress-1>', lambda e: self.compare(True)); bb.bind('<ButtonRelease-1>', lambda e: self.compare(False))
        tk.Label(foot, text='right click a slider: its default', fg=DIM, bg=TOPBG, font=f9).pack(side='left', padx=8)
        for k in ('<Tab>', '<Escape>'):
            t.bind(k, lambda e: (self.close(), 'break')[1])
        t.bind('<F11>', lambda e: (a.toggle_full(), 'break')[1])   # (full screen also while the Config has the keys)
        t.bind('<KeyPress-h>', lambda e: self.compare(True)); t.bind('<KeyRelease-h>', lambda e: self.compare(False))
        # the emulator's own keys - P, O, L, C, +, -, 0, 9, Ctrl+R, Page Up / Down, Alt + a menu ... - work with this window
        # in front too: the main window's bindings, the same calls (the owner, 2026-10-29; the game's own keys reach it
        # anyway). Not this window's own (Tab, Esc, F11, H), nor the ROM browser's (arrows, Enter, Backspace), nor any key
        # (switching on) or the mouse's
        own = set(t.bind()) | {'<Key-Tab>', '<Key-Escape>', '<Key-F11>', 'h', 'H', '<Key>', '<Key-Right>', '<Key-Left>',
                               '<Key-Up>', '<Key-Down>', '<Key-Return>', '<Key-BackSpace>', '<Motion>', '<FocusOut>'}
        for seq in a.root.bind():
            if seq not in own and ('Key' in seq or len(seq) == 1):
                try: t.tk.call('bind', str(t), seq, a.root.bind(seq))
                except tk.TclError: pass
        t.protocol('WM_DELETE_WINDOW', self.close)
        self.fix_width()
        self.prebuild()
        self.show(self.strip)
        t.update_idletasks()
        r = a.root
        pos = getattr(a, 'deck_pos', None)                       # (where it was last time - if still on a screen)
        sw, sh = t.winfo_screenwidth(), t.winfo_screenheight()
        vx, vy = t.winfo_vrootx(), t.winfo_vrooty()
        vw, vh = max(sw, t.winfo_vrootwidth()), max(sh, t.winfo_vrootheight())
        if pos and vx - 50 <= pos[0] <= vx + vw - 100 and vy - 10 <= pos[1] <= vy + vh - 60:
            x, y = pos
        else:
            x = r.winfo_rootx() + max(0, (r.winfo_width() - t.winfo_reqwidth()) // 2)
            y = r.winfo_rooty() + max(0, (r.winfo_height() - t.winfo_reqheight()) // 3)
        x0, y0, x1, y1 = work_area(t)                             # (all of it on the desktop)
        x = max(x0, min(x, x1 - t.winfo_reqwidth())); y = max(y0, min(y, y1 - t.winfo_reqheight() - 40))
        t.geometry('+%d+%d' % (x, y))
        over_full(t, a)                                         # (full screen: kept above the picture, on its screen)
        reveal(t)
        try: V.dark_title(t)                                    # (the system's own title bar and close button - where
        except Exception: pass   # noqa                         #  each system puts it: Windows / Linux right, macOS left -
        t.focus_force()                                         #  dark, as the main window's; Graham Toal, 2026-10-26)
        self.tick()

    # ---- the strips: rows of (kind, ...) - check / slider / unit / choice / buttons / note
    def spec(self, s):
        return self._spec(s)

    def _spec(self, s):
        a, V = self.app, self.V
        R = lambda: a.ren
        C = lambda: a.run.crt
        def A(k, d=False):
            if self.reading is not None:
                self.reading.append(k)                  # (a check's setting: for its changed mark, see mk_check)
            return getattr(a.ren, k, d) if a.ren else d
        has = lambda k: bool(a.ren) and hasattr(a.ren, k)
        PH = lambda: A('phosphor'); AG = lambda: A('phosphor') and A('trail_on')
        OV = lambda: bool(A('art_on') and A('art', None)); SC = lambda: OV() and A('scr_on')
        flip = lambda k: (lambda: a.flip(a.ren, k))
        snd = self.sound_changed
        if s == 'Tube':
            return [('group', 'PHOSPHOR'),
                    ('check', 'Phosphor', PH, a.toggle_phosphor, None),
                    ('in', [('slider', 'bloom', PH),
                            ('check', 'Afterglow', lambda: A('trail_on'), flip('trail_on'), PH),
                            ('in', [('slider', 'trail', AG),
                                    ('check', 'Afterglow joining', lambda: A('phos_grow_on'), flip('phos_grow_on'), AG),
                                    ('in', [('slider', 'phos_grow', lambda: AG() and A('phos_grow_on'))]),
                                    ('check', 'Afterglow smoothing', lambda: A('phos_smooth_on'), flip('phos_smooth_on'), AG),
                                    ('in', [('slider', 'phos_smooth', lambda: AG() and A('phos_smooth_on'))])])]),
                    ('slider', 'white_k', lambda: has('white_k')),
                    ('group', 'SHAPE AND GLASS'),
                    ('check', 'Tube geometry', lambda: A('curve_on'), flip('curve_on'), None),
                    ('in', [('slider', 'geom_k', lambda: A('curve_on'))]),
                    ('slider', 'glass_k', lambda: True),
                    ('check', 'Glass edge', lambda: A('glass_on'), flip('glass_on'), None),
                    ('check', 'Bend smoothing', lambda: A('bend_smooth', True), flip('bend_smooth'), None),
                    ('group', 'GLOW AND BEAM'),
                    ('check', 'Second glow pass', lambda: A('passes', 2) >= 2,
                     lambda: (setattr(a.ren, 'passes', 1 if a.ren.passes >= 2 else 2), a.save_settings()), PH),
                    ('check', 'Dither', lambda: A('dither_on', True), flip('dither_on'), None),
                    ('check', 'Corner glow', lambda: A('halo_on'), flip('halo_on'), lambda: not A('even_beam')),
                    ('in', [('slider', 'halo_k', lambda: A('halo_on') and not A('even_beam'))]),
                    ('slider', 'dot_k', lambda: not A('even_beam')),
                    ('check', 'Even beam', lambda: A('even_beam'), flip('even_beam'), lambda: not A('beam_trace')),
                    ('check', 'Beam trace', lambda: A('beam_trace'), flip('beam_trace'), lambda: has('beam_trace')),
                    ('group', 'SIGNAL'),
                    ('check', 'Mains hum wobble', lambda: A('wob_on'), a.toggle_wobble, None),
                    ('in', [('slider', 'wob_strength', lambda: A('wob_on')),
                            ('choice', 'Mains', [('%d Hz' % hz, (lambda hz=hz: A('mains_hz', 0) == hz), (lambda hz=hz: a.set_mains(hz)))
                                                 for hz in V.vecrender.MAINS_HZ], lambda: A('wob_on'), lambda: self.changed('mains_hz'))]),
                    ('check', 'Brightness bending', lambda: A('bend_on'), flip('bend_on'), None),
                    ('in', [('choice', 'Bending type', [(n, (lambda i=i: A('bend_type', 0) % len(V.vecrender.BEND_TYPES) == i),
                                                         (lambda i=i: a.set_bend_type(i))) for i, n in enumerate(V.vecrender.BEND_TYPES)],
                             lambda: A('bend_on'), lambda: self.changed('bend_type')),
                            ('slider', 'bend_strength', lambda: A('bend_on'))])]
        if s == 'Room light':
            rows = [('group', 'THE ROOM'), ('slider', 'art_room', None),
                    ('check', 'Glass catches room light', lambda: A('glass_room'), flip('glass_room'), None)]
            if getattr(a.ren, 'lprog', None):
                rows += [('group', 'THE CEILING LIGHT'),
                         ('check', 'Reflected on the glass', lambda: A('light_on'), flip('light_on'), lambda: A('glass_room')),
                         ('in', [('slider', 'light_k', lambda: A('glass_room') and A('light_on'))]),
                         ('check', 'Reflected on the overlay', lambda: A('light_ov'), flip('light_ov'), None),
                         ('in', [('slider', 'light_ov_k', lambda: A('light_ov'))]),
                         ('buttons', [('Light position...', a.light_pad)], lambda: A('light_on') or A('light_ov'),
                          lambda: self.changed('light_x') or self.changed('light_y'))]
            return rows
        if s == 'Case':
            CS = lambda: A('case_on')
            return [('group', 'THE PLASTIC ROUND THE GLASS'),
                    ('check', 'Case  (C)', CS, a.toggle_case, None),
                    ('in', [('slider', 'case_refl', CS), ('slider', 'case_wear', CS)]),
                    ]
        if s == 'Overlay':
            rows = [('group', 'SHEET'),
                    ('check', 'Overlay  (O)', lambda: A('art_on') and bool(A('art', None)), a.toggle_art, None),
                    ('check', 'Choose automatically', lambda: a.art_auto, a.toggle_art_auto, None, lambda: not a.art_auto),
                    ('buttons', [('Choose overlay...', a.choose_art), ('No overlay for this game', a.no_art)], None),
                    ('in', [('slider', 'art_tint', OV), ('slider', 'art_print', OV),
                            ('check', 'Parallax', lambda: A('art_parallax'), flip('art_parallax'), OV)])]
            if getattr(a.ren, 'gprog', None):
                rows += [('group', 'PLASTIC AND SCRATCHES'),
                         ('check', 'Plastic glow', lambda: A('art_glow'), flip('art_glow'), OV),
                         ('in', [('slider', 'art_glow_k', lambda: OV() and A('art_glow'))]),
                         ('check', 'Scratches', lambda: A('scr_on'), a.toggle_scratches, OV),
                         ('in', [('slider', 'scr_wear', SC), ('slider', 'scr_fine', SC), ('slider', 'scr_medium', SC),
                                 ('slider', 'scr_large', SC), ('slider', 'scr_scrib', SC), ('slider', 'scr_glint', SC),
                                 ('slider', 'scr_reach', SC), ('buttons', [('New random scratches', a.new_scratches)], SC)])]
            rows += [('group', 'POSITION ON THE SCREEN'),
                     ('slider', 'art_dx', OV), ('slider', 'art_dy', OV), ('slider', 'art_scale', OV),
                     ('note', lambda: a.art_adj_note()),
                     ('buttons', [('Make global default', a.art_adj_global), ('Reset', a.reset_art_adj)], OV)]
            return rows
        if s == 'Sound':
            return [('group', 'THE VECTREX'), ('slider', 'volume', None),
                    ('check', 'Vectrex buzz', lambda: C().buzz_on, lambda: a.flip(a.run.crt, 'buzz_on'), None, lambda: snd('buzz_on')),
                    ('in', [('slider', 'buzz_vol', lambda: C().buzz_on)]),
                    ('check', 'Vectrex speaker', lambda: C().speaker_on, lambda: a.flip(a.run.crt, 'speaker_on'), None,
                     lambda: snd('speaker_on')),
                    ('group', 'SPEECH MODULE (PORT 2)'),
                    ('slider', 'voice_vol', lambda: a.speech in ('vecvoice', 'vecvox')),
                    ('choice', 'Speech module', [('Full-range speakers', lambda: C().voice_full, lambda: a.set_voice_full(True)),
                                                 ('Small speaker', lambda: not C().voice_full, lambda: a.set_voice_full(False))],
                     lambda: a.speech in ('vecvoice', 'vecvox'), lambda: snd('voice_full')),
                    ('group', '3D IMAGER (PORT 2)'),
                    ('check', 'Disc motor sound', lambda: C().motor_on, a.toggle_motor_snd, lambda: a.speech == 'imager',
                     lambda: snd('motor_on')),
                    ('in', [('slider', 'motor_vol', lambda: a.speech == 'imager' and C().motor_on)])]
        if s == 'Unit':                                     # (the Vectrex unit, what an owner turns)
            return [('group', 'WHICH VECTREX'),
                    ('choice', 'Unit', [(V.UNIT_NAMES[n].replace(' (measured)', ''), (lambda n=n: a.unit_name == n),
                                         (lambda n=n: a.unit_preset(n))) for n in ('owner', 'ideal', 'cranky', 'random')], None,
                     lambda: a.unit_name != 'owner'),
                    ('buttons', [('Export...', a.export_unit), ('Import...', a.import_unit)], None),
                    ('group', 'STARTING UP'),
                    ('check', 'Start switched on', lambda: a.start_on, a.toggle_start_on, None, lambda: bool(a.start_on)),
                    ('check', 'Skip the Vectrex intro', lambda: a.skip_boot, a.toggle_skip_boot, None, lambda: bool(a.skip_boot)),
                    ('group', 'THE PICTURE: KNOB, POTS AND YOKE')] + [('unit', k) for k in V.UNIT_TAB_KEYS]
        if s == 'Circuit':                                  # (the Vectrex unit: the service trimmers, the analog parts)
            keys = [p[0] for p in V.UNIT_PARAMS if p[0] not in V.UNIT_TAB_KEYS]
            pots = ('dacofs', 'drift_x', 'drift_y')
            rest = [k for k in keys if k not in pots]
            cut = rest.index('zerotx')                          # (boxes: side by side when the screen is short)
            return ([('group', 'SERVICE TRIM POTS')] + [('unit', k) for k in pots + ('ring',)]
                    + [('group', 'ITS PARTS: ZERO REFERENCE, BEAM')] + [('unit', k) for k in rest[:cut]]
                    + [('group', 'ITS PARTS: ZEROING, LAG, DAC')] + [('unit', k) for k in rest[cut:]] + [('unit', 'gap')])
        if s == 'Display':
            rates = [('Matched to the monitor', lambda: not a.vrr and a.rate != 'max', lambda: a.set_screen('60')),
                     ("Monitor's full rate", lambda: not a.vrr and a.rate == 'max', lambda: a.set_screen('max')),
                     ('G-Sync / FreeSync', lambda: a.vrr, lambda: a.set_screen('vrr'))]
            rot = [(n, (lambda d=d: a.rot == d), (lambda d=d: a.set_rotation(d))) for d, n in
                   ((0, 'Normal'), (90, '90 right'), (180, '180'), (270, '90 left'))]
            return [('group', 'THE SCREEN'), ('choice', 'Screen', rates, None),
                    ('in', [('check', 'Smooth motion', lambda: a.smooth,
                             a.toggle_smooth, lambda: not a.vrr)]),
                    ('choice', 'Rotate', rot, None, lambda: a.rot != 0),
                    ('group', 'WEAK GRAPHICS'),
                    ('check', '30 pictures a second', lambda: bool(getattr(a.ren, 'half_rate', False)),
                     lambda: (setattr(a.ren, 'half_rate', not getattr(a.ren, 'half_rate', False)), a.save_settings()), None,
                     lambda: self.changed('half_rate')),
                    ('choice', 'Phosphor size', [(n, (lambda v=v: abs(getattr(a.ren, 'phos_scale', 1.0) - v) < 0.01),
                                                  (lambda v=v: (setattr(a.ren, 'phos_scale', v), a.save_settings())))
                                                 for n, v in (('Full', 1.0), ('3/4', 0.75), ('Half', 0.5))], None,
                     lambda: self.changed('phos_scale')),
                    ('group', 'BELOW THE PICTURE'),
                    ('check', 'On-screen display', lambda: a.fps_on, a.toggle_fps, None, lambda: not a.fps_on),
                    ('in', [('check', 'CPU and GPU meters', lambda: getattr(a, 'osd_meters', True),
                             a.toggle_osd_meters, lambda: a.fps_on, lambda: not getattr(a, 'osd_meters', True))]),
                    ('check', 'Volume knob and RESET button', lambda: a.knob_on, a.toggle_knob, None, lambda: not a.knob_on),
                    ('check', 'Cartridge', lambda: a.cart_on, a.toggle_cart, None, lambda: not a.cart_on),
                    ('group', 'SIZE'),
                    ('slider', 'monitor_inch', None),
                    ('buttons', [('Authentic (9)', a.real_size), ('Fit screen (0)', lambda: a.zoom_step(None))], None)]
        if s == 'Head':
            HD = lambda: A('head_on')
            return [('group', 'HEAD TRACKING'),
                    ('check', 'Head tracking', HD, flip('head_on'), None),
                    ('in', [('note', lambda: a.head_note()),
                            ('buttons', [('Centre', a.head_centre)], HD)]),
                    ('buttons', [('SmoothTrack: what it is, setting it up...', lambda: a.help_window('headtrack'))], None),
                    ('group', 'THE PHONE'),
                    ('slider', 'head_tilt', HD),
                    ('group', 'SMOOTHING'),
                    ('slider', 'head_smooth_x', HD), ('slider', 'head_smooth_z', HD), ('slider', 'head_smooth_y', HD),
                    ('group', 'PICTURE BEHIND THE OVERLAY'),
                    ('check', 'Picture shift (parallax)', lambda: A('head_pic_on', True), flip('head_pic_on'), HD),
                    ('in', [('slider', 'head_k', lambda: HD() and A('head_pic_on', True))]),
                    ('group', 'ROOM LIGHT REFLECTIONS'),
                    ('check', 'Reflections on the glass', lambda: A('head_glass_on', True), flip('head_glass_on'), HD),
                    ('in', [('slider', 'head_glass_k', lambda: HD() and A('head_glass_on', True))]),
                    ('check', 'Reflections on the overlay', lambda: A('head_ov_on', True), flip('head_ov_on'), HD),
                    ('in', [('slider', 'head_ov_k', lambda: HD() and A('head_ov_on', True))])]
        if s == 'Ports':
            hw = [(lbl, (lambda k=k: a.cart_hw == k), (lambda k=k: a.set_cart_hw(k))) for k, lbl in V.CART_HW.items()]
            p2 = [(lbl, (lambda k=k: a.speech == k), (lambda k=k: a.set_speech(k))) for k, lbl in V.PORT2.items()]
            um2 = lambda: (a.args.rom or '').lower().endswith('.um2')
            IM = lambda: a.speech == 'imager'                    # (the 3D Imager in port 2: its rows usable)
            def px_now():
                r = a.args.rom or ''
                if not r.lower().endswith(('.img', '.pit')):
                    return 'not in the slot'
                n = os.path.splitext(os.path.basename(r))[0]
                return 'in the slot now: ' + ('its menu' if n.lower() == 'pitrex' else n)
            return [('group', 'THE SLOT AND PORT 2'),
                    ('menu', 'Cartridge hardware', hw, None, lambda: a.cart_hw != 'auto'), ('menu', 'Controller port 2', p2, None),
                    ('check', 'Auto-select port 2', lambda: getattr(a, 'p2_auto', True), a.toggle_p2_auto, None,
                     lambda: not getattr(a, 'p2_auto', True)),
                    ] + ([('in', [('check', 'Disc motor sound', lambda: C().motor_on, a.toggle_motor_snd, None, lambda: snd('motor_on')),
                                  ('slider', 'motor_vol', lambda: C().motor_on),
                                  ('check', 'Spin-up note on the picture', lambda: a.imager_note, a.toggle_imager_note, None,
                                   lambda: not a.imager_note)])]
                         if IM() else []) + [   # (the 3D Imager in port 2: its motor and note)
                    ('group', 'USB KEYBOARD AND MOUSE'),
                    ('check', 'PC mouse and keys on USB', lambda: a.usb_hid, a.toggle_usb_hid, None, lambda: not a.usb_hid),
                    ('group', 'ULTIMATE VECTREX MULTICART 2'),
                    ('note', lambda: 'in the slot now' if um2() else 'not in the slot'),
                    ('check', 'UVMC2 games in the ROM list', lambda: a.show_um2, a.toggle_um2, None, lambda: not a.show_um2),
                    ('choice', 'Headphone jack', [('Clean (line out)', lambda: C().jack_mode == 'clean', lambda: a.set_jack('clean')),
                                                  ('Through the speaker', lambda: C().jack_mode == 'speaker', lambda: a.set_jack('speaker')),
                                                  ('Off', lambda: C().jack_mode == 'off', lambda: a.set_jack('off'))], None,
                     lambda: snd('jack_mode')),
                    ('in', [('slider', 'jack_vol', lambda: C().jack_mode != 'off')]),
                    ('buttons', [('Open UVMC2 game...', a.open_um2), ('Open the SD folder', a.open_sd_dir),
                                 ('Choose SD folder...', a.choose_sd_dir)], None),
                    ('note', lambda: 'SD folder: %s' % _rel(a.sd_dir)),
                    ('group', 'PITREX'),
                    ('note', px_now),
                    ('buttons', [('Start the PiTrex', a.start_pitrex), ('Open PiTrex program...', a.open_pitrex)], None),
                    ('buttons', [('Open the SD folder', a.open_px_dir), ('Choose SD folder...', a.choose_px_dir)], None),
                    ('note', lambda: 'SD folder: %s' % _rel(a.px_dir))]
        return []

    RESETS = {'Tube': 'reset_tube', 'Room light': 'reset_light', 'Case': 'reset_case', 'Overlay': 'reset_overlay', 'Sound': 'reset_sound',
              'Display': 'reset_display', 'Ports': 'reset_addons', 'Head': 'reset_head'}

    def reset_all(self):
        """every tab to its defaults: those of the Quality profile chosen (or Optimize's result) - asked first"""
        a = self.app
        if dialog('question', 'Reset all', 'Reset every tab to the %s defaults?' % a.base_name(), parent=self.t, yesno=True,
                  buttons=('Reset all', 'Cancel')):
            self.frozen(lambda: (a.reset_all(), self.show(self.strip)))
        else:
            self.sync_all()

    def reset_strip(self):
        def go():
            if self.strip in self.UNIT_STRIPS:
                self.app.reset_unit(self.strip.lower())
            elif self.strip in self.RESETS:
                getattr(self.app, self.RESETS[self.strip])()
            self.show(self.strip)                               # (kept: its values set; rebuilt only if its rows changed)
        self.frozen(go)

    def layout_key(self):
        """the screen the layout was measured for: its work area and scaling"""
        try:
            return (work_area(self.t), round(self.t.winfo_fpixels('1i'), 1))
        except tk.TclError:
            return None

    def fix_width(self):
        """the window as wide as its widest tab, always: switching tabs never changes its width (the owner, 2026-10-27).
        Each tab built once to measure it (the window not shown yet); kept for the session, per screen"""
        a, t = self.app, self.t
        try:
            key = (work_area(t), round(t.winfo_fpixels('1i'), 1))
            cache = getattr(a, 'deck_width', None)
            if not cache or cache[0] != key:
                was = self.strip
                self.show('Display'); t.update_idletasks()      # (the widest tab: the owner, 2026-10-27)
                widest = t.winfo_reqwidth()
                self.strip = was
                a.deck_width = cache = (key, widest)
            if getattr(self, 'width_pin', None) is None:
                self.width_pin = tk.Frame(t, bg=BG, height=0)
                self.width_pin.pack(side='top', before=t.pack_slaves()[0])
            self.width_pin.configure(width=cache[1])
        except (tk.TclError, IndexError):
            pass

    def switch(self, st):
        """another tab, shown cleanly: behind a still picture of the window until the new tab is fully painted (Windows
        paints a new tab's controls one by one: empty boxes and white buttons for a frame or two)"""
        if st == self.strip:
            return
        try:
            shown = self.t.winfo_ismapped()
        except tk.TclError:
            shown = False
        if shown:                                               # (kept tabs: hide one, show the other - behind a
            self.frozen(lambda: self.show(st))                  #  picture of the window until painted: Windows paints
        else:                                                   #  ~150 controls over more than one screen refresh)
            self.show(st)

    def frozen(self, work):
        """work done behind a still picture of the window, which then fades away: a reset redraws the tab, its rows and
        maybe the window's size several times - seen, a jumble (the owner, 2026-10-27). Without a picture (no PIL, an
        error): done as it is"""
        t = self.t
        cover = None
        try:
            from PIL import ImageTk
            x, y, w, h = t.winfo_rootx(), t.winfo_rooty(), t.winfo_width(), t.winfo_height()
            img = grab_screen(x, y, w, h)
            cv = getattr(self, '_cover', None)                  # (one cover window, made once: kept shown but off the
            if cv is None:                                      #  screen - shown fresh, Windows faded it in over a few
                c = tk.Toplevel(t, bg=BG); c.overrideredirect(True)   # frames while the tab changed under it)
                c.geometry('1x1+-32000+-32000')
                try: c.attributes('-topmost', True)
                except tk.TclError: pass
                lb = tk.Label(c, bd=0, highlightthickness=0); lb.pack()
                no_transitions(c)
                cv = self._cover = (c, lb)
            cover, lb = cv
            ph = ImageTk.PhotoImage(img)
            lb.configure(image=ph); lb.image = ph
            cover.attributes('-alpha', 0.0)                     # (put in place unseen and painted there, then shown: off
            cover.geometry('%dx%d+%d+%d' % (w, h, x, y))        #  the screen Windows does not refresh its picture - moved
            cover.lift(); paint_now(cover)                      #  on, it showed the switch before's for a refresh: a
            cover.attributes('-alpha', 1.0); cover.update_idletasks()   # flicker; the owner, 2026-10-27)
        except Exception:   # noqa
            cover = None
        try:
            work()
        finally:
            if cover is not None:
                paint_now(t)                            # (the new state fully painted underneath first)
                def gone():
                    try: cover.attributes('-alpha', 0.0); cover.geometry('+-32000+-32000')   # (parked again)
                    except tk.TclError: pass
                t.after(34, gone)                       # (then, a refresh later, the picture away: the new state, whole)

    # ---- building a strip
    def show(self, s):
        if getattr(self, 'strip', None) and self.rows:          # (the row a controller was on, per strip: kept)
            self.app.deck_focus = dict(getattr(self.app, 'deck_focus', None) or {}, **{self.strip: self.focus})
        self.strip = s; self.app.deck_strip = s
        self.focus = (getattr(self.app, 'deck_focus', None) or {}).get(s, 0)
        self.paint_tabs()
        # each tab built once and kept: switching hides one and shows the other, a reset only sets the values. Built
        # anew every time, Windows created and painted its ~150 controls one by one - ~0.2 s, seen as blocks. A tab is
        # built again only when its rows change (the 3D Imager's rows on Cartridge...) or the screen does
        old = getattr(self, 'shown', None)
        ent = self.ensure(s)
        self.inner, self.rows, self.cols, self.split = ent['frame'], ent['rows'], ent['cols'], ent['split']
        self.focus = min(self.focus, max(0, len(self.rows) - 1))
        self.sync_all()
        if old is not None and old is not self.inner:
            try: old.pack_forget()
            except tk.TclError: pass
        if not self.inner.winfo_manager():
            self.inner.pack(fill='both', expand=True, padx=8, pady=8)
        self.shown = self.inner
        try:
            self.sc.fit_window(self.t)
            if self.t.winfo_ismapped():
                on_screen(self.t)
        except tk.TclError:
            pass

    def ensure(self, s):
        """tab s built (not shown) and kept - again only when its rows or the screen changed"""
        tabs = self.__dict__.setdefault('tabs', {})
        key = self.layout_key()
        sig = self.signature(s)
        ent = tabs.get(s)
        if ent and ent['sig'] == sig and ent['key'] == key:
            return ent
        keep = (self.inner, self.rows, self.cols, getattr(self, 'split', None))
        if ent:
            try: ent['frame'].destroy()
            except tk.TclError: pass
        was = self.strip; self.strip = s                        # (the rows' hover info looks up this tab's Help)
        try:
            lay = getattr(self.app, 'deck_layout', None)        # (each tab's columns: measured once, then built so)
            if lay and lay[0] == key and s in lay[1]:
                self.build(s, lay[1][s])
            else:
                self.build(s, None)
                split = self.two_columns()
                if split:                                       # (too tall for this screen: its boxes side by side)
                    self.inner.destroy(); self.build(s, split)
                    if self.too_wide():
                        self.inner.destroy(); self.build(s, None); split = None
                if not lay or lay[0] != key:
                    lay = self.app.deck_layout = (key, {})
                lay[1][s] = split
            ent = tabs[s] = dict(frame=self.inner, rows=self.rows, cols=self.cols, split=self.split, sig=sig, key=key)
        finally:
            self.strip = was
            self.inner, self.rows, self.cols, self.split = keep
        return ent

    def prebuild(self):
        """every tab built at once (none shown - quick: nothing is painted), and the window as tall as the tallest: a
        switch then only shows a page that is ready, the window's size never changes"""
        try:
            for st in self.STRIPS:
                self.ensure(st)
            self.t.update_idletasks()
            self.sc.min_h = max(e['frame'].winfo_reqheight() for e in self.tabs.values()) + 16
        except tk.TclError:
            pass

    def signature(self, s):
        """a tab's rows as their kinds and names: built again only when these change"""
        def sig(rows):
            out = []
            for r in rows:
                if r[0] == 'in':
                    out.append(('in', sig(r[1])))
                elif r[0] == 'buttons':
                    out.append(('buttons', tuple(t for t, c in r[1])))
                else:
                    out.append((r[0], r[1] if isinstance(r[1], str) else ''))
            return tuple(out)
        try:
            return sig(self._spec(s))
        except Exception:   # noqa
            return None

    def paint_tabs(self):
        """the tabs: the chosen one joined to the area below it; one with a setting changed from its default: its name
        orange"""
        for k, b in self.sbtn.items():
            try: chg = self.tab_changed(k)
            except Exception: chg = False   # noqa
            on = k == self.strip
            b.configure(bg=BODY if on else BG, fg=TAB_CHG_FG if chg else (HI if on else DIM),
                        highlightbackground=EDGE2 if on else EDGE)

    def tab_changed(self, st):
        """a setting on tab st differs from its default: one of its rows' tests (row_chg - what the row shows too)"""
        def walk(rows):
            for row in rows:
                if row[0] == 'in':
                    if walk(row[1]):
                        return True
                    continue
                if row[0] == 'group':
                    continue
                chg = self.row_chg(row)
                try:
                    if chg and chg():
                        return True
                except Exception:   # noqa
                    pass
            return False
        return walk(self._spec(st))

    def build(self, s, split):
        """the tab's rows; split: the number of boxes in the left column (None: one column)"""
        self.inner = tk.Frame(self.sc.holder, bg=BODY)          # (not packed yet: show swaps it in when it is ready)
        self.cols = None
        if split:
            self.cols = [tk.Frame(self.inner, bg=BODY) for _ in range(2)]
            self.cols[0].pack(side='left', fill='y', anchor='n', padx=(0, 7)); self.cols[1].pack(side='left', fill='y', anchor='n')
        self.split, self.gi = split, 0
        self.box = None; self.parent = self.inner; self.bgc = BODY
        self.rows = []
        self.add_rows(self.spec(s))

    def room(self):
        """the height the tab's area may take on this screen"""
        self.t.update_idletasks()
        x0, y0, x1, y1 = work_area(self.t)
        ui = self.t.winfo_fpixels('1i') / 96.0
        return (y1 - y0) - (self.t.winfo_reqheight() - self.sc.cv.winfo_reqheight()) - int(50 * ui)

    def two_columns(self):
        """the boxes' split for two columns when the tab is too tall for the screen - balanced by height (None: it fits)"""
        try:
            room = self.room()                                  # (first: it brings the sizes up to date)
            if self.inner.winfo_reqheight() + 16 <= room:       # (the new tab, not shown yet: its own size)
                return None
            hs = [b.winfo_reqheight() for b in self.inner.winfo_children()]
        except tk.TclError:
            return None
        if len(hs) < 2:
            return None
        tot = sum(hs)
        best = min(range(1, len(hs)), key=lambda k: max(sum(hs[:k]), tot - sum(hs[:k])))
        return best

    def too_wide(self):
        self.t.update_idletasks()
        x0, y0, x1, y1 = work_area(self.t)
        return self.inner.winfo_reqwidth() + 60 > (x1 - x0) - 20

    def add_rows(self, rows):
        for row in rows:
            if row[0] == 'group':                               # (a box with its caption)
                self.group = row[1]
                where = self.inner if not self.cols else self.cols[0 if self.gi < self.split else 1]
                self.gi += 1
                self.box = tk.Frame(where, bg=PANEL, highlightthickness=1, highlightbackground=EDGE)
                self.box.pack(fill='x', pady=(0, 7))
                tk.Label(self.box, text=row[1], fg=DIM, bg=PANEL, font=_font(self.app, 8), anchor='w').pack(fill='x', padx=8, pady=(4, 0))
                self.parent = tk.Frame(self.box, bg=PANEL); self.parent.pack(fill='x', padx=8, pady=(0, 5))
                self.bgc = PANEL
            elif row[0] == 'in':                                # (what hangs on the row above: indented, a line before it)
                outer = self.parent
                f = tk.Frame(outer, bg=self.bgc); f.pack(fill='x', padx=(14, 0))
                tk.Frame(f, bg=EDGE2, width=1).pack(side='left', fill='y', padx=(0, 8))
                self.parent = tk.Frame(f, bg=self.bgc); self.parent.pack(side='left', fill='x', expand=True)
                self.add_rows(row[1])
                self.parent = outer
            else:
                n = self.ROW_ARGS.get(row[0])                   # (a row's own arguments; one more: its 'changed' test)
                r = getattr(self, 'mk_' + row[0])(*(row[1:1 + n] if n else row[1:]))
                r['bg'] = self.bgc
                self.mark_row(r, self.row_chg(row))
                self.rows.append(r)

    ROW_ARGS = {'check': 4, 'choice': 3, 'menu': 3, 'buttons': 2}

    def sound_changed(self, k):
        a = self.app
        d = getattr(a, 'sound_default', {})
        if k not in d:
            return False
        v, dv = getattr(a.run.crt, k), d[k]
        return abs(float(v) - float(dv)) > 1e-6 if isinstance(dv, float) else v != dv

    def row_chg(self, row):
        """a row's 'changed from its default' test (None: a row that never counts - normal use, as the screen rate, the
        volume, port 2, the monitor's size, the SD folders): the picture's settings against the Quality profile, the
        sound's against their defaults, the unit's against the owner's measured Vectrex, the rest as their row says"""
        a, k = self.app, row[0]
        n = self.ROW_ARGS.get(k)
        if n and len(row) > 1 + n:
            return row[1 + n]
        if k == 'slider':
            t = a.SLIDERS.get(row[1])
            if not t:
                return None
            if t[2] == 'ren':
                return lambda: self.changed(t[3])
            if t[2] == 'crt' and t[3] != 'volume':
                return lambda: self.sound_changed(t[3])
            return None
        if k == 'unit':
            V = self.V
            own = V.UNIT_PRESETS['owner'][row[1]]
            return lambda: abs(float(a.unit.get(row[1], own)) - float(own)) > 1e-6
        if k == 'check':                                        # (a switch of the picture: the setting it reads)
            self.reading = []
            try: row[2]()
            except Exception: pass   # noqa
            keys, self.reading = self.reading, None
            if keys:
                key = keys[-1]
                return lambda: self.changed(key)
        return None

    def mark_row(self, r, chg):
        """a row's mark, painted with it: a checkbox's text, a choice's or menu's label, a button's text orange (a slider's
        stripe: mk_scale)"""
        if not chg or r['kind'] in ('scale', 'note'):
            return
        sync0 = r['sync']
        f = r['f']
        if r['kind'] == 'check':
            ws, col0, act0 = [r['w']], G, HI
        elif r['kind'] in ('choice', 'menu'):
            ws, col0, act0 = [f.winfo_children()[0]], G, None   # (the label in front)
        elif r['kind'] == 'buttons':
            ws, col0, act0 = list(r['w']), G, HI
        else:
            return
        def sync():
            sync0()
            try: on = bool(chg())
            except Exception: on = False   # noqa
            for w in ws:
                col = WARN if on else col0
                if w.cget('fg') != col:
                    w.configure(fg=col)
                    if act0 is not None:
                        w.configure(activeforeground=col if on else act0)
        r['sync'] = sync

    def frame(self):
        f = tk.Frame(self.parent, bg=self.bgc); f.pack(fill='x', pady=1)
        return f

    def info(self, label, unit=None):
        '''a row's hover info: the Help's text (a Vectrex unit part: and the circuit behind it), and what used to be a
        note below it (EXTRA)'''
        t = help_info(label, STRIP_TOPIC.get(self.strip))
        x = EXTRA.get('%s/%s' % (getattr(self, 'group', ''), label)) or EXTRA.get(label)
        if x:
            t = (t + '\n\n' + x) if t else x
        if unit:
            import help_text
            c = help_text.UNIT_CIRCUIT.get('dac' if unit.startswith('dac') else unit)
            if c:
                t = (t + '\n\n' + c) if t else c
        return t

    def with_info(self, f, label_w, text, unit=None):
        '''the i mark after label_w (hover info on both), or the same space empty'''
        t = self.info(text, unit)
        m = info_mark(f, self.bgc, t); m.pack(side='left', padx=(3, 4), after=label_w)
        if t:
            Tip(m, t, also=(label_w,))
        return m

    reading = None                                      # (while a check is made: the settings its state reads)

    def changed(self, key):
        """the look setting key differs from its default - under the Quality profile chosen last (its checkbox orange; a slider: an orange stripe there)"""
        a = self.app
        d = getattr(a, 'look_default', {})
        if not a.ren or key not in d:
            return False
        if getattr(self, '_pv', None) is None:                  # (the profile's values: once per sync_all)
            cur = a.current_profile()
            self._pv = a.base_values()
        v, dv = getattr(a.ren, key, None), self._pv.get(key, d[key])
        return abs(float(v) - float(dv)) > 1e-6 if isinstance(dv, float) else v != dv

    def mark(self, f):
        m = tk.Label(f, text='', fg=WARN, bg=self.bgc, font=_font(self.app, 9), width=2, anchor='w')
        return m

    def mk_check(self, label, get, cmd, dep):
        f = self.frame()
        self.reading = []
        v = tk.BooleanVar(value=bool(get()))
        self.reading = None
        c = tk.Checkbutton(f, text=label, variable=v, command=lambda: (cmd(), self.sync_all()), fg=G, bg=self.bgc,
                           selectcolor=self.bgc, activebackground=self.bgc, activeforeground=HI, font=_font(self.app, 9),
                           anchor='w', highlightthickness=0, bd=0, disabledforeground='#2a4a3a')
        c.pack(side='left')
        self.with_info(f, c, label)
        def sync():                                             # (changed from its default: its text orange - mark_row)
            v.set(bool(get()))
        return dict(kind='check', f=f, w=c, sync=sync, dep=dep, act=lambda d: (cmd(), self.sync_all()))

    def mk_note(self, text):
        """a line of text; one that starts with '!' in red (without the '!')"""
        f = self.frame()
        l = tk.Label(f, fg=DIM, bg=self.bgc, font=_font(self.app, 9), anchor='w', justify='left', wraplength=1200)
        l.pack(fill='x')
        def sync():
            t = text()
            l.configure(text=t[1:] if t.startswith('!') else t, fg=RED if t.startswith('!') else DIM)
        return dict(kind='note', f=f, w=l, sync=sync, dep=None, act=None)

    def mk_buttons(self, items, dep):
        f = self.frame(); bs = []
        for text, cmd in items:
            b = _button(f, text, lambda c=cmd: (c(), self.sync_all()), self.app, small=True); b.pack(side='left', padx=(0, 6)); bs.append(b)
            t = self.info(text)
            if t: Tip(b, t)
        return dict(kind='buttons', f=f, w=bs, sync=lambda: None, dep=dep, act=lambda d: (items[0][1](), self.sync_all()))

    def mk_choice(self, label, opts, dep):
        f = self.frame()
        lw = tk.Label(f, text=label, fg=G, bg=self.bgc, font=_font(self.app, 9), width=14, anchor='w'); lw.pack(side='left')
        lt = self.info(label); self.with_info(f, lw, label)
        bs = []
        for text, get, cmd in opts:
            b = _button(f, text, lambda c=cmd: (c(), self.sync_all()), self.app, small=True); b.pack(side='left', padx=2)
            bs.append((b, get))
            t = self.info(text)
            if t and t != lt: Tip(b, t)                  # (an option with its own Help item)
        def sync():
            for b, get in bs:
                _paint(b, bool(get()))
        def act(d):
            i = next((k for k, (b, get) in enumerate(bs) if get()), 0)
            opts[(i + d) % len(opts)][2](); self.sync_all()
        return dict(kind='choice', f=f, w=[b for b, g in bs], sync=sync, dep=dep, act=act)

    def mk_menu(self, label, opts, dep):
        f = self.frame()
        lw = tk.Label(f, text=label, fg=G, bg=self.bgc, font=_font(self.app, 9), width=18, anchor='w'); lw.pack(side='left')
        self.with_info(f, lw, label)
        mb = tk.Label(f, fg=BG, bg=G, font=_font(self.app, 9), padx=8, pady=1, anchor='w', cursor='hand2')
        m = darkmenu.DMenu(mb)                                  # (a dark drop-down, not Windows' own)
        for text, get, cmd in opts:
            m.add_checkbutton(label=text, variable=tk.BooleanVar(value=bool(get())), command=lambda c=cmd: (c(), self.sync_all()))
        def drop(e=None):
            for it, (text, get, cmd) in zip(m.items, opts):
                it['var'].set(bool(get()))
            m.post(mb.winfo_rootx(), mb.winfo_rooty() + mb.winfo_height(), self.t)
        mb.bind('<Button-1>', drop)
        mb.pack(side='left')
        def sync():
            mb.configure(text=next((t for t, get, c in opts if get()), '?') + '  ▾')
        def act(d):
            i = next((k for k, (t, get, c) in enumerate(opts) if get()), 0)
            opts[(i + d) % len(opts)][2](); self.sync_all()
        return dict(kind='menu', f=f, w=mb, sync=sync, dep=dep, act=act)

    def range_of(self, name):
        """a slider's object, attribute, range and default - as App.slider has them"""
        a, V = self.app, self.V
        title, keys, where, attr, lo, hi, log, fmt, show = a.SLIDERS[name]
        obj = (lambda: a.run.crt) if where == 'crt' else (lambda: a) if where == 'app' else (lambda: a.ren)
        if not obj() or not hasattr(obj(), attr):
            return None
        default = (a.sound_default if where == 'crt' else a.look_default).get(attr, getattr(obj(), attr))
        if attr == 'monitor_inch':
            default = a.monitor_auto_inch() or 24.0
        if attr == 'volume':
            default = 1.0
        default = V.ART_ADJ_DEFAULT.get(attr, default)
        rel = attr not in ('volume', 'line_gap', 'monitor_inch', 'scr_large', 'white_k') + tuple(V.ART_ADJ_DEFAULT) and default > 0
        if rel:
            lo, hi, log = default * (1 - V.SLIDER_SPAN), default * (1 + V.SLIDER_SPAN), False
        return dict(title=title, obj=obj, attr=attr, lo=lo, hi=hi, log=log, fmt=fmt, show=show, default=default)

    def mk_slider(self, name, dep):
        r = self.range_of(name)
        if r is None:
            return dict(kind='note', f=self.frame(), w=None, sync=lambda: None, dep=None, act=None)
        a = self.app
        def setv(v):
            if r['attr'] == 'volume':
                a.set_volume(round(v * 20) / 20.0)
            else:
                setattr(r['obj'](), r['attr'], round(v, 4))
                if r['attr'] == 'monitor_inch':                 # (the size only matters in Real size: kept as it is)
                    if getattr(a, 'real_on', False): a.real_size()
                    return
            a.show_look(r['attr'], r['obj']())
        label = r['title'].split(' (')[0].split(' - ')[0].split(': strength')[0]
        for pre in ('Overlay ', 'Scratches: ', 'UVMC2 '):        # (the strip says it already)
            if label.startswith(pre) and len(label) > 24:
                label = label[len(pre):]
        label = label[0].upper() + label[1:]
        get = lambda: getattr(r['obj'](), r['attr'])
        if r['attr'] == 'monitor_inch':                        # (0 = as the monitor reports it)
            get = lambda: a.monitor_inch or a.monitor_auto_inch() or 24.0
        t = a.SLIDERS.get(name)
        if t and t[2] == 'crt' and t[3] != 'volume':           # (a sound setting: its default)
            ref = lambda: a.sound_default.get(r['attr'])
        elif t and t[2] == 'ren':
            ref = lambda: self.look_ref(r['attr'])
        else:
            ref = None                                          # (the volume, the monitor's size: normal use)
        return self.mk_scale(label, get, setv, r['lo'], r['hi'], r['log'],
                             lambda v: r['fmt'] % (v * r['show']), r['default'], dep, ref=ref)

    def mk_unit(self, key):
        a, V = self.app, self.V
        if key == 'gap':
            label, lo, hi, fmt = 'Line gaps (stop delay)', 4.0, 10.0, '%.1f cycles'
        else:
            k, i, label, lo, hi, fmt, var = next(p for p in V.UNIT_PARAMS + V.UNIT_PICTURE if p[0] == key)
            label = label.split(' (')[0]
        def setv(v):
            a.set_unit(key, round(v, 4))
            if self.save_job: self.t.after_cancel(self.save_job)
            self.save_job = self.t.after(400, a.save_settings)
        own = V.UNIT_PRESETS['owner'][key]
        return self.mk_scale(label, lambda: a.unit.get(key, 0.0), setv, lo, hi, False, fmt if callable(fmt) else (lambda v: fmt % v),
                             own, None, unit=key, ref=lambda: own)   # (the reference: the owner's measured Vectrex)

    def look_ref(self, key):
        """the value a look setting is compared with (the Quality profile chosen last), or None"""
        a = self.app
        d = getattr(a, 'look_default', {})
        if not a.ren or key not in d:
            return None
        if getattr(self, '_pv', None) is None:
            self._pv = a.base_values()
        return self._pv.get(key, d[key])

    def mk_scale(self, label, get, setv, lo, hi, log, fmt, default, dep, unit=None, ref=None):
        """a slider row; ref() - the value it counts as changed from: an orange stripe on the bar there while the
        slider is elsewhere (where it goes back to)"""
        f = self.frame(); N = 1000
        if log == 'mired':                                      # (even in mireds: White balance)
            to_pos = lambda v: N * (V.vecrender.white_pos(v) + 1.0) / 2.0     # (the middle neutral: white_pos)
            to_val = lambda p: V.vecrender.white_from_pos(2.0 * p / N - 1.0)
        else:
            to_pos = lambda v: N * (math.log(v / lo) / math.log(hi / lo) if log else (v - lo) / (hi - lo))
            to_val = lambda p: lo * (hi / lo) ** (p / N) if log else lo + (hi - lo) * p / N
        lw = tk.Label(f, text=label, fg=G, bg=self.bgc, font=_font(self.app, 9), width=26, anchor='w'); lw.pack(side='left')
        self.with_info(f, lw, label, unit)
        val = tk.Label(f, fg=G, bg=self.bgc, font=_font(self.app, 9), width=12, anchor='w')
        busy = [False]
        def moved(p):
            if busy[0]: return
            if abs(float(p) - to_pos(max(lo, min(hi, get())))) < 0.5:   # (Tk reports the value set by sync() later:
                return                                                  #  not a move - nothing changes)
            setv(to_val(float(p))); val.configure(text=fmt(get())); stripe_at()
        sc = tk.Scale(f, from_=0, to=N, orient='horizontal', length=300, showvalue=0, command=moved, bg=G, fg=G,
                      troughcolor='#123', highlightthickness=0, activebackground=HI, sliderrelief='flat', bd=0,
                      width=12, sliderlength=18)
        val.pack(side='right'); sc.pack(side='right', padx=6)        # (from the right: lined up at every indent)
        stripe = tk.Frame(sc, bg=WARN, width=3, height=4, bd=0, highlightthickness=0)
        def stripe_at(e=None):                                  # (changed: the stripe where the reference value sits)
            v = ref() if ref else None
            try:
                cur = float(get())
                off = v is None or abs(cur - float(v)) <= 1e-6 * max(1.0, abs(float(v)))
            except (TypeError, ValueError):
                off = True
            w = sc.winfo_width()
            if off or w < 30:
                stripe.place_forget(); return
            p = max(0.0, min(1.0, to_pos(max(lo, min(hi, float(v)))) / N))
            stripe.place(x=int(round(9 + p * (w - 18))) - 1, y=0, width=3, relheight=1.0)
        sc.bind('<Configure>', stripe_at, add='+')
        def sync():
            busy[0] = True; sc.set(to_pos(max(lo, min(hi, get())))); busy[0] = False
            val.configure(text=fmt(get())); stripe_at()
        def to_default(e=None):                                 # (right click: the default the stripe shows - the
            v = None                                            #  Quality profile chosen's, not Ultra's; the owner)
            if ref is not None:
                try: v = ref()
                except Exception: v = None   # noqa
            setv(default if v is None else v); sync(); return 'break'
        def jump(e):                                            # (a click on the bar: the handle goes there)
            if 'trough' not in sc.identify(e.x, e.y): return None
            sc.set(max(0, min(N, (e.x - 9) / float(max(1, sc.winfo_width() - 18)) * N))); return 'break'
        sc.bind('<Button-1>', jump)
        sc.bind(self.V.RCLICK, to_default); val.bind(self.V.RCLICK, to_default)
        def act(d):                                             # (a controller: 2 % steps)
            setv(to_val(max(0, min(N, to_pos(max(lo, min(hi, get()))) + d * 20)))); sync()
        return dict(kind='scale', f=f, w=sc, sync=sync, dep=dep, act=act)

    # ---- keeping it in step (keys and the menus change things too)
    def sync_all(self):
        a = self.app
        self._pv = None                                         # (the profile's values: looked up again)
        for r in self.rows:
            try:
                r['sync']()
                on = r['dep'] is None or bool(r['dep']())
                ws = r['w'] if isinstance(r['w'], list) else [r['w']] if r['w'] is not None else []
                for w in ws:
                    try:
                        w.configure(state='normal' if on else 'disabled')
                        if r['kind'] == 'scale':                # (a Scale looks the same disabled: dimmed by hand)
                            w.configure(bg=G if on else '#2a4a3a', troughcolor='#123' if on else '#16261f')
                    except tk.TclError: pass
            except Exception:   # noqa
                pass
        for i, r in enumerate(self.rows):                       # (the controller's row)
            r['f'].configure(bg=ROWBG if (i == self.focus and self.pad_used()) else r.get('bg', BG))
        mode = a.current_profile()
        for m, b in self.qbtn.items():
            _paint(b, m == mode)

        diff = any(self.changed(k) for k in getattr(a, 'look_default', {}) if k != 'line_gap')
        self.qhint.configure(text=('changed from %s' % a.base_name()) if diff else '', fg=WARN)
        self._pv = None
        self.paint_tabs()                                       # (the tabs with changes: orange)
        self._pv = None
        st = a.game_state()
        name = a.rom_name
        self.ghint.configure(text={'everyday': '%s uses the everyday settings (changes are kept for all games).' % name,
                                   'own': '%s has its own settings (changes are kept for it).' % name,
                                   'changed': '%s has its own settings - changed and kept (Revert: as it was).' % name}[st],
                             fg=DIM)
        for k, b in self.gbtn.items():
            ok = dict(save=True, revert=st == 'changed', remove=st != 'everyday', default=st != 'everyday')[k]
            b.configure(state='normal' if ok else 'disabled')

    def pad_used(self):
        return time.perf_counter() - self.pad_t < 8.0

    def set_quality(self, m):
        if m != 'custom':                               # (Custom: what the switches below make it)
            self.app.set_profile(m)
        self.sync_all()

    def step_strip(self, d):
        i = self.STRIPS.index(self.strip)
        self.switch(self.STRIPS[(i + d) % len(self.STRIPS)])

    def compare(self, on):
        """Hold: before - the look (and the Vectrex unit) from before the deck opened, while held"""
        a = self.app
        if on and self.comparing is None:
            self.comparing = dict(look=a.game_now()['look'], unit=dict(a.unit), name=a.unit_name)
            self.apply(self.before['look'], self.before['unit'])
        elif not on and self.comparing is not None:
            c, self.comparing = self.comparing, None
            self.apply(c['look'], c['unit']); a.unit_name = c['name']
        return 'break'

    def apply(self, look, unit):
        a = self.app
        for k, v in look.items():
            setattr(a.ren, k, v)
        for k, v in unit.items():
            if a.unit.get(k) != v:
                a.set_unit(k, v, a.unit_name)

    def tick(self):
        """a controller on the deck, and keeping the rows in step"""
        if not self.t.winfo_exists():
            return
        self.n = getattr(self, 'n', 0) + 1                      # (a controller no longer works the window: the game keeps
        if self.n % 6 == 0:                                     #  it - see the top of this file)
            self.sync_all()
        self.t.after(50, self.tick)

    def close(self):
        if self.comparing is not None:
            self.compare(False)
        a = self.app
        try:
            a.deck_pos = (self.t.winfo_x(), self.t.winfo_y())  # (it opens there next time)
        except tk.TclError:
            pass
        a.deck_focus = dict(getattr(a, 'deck_focus', None) or {}, **{self.strip: self.focus})
        a.inp.block = bool(a.browser)
        a.save_settings()
        a.deck = None
        try: self.t.destroy()
        except tk.TclError: pass
        try: a.root.focus_force()
        except tk.TclError: pass


def over_full(t, app):
    """window t in full screen: always above the picture (the full-screen window would hide it once it is clicked)
    and on the picture's screen - out of full screen: an ordinary window again"""
    try:
        full = bool(getattr(app, 'full', False))
        if t.state() == 'withdrawn':                            # (hidden along with the main window's full-screen switch)
            t.deiconify()
        t.attributes('-topmost', full)
        if not full:                                            # (no longer topmost: Windows may put it behind other
            t.lift()                                            #  programs - brought to the front again)
            if V.IS_WIN:
                import ctypes
                u = ctypes.windll.user32
                h = u.GetParent(t.winfo_id()) or t.winfo_id()
                u.SetWindowPos(h, 0, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010)   # (HWND_TOP; NOSIZE|NOMOVE|NOACTIVATE)
            return
        r = app.root
        rx, ry, rw, rh = r.winfo_rootx(), r.winfo_rooty(), r.winfo_width(), r.winfo_height()
        t.update_idletasks()
        x, y = t.winfo_x(), t.winfo_y()
        if not (rx - 20 <= x <= rx + rw - 100 and ry - 10 <= y <= ry + rh - 60):   # (not on the picture: into its middle)
            x = rx + max(0, (rw - t.winfo_reqwidth()) // 2)
            y = ry + max(0, (rh - t.winfo_reqheight()) // 3)
            t.geometry('+%d+%d' % (x, y))
        t.lift()
    except tk.TclError:
        pass


def open_deck(app, e=None):
    d = getattr(app, 'deck', None)
    if d is not None:
        try:
            hidden = d.t.state() == 'withdrawn'                 # (open but hidden - Windows hides it when the main
        except tk.TclError:                                     #  window goes in or out of full screen): shown again
            hidden = False
        if hidden:
            over_full(d.t, app)
        else:
            d.close()
    else:
        app.deck = Deck(app)
    return 'break'


# ------------------------------------------------------------------ first-run setup
def setup_window(app, first=True):
    """the first-run setup: what was found (BIOS, games, the UVMC2 SD folder, the speech ROM), the quality
    for this PC (a quick test), the display mode (G-Sync / FreeSync from the monitor's own data, the full rate,
    Smooth motion or plain - the best one for this screen chosen and each explained), player 1's keys and
    whether the Vectrex starts switched on. Shown once; View > Setup... shows it again."""
    import vrrdetect
    a = app
    t = tk.Toplevel(a.root, bg=BG)
    t.withdraw()                                        # (shown once built, in its place)
    t.title('Setup'); t.transient(a.root); t.resizable(False, False)
    f9, f10b = _font(a, 9), _font(a, 10, True)
    top = tk.Frame(t, bg=TOPBG, highlightthickness=1, highlightbackground=EDGE); top.pack(fill='x')
    if first:                                           # (the title bar says Setup: the welcome only the first time)
        tk.Label(top, text='WELCOME TO VECTREX_EMU', fg=HI, bg=TOPBG, font=f10b, anchor='w').pack(fill='x', padx=14, pady=(8, 0))
    tk.Label(top, text='Shown once. Change anything later in Config (Tab).',
             fg=DIM, bg=TOPBG, font=f9, anchor='w').pack(fill='x', padx=14, pady=(0 if first else 8, 8))
    PB = PANEL                                          # (the boxes' colour)
    # the boxes scroll between the title and the buttons when the screen is not tall enough for all of them (a 1080p
    # laptop at 150 %: the bottom of the window, with Start playing, was below the taskbar)
    mid = tk.Frame(t, bg=BG); mid.pack(fill='both', expand=True)
    cv = tk.Canvas(mid, bg=BG, highlightthickness=0, bd=0)
    body = tk.Frame(cv, bg=BG)
    bwin = cv.create_window(0, 0, window=body, anchor='nw')
    cv.bind('<Configure>', lambda e: cv.itemconfigure(bwin, width=e.width))   # (the boxes as wide as the window)
    sbar = darkmenu.DScroll(mid, command=cv.yview, bg=BG)
    cv.configure(yscrollcommand=sbar.set)
    cv.pack(side='left', fill='both', expand=True)
    def wheel(e):
        if sbar.winfo_ismapped():
            cv.yview_scroll(-1 if (e.delta > 0 or getattr(e, 'num', 0) == 4) else 1, 'units')
    for seq in ('<MouseWheel>', '<Button-4>', '<Button-5>'):
        t.bind(seq, wheel)
    grid = None
    rowi = [0]
    def box(caption):
        """a captioned box; the rows after it go in it"""
        nonlocal grid
        bx = tk.Frame(body, bg=PB, highlightthickness=1, highlightbackground=EDGE); bx.pack(fill='x', padx=10, pady=(8, 0))
        tk.Label(bx, text=caption, fg=DIM, bg=PB, font=_font(a, 8), anchor='w').pack(fill='x', padx=10, pady=(4, 0))
        grid = tk.Frame(bx, bg=PB); grid.pack(fill='x', padx=10, pady=(0, 6))
        rowi[0] = 0
    def row(name, info=None):
        r = rowi[0]; rowi[0] += 1
        mark = tk.Label(grid, fg=G, bg=PB, font=f9, width=2); mark.grid(row=r, column=0, sticky='w', pady=3)
        nf = tk.Frame(grid, bg=PB); nf.grid(row=r, column=1, sticky='w')
        nl_ = tk.Label(nf, text=name, fg=HI, bg=PB, font=f9, width=16, anchor='w'); nl_.pack(side='left')
        im = info_mark(nf, PB, info); im.pack(side='left', padx=(0, 6))       # (the long explanation: on hover)
        if info:
            Tip(im, info, also=(nl_,))
        txt = tk.Label(grid, fg=G, bg=PB, font=f9, anchor='w', justify='left', wraplength=520); txt.grid(row=r, column=2, sticky='w')
        side = tk.Frame(grid, bg=PB); side.grid(row=r, column=3, sticky='e', padx=(8, 0))
        return mark, txt, side
    def ok(mark, good):
        mark.configure(text='✓' if good else '!', fg=G if good else WARN)
    # what was found
    box('WHAT WAS FOUND')
    m, txt, side = row('BIOS', "The Vectrex's own 8 KB ROM, with Mine Storm built in: system/bios.bin.")
    ok(m, os.path.exists(V.BIOS)); txt.configure(text=_rel(V.BIOS) if os.path.exists(V.BIOS) else 'not found: put bios.bin in system')
    # the games, the overlays, the PiTrex, the speech ROM: what is missing red - it can all be downloaded in one go
    import getter
    getter.V_APP = a
    roms = os.path.join(V.ROOT, 'roms')
    def count():
        n = 0
        for dp, dn, fn in os.walk(roms):
            n += sum(1 for f in fn if f.lower().endswith(('.bin', '.vec', '.gam', '.v4e', '.um2')))
        return n
    def miss(mark, txt, text):
        mark.configure(text='✗', fg=RED); txt.configure(text=text, fg=RED)
    found = []                                          # (refreshed after a download: the rows below)
    def games_row():
        m, txt, side = row('Games', "The games in the roms folder (the ROM browser: L). The GCE originals can be downloaded "
                                    "from their own sites with the button below.")
        def upd(m=m, txt=txt):
            have, of = getter.games_have()
            if have >= of:
                ok(m, True); txt.configure(text='%d games, all %d originals' % (count(), of), fg=G)
            else:
                miss(m, txt, '%d of %d original games' % (have, of))
        found.append(upd); upd()
    games_row()
    m, txt, side = row('Overlays', 'The plastic overlays: they come with the emulator, in the overlays folder.')
    od = os.path.join(V.ROOT, 'overlays')
    no = len([f for f in os.listdir(od) if f.lower().endswith(('.art', '.png'))]) if os.path.isdir(od) else 0
    if no > 1:
        ok(m, True); txt.configure(text='%d overlays' % no)
    else:
        miss(m, txt, 'only %d: put them back in overlays' % no)
    def item_row(name, key, have_text, miss_text, info=None):
        m, txt, side = row(name, info)
        def upd(m=m, txt=txt):
            if getter.state(key, a)[0]:
                ok(m, True); txt.configure(text=have_text, fg=G)
            else:
                miss(m, txt, miss_text)
        found.append(upd); upd()
    item_row('Homebrew games', 'homebrew', 'found', 'missing',
             'The free homebrew games that have an overlay; they can be downloaded with the button below.')
    item_row('Speech ROM', 'speech', 'found', 'missing: speech games stay silent',
             'The speech chip ROM for the VecVoice / VecVox games; without it they are silent. It can be downloaded.')
    item_row('PiTrex SD card', 'pitrex', _rel(a.px_dir), 'missing',
             "The PiTrex's programs (its SD card); they can be downloaded. Start it: Vectrex > PiTrex.")
    # the UVMC2 SD card
    m, txt, side = row('UVMC2 SD folder', 'The folder that is the UVMC2 cartridge\'s SD card: games such as Doom read their files from it.')
    def sd_state(m=m, txt=txt):
        e = os.path.isdir(a.sd_dir); ok(m, e)
        txt.configure(text=('%s' if e else '%s: not there yet') % _rel(a.sd_dir))
        return e
    if not sd_state():
        def mk_sd():
            os.makedirs(a.sd_dir, exist_ok=True); sd_state(); sb.destroy()
        sb = _button(side, 'Create', mk_sd, a, small=True); sb.pack()
    # one button for all that is missing
    gf = tk.Frame(grid, bg=PB); gf.grid(row=rowi[0], column=0, columnspan=4, sticky='w', pady=(6, 2)); rowi[0] += 1
    def missing():
        return [k for k in ('games', 'homebrew', 'speech', 'pitrex') if not getter.state(k, a)[0]]
    def get_all():
        getter.window(a, only=missing() or None, parent=t)
        def watch():                                    # (the rows follow the downloads)
            for f in found:
                f()
            if getattr(a, 'getter_win', None) and a.getter_win.winfo_exists() and t.winfo_exists():
                t.after(1000, watch)
        t.after(1000, watch)
    gb = _button(gf, 'Download everything missing', get_all, a, on=bool(missing())); gb.pack(side='left')
    Tip(gb, 'Everything in red, downloaded from its own site in one click. Vectrex > Get games & extras... fetches it again any time.')
    if not missing():
        tk.Label(gf, text='All there.', fg=DIM, bg=PB, font=f9).pack(side='left', padx=(10, 0))
    # quality
    box('QUALITY')
    m, txt, side = row('Quality', 'How many of the picture effects this PC draws: Low for weak graphics up to Ultra, every effect. '
                                  'Optimize: a few short tests find the best settings this PC can run at full speed.'); ok(m, True)
    qv = tk.StringVar(value=a.current_profile())
    qf = tk.Frame(grid, bg=PB); qf.grid(row=rowi[0] - 1, column=2, sticky='w')   # (the buttons on the row itself; the
    txt.grid_remove(); txt.grid(row=rowi[0], column=2, columnspan=2, sticky='w'); rowi[0] += 1   #  result below, when there is one)
    qb = {}
    def qset(mode):
        qv.set(mode)
        for k, b in qb.items(): _paint(b, k == mode)
    for key in ('basic', 'low', 'medium', 'high', 'ultra'):
        b = _button(qf, a.QUALITY_NAMES[key], lambda k=key: qset(k), a, small=True); b.pack(side='left', padx=2); qb[key] = b
    qset(qv.get())
    def optimize(txt=txt):
        """Optimize (pctest): the best this PC runs at full speed, set now; the presets here still choose instead"""
        import pctest
        try:
            rep = pctest.optimize(a, lambda st: (txt.configure(text='optimizing: ' + st), t.update()))
        except (AttributeError, tk.TclError):           # (the emulator closed meanwhile)
            return
        if rep == 'online':
            txt.configure(text='not during an online game - press Optimize later'); return
        if rep is None:
            txt.configure(text='drawn without OpenGL: no effects to choose'); return
        pctest.apply(a, rep)
        qset(a.current_profile())
        txt.configure(text='\n'.join(pctest.result_lines(rep)[:-1]))
    _button(side, 'Optimize', optimize, a, small=True).pack()
    if first and not a.net_busy():                      # (a new install: at once - not when it starts an online game)
        txt.configure(text='optimizing...')
        t.after(400, optimize)
    elif first:
        txt.configure(text='Optimize: after the online game')
    else:
        txt.grid_remove()                               # (nothing to say until Optimize runs)
        _orig_cfg = txt.configure
        def _show(**kw):
            txt.grid(); return _orig_cfg(**kw)
        txt.configure = _show
    # the display: one choice, explained, the best for this screen chosen
    v = vrrdetect.detect(a.screen.winfo_id())
    ref = int(round(getattr(a, 'refresh', 0) or 60))
    n60 = max(1, round(ref / 60.0))                     # (the matched rate, as App.apply_rate picks it)
    mhz = 60 if abs(ref - 60 * n60) <= 1 else (int(round(ref / max(1, ref // 50))) if ref >= 50 else 60)
    box('DISPLAY: THIS SCREEN IS %d HZ - ALL OF THESE ARE TEAR-FREE' % ref)
    best = ('vrr' if v['vrr'] and (not v.get('lo') or v['lo'] <= 50) else 'max' if ref >= 100 else
            'plain' if mhz % 50 == 0 else 'smooth')
    cur_mode = 'vrr' if a.vrr else 'max' if a.rate == 'max' else 'smooth' if a.smooth else 'plain'
    dv = tk.StringVar(value=best if first else cur_mode)
    touched = [False]
    opts = [('vrr', 'G-Sync / FreeSync', "The best: the screen waits for each game frame and shows it the moment it is drawn - 50 a "
                                           "second, exactly as the Vectrex makes them. No stutter. " + vrr_note(v) +
             ('' if not (v.get('lo') and v['lo'] > 50) else " - its range starts at %d Hz, above the Vectrex's 50" % v['lo']) + '.'),
            ('max', "The monitor's full rate (%d Hz)" % ref,
             ("Very smooth on 100 Hz and up: every refresh shows the beam's lines at the moment they are drawn, and the "
              "afterglow fades in %d steps a second instead of 60 - closest to a real tube. Needs a PC that draws %d pictures "
              "a second." % (ref, ref)) if ref >= 100 else 'Needs a screen above 60 Hz (this one: %d Hz).' % ref),
            ('smooth', 'Smooth motion (%d Hz)' % mhz,
             "A compromise for screens without G-Sync / FreeSync: the emulator draws pictures in between the game's frames "
             "(frame interpolation), so movement is smooth. The picture comes about 20 ms later, and where two frames do not "
             "match (things appearing, UVMC2 3D games) %s." % ('one picture in five is still shown twice' if mhz == 60 else
                                                                'a picture is still shown longer now and then')),
            ('plain', 'Plain %d Hz' % mhz,
             ("Every game frame as it is - at %d Hz exactly in step." % mhz) if mhz % 50 == 0 else
             ("The purist picture: every game frame as it is. At 60 Hz one picture in five is shown twice - a slight stutter "
              "in moving things." if mhz == 60 else "The purist picture: every game frame as it is. At %d Hz some pictures "
              "stay longer than others - a slight stutter in moving things." % mhz))]
    short = {'vrr': 'no stutter: each frame shown when drawn',
             'max': 'every refresh: the finest afterglow' if ref >= 100 else 'needs a screen above 60 Hz',
             'smooth': 'smooth movement, the picture 20 ms later',
             'plain': 'every frame as it is' + (', exactly in step' if mhz % 50 == 0 else ', a slight stutter')}
    df = tk.Frame(grid, bg=PB); df.grid(row=rowi[0], column=0, columnspan=4, sticky='w', pady=(2, 2)); rowi[0] += 1
    for k, title, expl in opts:
        usable = k != 'max' or ref >= 100
        rf = tk.Frame(df, bg=PB); rf.pack(fill='x', anchor='w', pady=1)
        rb = tk.Radiobutton(rf, text=title + (' (recommended)' if k == best else ''), variable=dv, value=k,
                            command=lambda: touched.__setitem__(0, True), fg=HI if usable else '#2a4a3a', bg=PB,
                            selectcolor=PB, activebackground=PB, activeforeground=HI, font=f9, anchor='w', width=36,
                            highlightthickness=0, bd=0, disabledforeground='#2a4a3a', state='normal' if usable else 'disabled')
        rb.pack(side='left')
        sl = tk.Label(rf, text=short[k], fg=DIM if usable else '#2a4a3a', bg=PB, font=f9, anchor='w')
        sl.pack(side='left')
        if usable:
            im = info_mark(rf, PB, expl); im.pack(side='left', padx=(6, 0))
            Tip(im, expl, also=(sl,))
    def disp_after_test(ms):
        """the quality test: a PC too slow to draw every refresh of a fast screen gets Smooth motion instead"""
        if first and not touched[0] and dv.get() == 'max' and ms and ms * ref > 350.0:
            dv.set('plain' if mhz % 50 == 0 else 'smooth')
    # keys
    box('KEYS AND START')
    m, txt, side = row('Player 1 keys'); ok(m, True)
    cur = 'row' if [x[0] for x in a.inp.keys['p1']['b2']] == [0x53] else 'cross'
    kv = tk.StringVar(value=cur)
    kb = {}
    def kset(k):
        kv.set(k)
        for n, b in kb.items(): _paint(b, n == k)
    kf = txt.master; txt.destroy()
    kfr = tk.Frame(kf, bg=PB); kfr.grid(row=rowi[0] - 1, column=2, sticky='w')
    for text, k in (('Cross W A S D', 'cross'), ('Row A S D F', 'row')):
        b = _button(kfr, text, lambda k=k: kset(k), a, small=True); b.pack(side='left', padx=2); kb[k] = b
    tk.Label(kfr, text='  arrows: the stick', fg=DIM, bg=PB, font=f9).pack(side='left')
    kset(cur)
    # switched on
    m, txt, side = row('Start', 'Off: the Vectrex starts switched off - turn the volume knob up (Page Up) to switch it on, '
                                'as on a real Vectrex.'); ok(m, True)
    son = tk.BooleanVar(value=True if first else a.start_on)
    txt.destroy()
    tk.Checkbutton(grid, text='Start switched on', variable=son,
                   fg=G, bg=PB, selectcolor=PB, activebackground=PB, activeforeground=HI, font=f9,
                   highlightthickness=0, bd=0).grid(row=rowi[0] - 1, column=2, columnspan=2, sticky='w')
    # done
    foot = tk.Frame(t, bg=TOPBG, highlightthickness=1, highlightbackground=EDGE); foot.pack(fill='x', pady=(10, 0))
    tk.Label(foot, text='Enter: start playing   Esc: skip', fg=DIM, bg=TOPBG, font=f9).pack(side='left', padx=(12, 8), pady=7)
    def done(browser=False, skip=False):
        if not skip:
            if qv.get() not in (a.current_profile(), 'custom'): a.set_profile(qv.get())
            d = dv.get()
            a.set_screen('vrr' if d == 'vrr' else 'max' if d == 'max' else '60')
            if (d in ('smooth', 'max')) != bool(a.smooth): a.toggle_smooth()   # (the full rate: smooth too, as
                                                                                  #  the default - 144 / 165 Hz evenly)
            if kv.get() != cur:
                keys = {'cross': ((0x41, 'A'), (0x57, 'W'), (0x53, 'S'), (0x44, 'D')),
                        'row': ((0x41, 'A'), (0x53, 'S'), (0x44, 'D'), (0x46, 'F'))}[kv.get()]
                for i, k in enumerate(('b1', 'b2', 'b3', 'b4')):
                    a.inp.keys['p1'][k] = [keys[i]]
            if bool(son.get()) != bool(a.start_on): a.toggle_start_on()
            if son.get() and not a.run.power: a.power_up()
        a.setup_done = True
        a.save_settings()
        try: t.destroy()
        except tk.TclError: pass
        a.root.focus_force()
        if browser: a.open_browser()
    _button(foot, 'Reset Vectrex settings', lambda: a.reset_vectrex(), a).pack(side='left')
    _button(foot, 'Start playing', lambda: done(), a, on=True).pack(side='right', padx=(0, 12))
    _button(foot, 'Open the ROM browser', lambda: done(True), a).pack(side='right', padx=8)
    t.bind('<Return>', lambda e: done()); t.bind('<Escape>', lambda e: done(skip=True))
    t.protocol('WM_DELETE_WINDOW', lambda: done(skip=True))
    t.update_idletasks()
    r = a.root
    x0, y0, x1, y1 = work_area(r)
    bw, bh = body.winfo_reqwidth(), body.winfo_reqheight() + 4
    room = (y1 - y0) - top.winfo_reqheight() - foot.winfo_reqheight() - int(60 * t.winfo_fpixels('1i') / 96.0)   # (the title bar, a margin)
    if bh > room:                                       # (not all of it fits: the boxes scroll)
        sbar.pack(side='right', fill='y', padx=(0, 3), pady=3)
        bh = max(200, room)
    cv.configure(width=bw, height=bh, scrollregion=(0, 0, bw, body.winfo_reqheight() + 4))
    t.update_idletasks()
    tw, th = t.winfo_reqwidth(), t.winfo_reqheight()
    x = r.winfo_rootx() + max(0, (r.winfo_width() - tw) // 2)
    y = r.winfo_rooty() + max(0, (r.winfo_height() - th) // 3)
    x = max(x0, min(x, x1 - tw)); y = max(y0, min(y, y1 - th - 40))   # (all of it on the desktop)
    pos = saved_place(t, 'setup', a)
    t.geometry('+%d+%d' % (pos or (x, y)))
    reveal(t, lambda: no_close_button(t))
    remember(t, 'setup', a)
    t.focus_force()
    return t


def vrr_note(v):
    """what the monitor says about G-Sync / FreeSync, as a sentence"""
    rng = ' (%d-%d Hz)' % (v['lo'], v['hi']) if v.get('lo') and v.get('hi') else ''
    if v['vrr']:
        w = v.get('why', '')
        return 'This monitor reports ' + (w[len('the monitor reports '):] if w.startswith('the monitor reports ') else 'it%s' % rng)
    if v['vrr'] is None:
        return 'This monitor may have it%s - try it' % rng
    return 'This monitor does not report it%s' % rng
