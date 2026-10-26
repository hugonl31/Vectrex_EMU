"""deck.py - the Config window: one window over the running game with every display, sound and hardware
setting in strips (Tube, Room light, Case, Overlay, Sound, Vectrex unit, Display, Cartridge). Every switch and
slider acts on the game at once; Quality and this game's own settings on top; hold H (or the Before button)
to see the look from before the deck opened. Keys: Tab / Esc close, Left / Right strips. A controller:
LB / RB strips, up / down a row, left / right change it, A switch, B close.
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


def _button(parent, text, cmd, app, on=False, small=False):
    b = tk.Button(parent, text=text, command=cmd, font=_font(app, 9), relief='flat', bd=1, padx=6 if small else 8,
                  pady=1, highlightthickness=1, highlightbackground=G, cursor='hand2')
    _paint(b, on)
    return b


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
    t.deiconify(); no_close_button(t); t.focus_force()
    try: t.grab_set()
    except tk.TclError: pass
    t.wait_window()
    return res[0]


def center(t, over=None):
    """a window placed in the middle of the emulator's (seen at once), drawn there before it shows"""
    over = over or getattr(V, 'APP_ROOT', None) or tk._default_root
    try:
        t.withdraw(); t.update_idletasks()
        x = over.winfo_rootx() + (over.winfo_width() - t.winfo_reqwidth()) // 2
        y = over.winfo_rooty() + (over.winfo_height() - t.winfo_reqheight()) // 3
        t.geometry('+%d+%d' % (max(0, x), max(0, y)))
        t.deiconify(); t.lift(); t.focus_force()
    except tk.TclError:
        pass


def _rel(p):
    try:
        r = os.path.relpath(p, V.ROOT)
        return p if r.startswith('..') else r
    except ValueError:                                  # (another drive)
        return p


def _paint(b, on):
    b.configure(fg=BG if on else G, bg=G if on else BG, activebackground=HI if on else '#123',
                activeforeground=BG if on else HI)


class Deck:
    STRIPS = ('Tube', 'Room light', 'Case', 'Overlay', 'Sound', 'Vectrex unit', 'Display', 'Cartridge')

    def __init__(self, app):
        self.V = V
        self.app = app
        a = app
        self.before = dict(look=a.game_now()['look'], unit=dict(a.unit))   # (Before: the look as it was)
        self.comparing = None
        self.strip = getattr(a, 'deck_strip', 'Tube')
        if self.strip not in self.STRIPS: self.strip = 'Tube'
        self.rows = []; self.focus = 0
        self.pad_prev = set(); self.pad_t = 0.0
        self.save_job = None
        t = self.t = tk.Toplevel(a.root, bg=BG)
        t.withdraw()                                            # (shown once it is built and in its place: no flash)
        t.title('Config'); t.transient(a.root); t.resizable(False, False)
        try: t.attributes('-toolwindow', True)
        except tk.TclError: pass
        a.inp.block = True                                      # (the game runs on behind it, without input)
        f9, f10b = _font(a, 9), _font(a, 10, True)
        self.bgc = BG
        top = tk.Frame(t, bg=TOPBG, highlightthickness=1, highlightbackground=EDGE); top.pack(fill='x')
        tk.Label(top, text='Tab: close   hold H: before   LB / RB: strip', fg=DIM, bg=TOPBG, font=f9).pack(side='left', padx=12, pady=6)
        # the whole emulator and this game: one panel
        pan = tk.Frame(t, bg=PANEL, highlightthickness=1, highlightbackground=EDGE); pan.pack(fill='x', padx=10, pady=(8, 0))
        q = tk.Frame(pan, bg=PANEL); q.pack(fill='x', padx=10, pady=(6, 0))
        tk.Label(q, text='QUALITY', fg=DIM, bg=PANEL, font=f9, width=10, anchor='w').pack(side='left')
        self.qbtn = {}
        for key in ('low', 'medium', 'high', 'custom'):
            b = _button(q, a.QUALITY_NAMES[key], lambda k=key: self.set_quality(k), a, small=True); b.pack(side='left', padx=2)
            self.qbtn[key] = b
        _button(q, 'Optimize', lambda: a.test_pc(self.t), a, small=True).pack(side='left', padx=(14, 2))
        self.qhint = tk.Label(pan, fg=DIM, bg=PANEL, font=f9, anchor='w', justify='left'); self.qhint.pack(fill='x', padx=(10 + 10 * 8, 10))
        tk.Frame(pan, bg=EDGE, height=1).pack(fill='x', padx=10, pady=4)
        gm = tk.Frame(pan, bg=PANEL); gm.pack(fill='x', padx=10)
        tk.Label(gm, text='THIS GAME', fg=DIM, bg=PANEL, font=f9, width=10, anchor='w').pack(side='left')
        self.gbtn = {}
        for key, name, cmd in (('save', 'Save', a.save_game), ('revert', 'Revert', a.revert_game),
                               ('remove', 'Remove', a.remove_game), ('default', 'Make default', a.make_default)):
            b = _button(gm, name, lambda c=cmd: (c(), self.sync_all()), a, small=True); b.pack(side='left', padx=2)
            self.gbtn[key] = b
        self.ghint = tk.Label(pan, fg=DIM, bg=PANEL, font=f9, anchor='w'); self.ghint.pack(fill='x', padx=(10 + 10 * 8, 10), pady=(0, 6))
        # the strips: tabs on top of the strip's area (the chosen one in the area's colour)
        sb = tk.Frame(t, bg=BG); sb.pack(fill='x', padx=10, pady=(10, 0))
        self.sbtn = {}
        for st in self.STRIPS:
            b = tk.Label(sb, text=st, font=f9, padx=8, pady=3, cursor='hand2', highlightthickness=1)
            b.bind('<Button-1>', lambda e, st=st: self.show(st)); b.pack(side='left', padx=(0, 2))
            self.sbtn[st] = b
        self.body = tk.Frame(t, bg=BODY, highlightthickness=1, highlightbackground=EDGE2); self.body.pack(fill='x', padx=10)
        self.inner = None
        foot = tk.Frame(t, bg=TOPBG, highlightthickness=1, highlightbackground=EDGE); foot.pack(fill='x', pady=(10, 0))
        self.reset_b = _button(foot, 'Reset this strip', self.reset_strip, a, small=True); self.reset_b.pack(side='left', padx=(12, 0), pady=6)
        _button(foot, 'Reset all', self.reset_all, a, small=True).pack(side='left', padx=(8, 0), pady=6)
        bb = _button(foot, 'Hold: before', None, a, small=True); bb.pack(side='left', padx=8)
        bb.bind('<ButtonPress-1>', lambda e: self.compare(True)); bb.bind('<ButtonRelease-1>', lambda e: self.compare(False))
        tk.Label(foot, text='right click a slider: its default', fg=DIM, bg=TOPBG, font=f9).pack(side='left', padx=8)
        for k in ('<Tab>', '<Escape>'):
            t.bind(k, lambda e: (self.close(), 'break')[1])
        t.bind('<KeyPress-h>', lambda e: self.compare(True)); t.bind('<KeyRelease-h>', lambda e: self.compare(False))
        t.bind('<Left>', lambda e: (self.step_strip(-1), 'break')[1]); t.bind('<Right>', lambda e: (self.step_strip(1), 'break')[1])
        t.protocol('WM_DELETE_WINDOW', self.close)
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
        t.geometry('+%d+%d' % (x, y))
        t.deiconify()
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
        if s == 'Tube':
            return [('group', 'PHOSPHOR'),
                    ('check', 'Phosphor (afterglow and bloom)', PH, a.toggle_phosphor, None),
                    ('in', [('slider', 'bloom', PH),
                            ('check', 'Afterglow', lambda: A('trail_on'), flip('trail_on'), PH),
                            ('in', [('slider', 'trail', AG), ('slider', 'phos_grow', AG), ('slider', 'phos_smooth', AG)])]),
                    ('slider', 'white_k', lambda: has('white_k')),
                    ('group', 'SHAPE AND GLASS'),
                    ('check', 'Tube geometry (a real Vectrex)', lambda: A('curve_on'), flip('curve_on'), None),
                    ('in', [('slider', 'geom_k', lambda: A('curve_on'))]),
                    ('check', 'Glass edge (rounded corners)', lambda: A('glass_on'), flip('glass_on'), None),
                    ('check', 'Bend smoothing (curves without facets)', lambda: A('bend_smooth', True), flip('bend_smooth'), None),
                    ('check', 'Second glow pass (a softer, wider glow)', lambda: A('passes', 2) >= 2,
                     lambda: (setattr(a.ren, 'passes', 1 if a.ren.passes >= 2 else 2), a.save_settings()), PH),
                    ('check', 'Dither (no bands in soft glows)', lambda: A('dither_on', True), flip('dither_on'), None),
                    ('check', 'Corner glow (halo where the beam rests)', lambda: A('halo_on'), flip('halo_on'), lambda: not A('even_beam')),
                    ('in', [('slider', 'halo_k', lambda: A('halo_on') and not A('even_beam'))]),
                    ('slider', 'dot_k', lambda: not A('even_beam')),
                    ('check', 'Even beam (an ideal vector display)', lambda: A('even_beam'), flip('even_beam'), lambda: not A('beam_trace')),
                    ('check', 'Beam trace (the spot lights the phosphor)', lambda: A('beam_trace'), flip('beam_trace'), lambda: has('beam_trace')),
                    ('group', 'SIGNAL'),
                    ('check', 'Mains hum wobble', lambda: A('wob_on'), a.toggle_wobble, None),
                    ('in', [('slider', 'wob_strength', lambda: A('wob_on')),
                            ('choice', 'Mains', [('%d Hz' % hz, (lambda hz=hz: A('mains_hz', 0) == hz), (lambda hz=hz: a.set_mains(hz)))
                                                 for hz in V.vecrender.MAINS_HZ], lambda: A('wob_on'))]),
                    ('check', 'Brightness bending', lambda: A('bend_on'), flip('bend_on'), None),
                    ('in', [('choice', 'Bending type', [(n, (lambda i=i: A('bend_type', 0) % len(V.vecrender.BEND_TYPES) == i),
                                                         (lambda i=i: a.set_bend_type(i))) for i, n in enumerate(V.vecrender.BEND_TYPES)],
                             lambda: A('bend_on')),
                            ('slider', 'bend_strength', lambda: A('bend_on'))])]
        if s == 'Room light':
            rows = [('group', 'THE ROOM'), ('slider', 'art_room', None),
                    ('check', 'Glass catches room light (without an overlay)', lambda: A('glass_room'), flip('glass_room'), None)]
            if getattr(a.ren, 'lprog', None):
                rows += [('group', 'THE CEILING LIGHT'),
                         ('check', 'Reflected on the glass', lambda: A('light_on'), flip('light_on'), lambda: A('glass_room')),
                         ('in', [('slider', 'light_k', lambda: A('glass_room') and A('light_on'))]),
                         ('check', 'Reflected on the overlay', lambda: A('light_ov'), flip('light_ov'), None),
                         ('in', [('slider', 'light_ov_k', lambda: A('light_ov'))]),
                         ('buttons', [('Light position...', a.light_pad)], lambda: A('light_on') or A('light_ov'))]
            return rows
        if s == 'Case':
            CS = lambda: A('case_on')
            return [('group', 'THE PLASTIC ROUND THE GLASS'),
                    ('check', 'Case  (C)', CS, a.toggle_case, None),
                    ('in', [('slider', 'case_refl', CS), ('slider', 'case_wear', CS)]),
                    ('note', lambda: 'shown without an overlay - lit by the Room light strip')]
        if s == 'Overlay':
            rows = [('group', 'SHEET'),
                    ('check', 'Overlay  (O)', lambda: A('art_on') and bool(A('art', None)), a.toggle_art, None),
                    ('check', 'Choose automatically (by ROM name)', lambda: a.art_auto, a.toggle_art_auto, None),
                    ('buttons', [('Choose overlay...', a.choose_art), ('No overlay for this game', a.no_art)], None),
                    ('in', [('slider', 'art_tint', OV), ('slider', 'art_print', OV),
                            ('check', 'Parallax (seen from the usual height)', lambda: A('art_parallax'), flip('art_parallax'), OV)])]
            if getattr(a.ren, 'gprog', None):
                rows += [('group', 'PLASTIC AND SCRATCHES'),
                         ('check', 'Plastic glow', lambda: A('art_glow'), flip('art_glow'), OV),
                         ('in', [('slider', 'art_glow_k', lambda: OV() and A('art_glow'))]),
                         ('check', 'Scratches (decades of handling)', lambda: A('scr_on'), a.toggle_scratches, OV),
                         ('in', [('slider', 'scr_wear', SC), ('slider', 'scr_fine', SC), ('slider', 'scr_medium', SC),
                                 ('slider', 'scr_large', SC), ('slider', 'scr_scrib', SC), ('slider', 'scr_glint', SC),
                                 ('slider', 'scr_reach', SC), ('buttons', [('New random scratches', a.new_scratches)], SC)])]
            rows += [('group', 'POSITION ON THE SCREEN'),
                     ('slider', 'art_dx', OV), ('slider', 'art_dy', OV), ('slider', 'art_scale', OV),
                     ('buttons', [('Reset position and size', a.reset_art_adj)], OV)]
            return rows
        if s == 'Sound':
            return [('group', 'THE VECTREX'), ('slider', 'volume', None),
                    ('check', 'Vectrex buzz', lambda: C().buzz_on, lambda: a.flip(a.run.crt, 'buzz_on'), None),
                    ('in', [('slider', 'buzz_vol', lambda: C().buzz_on)]),
                    ('check', 'Vectrex speaker', lambda: C().speaker_on, lambda: a.flip(a.run.crt, 'speaker_on'), None),
                    ('group', 'SPEECH MODULE (CONTROLLER PORT 2)'),
                    ('slider', 'voice_vol', lambda: a.speech != 'off'),
                    ('choice', 'Speech module', [('Full-range speakers', lambda: C().voice_full, lambda: a.set_voice_full(True)),
                                                 ('Small speaker', lambda: not C().voice_full, lambda: a.set_voice_full(False))],
                     lambda: a.speech != 'off')]
        if s == 'Vectrex unit':
            rows = [('group', 'WHICH VECTREX'),
                    ('choice', 'Unit', [(V.UNIT_NAMES[n].replace(' (measured)', ''), (lambda n=n: a.unit_name == n),
                                         (lambda n=n: a.unit_preset(n))) for n in ('owner', 'ideal', 'cranky', 'random')], None),
                    ('note', lambda: 'now: %s - every part acts on the game at once' % V.UNIT_NAMES.get(a.unit_name, 'Custom')),
                    ('group', 'STARTING UP'),
                    ('check', 'Start switched on', lambda: a.start_on, a.toggle_start_on, None),
                    ('check', 'Skip the Vectrex intro (start screen)', lambda: a.skip_boot, a.toggle_skip_boot, None),
                    ('group', 'ITS PARTS')]
            rows += [('unit', p[0]) for p in V.UNIT_PARAMS]
            rows += [('unit', 'gap'), ('buttons', [('Export...', a.export_unit), ('Import...', a.import_unit)], None)]
            return rows
        if s == 'Display':
            rates = [('Matched to the monitor', lambda: not a.vrr and a.rate != 'max', lambda: a.set_screen('60')),
                     ("Monitor's full rate", lambda: not a.vrr and a.rate == 'max', lambda: a.set_screen('max')),
                     ('G-Sync / FreeSync', lambda: a.vrr, lambda: a.set_screen('vrr'))]
            rot = [(n, (lambda d=d: a.rot == d), (lambda d=d: a.set_rotation(d))) for d, n in
                   ((0, 'Normal'), (90, '90 right'), (180, '180'), (270, '90 left'))]
            return [('group', 'THE SCREEN'), ('choice', 'Screen', rates, None),
                    ('in', [('check', 'Smooth motion (60 Hz screens: vectors between game frames, +20 ms)', lambda: a.smooth,
                             a.toggle_smooth, lambda: not a.vrr)]),
                    ('choice', 'Rotate', rot, None),
                    ('group', 'BELOW THE PICTURE'),
                    ('check', 'On-screen display (fps, Hz, speed, volume)', lambda: a.fps_on, a.toggle_fps, None),
                    ('check', 'Volume knob and RESET button', lambda: a.knob_on, a.toggle_knob, None),
                    ('check', 'Cartridge (click: the ROM browser)', lambda: a.cart_on, a.toggle_cart, None),
                    ('group', 'SIZE'),
                    ('slider', 'monitor_inch', None),
                    ('buttons', [('Authentic (9)', a.real_size), ('Fit screen (0)', lambda: a.zoom_step(None))], None)]
        if s == 'Cartridge':
            hw = [(lbl, (lambda k=k: a.cart_hw == k), (lambda k=k: a.set_cart_hw(k))) for k, lbl in V.CART_HW.items()]
            p2 = [(lbl, (lambda k=k: a.speech == k), (lambda k=k: a.set_speech(k))) for k, lbl in V.PORT2.items()]
            um2 = lambda: (a.args.rom or '').lower().endswith('.um2')
            def px_now():
                r = a.args.rom or ''
                if not r.lower().endswith(('.img', '.pit')):
                    return 'not in the slot'
                n = os.path.splitext(os.path.basename(r))[0]
                return 'in the slot now: ' + ('its menu' if n.lower() == 'pitrex' else n)
            return [('group', 'THE SLOT AND PORT 2'),
                    ('menu', 'Cartridge hardware', hw, None), ('menu', 'Controller port 2', p2, None),
                    ('note', lambda: "a game won't start with a speech module? Choose Joystick."),
                    ('group', 'USB KEYBOARD AND MOUSE'),
                    ('check', "The PC's mouse and keyboard on the cartridge's USB port", lambda: a.usb_hid, a.toggle_usb_hid, None),
                    ('note', lambda: 'Ctrl+K: type on it (Ctrl+K again: the keys back to the controller)'),
                    ('group', 'ULTIMATE VECTREX MULTICART 2'),
                    ('note', lambda: 'in the slot now' if um2() else 'not in the slot'),
                    ('check', 'Show UVMC2 games (.um2) in the ROM list', lambda: a.show_um2, a.toggle_um2, None),
                    ('choice', 'Headphone jack', [('Clean (line out)', lambda: C().jack_mode == 'clean', lambda: a.set_jack('clean')),
                                                  ('Through the speaker', lambda: C().jack_mode == 'speaker', lambda: a.set_jack('speaker')),
                                                  ('Off', lambda: C().jack_mode == 'off', lambda: a.set_jack('off'))], None),
                    ('in', [('slider', 'jack_vol', lambda: C().jack_mode != 'off')]),
                    ('buttons', [('Open UVMC2 game...', a.open_um2), ('Open the SD folder', a.open_sd_dir),
                                 ('Choose SD folder...', a.choose_sd_dir)], None),
                    ('note', lambda: 'games that need files (Doom: doom/DOOM.WAD, Quake: quake/pak0.pak) read them from it'),
                    ('note', lambda: 'SD card folder: %s' % _rel(a.sd_dir)),
                    ('group', 'PITREX'),
                    ('note', px_now),
                    ('buttons', [('Start the PiTrex', a.start_pitrex), ('Open PiTrex program...', a.open_pitrex)], None),
                    ('buttons', [('Open the SD folder', a.open_px_dir), ('Choose SD folder...', a.choose_px_dir)], None),
                    ('note', lambda: 'arcade games need your own ROMs: pitrex/roms (MAME), pitrex/roms/aae'),
                    ('note', lambda: 'SD card folder: %s' % _rel(a.px_dir))]
        return []

    RESETS = {'Tube': 'reset_tube', 'Room light': 'reset_light', 'Case': 'reset_case', 'Overlay': 'reset_overlay', 'Sound': 'reset_sound',
              'Display': 'reset_display', 'Cartridge': 'reset_addons'}

    def reset_all(self):
        """every strip to its defaults: those of the Quality profile chosen (Custom: High's) - asked first"""
        a = self.app
        key = a.current_profile()
        name = a.QUALITY_NAMES['high' if key == 'custom' else key]
        if dialog('question', 'Reset all', 'Reset every tab to the %s defaults?' % name, parent=self.t, yesno=True,
                  buttons=('Reset all', 'Cancel')):
            a.reset_all()
        self.sync_all()

    def reset_strip(self):
        if self.strip == 'Vectrex unit':
            self.app.unit_preset('owner')
        elif self.strip in self.RESETS:
            getattr(self.app, self.RESETS[self.strip])()
        self.sync_all()

    # ---- building a strip
    def show(self, s):
        if getattr(self, 'strip', None) and self.rows:          # (the row a controller was on, per strip: kept)
            self.app.deck_focus = dict(getattr(self.app, 'deck_focus', None) or {}, **{self.strip: self.focus})
        self.strip = s; self.app.deck_strip = s
        self.focus = (getattr(self.app, 'deck_focus', None) or {}).get(s, 0)
        for k, b in self.sbtn.items():                          # (the tab: joined to the strip's area when chosen)
            b.configure(bg=BODY if k == s else BG, fg=HI if k == s else DIM,
                        highlightbackground=EDGE2 if k == s else EDGE)
        if self.inner is not None:
            self.inner.destroy()
        self.inner = tk.Frame(self.body, bg=BODY); self.inner.pack(fill='both', expand=True, padx=8, pady=8)
        self.box = None; self.parent = self.inner; self.bgc = BODY
        self.rows = []
        self.add_rows(self.spec(s))
        self.focus = min(self.focus, max(0, len(self.rows) - 1))
        self.sync_all()

    def add_rows(self, rows):
        for row in rows:
            if row[0] == 'group':                               # (a box with its caption)
                self.box = tk.Frame(self.inner, bg=PANEL, highlightthickness=1, highlightbackground=EDGE)
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
                r = getattr(self, 'mk_' + row[0])(*row[1:])
                r['bg'] = self.bgc
                self.rows.append(r)

    def frame(self):
        f = tk.Frame(self.parent, bg=self.bgc); f.pack(fill='x', pady=1)
        return f

    reading = None                                      # (while a check is made: the settings its state reads)

    def changed(self, key):
        """the look setting key differs from its default (the mark \u25cf beside it)"""
        a = self.app
        d = getattr(a, 'look_default', {})
        if not a.ren or key not in d:
            return False
        v, dv = getattr(a.ren, key, None), d[key]
        return abs(float(v) - float(dv)) > 1e-6 if isinstance(dv, float) else v != dv

    def mark(self, f):
        m = tk.Label(f, text='', fg=WARN, bg=self.bgc, font=_font(self.app, 9), width=2, anchor='w')
        return m

    def mk_check(self, label, get, cmd, dep):
        f = self.frame()
        self.reading = []
        v = tk.BooleanVar(value=bool(get()))
        keys, self.reading = self.reading, None
        key = keys[-1] if keys else None
        c = tk.Checkbutton(f, text=label, variable=v, command=lambda: (cmd(), self.sync_all()), fg=G, bg=self.bgc,
                           selectcolor=self.bgc, activebackground=self.bgc, activeforeground=HI, font=_font(self.app, 9),
                           anchor='w', highlightthickness=0, bd=0, disabledforeground='#2a4a3a')
        c.pack(side='left')
        m = self.mark(f); m.pack(side='left')
        def sync():
            v.set(bool(get()))
            m.configure(text='\u25cf' if key and self.changed(key) else '')
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
        return dict(kind='buttons', f=f, w=bs, sync=lambda: None, dep=dep, act=lambda d: (items[0][1](), self.sync_all()))

    def mk_choice(self, label, opts, dep):
        f = self.frame()
        tk.Label(f, text=label, fg=G, bg=self.bgc, font=_font(self.app, 9), width=16, anchor='w').pack(side='left')
        bs = []
        for text, get, cmd in opts:
            b = _button(f, text, lambda c=cmd: (c(), self.sync_all()), self.app, small=True); b.pack(side='left', padx=2)
            bs.append((b, get))
        def sync():
            for b, get in bs:
                _paint(b, bool(get()))
        def act(d):
            i = next((k for k, (b, get) in enumerate(bs) if get()), 0)
            opts[(i + d) % len(opts)][2](); self.sync_all()
        return dict(kind='choice', f=f, w=[b for b, g in bs], sync=sync, dep=dep, act=act)

    def mk_menu(self, label, opts, dep):
        f = self.frame()
        tk.Label(f, text=label, fg=G, bg=self.bgc, font=_font(self.app, 9), width=20, anchor='w').pack(side='left')
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
        row = self.mk_scale(label, get, setv, r['lo'], r['hi'], r['log'],
                            lambda v: r['fmt'] % (v * r['show']), r['default'], dep)
        sc = row['f'].winfo_children()[-1]                   # (the slider: the mark between it and the value)
        m = self.mark(row['f']); m.pack(side='right', before=sc)
        sync0 = row['sync']
        def sync():
            sync0()
            m.configure(text='\u25cf' if self.changed(r['attr']) else '')
        row['sync'] = sync
        return row

    def mk_unit(self, key):
        a, V = self.app, self.V
        if key == 'gap':
            label, lo, hi, fmt = 'Line gaps (stop delay)', 4.0, 10.0, '%.1f cycles'
        else:
            k, i, label, lo, hi, fmt, var = next(p for p in V.UNIT_PARAMS if p[0] == key)
            label = label.split(' (')[0]
        def setv(v):
            a.set_unit(key, round(v, 4))
            if self.save_job: self.t.after_cancel(self.save_job)
            self.save_job = self.t.after(400, a.save_settings)
        return self.mk_scale(label, lambda: a.unit.get(key, 0.0), setv, lo, hi, False, lambda v: fmt % v,
                             V.UNIT_PRESETS['owner'][key], None)

    def mk_scale(self, label, get, setv, lo, hi, log, fmt, default, dep):
        f = self.frame(); N = 1000
        if log == 'mired':                                      # (even in mireds: White balance)
            to_pos = lambda v: N * (V.vecrender.white_pos(v) + 1.0) / 2.0     # (the middle neutral: white_pos)
            to_val = lambda p: V.vecrender.white_from_pos(2.0 * p / N - 1.0)
        else:
            to_pos = lambda v: N * (math.log(v / lo) / math.log(hi / lo) if log else (v - lo) / (hi - lo))
            to_val = lambda p: lo * (hi / lo) ** (p / N) if log else lo + (hi - lo) * p / N
        tk.Label(f, text=label, fg=G, bg=self.bgc, font=_font(self.app, 9), width=28, anchor='w').pack(side='left')
        val = tk.Label(f, fg=G, bg=self.bgc, font=_font(self.app, 9), width=12, anchor='w')
        busy = [False]
        def moved(p):
            if busy[0]: return
            if abs(float(p) - to_pos(max(lo, min(hi, get())))) < 0.5:   # (Tk reports the value set by sync() later:
                return                                                  #  not a move - nothing changes)
            setv(to_val(float(p))); val.configure(text=fmt(get()))
        sc = tk.Scale(f, from_=0, to=N, orient='horizontal', length=300, showvalue=0, command=moved, bg=G, fg=G,
                      troughcolor='#123', highlightthickness=0, activebackground=HI, sliderrelief='flat', bd=0,
                      width=12, sliderlength=18)
        val.pack(side='right'); sc.pack(side='right', padx=6)        # (from the right: lined up at every indent)
        def sync():
            busy[0] = True; sc.set(to_pos(max(lo, min(hi, get())))); busy[0] = False
            val.configure(text=fmt(get()))
        def to_default(e=None):
            setv(default); sync(); return 'break'
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
        self.qhint.configure(text='\u25cf changed from default', fg=WARN)
        st = a.game_state()
        name = a.rom_name
        self.ghint.configure(text={'everyday': '%s uses the everyday settings (changes are kept for all games).' % name,
                                   'own': '%s has its own settings (saved).' % name,
                                   'changed': '%s has its own settings - changed, not saved yet.' % name}[st],
                             fg=WARN if st == 'changed' else DIM)
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
        self.show(self.STRIPS[(i + d) % len(self.STRIPS)])

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
        a = self.app
        try:
            d = a.inp.resolve('p1') or next(iter(a.inp._devs), None)
            st = a.inp.pad_state(d) if d else None
        except Exception:   # noqa
            st = None
        if st:
            b = set(st['buttons'])
            x, y = st['pov'] or (st['axes'].get('LX', 0.0), st['axes'].get('LY', 0.0))
            if x > 0.6: b.add('>')
            if x < -0.6: b.add('<')
            if y > 0.6: b.add('^')
            if y < -0.6: b.add('v')
            new = b - self.pad_prev; self.pad_prev = b
            if new:
                self.pad_t = time.perf_counter()
            if 'LB' in new: self.step_strip(-1)
            if 'RB' in new: self.step_strip(1)
            live = [i for i, r in enumerate(self.rows) if r['act']]
            if live and ('^' in new or 'v' in new):
                k = live.index(self.focus) if self.focus in live else 0
                self.focus = live[(k + (1 if 'v' in new else -1)) % len(live)]
                self.rows[self.focus]['f'].update_idletasks()
            r = self.rows[self.focus] if 0 <= self.focus < len(self.rows) else None
            if r and r['act'] and (r['dep'] is None or r['dep']()):
                if '>' in b and (r['kind'] == 'scale' or '>' in new): r['act'](1)
                if '<' in b and (r['kind'] == 'scale' or '<' in new): r['act'](-1)
                if 'A' in new and r['kind'] in ('check', 'buttons'): r['act'](1)
            if 'B' in new or 'BACK' in new:
                self.close(); return
            if 'Y' in new: self.compare(True)
            if 'Y' not in b and self.comparing is not None: self.compare(False)
        self.n = getattr(self, 'n', 0) + 1
        if self.n % 6 == 0 or (st and self.pad_used()):
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


def open_deck(app, e=None):
    d = getattr(app, 'deck', None)
    if d is not None:
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
    tk.Label(top, text='Shown once. Everything here can be changed later: the menus, and Config (Tab).',
             fg=DIM, bg=TOPBG, font=f9, anchor='w').pack(fill='x', padx=14, pady=(0 if first else 8, 8))
    PB = PANEL                                          # (the boxes' colour)
    grid = None
    rowi = [0]
    def box(caption):
        """a captioned box; the rows after it go in it"""
        nonlocal grid
        bx = tk.Frame(t, bg=PB, highlightthickness=1, highlightbackground=EDGE); bx.pack(fill='x', padx=10, pady=(8, 0))
        tk.Label(bx, text=caption, fg=DIM, bg=PB, font=_font(a, 8), anchor='w').pack(fill='x', padx=10, pady=(4, 0))
        grid = tk.Frame(bx, bg=PB); grid.pack(fill='x', padx=10, pady=(0, 6))
        rowi[0] = 0
    def row(name):
        r = rowi[0]; rowi[0] += 1
        mark = tk.Label(grid, fg=G, bg=PB, font=f9, width=2); mark.grid(row=r, column=0, sticky='w', pady=3)
        tk.Label(grid, text=name, fg=HI, bg=PB, font=f9, width=18, anchor='w').grid(row=r, column=1, sticky='w')
        txt = tk.Label(grid, fg=G, bg=PB, font=f9, anchor='w', justify='left', wraplength=430); txt.grid(row=r, column=2, sticky='w')
        side = tk.Frame(grid, bg=PB); side.grid(row=r, column=3, sticky='e', padx=(8, 0))
        return mark, txt, side
    def ok(mark, good):
        mark.configure(text='✓' if good else '!', fg=G if good else WARN)
    # what was found
    box('WHAT WAS FOUND')
    m, txt, side = row('BIOS')
    ok(m, os.path.exists(V.BIOS)); txt.configure(text=_rel(V.BIOS) + ' (with Mine Storm built in)' if os.path.exists(V.BIOS) else 'not found: put bios.bin in the system folder')
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
        m, txt, side = row('Games')
        def upd(m=m, txt=txt):
            have, of = getter.games_have()
            if have >= of:
                ok(m, True); txt.configure(text='roms - %d games, all %d original ones (the ROM browser: L)' % (count(), of), fg=G)
            else:
                miss(m, txt, 'roms - %d of the %d original games: the missing ones can be downloaded' % (have, of))
        found.append(upd); upd()
    games_row()
    m, txt, side = row('Overlays')
    od = os.path.join(V.ROOT, 'overlays')
    no = len([f for f in os.listdir(od) if f.lower().endswith(('.art', '.png'))]) if os.path.isdir(od) else 0
    if no > 1:
        ok(m, True); txt.configure(text='%d overlays (they come with the emulator)' % no)
    else:
        miss(m, txt, "only %d: the overlays come with the emulator - put them back in the overlays folder" % no)
    def item_row(name, key, have_text, miss_text):
        m, txt, side = row(name)
        def upd(m=m, txt=txt):
            if getter.state(key, a)[0]:
                ok(m, True); txt.configure(text=have_text, fg=G)
            else:
                miss(m, txt, miss_text)
        found.append(upd); upd()
    item_row('Homebrew games', 'homebrew', 'the free homebrew games that have an overlay',
             'missing: free homebrew games that have an overlay - they can be downloaded')
    item_row('Speech ROM', 'speech', 'found: VecVoice / VecVox games can speak',
             'missing: VecVoice / VecVox games are silent - it can be downloaded')
    item_row('PiTrex SD card', 'pitrex', '%s (PiTrex: Vectrex > PiTrex)' % _rel(a.px_dir),
             'missing: the PiTrex has no programs - they can be downloaded')
    # the UVMC2 SD card
    m, txt, side = row('UVMC2 SD folder')
    def sd_state(m=m, txt=txt):
        e = os.path.isdir(a.sd_dir); ok(m, e)
        txt.configure(text=('%s' if e else '%s does not exist yet (UVMC2 games such as Doom need it)') % _rel(a.sd_dir))
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
    tk.Label(gf, text=('Everything in red can be downloaded automatically, in one click.' if missing() else
                       'All there. Vectrex > Get games & extras... fetches them again any time.'),
             fg=RED if missing() else DIM, bg=PB, font=f9).pack(side='left', padx=(10, 0))
    # quality
    box('QUALITY')
    m, txt, side = row('Quality'); ok(m, True)
    qv = tk.StringVar(value=a.current_profile())
    qf = tk.Frame(grid, bg=PB); qf.grid(row=rowi[0], column=2, columnspan=2, sticky='w'); rowi[0] += 1
    qb = {}
    def qset(mode):
        qv.set(mode)
        for k, b in qb.items(): _paint(b, k == mode)
    for key in ('low', 'medium', 'high'):
        b = _button(qf, a.QUALITY_NAMES[key], lambda k=key: qset(k), a, small=True); b.pack(side='left', padx=2); qb[key] = b
    qset(qv.get())
    def optimize(txt=txt):
        """Optimize (pctest): the best this PC runs at full speed, set now; the presets here still choose instead"""
        import pctest
        rep = pctest.optimize(a, lambda st: (txt.configure(text='optimizing: ' + st), t.update()))
        if rep is None:
            txt.configure(text='drawn without OpenGL: no effects to choose'); return
        pctest.apply(a, rep)
        qset(a.current_profile())
        txt.configure(text='\n'.join(pctest.result_lines(rep)[:-1]))
    _button(side, 'Optimize', optimize, a, small=True).pack()
    if first:
        txt.configure(text='Optimize: a few short tests find the best settings this PC can run at full speed.')
        t.after(400, optimize)                          # (a new install: at once)
    else:
        txt.configure(text='%s now. Optimize finds the best this PC can run at full speed.' % a.QUALITY_NAMES[a.current_profile()])
    # the display: one choice, explained, the best for this screen chosen
    v = vrrdetect.detect(a.screen.winfo_id())
    ref = int(round(getattr(a, 'refresh', 0) or 60))
    n60 = max(1, round(ref / 60.0))                     # (the matched rate, as App.apply_rate picks it)
    mhz = 60 if abs(ref - 60 * n60) <= 1 else (int(round(ref / max(1, ref // 50))) if ref >= 50 else 60)
    box('DISPLAY')
    m, txt, side = row('Display'); ok(m, True)
    txt.configure(text='How 50 game frames a second meet this %d Hz screen. None of these tear: all wait for the '
                       "screen's refresh." % ref)
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
    df = tk.Frame(grid, bg=PB); df.grid(row=rowi[0], column=1, columnspan=3, sticky='w', pady=(2, 4)); rowi[0] += 1
    for k, title, expl in opts:
        usable = k != 'max' or ref >= 100
        rb = tk.Radiobutton(df, text=title + ('   (recommended)' if k == best else ''), variable=dv, value=k,
                            command=lambda: touched.__setitem__(0, True), fg=HI if usable else '#2a4a3a', bg=PB,
                            selectcolor=PB, activebackground=PB, activeforeground=HI, font=f9, anchor='w',
                            highlightthickness=0, bd=0, disabledforeground='#2a4a3a', state='normal' if usable else 'disabled')
        rb.pack(fill='x', anchor='w')
        tk.Label(df, text=expl, fg=DIM if usable else '#2a4a3a', bg=PB, font=f9, anchor='w', justify='left',
                 wraplength=640).pack(fill='x', padx=(22, 0), pady=(0, 4))
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
    m, txt, side = row('Start'); ok(m, True)
    son = tk.BooleanVar(value=True if first else a.start_on)
    txt.destroy()
    tk.Checkbutton(grid, text='Start switched on (else: turn the volume knob, as on a real Vectrex)', variable=son,
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
            if (d == 'smooth') != bool(a.smooth): a.toggle_smooth()
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
    t.geometry('+%d+%d' % (r.winfo_rootx() + max(0, (r.winfo_width() - t.winfo_reqwidth()) // 2),
                           r.winfo_rooty() + max(0, (r.winfo_height() - t.winfo_reqheight()) // 3)))
    t.deiconify()
    no_close_button(t)
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
