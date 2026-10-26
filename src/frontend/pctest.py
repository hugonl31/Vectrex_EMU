"""pctest.py - Optimize (Vectrex > Quality, the Config window, the first-start setup): measures once what this PC can
draw at full speed and turns off only the effects it can not keep up with - the effects' own switches, so the profile
reads Custom and Config marks each change. Nothing switches by itself while playing.

The measurement: a heavy scene drawn the way a busy game draws it (made here - circles of joined pieces with rests,
raster text, long lines, resting dots: ~2900 pieces, like Vectorblade's busiest frames), at each profile: the
processor's time to build its lines and to draw a picture, the graphics card's time for a picture (a GL_TIME_ELAPSED
query; without one: the time to finish, less the processor's), and the game thread's own work per frame (the sound,
the frame read from the core). The game is paused meanwhile. One processor core does all the Python work (the game
thread and the drawing share it), so its load is the sum: (game thread + lines) x 50 game frames + picture x the
screen's refreshes a second. Full speed: under 75 % of the processor and of the graphics card."""
import ctypes, math, os, platform, random, time
import tkinter as tk

V = None                                # (the app's module: set by vectrex_emu)
FIT = 0.75                              # (a profile fits: processor and graphics card each busy under this)
OFF_NAMES = dict(passes='the second glow pass', curve_on='tube curve', glass_on='glass edge', glass_room='glass in room light',
                 light_on='light on the glass', light_ov='light on the overlay', art_glow='plastic glow', scr_on='scratches',
                 art_parallax='parallax', case_refl='case reflections', bend_smooth='bend smoothing', dither_on='dither')
KEPT = 'kept: the afterglow, the glow, the corner dots and their glow, the overlay\'s colours'


def scene():
    """a heavy frame as the core sends it: a float array, columns x0 y0 x1 y1 brightness e (1/8 cycles) t (cycles)"""
    np = V._np
    rnd = random.Random(7)
    mx, my = V.ALG_MAX_X, V.ALG_MAX_Y
    rows, t = [], 0.0
    def piece(x0, y0, x1, y1, z, cyc):
        nonlocal t
        rows.append((x0, y0, x1, y1, z, 8.0 * cyc, t)); t += cyc
    for _ in range(60):                                 # circles of joined pieces, a rest where each starts
        cx, cy, r = rnd.uniform(0.2, 0.8) * mx, rnd.uniform(0.2, 0.8) * my, rnd.uniform(600, 3000)
        z = rnd.choice((127, 95, 63))
        piece(cx + r, cy, cx + r, cy, z, 6)
        for k in range(24):
            a0, a1 = 2 * math.pi * k / 24, 2 * math.pi * (k + 1) / 24
            piece(cx + r * math.cos(a0), cy + r * math.sin(a0), cx + r * math.cos(a1), cy + r * math.sin(a1), z,
                  max(1.0, 2 * math.pi * r / 24 / 80.0))
        t += 20
    for row in range(15):                               # raster text: rows of short dashes
        y = my * (0.1 + 0.05 * row)
        x = mx * 0.15
        for k in range(60):
            w = rnd.choice((140, 140, 280, 420))
            piece(x, y, x + w, y, 127, 2); x += w + 80
        t += 30
    for _ in range(400):                                # long lines
        x0, y0 = rnd.uniform(0, mx), rnd.uniform(0, my)
        x1, y1 = rnd.uniform(0, mx), rnd.uniform(0, my)
        piece(x0, y0, x1, y1, 127, math.hypot(x1 - x0, y1 - y0) / 80.0); t += 8
    for _ in range(100):                                # resting dots (shots, stars)
        x, y = rnd.uniform(0, mx), rnd.uniform(0, my)
        piece(x, y, x, y, rnd.choice((127, 63, 31)), 30); t += 8
    return np.array(rows, dtype=np.float64)


def _game_thread_ms(app, A):
    """the game thread's own work for such a frame: the frame read (records into lines) and the sound"""
    np = V._np
    n = len(A)
    rec = np.zeros(n, dtype=V.LINE_DT)
    for i, f in enumerate(('x0', 'y0', 'x1', 'y1', 'c', 'e', 't')):
        rec[f] = np.clip(A[:, i], 0, 65535 if f != 'c' else 255)
    body = rec.tobytes()
    from array import array
    pcm = array('h', [random.randint(-3000, 3000) for _ in range(882)]).tobytes()   # (a frame's sound)
    crt = app.run.crt
    t0 = time.perf_counter()
    for _ in range(10):
        arr = np.frombuffer(body, dtype=V.LINE_DT, count=n)
        lines = V.Lines(arr.tolist())
        B = np.empty((n, 7)); [B.__setitem__((slice(None), i), arr[f]) for i, f in enumerate(('x0', 'y0', 'x1', 'y1', 'c', 'e', 't'))]
        lines.np = B
        crt.process(pcm, lines, None, arr)
    return 1000.0 * (time.perf_counter() - t0) / 10


def _profile_ms(app, A, vals, n=24):
    """(processor ms for a frame's lines, processor ms for a picture, graphics card ms for a picture) with these
    settings on the renderer"""
    r = app.ren
    keep = {k: getattr(r, k) for k in vals if hasattr(r, k)}
    for k, v in vals.items():
        if hasattr(r, k):
            setattr(r, k, v)
    t0 = time.perf_counter()
    for _ in range(8):
        r._build(A, load=False)
    build = 1000.0 * (time.perf_counter() - t0) / 8
    gl = r.gl
    q = None
    if getattr(r, 'gpu_q', None) is not None:
        try:
            qs = (ctypes.c_uint * 1)(); r.GenQ(1, qs); q = qs[0]
        except Exception:   # noqa
            q = None
    cpu, gpu, tot = [], [], []
    mark = {}
    def timed():                                        # (instead of the swap: the picture's work only)
        mark['c'] = time.perf_counter()
        if q is not None:
            r.EndQ(0x88BF)
        gl.glFinish()
        mark['f'] = time.perf_counter()
    sw = r.__dict__.get('_swap')
    r._swap = timed
    try:
        T = time.perf_counter()
        for i in range(n):
            T += 0.02
            r.add_frame(A, T - 0.02, 0.02)
            if q is not None:
                r.BeginQ(0x88BF, q)
            t0 = time.perf_counter()
            r._present(T)
            cpu.append(mark['c'] - t0); tot.append(mark['f'] - t0)
            if q is not None:
                ns = ctypes.c_uint64(0); r.QueryU64(q, 0x8866, ctypes.byref(ns)); gpu.append(ns.value / 1e9)
    finally:
        if sw is None:
            del r._swap
        else:
            r._swap = sw
        for k, v in keep.items():
            setattr(r, k, v)
        r.young = []; r.last = None
    med = lambda xs: sorted(xs)[len(xs) // 2] * 1000.0 if xs else None
    c = med(cpu)
    g = med(gpu) if gpu else max(0.0, med(tot) - c)
    return build, c, g


def _loads(game, hz, b, c, g):
    """(processor, graphics card) share in use for a frame's lines b, a picture's processor time c and graphics time g"""
    return (game + b) * 50.0 / 1000.0 + c * hz / 1000.0, g * hz / 1000.0


# the effects Optimize may turn off, and what each is when off (vecrender PROFILE_KEYS)
FEATURES = (('dither_on', False), ('bend_smooth', False), ('case_refl', 0.0), ('art_parallax', False), ('glass_room', False),
            ('light_ov', False), ('light_on', False), ('passes', 1), ('art_glow', False), ('scr_on', False),
            ('glass_on', False), ('curve_on', False))


def optimize(app, say=lambda s: None):
    """Optimize: every effect on (High), then - only if this PC can not keep full speed so - the effects that cost it
    most turned off one by one, until it can (each one's cost measured on its own first; those that cost nothing
    measurable stay on). The game is paused meanwhile. dict(vals= the switches, off=[(key, saving %)], before=,
    after= (processor, graphics) shares, fits=, cpu=, gpu=, hz=); None without the OpenGL renderer"""
    a = app
    if not (a.ren and hasattr(a.ren, '_present')):
        return None
    was = a.run.paused
    a.run.paused = True
    try:
        A = scene()
        say('measuring the processor...')
        game = _game_thread_ms(a, A)
        hz = float(a.hz or 60.0)
        vals = dict(a.profile_values('high'))
        fits = lambda l: l[0] < FIT and (l[1] or 0.0) < FIT
        say('drawing with every effect on...')
        before = _loads(game, hz, *_profile_ms(a, A, vals, 12))
        off = []
        now = before
        if not fits(before):
            gains = []
            for i, (k, v) in enumerate(FEATURES):
                if k not in vals or vals[k] == v:
                    continue
                say('measuring each effect (%d of %d)...' % (i + 1, len(FEATURES)))
                l = _loads(game, hz, *_profile_ms(a, A, dict(vals, **{k: v}), 12))
                gain = max(before[0] - l[0], before[1] - l[1])
                if gain > 0.02:                         # (2 % of the processor or the card: it matters)
                    gains.append((gain, k, v))
            gains.sort(reverse=True)
            for gain, k, v in gains:
                old = vals[k]
                vals[k] = v
                say('trying without %s...' % OFF_NAMES[k])
                l = _loads(game, hz, *_profile_ms(a, A, vals, 16))
                saved = round(100 * max(now[0] - l[0], now[1] - l[1]))
                if saved < 2:                           # (no real saving once the others are off: stays on)
                    vals[k] = old
                    continue
                off.append((k, saved))
                now = l
                if fits(now):
                    break
    finally:
        a.run.paused = was
    return dict(vals=vals, off=off, before=before, after=now, fits=fits(now), cpu=cpu_name(), gpu=gpu_name(a), hz=hz)


def cpu_name():
    n = platform.processor() or platform.machine()
    if V.IS_WIN:
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as k:
                n = winreg.QueryValueEx(k, 'ProcessorNameString')[0].strip()
        except OSError:
            pass
    return '%s, %d cores' % (n, os.cpu_count() or 0)


def gpu_name(a):
    try:
        gl = a.ren.gl; gl.glGetString.restype = ctypes.c_char_p
        return (gl.glGetString(0x1F01) or b'?').decode(errors='replace')
    except Exception:   # noqa
        return '?'


def apply(app, rep):
    """the result as the effects' switches (the profile then reads Custom, or the preset it matches); the switches
    before it, for Undo"""
    r = app.ren
    undo = {k: getattr(r, k) for k in rep['vals'] if hasattr(r, k)}
    for k, v in rep['vals'].items():
        if hasattr(r, k):
            setattr(r, k, v)
    if not rep['fits']:                                 # (even so too slow: the screen's own rate, no in-betweens)
        undo['_rate'] = app.rate; undo['_smooth'] = app.smooth
        if app.rate == 'max':
            app.set_screen('60')
        if app.smooth:
            app.toggle_smooth()
    app.save_settings()
    if getattr(app, 'deck', None):
        try: app.deck.sync_all()
        except Exception: pass   # noqa
    return undo


def undo(app, before):
    r = app.ren
    for k, v in before.items():
        if not k.startswith('_') and hasattr(r, k):
            setattr(r, k, v)
    if '_rate' in before and before['_rate'] != app.rate:
        app.set_screen(before['_rate'])
    if '_smooth' in before and bool(before['_smooth']) != bool(app.smooth):
        app.toggle_smooth()
    app.save_settings()
    if getattr(app, 'deck', None):
        try: app.deck.sync_all()
        except Exception: pass   # noqa


def result_lines(rep):
    """what Optimize did, as text"""
    pct = lambda l: 'processor %d %%, graphics %d %%' % (round(100 * l[0]), round(100 * (l[1] or 0.0)))
    if not rep['off']:
        return ['This PC runs every effect at full speed.', 'Nothing was turned off.',
                '(a heavy scene: %s)' % pct(rep['before'])]
    rows = ['Turned off to keep this PC at full speed:']
    rows += ['  - %s  (saves %d %%)' % (OFF_NAMES[k], g) for k, g in rep['off']]
    rows.append('Everything else stays on. In Config the changes are marked ●.')
    rows.append('(a heavy scene: before %s; now %s)' % (pct(rep['before']), pct(rep['after'])))
    if not rep['fits']:
        rows.append("Even so a heavy game may run a little slow: the screen at the monitor's own rate, Smooth motion off.")
    return rows


def window(app, parent=None, on_done=None):
    """Optimize: says what it will do, does it (the game paused), shows what was turned off - OK or Undo"""
    import deck
    a = app
    t = tk.Toplevel(parent or a.root, bg=deck.BG)
    t.title('Optimize'); t.transient(parent or a.root); t.resizable(False, False)
    f9, f10b = deck._font(a, 9), deck._font(a, 10, True)
    top = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); top.pack(fill='x')
    tk.Label(top, text='OPTIMIZE', fg=deck.HI, bg=deck.TOPBG, font=f10b, anchor='w').pack(fill='x', padx=14, pady=8)
    bx = tk.Frame(t, bg=deck.PANEL, highlightthickness=1, highlightbackground=deck.EDGE); bx.pack(fill='x', padx=10, pady=(8, 0))
    body = tk.Label(bx, fg=deck.G, bg=deck.PANEL, font=f9, justify='left', anchor='w', wraplength=560,
                    text='Optimize will now run a few short tests (a few seconds, the game paused) to find the best '
                         'settings this PC can run at full speed.\n\nIt starts with every effect on and turns off only '
                         'what this PC can not keep up with. Afterwards you see what was changed.')
    body.pack(fill='x', padx=12, pady=10)
    bar = tk.Frame(t, bg=deck.BG); bar.pack(fill='x', padx=10, pady=10)
    st = {}
    def close():
        t.destroy()
        if on_done:
            on_done(st.get('rep'))
    def start():
        go.pack_forget(); no.pack_forget()
        rep = optimize(a, lambda s: (body.configure(text=s), t.update()))
        if rep is None:
            body.configure(text='This PC draws without OpenGL: there are no effects to choose.')
            deck._button(bar, 'OK', close, a, on=True).pack(side='left')
            return
        st['rep'] = rep
        st['undo'] = apply(a, rep)
        body.configure(text='\n'.join(result_lines(rep)))
        deck._button(bar, 'OK', close, a, on=True).pack(side='left')
        if rep['off']:
            deck._button(bar, 'Undo', lambda: (undo(a, st['undo']), st.pop('rep', None), close()), a).pack(side='left', padx=8)
        a.show_look('quality: optimized (%s)' % a.QUALITY_NAMES[a.current_profile()])
        a.flash('QUALITY OPTIMIZED', ('%d EFFECTS OFF' % len(rep['off'])) if rep['off'] else 'EVERY EFFECT ON', secs=3.0)
        deck.center(t, a.root)                          # (grown: in the middle again)
    go = deck._button(bar, 'Start', start, a, on=True); go.pack(side='left')
    no = deck._button(bar, 'Cancel', close, a); no.pack(side='left', padx=8)
    t.bind('<Escape>', lambda e: close())
    deck.center(t, a.root)                              # (in the middle of the emulator: seen at once)
    return t
