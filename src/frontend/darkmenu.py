"""darkmenu.py - the window's menu bar and its drop-downs, drawn by the emulator itself: dark, like VS Code
(Windows draws its own menu bar light, and a white frame round its own drop-downs, whatever the program asks).

DMenu takes the calls a tk.Menu takes here (add_command, add_checkbutton, add_cascade, add_separator,
delete, index, entryconfigure, postcommand), so the menus are built the same way. DBar is the bar: a row
of names; a click opens a menu, moving to the next name while one is open opens that one. Keys: Up / Down,
Enter or Space, Right into a submenu, Left out of it, Esc closes. A click outside closes it. Alt (or F10) opens
the first menu, Alt + the underlined letter that one (the letters shown while Alt is held). A name can also be
a plain command (no menu): Config."""
import tkinter as tk

BAR, FG, DIM, HOV, MENU, SEL, EDGE, LINE = '#0b0f0e', '#c8d6cf', '#6f857b', '#1c2723', '#111816', '#24443a', '#2f5544', '#1d2a25'
TIPBG = '#16211d'


class DMenu:
    def __init__(self, master=None, tearoff=0, postcommand=None, **kw):
        self.items = []                                 # dicts: kind, label, accel, command, var, menu, state
        self.postcommand = postcommand
        self.win = None; self.rows = []; self.sel = -1; self.child = None; self.parent = None
        self.tipw = None; self.tipjob = None
        self.bar = None

    # ---- the tk.Menu calls
    def add_command(self, label='', accelerator='', command=None, state='normal', tip=None, icon='', **kw):
        self.items.append(dict(kind='command', label=label, accel=accelerator or '', command=command, state=state, tip=tip, icon=icon))

    def add_checkbutton(self, label='', accelerator='', variable=None, command=None, state='normal', tip=None, **kw):
        self.items.append(dict(kind='check', label=label, accel=accelerator or '', command=command, var=variable, state=state, tip=tip))

    def add_cascade(self, label='', menu=None, **kw):
        self.items.append(dict(kind='cascade', label=label, accel='', menu=menu, state='normal'))
        if menu is not None:
            menu.parent = self

    def add_separator(self):
        self.items.append(dict(kind='separator'))

    def delete(self, first, last=None):
        self.items = []

    def index(self, what):
        return len(self.items) - 1 if self.items else None

    def entryconfigure(self, i, **kw):
        if 0 <= i < len(self.items):
            self.items[i].update(kw)

    # ---- drawing
    def font(self):
        return getattr(self.bar, 'font', ('Segoe UI', 10)) if self.bar else (self.parent.font() if self.parent else ('Segoe UI', 10))

    def root_bar(self):
        m = self
        while m.parent is not None:
            m = m.parent
        return m.bar

    def post(self, x, y, owner):
        """open at (x, y) on the screen"""
        if self.postcommand:
            try:
                self.postcommand()
            except Exception:   # noqa
                pass
        self.unpost()
        f = self.font()
        w = self.win = tk.Toplevel(owner, bg=EDGE)
        w.overrideredirect(True)
        try: w.attributes('-topmost', True)
        except tk.TclError: pass
        inner = tk.Frame(w, bg=MENU); inner.pack(padx=1, pady=1)
        self.rows = []
        for i, it in enumerate(self.items):
            if it['kind'] == 'separator':
                s = tk.Frame(inner, bg=MENU, height=9); s.grid(row=i, column=0, columnspan=4, sticky='ew')
                tk.Frame(s, bg=LINE, height=1).place(relx=0, rely=0.5, relwidth=1)
                self.rows.append(None)
                continue
            on = it.get('state', 'normal') != 'disabled'
            fg = FG if on else DIM
            mark = '✓' if it['kind'] == 'check' and it.get('var') is not None and it['var'].get() else it.get('icon', '')
            cells = [tk.Label(inner, text=mark, bg=MENU, fg=fg if not it.get('icon') else DIM, font=f, width=2, anchor='e'),
                     tk.Label(inner, text=it['label'], bg=MENU, fg=fg, font=f, anchor='w', padx=6, pady=3),
                     tk.Label(inner, text=it.get('accel', ''), bg=MENU, fg=DIM, font=f, anchor='e', padx=10),
                     tk.Label(inner, text='›' if it['kind'] == 'cascade' else '', bg=MENU, fg=fg, font=f, width=2)]
            for c, cell in enumerate(cells):
                cell.grid(row=i, column=c, sticky='nsew')
                cell.bind('<Enter>', lambda e, i=i: self.hover(i))
                cell.bind('<ButtonRelease-1>', lambda e, i=i: self.choose(i))
            self.rows.append(cells)
        inner.grid_columnconfigure(1, weight=1)
        w.update_idletasks()
        sw, sh = w.winfo_screenwidth(), w.winfo_screenheight()
        vx, vy = w.winfo_vrootx(), w.winfo_vrooty()
        ww, wh = w.winfo_reqwidth(), w.winfo_reqheight()
        x = min(x, vx + max(sw, w.winfo_vrootwidth()) - ww)    # (kept on the screen)
        y = min(y, vy + max(sh, w.winfo_vrootheight()) - wh)
        w.geometry('+%d+%d' % (x, y))
        self.sel = -1
        if self.parent is None:                         # (the top menu: it takes the clicks and keys)
            w.bind('<ButtonPress-1>', self.click_out)
            for k, f_ in (('<Escape>', lambda e: self.close_all()), ('<Up>', lambda e: self.step(-1)),
                          ('<Down>', lambda e: self.step(1)), ('<Return>', lambda e: self.key_enter()),
                          ('<space>', lambda e: self.key_enter()), ('<Right>', lambda e: self.key_right()),
                          ('<Left>', lambda e: self.key_left())):
                w.bind(k, f_)
            w.bind('<Motion>', self.motion)
            for k in ('<KeyPress-Alt_L>', '<KeyPress-Alt_R>', '<F10>'):
                w.bind(k, lambda e: (self.close_all(), 'break')[1])
            w.focus_force()
            try: w.grab_set()
            except tk.TclError: pass

    def tip_hide(self):
        if self.tipjob is not None and self.win is not None:
            try: self.win.after_cancel(self.tipjob)
            except tk.TclError: pass
        self.tipjob = None
        if self.tipw is not None:
            try: self.tipw.destroy()
            except tk.TclError: pass
            self.tipw = None

    def tip_show(self, i):
        """the info box for row i: beside the menu, level with the row (left of it if there is no room)"""
        self.tipjob = None
        it = self.items[i]
        if self.win is None or self.sel != i or not it.get('tip') or not self.rows[i]:
            return
        t = self.tipw = tk.Toplevel(self.win, bg=EDGE)
        t.overrideredirect(True)
        try: t.attributes('-topmost', True)
        except tk.TclError: pass
        f = self.font()
        tk.Label(t, text=it['tip'], bg=TIPBG, fg=FG, font=(f[0], max(8, f[1] - 1)), justify='left', anchor='w',
                 wraplength=340, padx=10, pady=7).pack(padx=1, pady=1)
        t.update_idletasks()
        r = self.rows[i][1]
        x = self.win.winfo_rootx() + self.win.winfo_width() + 6
        if x + t.winfo_reqwidth() > t.winfo_vrootx() + max(t.winfo_screenwidth(), t.winfo_vrootwidth()):
            x = self.win.winfo_rootx() - t.winfo_reqwidth() - 6
        t.geometry('+%d+%d' % (x, r.winfo_rooty() - 4))

    def unpost(self):
        self.tip_hide()
        if self.child is not None:
            self.child.unpost(); self.child = None
        if self.win is not None:
            try: self.win.destroy()
            except tk.TclError: pass
            self.win = None

    def active(self):
        """the menu the keys go to: the deepest open one with a row chosen (a submenu the mouse opened
        without choosing in it leaves the keys with its parent)"""
        m = self
        while m.child is not None and m.child.win is not None and m.child.sel >= 0:
            m = m.child
        return m

    def paint(self, i, on):
        if 0 <= i < len(self.rows) and self.rows[i]:
            for cell in self.rows[i]:
                cell.configure(bg=SEL if on else MENU)

    def hover(self, i, sub=True):
        """row i chosen; a submenu opens with it (the mouse), or only on Right / Enter (the keys: sub False)"""
        if i == self.sel and (not sub or self.child is not None):
            return
        self.paint(self.sel, False); self.sel = i; self.paint(i, True)
        it = self.items[i]
        self.tip_hide()
        if it.get('tip') and self.win is not None:
            self.tipjob = self.win.after(450, lambda i=i: self.tip_show(i))
        if self.child is not None and self.child is not it.get('menu'):
            self.child.unpost(); self.child = None
        if sub and it['kind'] == 'cascade' and it['menu'] is not None and self.child is None:
            r = self.rows[i][3]
            self.child = it['menu']
            self.child.post(self.win.winfo_rootx() + self.win.winfo_width() - 2, r.winfo_rooty() - 2, self.win)

    def choose(self, i):
        it = self.items[i]
        if it['kind'] == 'separator' or it.get('state', 'normal') == 'disabled':
            return
        if it['kind'] == 'cascade':
            self.hover(i, sub=True)
            if self.child is not None and self.child.sel < 0:
                self.child.step(1)                       # (Enter on a submenu: into it)
            return
        top = self
        while top.parent is not None:
            top = top.parent
        top.close_all()
        self.tip_hide()
        if it['kind'] == 'check' and it.get('var') is not None:
            it['var'].set(not it['var'].get())
        if it.get('command'):
            it['command']()

    # ---- the top menu: clicks, keys
    def under(self, x, y):
        """the open menu (deepest first) the screen point is in, or None"""
        m, chain = self, []
        while m is not None and m.win is not None:
            chain.append(m); m = m.child
        for m in reversed(chain):
            w = m.win
            if w.winfo_rootx() <= x < w.winfo_rootx() + w.winfo_width() and w.winfo_rooty() <= y < w.winfo_rooty() + w.winfo_height():
                return m
        return None

    def motion(self, e):
        """with the grab every move comes here: hover the row under the pointer, in whichever open menu; over
        the bar, another name opens its menu"""
        x, y = e.x_root, e.y_root
        m = self.under(x, y)
        if m is not None:
            w = m.win.winfo_containing(x, y)
            for i, cells in enumerate(m.rows):
                if cells and w in cells:
                    m.hover(i); return
            return
        bar = self.root_bar()
        if bar is not None:
            bar.motion_over(x, y, self)

    def click_out(self, e):
        x, y = e.x_root, e.y_root
        m = self.under(x, y)
        if m is not None:
            w = m.win.winfo_containing(x, y)
            for i, cells in enumerate(m.rows):
                if cells and w in cells:
                    m.choose(i); return
            return
        bar = self.root_bar()
        if bar is not None and bar.name_at(x, y) is self:      # (its own name again: closed)
            self.close_all(); return 'break'
        self.close_all()

    def close_all(self):
        bar = self.root_bar()
        try:
            if self.win is not None: self.win.grab_release()
        except tk.TclError: pass
        self.unpost()
        if bar is not None:
            bar.closed()

    def step(self, d):
        m = self.active()
        n = len(m.items)
        if not n:
            return
        i = m.sel
        for _ in range(n):
            i = (i + d) % n
            if m.items[i]['kind'] != 'separator':
                m.hover(i, sub=False); return

    def key_enter(self):
        m = self.active()
        if m.sel >= 0:
            m.choose(m.sel)

    def key_right(self):
        m = self.active()
        if m.sel >= 0 and m.items[m.sel]['kind'] == 'cascade':
            m.hover(m.sel, sub=True)
            if m.child is not None:
                m.child.step(1)
        else:
            bar = self.root_bar()
            if bar is not None: bar.step(self, 1)

    def key_left(self):
        m = self.active()
        if m.parent is not None and m.parent.child is m:
            p = m.parent; p.child.unpost(); p.child = None
        else:
            bar = self.root_bar()
            if bar is not None: bar.step(self, -1)


class DBar(tk.Frame):
    """the menu bar: the menus' names in a row"""
    def __init__(self, master, font=('Segoe UI', 10)):
        tk.Frame.__init__(self, master, bg=BAR, highlightthickness=0)
        self.font = font
        self.menus = []                                  # (name label, DMenu or None for a command)
        self.keys = {}                                   # (Alt + letter -> the menu or the command)
        self.open = None
        self.alt = dict(down=False, other=False)
        self.cmds = []                                   # (the plain commands' names)
        self.mode = False                                # (a menu was open: names light up / open on the way)
        self.bind('<Leave>', self.left)

    def name(self, label, underline):
        b = tk.Label(self, text=label, bg=BAR, fg=FG, font=self.font, padx=10, pady=4)
        b.pack(side='left')
        b.letter = underline
        b.bind('<Enter>', lambda e, b=b: self.enter(b))
        b.bind('<Leave>', lambda e, b=b: b.configure(bg=BAR) if self.open is None else None)
        return b

    def enter(self, b):
        """the pointer on a name: lit; while the menus are in use (one was open), a menu's name opens it"""
        if self.open is not None:
            return
        m = next((m for b2, m in self.menus if b2 is b), None)
        if self.mode and m is not None:
            self.show(m); return
        b.configure(bg=HOV)

    def left(self, e):
        """the pointer off the bar: the menus are no longer in use"""
        try:
            w = self.winfo_containing(e.x_root, e.y_root)
        except (KeyError, tk.TclError):
            w = None
        if w is not self and (w is None or w.master is not self):
            self.mode = False

    def add_cascade(self, label='', menu=None, underline=0, **kw):
        b = self.name(label, underline)
        menu.bar = self; menu.parent = None
        b.bind('<ButtonPress-1>', lambda e, m=menu: self.toggle(m))
        self.menus.append((b, menu))
        self.keys[label[underline].lower()] = menu

    def add_command(self, label='', command=None, underline=0, **kw):
        """a name that does something at once (no menu)"""
        b = self.name(label, underline)
        def press(e, b=b):
            self.mode = False; b.configure(bg=BAR)
            command()
        b.bind('<ButtonPress-1>', press)
        self.cmds.append(b)
        self.keys[label[underline].lower()] = command

    def bind_alt(self, root):
        """Alt alone (pressed and let go) or F10: the first menu, its first row chosen; Alt + a letter: that
        menu (or command); the letters underlined while Alt is held"""
        def press(e):
            self.alt.update(down=True, other=False); self.letters(True)
        def release(e):
            self.letters(False)
            if self.alt['down'] and not self.alt['other'] and self.open is None:
                self.by_key(None)
            self.alt['down'] = False
        def other(e):
            if self.alt['down'] and e.keysym not in ('Alt_L', 'Alt_R'):
                self.alt['other'] = True
        for k in ('<KeyPress-Alt_L>', '<KeyPress-Alt_R>'):
            root.bind(k, press, add='+')
        for k in ('<KeyRelease-Alt_L>', '<KeyRelease-Alt_R>'):
            root.bind(k, release, add='+')
        root.bind('<KeyPress>', other, add='+')
        root.bind('<FocusOut>', lambda e: (self.alt.update(down=False), self.letters(False)), add='+')
        root.bind('<F10>', lambda e: (self.by_key(None), 'break')[1], add='+')
        for ch in self.keys:
            root.bind('<Alt-KeyPress-%s>' % ch, lambda e, ch=ch: (self.letters(False), self.by_key(ch), 'break')[2])
            root.bind('<Alt-KeyPress-%s>' % ch.upper(), lambda e, ch=ch: (self.letters(False), self.by_key(ch), 'break')[2])

    def letters(self, on):
        for b, m in self.menus:
            b.configure(underline=b.letter if on else -1)
        for b in self.winfo_children():
            if hasattr(b, 'letter'):
                b.configure(underline=b.letter if on else -1)

    def by_key(self, ch):
        what = self.menus[0][1] if ch is None else self.keys.get(ch)
        if what is None:
            return
        if isinstance(what, DMenu):
            self.show(what)
            what.step(1)                                 # (from the keyboard: the first row chosen)
        else:
            what()

    def label_of(self, menu):
        return next(b for b, m in self.menus if m is menu)

    def toggle(self, menu):
        if self.open is menu:
            menu.close_all(); return
        self.show(menu)

    def show(self, menu):
        if self.open is not None and self.open is not menu:
            self.open.unpost()
            try: self.label_of(self.open).configure(bg=BAR)
            except StopIteration: pass
        self.open = menu
        b = self.label_of(menu); b.configure(bg=HOV)
        menu.post(b.winfo_rootx(), b.winfo_rooty() + b.winfo_height(), self.winfo_toplevel())

    def name_at(self, x, y):
        for b, m in self.menus:
            if b.winfo_rootx() <= x < b.winfo_rootx() + b.winfo_width() and b.winfo_rooty() <= y < b.winfo_rooty() + b.winfo_height():
                return m
        return None

    def motion_over(self, x, y, current):
        m = self.name_at(x, y)
        if m is not None and m is not current:
            self.show(m); return
        for b in self.cmds:                              # (a plain command: the menu closes, the name lit)
            if b.winfo_rootx() <= x < b.winfo_rootx() + b.winfo_width() and b.winfo_rooty() <= y < b.winfo_rooty() + b.winfo_height():
                current.close_all()
                self.mode = True
                b.configure(bg=HOV)
                return

    def step(self, current, d):
        i = next((k for k, (b, m) in enumerate(self.menus) if m is current), 0)
        nxt = self.menus[(i + d) % len(self.menus)][1]
        self.show(nxt); nxt.step(1)                      # (from the keys: its first row chosen)

    def closed(self):
        self.mode = False
        if self.open is not None:
            try: self.label_of(self.open).configure(bg=BAR)
            except StopIteration: pass
        self.open = None


class DScroll(tk.Canvas):
    """a slim dark scrollbar (Windows draws its own light): set() / command as tk.Scrollbar; drag the thumb,
    click the track to page"""
    def __init__(self, master, command=None, bg=MENU, width=10):
        tk.Canvas.__init__(self, master, width=width, bg=bg, highlightthickness=0, bd=0)
        self.command = command; self.lo, self.hi = 0.0, 1.0; self.drag = None
        self.bind('<Configure>', lambda e: self.draw())
        self.bind('<ButtonPress-1>', self.press)
        self.bind('<B1-Motion>', self.move)
        self.bind('<ButtonRelease-1>', lambda e: setattr(self, 'drag', None))

    def config(self, command=None, **kw):
        if command is not None:
            self.command = command
        if kw:
            tk.Canvas.config(self, **kw)
    configure = config

    def set(self, lo, hi):
        self.lo, self.hi = float(lo), float(hi); self.draw()

    def draw(self):
        self.delete('all')
        if self.hi - self.lo >= 0.999:
            return
        h, w = self.winfo_height(), self.winfo_width()
        y0, y1 = self.lo * h, max(self.lo * h + 24, self.hi * h)
        self.create_rectangle(2, y0 + 1, w - 2, y1 - 1, fill=EDGE, outline='')

    def press(self, e):
        h = max(1, self.winfo_height())
        if self.lo * h <= e.y <= self.hi * h:
            self.drag = (e.y, self.lo)
        elif self.command:
            self.command('scroll', -1 if e.y < self.lo * h else 1, 'pages')

    def move(self, e):
        if self.drag and self.command:
            y, lo = self.drag
            self.command('moveto', lo + (e.y - y) / float(max(1, self.winfo_height())))
