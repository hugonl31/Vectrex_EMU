"""vecrender.py - anti-aliased vector display for arena_play.py (Windows GDI+ through ctypes).

Draws the emulator's vectors into an off-screen 32-bit bitmap with GDI+ anti-aliasing, each line
twice: a wide faint glow and a bright core (the look of a vector CRT), then copies the bitmap to the
window in one call. Line widths scale with the picture, so any window size looks smooth.
"""
import ctypes, math, sys, ctypes.wintypes as W

IS_WIN = sys.platform == 'win32'      # (macOS / Linux: osdep.py makes the OpenGL context)

ALG_MAX_X, ALG_MAX_Y = 33000, 41000

class _StartupInput(ctypes.Structure):
    _fields_ = [('GdiplusVersion', ctypes.c_uint32), ('DebugEventCallback', ctypes.c_void_p),
                ('SuppressBackgroundThread', W.BOOL), ('SuppressExternalCodecs', W.BOOL)]

PARGB32 = 0x000E200B
SMOOTH_AA = 4                 # SmoothingModeAntiAlias
PIXOFF_HALF = 4               # PixelOffsetModeHalf (sharper AA at the same cost)
COMP_COPY = 1                 # CompositingModeSourceCopy
UNIT_PIXEL = 2
CAP_ROUND = 2
LEVELS = 16                   # brightness steps (pens)

class Renderer:
    def __init__(self, hwnd):
        self.g = ctypes.windll.gdiplus
        tok = ctypes.c_ulong()
        si = _StartupInput(1, None, False, False)
        if self.g.GdiplusStartup(ctypes.byref(tok), ctypes.byref(si), None) != 0:
            raise OSError('GDI+ not available')
        self.token = tok
        self.hwnd = hwnd
        self.win = self.bmp = self.buf = None
        self.pens = []
        self.size = (0, 0)
        self.f = ctypes.c_float
        self.DrawLine = self.g.GdipDrawLine
        self.DrawLine.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_float, ctypes.c_float,
                                  ctypes.c_float, ctypes.c_float]

    def _free(self):
        for p in self.pens:
            self.g.GdipDeletePen(p)
        self.pens = []
        for h, fn in ((self.buf, 'GdipDeleteGraphics'), (self.bmp, 'GdipDisposeImage'), (self.win, 'GdipDeleteGraphics')):
            if h:
                getattr(self.g, fn)(h)
        self.win = self.bmp = self.buf = None

    def resize(self, w, h):
        """(re)create the buffers and pens for a w x h window"""
        w, h = max(8, w), max(8, h)
        if (w, h) == self.size and self.buf:
            return
        self._free()
        self.size = (w, h)
        g = self.g
        win = ctypes.c_void_p(); g.GdipCreateFromHWND(W.HWND(self.hwnd), ctypes.byref(win)); self.win = win
        g.GdipSetCompositingMode(win, COMP_COPY)
        bmp = ctypes.c_void_p(); g.GdipCreateBitmapFromScan0(w, h, 0, PARGB32, None, ctypes.byref(bmp)); self.bmp = bmp
        buf = ctypes.c_void_p(); g.GdipGetImageGraphicsContext(bmp, ctypes.byref(buf)); self.buf = buf
        g.GdipSetSmoothingMode(buf, SMOOTH_AA)
        g.GdipSetPixelOffsetMode(buf, PIXOFF_HALF)
        # picture: 33000 x 41000 emulator units, fitted and centred
        s = min(w / ALG_MAX_X, h / ALG_MAX_Y)
        self.sx = self.sy = s
        self.ox = (w - ALG_MAX_X * s) / 2
        self.oy = (h - ALG_MAX_Y * s) / 2
        ph = ALG_MAX_Y * s                                  # picture height in pixels
        core = max(1.0, ph / 520.0)                         # ~0.35 mm beam on a Vectrex-size picture
        glow = core * 3.2
        self.pens = []
        for i in range(LEVELS):
            v = 0.32 + 0.68 * i / (LEVELS - 1)
            r, gr, b = int(200 * v), int(228 * v), int(255 * v)
            for width, alpha in ((glow, int(70 * v)), (core, 255)):
                pen = ctypes.c_void_p()
                g.GdipCreatePen1(ctypes.c_uint32((alpha << 24) | (r << 16) | (gr << 8) | b), ctypes.c_float(width),
                                 UNIT_PIXEL, ctypes.byref(pen))
                g.GdipSetPenLineCap197819(pen, CAP_ROUND, CAP_ROUND, 0)
                self.pens.append(pen)
        self.dot = core * 0.9                               # a dot is drawn as a short line

    def draw(self, lines):
        if not self.buf:
            return
        g, buf, DL = self.g, self.buf, self.DrawLine
        g.GdipGraphicsClear(buf, ctypes.c_uint32(0xFF000000))
        sx, sy, ox, oy, pens, dot = self.sx, self.sy, self.ox, self.oy, self.pens, self.dot
        segs = []
        for x0, y0, x1, y1, c, *_ in lines:
            X0, Y0, X1, Y1 = ox + x0 * sx, oy + y0 * sy, ox + x1 * sx, oy + y1 * sy
            if abs(X1 - X0) < 0.5 and abs(Y1 - Y0) < 0.5:
                X1 = X0 + dot
            segs.append((min(LEVELS - 1, c * LEVELS // 128), X0, Y0, X1, Y1))
        for k in (0, 1):                                    # all glows first, then all cores
            for lv, X0, Y0, X1, Y1 in segs:
                DL(buf, pens[lv * 2 + k], X0, Y0, X1, Y1)
        g.GdipDrawImageI(self.win, self.bmp, 0, 0)

    def close(self):
        self._free()
        self.g.GdiplusShutdown(self.token)

# ------------------------------------------------------------------ OpenGL (GPU, preferred)
class _PFD(ctypes.Structure):
    _fields_ = [('nSize', W.WORD), ('nVersion', W.WORD), ('dwFlags', W.DWORD), ('iPixelType', ctypes.c_ubyte),
                ('cColorBits', ctypes.c_ubyte), ('cRedBits', ctypes.c_ubyte), ('cRedShift', ctypes.c_ubyte),
                ('cGreenBits', ctypes.c_ubyte), ('cGreenShift', ctypes.c_ubyte), ('cBlueBits', ctypes.c_ubyte),
                ('cBlueShift', ctypes.c_ubyte), ('cAlphaBits', ctypes.c_ubyte), ('cAlphaShift', ctypes.c_ubyte),
                ('cAccumBits', ctypes.c_ubyte), ('cAccumRedBits', ctypes.c_ubyte), ('cAccumGreenBits', ctypes.c_ubyte),
                ('cAccumBlueBits', ctypes.c_ubyte), ('cAccumAlphaBits', ctypes.c_ubyte), ('cDepthBits', ctypes.c_ubyte),
                ('cStencilBits', ctypes.c_ubyte), ('cAuxBuffers', ctypes.c_ubyte), ('iLayerType', ctypes.c_ubyte),
                ('bReserved', ctypes.c_ubyte), ('dwLayerMask', W.DWORD), ('dwVisibleMask', W.DWORD),
                ('dwDamageMask', W.DWORD)]

GL_LINES, GL_BLEND, GL_LINE_SMOOTH, GL_LINE_SMOOTH_HINT, GL_NICEST = 0x0001, 0x0BE2, 0x0B20, 0x0C52, 0x1102
GL_SRC_ALPHA, GL_ONE, GL_COLOR_BUFFER_BIT, GL_FLOAT = 0x0302, 1, 0x4000, 0x1406
GL_VERTEX_ARRAY, GL_COLOR_ARRAY, GL_PROJECTION, GL_MODELVIEW = 0x8074, 0x8076, 0x1701, 0x1700

class GLRenderer:
    """anti-aliased lines on the graphics card: every line twice (wide faint glow, bright core),
    additive blending like phosphor, two draw calls per frame"""
    def __init__(self, hwnd, widget=None):
        if IS_WIN:
            self._wgl(hwnd)
        else:
            import osdep
            self.ctxo = osdep.gl_context(widget)
            self.gl = self.ctxo.gl
            self.swap_interval = self.ctxo.swap_interval
            self.swap_interval(0)
        gl = self.gl
        gl.glVertexPointer.argtypes = [ctypes.c_int, ctypes.c_uint, ctypes.c_int, ctypes.c_void_p]
        gl.glColorPointer.argtypes = [ctypes.c_int, ctypes.c_uint, ctypes.c_int, ctypes.c_void_p]
        self._gl_setup()

    def _swap(self):
        if IS_WIN:
            self.gdi.SwapBuffers(self.dc)
        else:
            self.ctxo.swap()

    def _proc(self, name, res, *args):
        """an OpenGL function beyond 1.1 (shaders, framebuffers)"""
        if not IS_WIN:
            return self.ctxo.proc(name, res, *args)
        p = self.gl.wglGetProcAddress(name.encode())
        if not p:
            raise OSError('no ' + name)
        return ctypes.WINFUNCTYPE(res, *args)(p)

    def refresh_rate(self):
        """the monitor's refresh rate (Hz), 0 if unknown"""
        if not IS_WIN:
            return self.ctxo.refresh_rate()
        gdc = ctypes.windll.gdi32.GetDeviceCaps
        gdc.argtypes = [W.HDC, ctypes.c_int]
        return gdc(self.dc, 116)                                   # VREFRESH

    def _wgl(self, hwnd):
        self.u, self.gdi, self.gl = ctypes.windll.user32, ctypes.windll.gdi32, ctypes.windll.opengl32
        # 64-bit handles: declare every handle-taking call (an undeclared int overflows)
        self.u.GetDC.argtypes = [W.HWND]; self.u.GetDC.restype = W.HDC
        self.u.ReleaseDC.argtypes = [W.HWND, W.HDC]
        self.gdi.ChoosePixelFormat.argtypes = [W.HDC, ctypes.c_void_p]
        self.gdi.SetPixelFormat.argtypes = [W.HDC, ctypes.c_int, ctypes.c_void_p]
        self.gdi.SwapBuffers.argtypes = [W.HDC]
        self.gl.wglCreateContext.argtypes = [W.HDC]; self.gl.wglCreateContext.restype = ctypes.c_void_p
        self.gl.wglMakeCurrent.argtypes = [W.HDC, ctypes.c_void_p]
        self.gl.wglDeleteContext.argtypes = [ctypes.c_void_p]
        self.gl.wglGetProcAddress.argtypes = [ctypes.c_char_p]
        self.gl.wglGetProcAddress.restype = ctypes.c_void_p
        self.hwnd = hwnd
        self.dc = self.u.GetDC(W.HWND(hwnd))
        pfd = _PFD(); pfd.nSize = ctypes.sizeof(_PFD); pfd.nVersion = 1
        pfd.dwFlags = 0x4 | 0x20 | 0x1                  # DRAW_TO_WINDOW | SUPPORT_OPENGL | DOUBLEBUFFER
        pfd.iPixelType = 0; pfd.cColorBits = 32; pfd.cAlphaBits = 8
        fmt = self.gdi.ChoosePixelFormat(self.dc, ctypes.byref(pfd))
        if not fmt or not self.gdi.SetPixelFormat(self.dc, fmt, ctypes.byref(pfd)):
            raise OSError('no OpenGL pixel format')
        self.ctx = self.gl.wglCreateContext(self.dc)
        if not self.ctx or not self.gl.wglMakeCurrent(self.dc, ctypes.c_void_p(self.ctx)):
            raise OSError('no OpenGL context')
        p = self.gl.wglGetProcAddress(b'wglSwapIntervalEXT')     # no vsync wait in the UI thread
        self.swap_interval = ctypes.WINFUNCTYPE(W.BOOL, ctypes.c_int)(p) if p else (lambda n: False)
        self.swap_interval(0)

    def _gl_setup(self):
        gl = self.gl
        for f in ('glLineWidth', 'glClearColor', 'glColor4f'):
            getattr(gl, f).argtypes = [ctypes.c_float] * (1 if f == 'glLineWidth' else 4)
        gl.glOrtho.argtypes = [ctypes.c_double] * 6
        gl.glEnable(GL_BLEND); gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE)
        gl.glEnable(GL_LINE_SMOOTH); gl.glHint(GL_LINE_SMOOTH_HINT, GL_NICEST)
        gl.glClearColor(0, 0, 0, 1)
        gl.glEnableClientState(GL_VERTEX_ARRAY); gl.glEnableClientState(GL_COLOR_ARRAY)
        self.size = (0, 0)
        self.core = 1.5
        self.phosphor = True                              # bloom + decay (B toggles)
        self.hist = []                                    # the last frames' vertex arrays (decay)

    def resize(self, w, h):
        w, h = max(8, w), max(8, h)
        if not IS_WIN:
            self.ctxo.reshape()                           # (macOS: the OpenGL view follows the frame)
            k = getattr(self.ctxo, 'scale', 1.0)          # (a Retina screen: its pixels, not Tk's points)
            w, h = int(round(w * k)), int(round(h * k))
        self.real_size = (w, h)                           # (the window; turned 90 / 270 degrees the picture is
        if getattr(self, 'rot', 0) in (90, 270):          #  laid out at the turned size and turned at the end)
            w, h = h, w
        self.size = (w, h)
        gl = self.gl
        gl.glViewport(0, 0, w, h)
        gl.glMatrixMode(GL_PROJECTION); gl.glLoadIdentity()
        rs = getattr(self, 'reserve', 0.0)               # (a strip below the picture: the knob panel)
        s = min(w / ALG_MAX_X, h / (ALG_MAX_Y * (1.0 + rs))) * getattr(self, 'view_scale', 1.0)   # (full screen: + / - size)
        ox = (w / s - ALG_MAX_X) / 2
        oy = (h / s - ALG_MAX_Y * (1.0 + rs)) / 2          # (above the picture; below it: the strip too)
        gl.glOrtho(-ox, ALG_MAX_X + ox, h / s - oy, -oy, -1, 1)      # emulator units, y down
        gl.glMatrixMode(GL_MODELVIEW); gl.glLoadIdentity()
        ph = ALG_MAX_Y * s
        self.map = (s, ox, oy)                            # emulator units -> window pixels
        self.core = max(1.0, ph / 560.0)
        self.dotlen = 0.6 * self.core / s                 # a dot: a short segment in emulator units

    def draw(self, lines):
        gl = self.gl
        gl.glClear(GL_COLOR_BUFFER_BIT)
        n = len(lines)
        if n:
            v = (ctypes.c_float * (n * 4))()
            c = (ctypes.c_float * (n * 8))()             # core colours
            cg = (ctypes.c_float * (n * 8))()            # glow colours (faint)
            dl = self.dotlen
            i = j = 0
            for x0, y0, x1, y1, b, *_ in lines:
                if x0 == x1 and y0 == y1:
                    x1 += dl
                v[i] = x0; v[i + 1] = y0; v[i + 2] = x1; v[i + 3] = y1; i += 4
                k = (0.30 + 0.70 * (b & 127) / 127.0) * min(1.0, beam_z((b & 127) / 127.0)[0] / 0.775) * (BEAM_FADE if b & 128 else 1.0)
                r, g, bl = 0.78 * k, 0.89 * k, k
                c[j] = c[j + 4] = cg[j] = cg[j + 4] = r
                c[j + 1] = c[j + 5] = cg[j + 1] = cg[j + 5] = g
                c[j + 2] = c[j + 6] = cg[j + 2] = cg[j + 6] = bl
                c[j + 3] = c[j + 7] = 1.0
                cg[j + 3] = cg[j + 7] = 0.22
                j += 8
        else:
            v = None
        if self.phosphor:
            # phosphor decay: the two previous frames fade out behind this one (~25 ms persistence)
            gl.glDisableClientState(GL_COLOR_ARRAY)
            gl.glLineWidth(self.core * 1.3)
            for (pv, pn), a in zip(self.hist, (0.10, 0.24)):
                gl.glColor4f(0.70, 0.84, 1.0, a)
                gl.glVertexPointer(2, GL_FLOAT, 0, pv)
                gl.glDrawArrays(GL_LINES, 0, pn * 2)
            if n:                                         # bloom: a wide, very faint halo
                gl.glColor4f(0.55, 0.70, 1.0, 0.05)
                gl.glVertexPointer(2, GL_FLOAT, 0, v)
                gl.glLineWidth(self.core * 9.0)
                gl.glDrawArrays(GL_LINES, 0, n * 2)
            gl.glEnableClientState(GL_COLOR_ARRAY)
            self.hist = (self.hist + [(v, n)])[-2:] if n else self.hist[-1:]
        if n:
            gl.glVertexPointer(2, GL_FLOAT, 0, v)
            gl.glColorPointer(4, GL_FLOAT, 0, cg)         # glow: wide and faint
            gl.glLineWidth(self.core * 3.4)
            gl.glDrawArrays(GL_LINES, 0, n * 2)
            gl.glColorPointer(4, GL_FLOAT, 0, c)          # core
            gl.glLineWidth(self.core)
            gl.glDrawArrays(GL_LINES, 0, n * 2)
        self._swap()

    def close(self):
        if not IS_WIN:
            self.ctxo.close(); return
        self.gl.wglMakeCurrent(None, None)
        self.gl.wglDeleteContext(ctypes.c_void_p(self.ctx))
        self.u.ReleaseDC(W.HWND(self.hwnd), self.dc)

# ------------------------------------------------------------------ OpenGL with real bloom (GPU)
GL_TEXTURE_2D, GL_RGBA, GL_RGBA16F, GL_HALF_FLOAT = 0x0DE1, 0x1908, 0x881A, 0x140B
GL_LINEAR, GL_TEX_MIN, GL_TEX_MAG, GL_WRAP_S, GL_WRAP_T, GL_CLAMP_EDGE = 0x2601, 0x2801, 0x2800, 0x2802, 0x2803, 0x812F
GL_FRAMEBUFFER, GL_COLOR_ATT0, GL_FB_COMPLETE = 0x8D40, 0x8CE0, 0x8CD5
GL_FRAGMENT_SHADER, GL_COMPILE_STATUS, GL_LINK_STATUS = 0x8B30, 0x8B81, 0x8B82
GL_QUADS, GL_ONE_MINUS_SRC_ALPHA, GL_TEXTURE_COORD_ARRAY = 0x0007, 0x0303, 0x8078

BLUR_SHADER = b"""
uniform sampler2D tex;
uniform vec2 dir;
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec4 c = texture2D(tex, uv) * 0.2270270270;
    c += texture2D(tex, uv + dir * 1.3846153846) * 0.3162162162;
    c += texture2D(tex, uv - dir * 1.3846153846) * 0.3162162162;
    c += texture2D(tex, uv + dir * 3.2307692308) * 0.0702702703;
    c += texture2D(tex, uv - dir * 3.2307692308) * 0.0702702703;
    gl_FragColor = c;
}
"""

# a beam segment as a capsule with a Gaussian cross-section (the electron spot): the quad around it
# is shaded by the distance to the segment, so lines get soft round ends and a zero-length segment
# (the beam resting: dots, corners) is a round spot. gl_Color.rgb = premultiplied brightness,
# gl_Color.a = the spot's sigma / 16 px.
CAPSULE_SHADER = b"""
uniform float premul;
uniform float timed, age0, dtw, gain;
void main() {
    vec4 s = gl_TexCoord[0];
    vec2 p = gl_FragCoord.xy, a = s.xy, ba = s.zw - s.xy;
    float h = clamp(dot(p - a, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
    float d = length(p - a - ba * h);
    float sg = gl_Color.a * 16.0;
    float g = exp(-0.5 * d * d / (sg * sg));
    if (timed > 0.5) {                      // fresh phosphor light: brightness, and its life left
        float age = timed > 1.5 ? 0.0 : age0 - gl_TexCoord[1].x;
        if (age < 0.0 || age >= dtw) discard;          // (not drawn yet / already lit earlier)
        gl_FragColor = vec4(gl_Color.rgb * g * gain, exp(-age));      // (alpha: freshness, e^-age)
        return;
    }
    gl_FragColor = premul > 0.5 ? vec4(gl_Color.rgb * g, g) : vec4(gl_Color.rgb, g);
}
"""

# the beam's energy model: a line that took `cyc` cycles over `len` game pixels (160 emulator
# units) got e = cyc/len cycles of beam per pixel. Fitted to the owner's photos (2026-10-08):
#  - width (FWHM, game px) = 0.7 + 0.04 e^2: small text (e ~2.6) ~0.95 px with its rows nearly
#    touching, the big ROBOT ARENA title (e ~1.3, rows 1.41 px apart) ~0.8 px with a thin dark gap,
#    the slowly drawn ship ring (e ~5.7) ~2 px
#  - brightness 1 - exp(-e / 0.7): fast sweeps fainter
#  - the energy comes from the emulator (VT_ZTAU: the beam level rises and falls with a 2.5 cycle
#    time constant), so one-pixel dashes of the raster text stay ~2x fainter than long bars
#  - a resting beam (zero length: dots, corners) is a spot 0.8 + 0.06 cyc px wide
# Owner's photos, second look: the intensity-80 small text has thin rows (~0.33 px) with dark gaps,
# the intensity-127 title ~1 px rows: the width scales with (Z/127)^2.5 (the spot grows with the
# beam current), times 0.85 + 0.03 e^2 (slow lines like the ship's ring ~2 px).
BEAM_W0, BEAM_WZ = 0.30, 0.18     # width (FWHM, game px) = W0 + WZ * intensity (reference look)
BEAM_SPEED = 0.35            # how much fainter the fastest sweeps get
BEAM_S0, BEAM_SK, BEAM_SMAX = 0.6, 0.015, 0.95
# a resting beam (corners, the shots' dots, stars) gets more time to light the phosphor than a moving
# one: the owner (2026-10-12): corners and dots 100 % white on the screen, lines slightly less - the
# difference in brightness, not in size. Lines: BEAM_LINE x (1 - BEAM_SPEED e^(-e/0.8)), fainter the
# faster; a resting spot: BEAM_DOTMAX x min(1, cycles / BEAM_DOTC), as wide as the lines.
BEAM_LINE = 1.15
BEAM_FADE = 0.3              # the beam fading out after /BLANK (intensity bit 7 from the core): this much of a line
# a resting beam (a corner: ~18 cycles in one spot, ~10x a moving line's energy there) also lights a soft
# halo around itself (light scattered in the tube's glass; the owner's photos: corner dots and stars in
# a glow). It softens the dark line ends next to corners without hiding them; text has no resting spots.
BEAM_HALO_W, BEAM_HALO_K, BEAM_HALO_C = 1.6, 0.014, 18.0  # FWHM (game px), brightness (default, the owner's choice 2026-10-14; Tube > Corner glow strength), cycles for full
BEAM_DOTC, BEAM_DOTMAX = 4.0, 1.6
BEAM_DOTW, BEAM_DOTWK = 1.0, 0.002   # its width: the line width x DOTW + DOTWK per cycle (game px)
# a long, bright rest blooms: Vectorblade's enemy shots (intensity 127, ~38 cycles in one spot) are as
# bright as the player's shots (short bars) on the owner's tube, on the emulator they were tiny points.
# The spot's energy E = cycles x the intensity's brightness (beam_z); corners in all games stay <= ~18
# (Spinball's dots 26): above BEAM_BLOOM_E0 the spot widens up to BEAM_BLOOM_W x and its glow up to
# BEAM_BLOOM_H x, fully at BEAM_BLOOM_E1.
BEAM_BLOOM_E0, BEAM_BLOOM_E1, BEAM_BLOOM_W, BEAM_BLOOM_H = 20.0, 38.0, 2.0, 2.5
# the beam's brightness and width with its intensity (Z/127): from intensity 63 up as before (0.55 + 0.45 z,
# the look tuned on the owner's photos); below it the tube is close to its cutoff and the spot fades and
# shrinks fast: the owner (2026-10-12): Vectorblade's background stars (intensity 15-47) are much
# thinner, smaller and fainter on the real tube than the shots (127), on the emulator nearly the same.
BEAM_LOWZ, BEAM_LOWP, BEAM_LOWW = 0.5, 1.7, 0.45  # below this z: the cutoff curve (BEAM_Z0); width x LOWW at z = 0
# the tube's cutoff, measured (testcart/bright_test, the owner's photo and count by eye 2026-10-14): the
# lines from $20 (very faint; $18 dark), then steeply brighter ($30 / $38 / $40 ~ 0.37 / 0.64 / 1 of each
# other), the dots clearly from $20: brightness ((Z - Z0) / (63.5 - Z0))^LOWP with Z0 = 27, zero below
BEAM_Z0 = 27 / 127.0


def beam_z(zi):
    """(brightness factor, width factor) for the intensity zi = Z / 127"""
    if zi >= BEAM_LOWZ:
        return 0.55 + 0.45 * zi, 1.0
    r = max(0.0, (zi - BEAM_Z0) / (BEAM_LOWZ - BEAM_Z0))
    return (0.55 + 0.45 * BEAM_LOWZ) * r ** BEAM_LOWP, BEAM_LOWW + (1.0 - BEAM_LOWW) * zi / BEAM_LOWZ


# a resting spot collects light: on the owner's tube (counted by eye, 2026-10-14) the test page's dots
# (~85 cycles of rest) show clearly from $20, its lines only very faintly - a dot's own cutoff
# BEAM_Z0DOT ($18 dark),
# fully for rests of BEAM_DOTLOWC cycles and more (short rests - Vectorblade's 3-cycle stars - keep the
# line cutoff)
BEAM_Z0DOT, BEAM_DOTLOWP, BEAM_DOTLOWC = 23 / 127.0, 1.0, 40.0


def beam_zdot(zi):
    """a long-resting spot's brightness factor below intensity 63 (see BEAM_Z0DOT)"""
    r = max(0.0, (zi - BEAM_Z0DOT) / (BEAM_LOWZ - BEAM_Z0DOT))
    return (0.55 + 0.45 * BEAM_LOWZ) * r ** BEAM_DOTLOWP          # (a sharp onset: $18 dark, $20 clear)


BEAM_PEAK = 0.62             # below clipping, so overlaps add up instead of turning white

# the picture to the screen with a soft shoulder: linear up to 0.6, then rolling off towards 1, so
# bright slow shapes (the ship at full intensity) keep their detail instead of clipping white
TONE_SHADER = b"""
uniform sampler2D tex;
void main() {
    vec3 c = texture2D(tex, gl_TexCoord[0].st).rgb;
    vec3 k = 0.55 + 0.45 * (1.0 - exp(-(c - 0.55) / 0.6));
    gl_FragColor = vec4(mix(c, k, step(0.55, c)), 1.0);
}
"""

# the curved tube (C): the finished picture bowed like on a curved CRT face - straight lines bend
# outwards towards the edges (barrel), the corners drawn in. norm: window uv -> units of half the
# picture height (round, not stretched); CURVE_K: how much a point at that distance moves inwards.
CURVE_K = 0.07
ART_PARALLAX = 0.008    # the overlay's parallax: lowered this share of the picture height (Overlay > Parallax)
CURVE_SHADER = b"""
uniform sampler2D tex;
uniform vec2 norm, ctr;
uniform float k;
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec2 d = (uv - ctr) * norm;
    vec2 s = ctr + (uv - ctr) * (1.0 + k * dot(d, d));
    if (s.x < 0.0 || s.x > 1.0 || s.y < 0.0 || s.y > 1.0) gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    else gl_FragColor = vec4(texture2D(tex, s).rgb, 1.0);
}
"""

# ------------------------------------------------------------------ the overlay
# The plastic sheet in front of the tube (Overlay menu): flat, so it is laid on after the curved tube bends
# the picture. Its alpha: 255 printed (blocks the beam: the print, lit by the room), 1-254 a coloured
# filter (the beam behind it takes its colour; the filter itself catches some room light), 0 clear. Two
# passes on the screen: 1. multiply (blend DST_COLOR, SRC_COLOR: dst x 2 src, so a filter can also lift its
# colour), 2. add the sheet's own light.
# ------------------------------------------------------------------ overlay scratches
# Decades of handling: many fine scratches (a matte that scatters the tube's light into a halo
# around the lines) and fewer, deeper ones, down to a few deep scratches. Made on the graphics
# card from a seed (SCR_GEN_VS draws every scratch from its index), in the sheet's own space (162 x 207 mm),
# so the pattern stays with the overlay. The recipe is the owner's choice from previews (2026-10-20):
# fine wear 1 M (over the 1360-wide plane: 18.5 per mm2), 5-35 mm, straight; then nine steps, about 4.6x fewer
# each, lengths 0.4-2.5x of each step's typical one (at most 100 mm), slightly bent, more where hands
# hold the sheet; their depth stays low for long, then rises fast (cubic, the last three +10 %).
# Revised from the owner's real sheet (2026-10-21, previews D, C4 -> D3): the count per step as a bell
# shifted to the larger scratches - few fine ones, most at steps 4-7, few of the deepest (SCR_COUNTS, the
# owner's Wear 0.70 / Damage 0.68 included); every step 5-80 mm long, random direction, one line wide, the
# depth per step as before; a few of them scribbles (SCR_SCRIBBLE: short, a smooth sweep ending in a curl).
# A scratch lights up from every bit of light it lies across from: each point of light lights the
# scratches square to the direction to it (an angle lobe |sin|^p, Glint width), summed over all the light
# around (SCR_GLINT_SIG). A bright dot so gets rings of glinting scratches; a long line lights scratches of
# every direction (each part of it other ones), as on the owner's sheet (photos, 2026-10-20). Done with
# angle harmonics: the scratch textures keep each texel's sum of d, d cos 2t, d sin 2t, d cos 4t, d sin 4t,
# and the light is blurred with the matching kernels (r/s)^4 G(r) x cos / sin 2phi, 4phi - separable,
# at quarter size (SCR_HARM_*). The room's light makes the sheet more matte in its own colours; the
# plastic itself spreads the light softly, wide around bright lines (Plastic glow).
SCR_TPM = 2048 / 207.0                  # texels per mm of the scratch textures (2048 high)
SCR_COUNTS = (9843, 24276, 8177, 3480, 2900, 2418, 684, 112, 8, 2)    # steps 1 (fine wear) .. 10, on the sheet
SCR_LEN = (5.0, 80.0)                   # every scratch's length, mm (log-uniform within each tier, SCR_TIERS):
SCR_TIERS = (15.0, 35.0, 0.04, 0.005)   #  most 5-15, 4 % 15-35 (medium long), 0.5 % 35-80 (long)
SCR_GROUP = (1.0, 1.0, 1.0)             # Wear type at x1: fine (steps 1-3), medium (4-7), damage (8-10)
SCR_PEN = (0.45, 0.85, 1.5)            # damage cuts through the colour layer where widest: (from, full, light through)
SCR_BASE = (1.8145, 1.202, 0.5043)      # the owner's look at the sliders' x1 (2026-10-21): steps 1-5 x1.81, 6-10
                                        #  x1.20 (number; depth x sqrt), Glint strength x0.50
SCR_FINE = (SCR_COUNTS[0] / (162 * 207.0), SCR_LEN[0], SCR_LEN[1], 0.3, 1.0, 0.0376)    # per mm2, length mm, depth, width mm
SCR_FINE_1M = 18.53                     # (the first recipe's 1 M: the matte's scale, SCR_REF, stays with it)
SCR_SCRIBBLE = (0.04, 1.0)              # the share of scratches that are scribbles, and how strongly they turn
SCR_STEP_W = 0.0538                     # a step scratch's width (mm) per unit depth (the previews' drawing)
def _scr_steps():
    out = []
    for i in range(9):
        depth = (0.02 + 1.58 * (i / 8.0) ** 3) * (1.1 if i >= 6 else 1.0)
        out.append((SCR_COUNTS[i + 1], SCR_LEN[0], SCR_LEN[1], depth, 0))
    return out
SCR_STEPS = _scr_steps()                # (count on the sheet, length mm lo / hi, depth, half width)
SCR_GLINT_SIG = ((4.0, 1.0), (12.0, 0.7))    # the glint's reach: (sigma in quarter px, weight at Glint reach x1);
                                        #  peaks at 2 sigma: ~32 and ~96 window px
SCR_GLOW_SIG = 12.0                     # the plastic's glow (quarter px; the wider glint sigma's Gaussian)
SCR_GLINT_K = 2.0                       # the glint's strength at Glint strength x1
ART_GLOW_K = 0.718                        # the plastic glow's strength
def scr_lobe(width):
    """Glint width (x1 = |sin|^4; wider = a lower power): the lobe's harmonics a2, a4 (1 - a2 cos 2x + a4 cos 4x)"""
    import math
    p = 4.0 * 4.0 ** (1.0 - max(0.0, min(2.0, width)))
    n = 720; xs = [(i + 0.5) * math.pi / n for i in range(n)]
    f = [abs(math.cos(x)) ** p for x in xs]; m0 = sum(f)
    return (2 * sum(v * math.cos(2 * x) for v, x in zip(f, xs)) / m0,
            2 * sum(v * math.cos(4 * x) for v, x in zip(f, xs)) / m0)
SCR_DAMAGE_FROM = 4                     # steps from here on are the damage (Overlay > Damage): steps 6-10;
                                        #  the fine wear and the steps before it the wear (Overlay > Wear)
ART_ROOM_MID = 0.075                    # the room light curve's pivot (the default now 0.0648, the owner's)
ART_ROOM_POW = 2.0                      # the plastic's light goes with the setting squared (x2 slider = x4 light);
ART_PRINT_POW = 1.0                     #  the print's straight with it (x2 = x2: it clips less)
def room_light(r):
    """the room's light on the sheet from the Room light setting: the default as it is, steeper around it"""
    return ART_ROOM_MID * (max(0.0, r) / ART_ROOM_MID) ** ART_ROOM_POW
# the matte's scale: the fine layer's mean sum at 4 M (the previews' reference)
SCR_REF = 4 * SCR_FINE_1M * 15.42 * 0.65 * 0.7628 * SCR_FINE[5]
def _scr_handled_mean():
    import math as m
    tot = 0.0; mx = 0.0
    for yi in range(64):
        for xi in range(48):
            x, y = xi / 47.0, yi / 63.0
            mid = m.exp(-((x - 0.5) ** 2 / 0.08 + (y - 0.62) ** 2 / 0.12))
            edge = m.exp(-min(x, 1 - x) / 0.06) * 0.6
            corner = 1 - 0.7 * m.exp(-(min(x, 1 - x) ** 2 + min(y, 1 - y) ** 2) / 0.02)
            d = (0.25 + mid + edge) * corner; tot += d; mx = max(mx, d)
    return tot / (64 * 48) / mx, mx
SCR_HANDLED_MEAN, SCR_HANDLED_MAX = _scr_handled_mean()

SCR_GEN_VS = b"""
uniform float batch, limit, seed, stepid, mode, place, L0, L1, d0, d1, wv, off, chan, dmax;
uniform vec2 scrib;                     // (the share of scribbles, how strongly they turn)
uniform vec4 llen;                      // (the length tiers: 15 mm, 35 mm, the share 15-35, the share 35-80)
varying vec4 val;
const float SW = 162.0, SH = 207.0, PI = 3.14159265;
float h(float j, float k) {
    vec3 p = fract(vec3(j, batch, seed + 13.0 * k + 97.0 * stepid) * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float dens(vec2 q) {                    // where hands hold the sheet: the middle, the lower half, the edges
    float x = q.x, y = q.y, ex = min(x, 1.0 - x), ey = min(y, 1.0 - y);
    float mid = exp(-((x - 0.5) * (x - 0.5) / 0.08 + (y - 0.62) * (y - 0.62) / 0.12));
    float edge = exp(-ex / 0.06) * 0.6;
    float corner = 1.0 - 0.7 * exp(-(ex * ex + ey * ey) / 0.02);
    return (0.25 + mid + edge) * corner;
}
void main() {
    float j = gl_Vertex.x, s = gl_Vertex.y;
    float u = h(j, 0.0), v = h(j, 1.0);
    bool keep = j < limit;
    if (place > 0.5) keep = keep && h(j, 2.0) < dens(vec2(u, v)) / dmax;
    float hl = h(j, 3.0), pl = llen.w, pm = llen.z;    // (three tiers: long, medium long, the rest)
    float L = hl < pl ? exp(mix(log(llen.y), log(L1), hl / pl))
            : hl < pl + pm ? exp(mix(log(llen.x), log(llen.y), (hl - pl) / pm))
            : exp(mix(log(L0), log(llen.x), (hl - pl - pm) / (1.0 - pl - pm)));
    L = min(L, 100.0);
    float th0 = h(j, 4.0) * 2.0 * PI;
    float bend = mode > 0.5 ? sqrt(-2.0 * log(max(h(j, 5.0), 1e-6))) * cos(2.0 * PI * h(j, 6.0)) * 0.35 : 0.0;
    float d = mix(d0, d1, h(j, 7.0));
    float taper = sqrt(max(sin(PI * s), 0.0));
    if (mode > 0.5) taper *= 0.75 + 0.25 * sin(7.0 * s + h(j, 8.0) * 6.2832);
    float th = th0 + bend * (s - 0.5), a0 = th0 - 0.5 * bend;
    vec2 p = abs(bend) > 1e-4 ? vec2(sin(th) - sin(a0), cos(a0) - cos(th)) * (L / bend) : vec2(cos(th0), sin(th0)) * s * L;
    p -= 0.5 * L * vec2(cos(th0), sin(th0));
    // a scribble (a few): a sand grain dragged under the sheet - short, one smooth sweep of up to half a
    // circle, ending in a small curl; the path summed in 32 pieces, centred
    float sc = h(j, 9.0) < scrib.x ? scrib.y : 0.0;
    if (sc > 0.0) {
        L = 6.0 + 18.0 * h(j, 10.0);
        float K = sc * (1.2 + 1.9 * h(j, 11.0)) * (h(j, 12.0) < 0.5 ? -1.0 : 1.0);     // (the sweep, radians)
        float C = sc * (0.8 + 2.2 * h(j, 13.0)) * (h(j, 14.0) < 0.5 ? -1.0 : 1.0);     // (the end's curl)
        vec2 acc = vec2(0.0), tot = vec2(0.0);
        for (int i = 0; i < 32; i++) {
            float t = (float(i) + 0.5) / 32.0, ts = t * s;
            float ea = clamp((t - 0.7) / 0.3, 0.0, 1.0), eb = clamp((ts - 0.7) / 0.3, 0.0, 1.0);
            float ha = th0 + K * (t - 0.5) + C * ea * ea, hb = th0 + K * (ts - 0.5) + C * eb * eb;
            tot += vec2(cos(ha), sin(ha)); acc += vec2(cos(hb), sin(hb)) * s;
        }
        p = (acc - 0.5 * tot) * (L / 32.0);
        float es = clamp((s - 0.7) / 0.3, 0.0, 1.0);
        th = th0 + K * (s - 0.5) + C * es * es;
    }
    p += vec2(u * SW, v * SH) + off * vec2(-sin(th), cos(th));
    gl_Position = keep ? vec4(p / vec2(SW, SH) * 2.0 - 1.0, 0.0, 1.0) : vec4(-9.0, -9.0, 0.0, 1.0);
    float w = d * taper * wv / max(abs(cos(th)), abs(sin(th)));     // (an aliased line: per length, not per texel)
    float c2 = cos(2.0 * th), s2 = sin(2.0 * th);
    if (chan < 0.5) val = w * vec4(1.0, c2, s2, cos(4.0 * th));
    else if (chan < 1.5) val = vec4(w * sin(4.0 * th), 0.0, 0.0, 0.0);
    else if (chan < 2.5) val = vec4(0.0, w, w * c2, w * s2);
    else {                                  // (C: the 4t terms; z: the damage's cut through the colour, widest part)
        float cut = stepid > 6.5 ? pow(max(sin(PI * s), 0.0), 3.0) * d / 1.25 / max(abs(cos(th)), abs(sin(th))) : 0.0;
        val = vec4(w * cos(4.0 * th), w * sin(4.0 * th), cut, 0.0);
    }
}
"""
SCR_GEN_FS = b"""
varying vec4 val;
void main() { gl_FragColor = val; }
"""
# the light's angle harmonics (quarter size): a horizontal pass per part (u = x / sigma, g = exp(-u^2/2)):
# part 0 / 1: sigma 1 / 2 (g, u g, u^2 g, u^3 g); part 2: u^4 g of both
SCR_HARM_H = b"""
uniform sampler2D src;
uniform float part, s1, s2;
uniform vec2 px;
float lum(vec3 c) { return max(max(c.r, c.g), c.b); }
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec4 o = vec4(0.0);
    float sg = part < 0.5 ? s1 : s2;
    int n = int(ceil(5.0 * max(s1, s2)));
    for (int i = -64; i <= 64; i++) {
        if (i < -n || i > n) continue;
        float l = lum(texture2D(src, uv + vec2(float(i) * px.x, 0.0)).rgb);
        if (part < 1.5) {
            float u = float(i) / sg, g = exp(-0.5 * u * u) * l;
            o += g * vec4(1.0, u, u * u, u * u * u);
        } else {
            float u1 = float(i) / s1, u2 = float(i) / s2;
            o.x += exp(-0.5 * u1 * u1) * u1 * u1 * u1 * u1 * l;
            o.y += exp(-0.5 * u2 * u2) * u2 * u2 * u2 * u2 * l;
        }
    }
    gl_FragColor = o;
}
"""
# vertical (v = y / sigma, y down): part 0: (R, C2, S2, C4), part 1: (S4, glow) - R = sum w (r/s)^4 G * light,
# normalized (16 pi s^2); C / S n = the same with cos / sin n phi; glow = G of sigma 2 (2 pi s^2)
SCR_HARM_V = b"""
uniform sampler2D H1, H2, H3;
uniform float part, s1, s2, w1, w2;
uniform vec2 px;
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec4 o = vec4(0.0);
    int n = int(ceil(5.0 * max(s1, s2)));
    float n1 = w1 / (50.265 * s1 * s1), n2 = w2 / (50.265 * s2 * s2);
    for (int j = -64; j <= 64; j++) {
        if (j < -n || j > n) continue;
        vec2 q = uv + vec2(0.0, float(j) * px.y);
        vec4 a = texture2D(H1, q), b = texture2D(H2, q), c = texture2D(H3, q);
        for (int k = 0; k < 2; k++) {
            float sg = k == 0 ? s1 : s2, nn = k == 0 ? n1 : n2;
            vec4 h = k == 0 ? a : b; float h4 = k == 0 ? c.x : c.y;
            float v = -float(j) / sg, g = exp(-0.5 * v * v);
            float v1 = g * v, v2 = v1 * v, v3 = v2 * v, v4 = v3 * v;
            if (part < 0.5)
                o += nn * vec4(h4 * g + 2.0 * h.z * v2 + h.x * v4, h4 * g - h.x * v4,
                               2.0 * (h.w * v1 + h.y * v3), h4 * g - 6.0 * h.z * v2 + h.x * v4);
            else {
                o.x += nn * 4.0 * (h.w * v1 - h.y * v3);
                if (k == 1) o.y += h.x * g / (6.2832 * s2 * s2);
            }
        }
    }
    gl_FragColor = o;
}
"""
# the scratches on the screen: A = fine wear (sum d, d cos 2t, d sin 2t, d cos 4t), B = (d sin 4t, the steps:
# sum d, d cos 2t, d sin 2t), C = (the steps: d cos 4t, d sin 4t); S = the screen before the overlay, M / F its
# light blurred (~6 / ~11 px); V1 / V2 the light's angle harmonics (SCR_HARM_V)
SCR_SHADE = b"""
uniform sampler2D art, A, B, C, S, M, F, V1, V2;
uniform float pass, sf, ref, glint, room, scrk, scron, a2, a4, gk, glow;
uniform vec4 pen;                       // (the damage's cut: from, full, light through)
uniform vec2 size, qpx;
float lum(vec3 c) { return max(max(c.r, c.g), c.b); }
void main() {
    vec2 tc = gl_TexCoord[0].st;
    float su = (tc.x - 0.5 * (1.0 - sf)) / sf;
    if (su < 0.0 || su > 1.0) { gl_FragColor = vec4(1.0 - pass, 1.0 - pass, 1.0 - pass, 1.0); return; }
    vec4 o = texture2D(art, tc);
    float opq = step(0.996, o.a), filt = step(0.004, o.a) * (1.0 - opq);
    vec3 hue = o.rgb / max(max(max(o.r, o.g), o.b), 0.02);
    vec3 trans = mix(mix(vec3(1.0), hue, filt), vec3(0.0), opq);    // (the plastic's colour; the print: no light)
    vec3 own = mix(vec3(0.35), o.rgb, max(filt, opq));
    vec2 sv = vec2(su, tc.y);
    vec4 a = texture2D(A, sv) * scron, b = texture2D(B, sv) * scron, cc = texture2D(C, sv) * scron;
    float D = max(a.x, 0.0);
    float m = D > 0.0 ? (1.0 - exp(-1.5 * D / ref)) / (1.0 - exp(-1.5)) : 0.0;
    vec2 uv = gl_FragCoord.xy / size, px = 1.5 / size;
    vec3 near = (texture2D(S, uv).rgb * 2.0 + texture2D(S, uv + px).rgb + texture2D(S, uv - px).rgb
                 + texture2D(S, uv + vec2(px.x, -px.y)).rgb + texture2D(S, uv - vec2(px.x, -px.y)).rgb) / 6.0;
    vec3 mid = texture2D(M, uv).rgb, far = texture2D(F, uv).rgb;
    float k = scrk * m;
    if (pass < 0.5) {                                   // the light the matte scatters leaves the line itself
        gl_FragColor = vec4(vec3(1.0 - 0.25 * clamp(k, 0.0, 1.0) * (1.0 - opq)), 1.0);
        return;
    }
    vec3 halo = (0.6 * near + 0.8 * mid + 0.5 * far) * trans;
    vec3 c = halo * k * 0.8 * glint + room * 0.6 * m * own;
    vec4 h = texture2D(V1, uv), h2 = texture2D(V2, uv);
    vec3 lc = (far + 0.002) / (lum(far) + 0.002);        // (the light's colour; the harmonics are its brightness)
    float Rl = max(h.x, 0.0);
    // the glint: each scratch from all the light it lies across from (the lobe 1 - a2 cos 2x + a4 cos 4x)
    float Gf = D * Rl - a2 * (a.y * h.y + a.z * h.z) + a4 * (a.w * h.w + b.x * h2.x);
    float Gs = b.y * Rl - a2 * (b.z * h.y + b.w * h.z) + a4 * (cc.x * h.w + cc.y * h2.x);
    float df = D > 0.0 ? clamp(Gf / max(D * Rl, 1e-7), 0.0, 4.0) : 0.0;
    float ds = b.y > 0.0 ? clamp(Gs / max(b.y * Rl, 1e-7), 0.0, 4.0) : 0.0;
    float dd = clamp(b.y * 0.6, 0.0, 1.4);
    c += (m * df * df + 2.0 * dd * ds * ds) * Rl * gk * glint * lc * trans + dd * room * 0.9 * own;
    c += glow * max(h2.y, 0.0) * lc * trans;            // (the plastic's own glow)
    // the damage cut through the colour layer at its widest: the tube's light comes through untinted (white)
    float cut = smoothstep(pen.x, pen.y, cc.z);
    c += cut * pen.z * (0.5 * near + 0.8 * mid + 0.6 * far) * (vec3(1.0) - trans * 0.5) + cut * room * 0.5;   // (it scatters the light around it)
    gl_FragColor = vec4(c, 1.0);
}
"""

ART_SHADER = b"""
uniform sampler2D tex;
uniform float pass, tint, room, print_;
void main() {
    vec4 o = texture2D(tex, gl_TexCoord[0].st);
    float filt = step(0.004, o.a) * (1.0 - step(0.996, o.a)), opq = step(0.996, o.a);
    vec3 hue = o.rgb / max(max(max(o.r, o.g), o.b), 0.02);       // (the filter's colour at full strength)
    if (pass < 0.5) {
        vec3 m = mix(vec3(1.0), hue * 1.15, tint * filt) * (1.0 - opq);
        gl_FragColor = vec4(0.5 * m, 1.0);
    } else
        gl_FragColor = vec4(o.rgb * (filt * room + opq * print_), 1.0);
}
"""

# ------------------------------------------------------------------ mains hum wobble
# The owner's Vectrex (50 Hz mains, not recapped) wobbles: the transformer field and the supply
# ripple push the beam with the mains hum. The offset follows the mains phase at the emulated moment
# each line is drawn (frame start + cycles), so a game at exactly 50 or 25 fps stands still and every
# other frame rate wobbles at its beat with the mains (the owner's observation, 2026-10-08). The mains
# frequency itself wanders a little around 50 Hz, and the strength breathes slowly.
# On 60 Hz mains (USA, Canada, ...) the same hum runs at 60 Hz: against a 50 fps game it beats at
# 10 Hz, so the bend ripples through the picture instead of standing almost still (MAINS_HZ).
# (weight, harmonic of 50 Hz, phase) per axis; the strength is the rms offset in game pixels.
HUM_AXES = ([(1.0, 1, 0.3), (0.55, 2, 1.7), (0.25, 3, 4.1)],
            [(1.0, 2, 2.2), (0.7, 1, 0.9), (0.3, 3, 3.3)])
MAINS_HZ = [50, 60]     # the mains frequencies to choose from (Europe / USA)
WOBBLE_SPEED = 2.62     # how far the mains wanders around 50 Hz (the owner's choice, 2026-10-08)

def mains_cycles(t, speed, hz=50):
    """mains cycles elapsed at emulated time t (s): 50 (60) Hz plus a slow, irregular wander"""
    return hz * t + speed * (0.002 * t + 0.15 * math.sin(2 * math.pi * t / 61.0)
                               + 0.08 * math.sin(2 * math.pi * t / 23.0 + 1.0) + 0.04 * math.sin(2 * math.pi * t / 9.7 + 2.0))

def hum_coeffs(t, strength, speed, hz=50):
    """per axis (phase0, [(P_h, Q_h) for h 0..3]): offset = sum_h P_h sin(h a) + Q_h cos(h a) with
    a = phase0 + 2 pi hz * (cycles in frame) / 1.5e6 (emulator units)"""
    a0 = 2 * math.pi * mains_cycles(t, speed, hz)
    breathe = 0.75 + 0.25 * math.sin(2 * math.pi * 0.029 * t) * math.sin(2 * math.pi * 0.047 * t + 1.0)
    out = []
    for axis in HUM_AXES:
        norm = math.sqrt(sum(w * w for w, _, _ in axis) / 2.0)
        k = 160.0 * strength * breathe / norm
        P = [0.0] * 4; Q = [0.0] * 4
        for w, h, ph in axis:
            P[h] += k * w * math.cos(ph); Q[h] += k * w * math.sin(ph)
        out.append((P, Q))
    return a0, out

# brightness bending: the beam current loads the high voltage; when much is lit it sags, slower
# electrons deflect more and the picture grows (or shifts); the supply smooths it over ~60 ms.
BEND_TYPES = ['grow', 'shift', 'grow+shift']

# ------------------------------------------------------------------ phosphor in real time
# The screen is shown at a fixed rate (60 Hz), whatever the game's frame rate. Each beam line lights
# the phosphor at the moment it was drawn (the frame's real start + its cycle in the frame) to its
# commanded brightness. A spot drawn again is lit again - it does not add up - so an object is
# equally bright standing still or moving; only where it was, its afterglow fades. Only what the beam
# draws lights up: a fast object at a low frame rate leaves separate fading copies, a slow one
# (copies overlapping) an almost smooth trail - as on the tube.
# The afterglow is measured on the owner's Vectrex (phosphor.MOV, 2026-10-09: the title text going
# dark when MINE STORM switches to PLAYER 1, two text rows, frame times from the video, Apple video
# gamma 1.96): after the beam stops, the text is at ~15 % of its lit brightness, 4 % at 0.15 s,
# 1.5 % at 0.5 s, 0.1 % at 1.5 s - two decays, fitted within 8 % from 17 ms to 1.8 s:
#     A(t) = 0.1404 exp(-t / 45 ms) + 0.0582 exp(-t / 350 ms)
# A spot holds its full brightness for one game frame (PHOS_HOLD: the eye sees the beam's flash and
# the next frame's redraw as one), then follows A. F7 / F8 stretch the time axis (1 = measured).
# The phosphor image keeps per pixel the brightness it was lit to and its freshness e^-age (alpha).
PHOS_GAIN = 1.0 / (1.0 - 0.40)  # a still picture as bright as with the earlier model (afterglow 0.40)
PHOS_HOLD = 0.021               # a lit spot is at full brightness at least this long (a 50 fps frame;
                                #  longer for a slower game: see hold())
PHOS_HOLD_OFF = 0.021           # trail off: a spot holds until the game draws it again, then is gone
PHOS_A = ((0.1404, 0.0454), (0.0582, 0.350))   # the measured afterglow (share, time constant s)
# the afterglow joins up: a fading spot (older than the hold) spreads to its neighbours each display frame,
# keeping PHOS_GROW of its brightness, so the copies a moving object leaves join into one smooth trail -
# the owner (2026-10-14): Mine Storm's big mothership leaves "a singular trail, not a bunch of snapshots";
# sharp, not blurred (PHOS_SPREAD). Light brighter than PHOS_GCAP (corner spots) spreads only as a line
# does. One step per display frame of 1 px in an 820 px high window at 60 Hz (scaled). Tube > Afterglow joining
PHOS_GCAP = 1.5
# the afterglow's spots overlap: the fading light (not the lit lines) is averaged over a spot PHOS_SMOOTH px
# wide (in an 820 px high window, scaled), so the copies' steps and the wing tips' corner dots blend into a
# smooth gradient, as overlapping light does on the tube (Tube > Afterglow overlap)
PHOS_SMOOTH = 0.8               # (the owner's choice side by side with the tube, 2026-10-14)
PHOS_GAGE = 0.035               # the afterglow grows only this long after its hold (2 display frames: it joins
                                #  the copies but stays the vector's size - a bullet's glow grew into a splotch)
PHOS_GROW = 0.5                 # (the owner's choice side by side with the tube, 2026-10-14)
PHOS_SPREAD = 0.0               # how much the afterglow diffuses per display frame (0 = crisp copies, the earlier
                                #  look: a fast object left a ripple of sharp outlines; the owner, 2026-10-14: the
                                #  tube's trail is smooth - Mine Storm's big mothership; no longer in the menu: no visible
                                #  effect, the glow fades first - Afterglow overlap smooths it)

PHOS_FUNC = b"""
uniform float hold, tscale, toff;
float bright(float f) {                     // a spot's brightness by its freshness f = e^-age
    if (f <= 0.0) return 0.0;
    float age = -log(f);
    if (age < hold) return 1.0;
    if (toff > 0.5) return 0.0;
    float t = age / tscale;
    return %.4f * exp(-t / %.4f) + %.4f * exp(-t / %.4f);
}
float lum(vec3 c) { return dot(c, vec3(0.3, 0.5, 0.2)); }
""" % (PHOS_A[0][0], PHOS_A[0][1], PHOS_A[1][0], PHOS_A[1][1])
# the phosphor image, one display frame on: the old light ages (kf = e^-dt), the power-off curve
# dims it (mul); where the fresh light is brighter than what is still glowing, the spot is lit again
UPDATE_SHADER = b"""
uniform sampler2D oldt, fresh;
uniform float kf, mul, spread, grow, gcap, gn, gage;
uniform vec2 px;
""" + PHOS_FUNC + b"""
vec4 grown(vec4 o, vec4 n, float g, float hf, inout float bb) {  // a fading neighbour spreads its light here
    if (n.a > 0.0 && n.a < hf && n.a > hf * gage) {   // (only young glow spreads: it stays the vector's size)
        float l = min(lum(n.rgb), gcap);                // (capped: bright corners spread like lines)
        float v = l * bright(n.a) * g;
        if (v > bb) { bb = v; return vec4(n.rgb * (l / max(lum(n.rgb), 1e-4)) * g, n.a); }
    }
    return o;
}
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec4 o = texture2D(oldt, uv);
    vec4 n1 = texture2D(oldt, uv + vec2(px.x, 0.0)), n2 = texture2D(oldt, uv - vec2(px.x, 0.0));
    vec4 n3 = texture2D(oldt, uv + vec2(0.0, px.y)), n4 = texture2D(oldt, uv - vec2(0.0, px.y));
    if (spread > 0.0)                                   // the afterglow diffuses as it ages (PHOS_SPREAD)
        o = mix(o, 0.25 * (n1 + n2 + n3 + n4), spread);
    if (grow > 0.0) {                                   // the fading glow widens (PHOS_GROW): only light
        float hf = exp(-hold), bb = lum(o.rgb) * bright(o.a);   //  older than the hold spreads, gn pixels
        for (int i = 1; i <= 4; i++) {                  //  per display frame (the window's size and rate)
            if (float(i) > gn) break;
            float g = pow(grow, float(i)); vec2 d = px * float(i);
            o = grown(o, texture2D(oldt, uv + vec2(d.x, 0.0)), g, hf, bb);
            o = grown(o, texture2D(oldt, uv - vec2(d.x, 0.0)), g, hf, bb);
            o = grown(o, texture2D(oldt, uv + vec2(0.0, d.y)), g, hf, bb);
            o = grown(o, texture2D(oldt, uv - vec2(0.0, d.y)), g, hf, bb);
        }
    }
    o.a *= kf; o.rgb *= mul;
    vec4 f = texture2D(fresh, uv);
    if (o.a < 0.002) o = vec4(0.0);                     // (6 s old: gone)
    gl_FragColor = lum(f.rgb) * bright(f.a) > lum(o.rgb) * bright(o.a) ? f : o;
}
"""
# what the phosphor image shows now (alpha: the bloom weight, as the earlier model)
SHOW_SHADER = b"""
uniform sampler2D tex;
uniform float sm;
uniform vec2 px;
""" + PHOS_FUNC + b"""
vec3 glow(vec2 uv) {                        // a pixel's afterglow (the lit part - younger than the hold - not)
    vec4 p = texture2D(tex, uv);
    if (p.a <= 0.0 || -log(p.a) < hold) return vec3(0.0);
    return p.rgb * bright(p.a);
}
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec4 p = texture2D(tex, uv);
    vec3 c = p.rgb * bright(p.a);
    if (sm > 0.0) {                         // the afterglow's spots overlap (PHOS_SMOOTH): it is averaged over
        vec2 d = px * sm;                   //  a spot about the copies' spacing; the lit lines stay sharp
        vec3 g = 0.25 * glow(uv)
               + 0.125 * (glow(uv + vec2(d.x, 0.0)) + glow(uv - vec2(d.x, 0.0)) + glow(uv + vec2(0.0, d.y)) + glow(uv - vec2(0.0, d.y)))
               + 0.0625 * (glow(uv + d) + glow(uv - d) + glow(uv + vec2(d.x, -d.y)) + glow(uv + vec2(-d.x, d.y)));
        vec3 lit = (p.a > 0.0 && -log(p.a) < hold) ? p.rgb : vec3(0.0);
        c = max(lit, g);
    }
    gl_FragColor = vec4(c, 0.7);
}
"""

# a small stroke font for the frame rate counter (4 x 6 grid, y down)
_A, _B, _C, _D = (0, 0, 4, 0), (4, 0, 4, 3), (4, 3, 4, 6), (0, 6, 4, 6)
_E, _F, _G = (0, 3, 0, 6), (0, 0, 0, 3), (0, 3, 4, 3)
HUD_FONT = {'0': [_A, _B, _C, _D, _E, _F], '1': [(2, 0, 2, 6), (1, 1, 2, 0)], '2': [_A, _B, _G, _E, _D], '3': [_A, _B, _G, _C, _D],
            '4': [_F, _G, _B, _C], '5': [_A, _F, _G, _C, _D], '6': [_A, _F, _G, _E, _D, _C], '7': [_A, _B, _C],
            '8': [_A, _B, _C, _D, _E, _F, _G], '9': [_A, _B, _C, _D, _F, _G], 'F': [_A, _F, _E, (0, 3, 3, 3)],
            'P': [_A, _B, _G, _F, _E], 'S': [_A, _F, _G, _C, _D], 'H': [_F, _E, _B, _C, _G],
            'Z': [_A, (4, 0, 0, 6), _D], 'V': [(0, 0, 2, 6), (2, 6, 4, 0)], 'R': [_A, _B, _G, _F, _E, (2, 3, 4, 6)],
            '%': [(0, 6, 4, 0), (0, 0, 1, 0), (1, 0, 1, 1), (1, 1, 0, 1), (0, 1, 0, 0),
                  (3, 5, 4, 5), (4, 5, 4, 6), (4, 6, 3, 6), (3, 6, 3, 5)], '-': [_G], 'O': [_A, _B, _C, _D, _E, _F],
            'L': [_F, _E, _D], 'N': [_E, _F, (0, 0, 4, 6), _B, _C], 'E': [_A, _F, _E, _D, (0, 3, 3, 3)],
            'T': [_A, (2, 0, 2, 6)], 'U': [_F, _E, _D, _B, _C], 'M': [_F, _E, _B, _C, (0, 0, 2, 3), (2, 3, 4, 0)],
            '/': [(0, 6, 4, 0)], 'G': [_A, _F, _E, _D, _C, (2, 3, 4, 3)], 'C': [_A, _F, _E, _D],
            'Y': [(0, 0, 2, 3), (4, 0, 2, 3), (2, 3, 2, 6)], 'A': [_A, _B, _C, _E, _F, _G],
            'B': [_F, _E, _D, (0, 0, 3, 0), (3, 0, 4, 1), (4, 1, 4, 2), (4, 2, 3, 3), (0, 3, 3, 3), (3, 3, 4, 4),
                  (4, 4, 4, 5), (4, 5, 3, 6)],
            'D': [(0, 0, 3, 0), (3, 0, 4, 1.5), (4, 1.5, 4, 4.5), (4, 4.5, 3, 6), (3, 6, 0, 6), _F, _E],
            'I': [(1, 0, 3, 0), (2, 0, 2, 6), (1, 6, 3, 6)], 'J': [_B, _C, _D, (0, 6, 0, 4)],
            'K': [_F, _E, (4, 0, 0, 3), (0, 3, 4, 6)], 'Q': [_A, _B, _C, _D, _E, _F, (2, 4, 4, 6)],
            'W': [_F, _E, (0, 6, 2, 3), (2, 3, 4, 6), _B, _C], 'X': [(0, 0, 4, 6), (4, 0, 0, 6)],
            '.': [(2, 5.5, 2, 6)], ':': [(2, 1.7, 2, 2.2), (2, 4.8, 2, 5.3)], ',': [(2, 5, 1.5, 6.5)],
            '(': [(3, 0, 2, 1.5), (2, 1.5, 2, 4.5), (2, 4.5, 3, 6)], ')': [(1, 0, 2, 1.5), (2, 1.5, 2, 4.5), (2, 4.5, 1, 6)],
            '+': [(2, 1.5, 2, 4.5), (0.5, 3, 3.5, 3)], '!': [(2, 0, 2, 4), (2, 5.5, 2, 6)],
            "'": [(2, 0, 2, 1.5)], '&': [(4, 6, 0, 1.5), (0, 1.5, 1, 0), (1, 0, 2, 1), (2, 1, 0, 4.5), (0, 4.5, 1.5, 6),
                  (1.5, 6, 4, 3.5)], '#': [(1, 0, 1, 6), (3, 0, 3, 6), (0, 2, 4, 2), (0, 4, 4, 4)],
            '_': [(0, 6, 4, 6)], '>': [(0, 0, 4, 3), (4, 3, 0, 6)], '<': [(4, 0, 0, 3), (0, 3, 4, 6)],
            '=': [(0, 2, 4, 2), (0, 4, 4, 4)], '?': [_A, _B, (4, 3, 2, 3), (2, 3, 2, 4.5), (2, 5.5, 2, 6)], ' ': []}

# the volume knob and the RESET button (menu: Volume knob), drawn like the Vectrex's own panel: a
# knob with a position marker, OFF/ON above it, the volume wedge and VOLUME to its right; the RESET
# button to its left. Sizes in units of the knob's radius (r); the layout is shared with the mouse
# handling (vectrex_emu.App.knob_geom).
KNOB_R = 0.0253         # knob radius / picture height (the owner: a third of the real knob's size, +10 %)
# the cartridge pictogram, to the proportions of the owner's cartridge model (Vectrex_cart.step: about
# 71 x 71 mm, square): the slightly narrower insert end with double edge lines and the arrow pointing to
# it, the insert line where it ends, a short slanted step out to the body, the large label with chamfered
# corners and the Vectrex logo on it, and the grip ridges at the far end of the sides. 100 x 100 grid,
# y down.
LOGO_ICON = [  # the Vectrex logo (the owner's vectrex-us-soldermask.kicad_mod), outlined, on the label
    [(62.8, 68.4), (63.7, 68.4), (63.5, 69.2), (62.8, 69.9), (62.1, 70.7), (61.5, 71.3), (61.5, 72.0), (62.1, 72.0), (62.7, 71.5), (63.4, 70.7), (63.9, 70.2), (65.7, 68.4), (67.5, 68.4), (68.5, 68.4), (69.2, 68.4), (69.0, 69.0), (68.3, 69.7), (67.8, 70.3), (66.3, 72.1), (63.7, 72.2), (63.7, 79.1), (60.3, 79.1), (60.3, 68.4), (62.2, 68.4), (62.8, 68.4)],
    [(57.8, 62.0), (57.8, 65.0), (60.5, 65.0), (63.2, 65.0), (59.8, 68.5), (58.8, 68.5), (57.8, 68.5), (57.8, 79.1), (54.4, 79.1), (54.4, 68.9), (55.4, 67.9), (56.1, 67.1), (56.5, 66.6), (56.0, 66.1), (55.3, 66.5), (54.6, 67.2), (53.1, 68.5), (51.4, 68.5), (50.4, 68.5), (49.7, 68.5), (50.2, 67.6), (51.0, 66.8), (51.8, 65.9), (52.4, 65.2), (53.0, 64.5), (53.6, 63.8), (54.2, 63.1), (54.8, 62.4), (55.3, 61.8), (55.9, 61.2), (56.3, 60.7), (56.7, 60.2), (57.3, 59.5), (57.8, 59.0), (57.8, 62.0), (57.8, 62.0)],
    [(48.9, 68.5), (49.7, 68.7), (50.5, 69.1), (51.2, 69.6), (50.3, 71.3), (49.0, 72.7), (48.4, 72.3), (47.8, 72.2), (46.9, 72.4), (46.4, 72.9), (46.2, 73.6), (46.2, 74.3), (46.8, 75.0), (47.7, 75.3), (48.7, 75.1), (49.3, 74.3), (49.5, 73.6), (50.2, 72.9), (50.8, 72.3), (51.6, 71.6), (52.1, 71.1), (52.7, 71.5), (52.9, 72.3), (53.1, 73.4), (53.1, 74.2), (53.0, 75.1), (52.7, 75.9), (52.3, 76.7), (51.7, 77.4), (50.7, 78.3), (49.9, 78.7), (49.0, 79.0), (48.1, 79.2), (47.2, 79.1), (46.2, 78.9), (45.4, 78.6), (44.6, 78.1), (43.9, 77.5), (43.3, 76.9), (42.9, 76.1), (42.6, 75.2), (42.4, 74.6), (42.4, 73.4), (42.5, 72.4), (42.8, 71.6), (43.3, 70.8), (43.8, 70.1), (44.5, 69.5), (45.3, 69.0), (46.1, 68.6), (47.0, 68.4), (48.0, 68.4), (48.9, 68.5), (48.9, 68.5)],
    [(37.4, 68.5), (38.1, 68.7), (38.9, 69.1), (39.6, 69.6), (40.4, 70.3), (37.9, 72.7), (37.3, 73.3), (36.8, 73.9), (36.1, 74.6), (35.6, 75.2), (36.7, 75.3), (37.5, 74.7), (37.8, 74.1), (38.2, 73.5), (38.8, 72.8), (39.2, 72.4), (39.7, 71.9), (40.4, 71.3), (41.0, 71.1), (41.2, 71.7), (41.4, 72.3), (41.6, 73.4), (41.6, 74.5), (41.2, 75.9), (40.7, 76.7), (40.2, 77.4), (39.5, 78.0), (38.8, 78.5), (38.1, 78.8), (37.4, 79.0), (36.7, 79.1), (36.1, 79.2), (34.6, 78.9), (33.8, 78.6), (33.0, 78.1), (32.5, 77.6), (32.0, 77.0), (31.6, 76.5), (31.3, 75.9), (31.0, 75.2), (30.9, 74.4), (30.9, 73.8), (34.6, 73.8), (35.5, 73.6), (36.1, 73.0), (36.6, 72.5), (35.7, 72.3), (35.0, 72.8), (34.6, 73.7), (30.9, 73.8), (30.9, 72.9), (31.0, 72.2), (31.4, 71.4), (31.7, 70.8), (32.1, 70.3), (33.2, 69.3), (33.9, 68.9), (34.8, 68.6), (35.6, 68.4), (36.5, 68.4), (37.4, 68.5), (37.4, 68.5)],
    [(20.6, 64.3), (21.7, 64.3), (22.9, 64.3), (23.9, 64.3), (24.9, 64.3), (25.8, 64.3), (26.6, 64.3), (27.2, 64.3), (28.3, 64.3), (27.9, 65.2), (27.0, 66.1), (26.5, 66.6), (25.9, 67.2), (25.3, 67.9), (24.4, 68.8), (23.8, 69.4), (23.2, 70.0), (22.7, 70.5), (22.3, 71.0), (21.6, 71.7), (21.2, 72.4), (21.7, 72.9), (22.3, 72.6), (23.0, 72.0), (23.4, 71.5), (24.0, 70.9), (24.6, 70.3), (25.4, 69.6), (26.1, 68.8), (30.6, 64.3), (33.2, 64.3), (33.9, 64.3), (35.0, 64.3), (35.7, 64.3), (35.2, 65.2), (34.8, 65.7), (34.3, 66.2), (33.8, 66.8), (33.2, 67.5), (32.6, 68.2), (32.0, 68.9), (31.3, 69.7), (30.6, 70.5), (29.9, 71.3), (29.2, 72.2), (28.4, 73.0), (27.7, 73.8), (27.1, 74.6), (26.4, 75.3), (25.8, 76.0), (25.2, 76.7), (24.7, 77.3), (24.2, 77.8), (23.8, 78.3), (23.2, 78.9), (22.5, 78.6), (21.8, 77.8), (21.3, 77.2), (20.7, 76.6), (20.1, 75.9), (19.4, 75.1), (18.7, 74.3), (17.9, 73.4), (17.1, 72.5), (16.5, 71.8), (15.7, 70.8), (14.9, 69.9), (14.1, 69.1), (13.4, 68.3), (12.7, 67.5), (12.1, 66.8), (11.6, 66.2), (11.1, 65.6), (10.7, 65.2), (10.2, 64.5), (11.2, 64.3), (11.8, 64.3), (12.6, 64.3), (13.5, 64.3), (14.5, 64.3), (15.6, 64.3), (16.8, 64.3), (18.1, 64.3), (19.4, 64.3), (20.6, 64.3), (20.6, 64.3)],
    [(73.8, 68.4), (74.6, 68.6), (75.4, 69.0), (76.2, 69.5), (76.7, 69.9), (76.5, 70.8), (75.8, 71.5), (75.3, 72.0), (74.9, 72.5), (74.0, 73.4), (73.5, 73.9), (72.8, 74.6), (72.3, 75.2), (72.9, 75.3), (73.8, 75.1), (74.3, 74.5), (74.5, 73.9), (75.1, 73.1), (75.9, 72.3), (76.7, 71.6), (77.2, 71.1), (77.8, 71.6), (78.1, 72.1), (78.2, 72.9), (78.2, 73.7), (78.2, 74.6), (78.1, 75.4), (77.7, 76.1), (77.4, 76.7), (77.0, 77.3), (76.0, 78.2), (75.2, 78.6), (74.4, 79.0), (73.5, 79.1), (72.6, 79.2), (71.3, 78.9), (70.5, 78.6), (69.7, 78.1), (69.1, 77.5), (68.5, 76.9), (68.0, 76.1), (67.7, 75.3), (67.6, 74.6), (67.5, 73.9), (71.3, 73.9), (72.2, 73.6), (72.8, 73.0), (73.3, 72.5), (72.4, 72.3), (71.6, 72.8), (71.3, 73.4), (67.5, 73.9), (67.6, 73.0), (67.9, 71.7), (68.3, 71.0), (68.7, 70.4), (69.6, 69.5), (70.4, 69.0), (71.2, 68.6), (72.1, 68.4), (72.9, 68.4), (73.8, 68.4), (73.8, 68.4)],
    [(88.7, 68.4), (89.6, 68.4), (89.3, 69.3), (88.5, 70.3), (88.0, 70.8), (85.5, 73.7), (86.2, 74.4), (86.8, 75.2), (87.6, 76.1), (88.0, 76.6), (88.4, 77.1), (88.8, 77.6), (89.5, 78.4), (89.9, 78.9), (89.3, 79.0), (88.2, 79.0), (87.6, 79.0), (86.8, 79.0), (86.1, 79.0), (81.6, 79.0), (83.1, 77.5), (83.9, 76.7), (84.4, 76.1), (84.4, 75.5), (83.7, 75.6), (83.1, 76.2), (82.1, 77.1), (80.2, 79.1), (78.3, 79.1), (77.7, 79.1), (76.8, 79.0), (77.0, 78.4), (77.7, 77.6), (78.1, 77.1), (78.6, 76.6), (79.2, 75.8), (79.7, 75.3), (80.1, 74.8), (80.6, 74.1), (80.7, 73.5), (80.2, 72.8), (79.4, 71.9), (79.0, 71.4), (78.2, 70.5), (77.8, 70.0), (77.4, 69.5), (76.8, 68.8), (77.6, 68.4), (78.9, 68.4), (79.6, 68.4), (80.4, 68.4), (81.7, 68.4), (82.5, 68.4), (83.3, 68.4), (84.3, 68.4), (84.0, 69.3), (83.4, 70.0), (82.9, 70.5), (82.3, 71.2), (82.0, 71.7), (82.6, 72.1), (83.3, 71.5), (84.1, 70.7), (86.4, 68.4), (88.2, 68.4), (88.7, 68.4)],
]
_TICKS = [[(0, v), (2, v)] for v in (74, 78, 82, 86, 90)] + [[(100, v), (98, v)] for v in (74, 78, 82, 86, 90)]
CART_ICON = [[(3, 0), (97, 0), (97, 37), (100, 40), (100, 99), (99, 100), (1, 100), (0, 99), (0, 40), (3, 37), (3, 0)],
             [(6, 0), (6, 36.5)], [(94, 0), (94, 36.5)], [(3, 36.5), (97, 36.5)],
             [(50, 6), (60, 14), (55, 14), (55, 20), (45, 20), (45, 14), (40, 14), (50, 6)],
             [(9, 44), (91, 44), (94, 47), (94, 93), (91, 96), (9, 96), (6, 93), (6, 47), (9, 44)]] + _TICKS + LOGO_ICON
# the load button: the slot (its opening as wide as the cartridge's insert end), an arrow into it, the
# cartridge 60 below - drawn turned 90 degrees left (the slot on the left, as on the Vectrex's side)
LOAD_ICON = [[(-2, 0), (102, 0), (102, 18), (97, 18), (97, 22), (3, 22), (3, 18), (-2, 18), (-2, 0)],
             [(50, 52), (50, 30)], [(43, 37), (50, 30), (57, 37)]] +             [[(u, v + 60) for u, v in line] for line in CART_ICON]
OSD_SCALE = 0.3         # the window OSD's text: 30 % of what would fill the strip (the owner)
KNOB_SWEEP = 150.0      # degrees either side of 12 o'clock: 0 % to 100 %
KNOB_STRIP = 3.7 * KNOB_R   # the strip below the picture: a little black space, the labels, the knob, black space

class GLBloomRenderer(GLRenderer):
    """Phosphor and bloom on the graphics card. The picture lives in a phosphor image (16-bit float,
    see PHOS_A): every spot is lit by the beam and fades as measured on a real Vectrex. A quarter-size
    copy is blurred (separable Gaussian shader, `passes` times) and added on top with `bloom` strength.
    B switches phosphor + bloom off (crisp lines), F5/F6 bloom -/+, F7/F8 trail -/+, T trail on/off."""
    def __init__(self, hwnd, widget=None):
        super().__init__(hwnd, widget)
        gl = self.gl
        proc = self._proc
        u, i, p_ = ctypes.c_uint, ctypes.c_int, ctypes.c_void_p
        self.GenFB = proc('glGenFramebuffers', None, i, ctypes.POINTER(u))
        self.BindFB = proc('glBindFramebuffer', None, u, u)
        self.FBTex = proc('glFramebufferTexture2D', None, u, u, u, u, i)
        self.FBStatus = proc('glCheckFramebufferStatus', u, u)
        self.DelFB = proc('glDeleteFramebuffers', None, i, ctypes.POINTER(u))
        CreateShader = proc('glCreateShader', u, u)
        ShaderSource = proc('glShaderSource', None, u, i, ctypes.POINTER(ctypes.c_char_p), p_)
        CompileShader = proc('glCompileShader', None, u)
        GetShaderiv = proc('glGetShaderiv', None, u, u, ctypes.POINTER(i))
        GetShaderLog = proc('glGetShaderInfoLog', None, u, i, p_, ctypes.c_char_p)
        CreateProgram = proc('glCreateProgram', u)
        AttachShader = proc('glAttachShader', None, u, u)
        LinkProgram = proc('glLinkProgram', None, u)
        GetProgramiv = proc('glGetProgramiv', None, u, u, ctypes.POINTER(i))
        self.UseProgram = proc('glUseProgram', None, u)
        GetUniform = proc('glGetUniformLocation', i, u, ctypes.c_char_p)
        self.Uniform1i = proc('glUniform1i', None, i, i)
        self.Uniform2f = proc('glUniform2f', None, i, ctypes.c_float, ctypes.c_float)
        self.Uniform4f = proc('glUniform4f', None, i, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float)
        gl.glTexImage2D.argtypes = [u, i, i, i, i, i, u, u, p_]
        gl.glTexParameteri.argtypes = [u, u, i]
        gl.glBindTexture.argtypes = [u, u]
        gl.glGenTextures.argtypes = [i, ctypes.POINTER(u)]
        gl.glDeleteTextures.argtypes = [i, ctypes.POINTER(u)]
        gl.glTexCoord2f.argtypes = [ctypes.c_float] * 2
        gl.glVertex2f.argtypes = [ctypes.c_float] * 2
        self.Uniform1f = proc('glUniform1f', None, i, ctypes.c_float)
        gl.glTexCoordPointer.argtypes = [ctypes.c_int, ctypes.c_uint, ctypes.c_int, ctypes.c_void_p]
        def program(source, what):
            sh = CreateShader(GL_FRAGMENT_SHADER)
            src = ctypes.c_char_p(source)
            ShaderSource(sh, 1, ctypes.byref(src), None)
            CompileShader(sh)
            ok = i(0); GetShaderiv(sh, GL_COMPILE_STATUS, ctypes.byref(ok))
            if not ok.value:
                log = ctypes.create_string_buffer(2000); GetShaderLog(sh, 2000, None, log)
                raise OSError(what + ' shader: ' + log.value.decode(errors='replace'))
            pr = CreateProgram()
            AttachShader(pr, sh); LinkProgram(pr)
            GetProgramiv(pr, GL_LINK_STATUS, ctypes.byref(ok))
            if not ok.value:
                raise OSError(what + ' shader link failed')
            return pr
        def program_vf(vsrc, fsrc, what):
            prs = []
            for kind, src_ in ((0x8B31, vsrc), (GL_FRAGMENT_SHADER, fsrc)):      # (GL_VERTEX_SHADER)
                sh = CreateShader(kind); s_ = ctypes.c_char_p(src_)
                ShaderSource(sh, 1, ctypes.byref(s_), None); CompileShader(sh)
                ok = i(0); GetShaderiv(sh, GL_COMPILE_STATUS, ctypes.byref(ok))
                if not ok.value:
                    log = ctypes.create_string_buffer(2000); GetShaderLog(sh, 2000, None, log)
                    raise OSError(what + ' shader: ' + log.value.decode(errors='replace'))
                prs.append(sh)
            pr = CreateProgram()
            for sh in prs: AttachShader(pr, sh)
            LinkProgram(pr)
            ok = i(0); GetProgramiv(pr, GL_LINK_STATUS, ctypes.byref(ok))
            if not ok.value:
                raise OSError(what + ' shader link failed')
            return pr
        self.prog = program(BLUR_SHADER, 'blur')
        self.u_tex = GetUniform(self.prog, b'tex')
        self.u_dir = GetUniform(self.prog, b'dir')
        self.cprog = program(CAPSULE_SHADER, 'capsule')
        self.u_premul = GetUniform(self.cprog, b'premul')
        self.u_t = {k: GetUniform(self.cprog, k.encode()) for k in ('timed', 'age0', 'dtw', 'gain')}
        self.ClientActiveTexture = proc('glClientActiveTexture', None, u)
        self.ActiveTexture = proc('glActiveTexture', None, u)
        self.BlendEqSep = proc('glBlendEquationSeparate', None, u, u)
        self.BlendFuncSep = proc('glBlendFuncSeparate', None, u, u, u, u)
        self.uprog = program(UPDATE_SHADER, 'phosphor update')
        self.u_up = {k: GetUniform(self.uprog, k.encode()) for k in ('oldt', 'fresh', 'kf', 'mul', 'hold', 'tscale', 'toff', 'spread', 'px', 'grow', 'gcap', 'gn', 'gage')}
        self.sprog = program(SHOW_SHADER, 'phosphor show')
        self.u_stex = GetUniform(self.sprog, b'tex')
        self.u_sh = {k: GetUniform(self.sprog, k.encode()) for k in ('hold', 'tscale', 'toff', 'sm', 'px')}
        try:                                                    # (max blending: overlaps do not add up)
            self.BlendEq = proc('glBlendEquation', None, u)
        except OSError:
            self.BlendEq = None
        self.kprog = program(CURVE_SHADER, 'curve')
        self.u_k = {k: GetUniform(self.kprog, k.encode()) for k in ('tex', 'norm', 'k', 'ctr')}
        self.tprog = program(TONE_SHADER, 'tone')
        self.u_ttex = GetUniform(self.tprog, b'tex')
        self.aprog = program(ART_SHADER, 'overlay')
        self.u_a = {k: GetUniform(self.aprog, k.encode()) for k in ('tex', 'pass', 'tint', 'room', 'print_')}
        try:                                                    # (the overlay's scratches; without them if the
            self.gprog = program_vf(SCR_GEN_VS, SCR_GEN_FS, 'scratch maker')    #  card can not)
            self.u_g = {k: GetUniform(self.gprog, k.encode()) for k in
                        ('batch', 'limit', 'seed', 'stepid', 'mode', 'place', 'L0', 'L1', 'd0', 'd1', 'wv', 'off', 'chan', 'dmax', 'scrib', 'llen')}
            self.rprog = program(SCR_SHADE, 'scratches')
            self.u_r = {k: GetUniform(self.rprog, k.encode()) for k in
                        ('art', 'A', 'B', 'C', 'S', 'M', 'F', 'V1', 'V2', 'pass', 'sf', 'ref', 'glint', 'room', 'scrk',
                         'scron', 'a2', 'a4', 'gk', 'glow', 'pen', 'size', 'qpx')}
            self.hprog = program(SCR_HARM_H, 'glint (horizontal)')
            self.u_h = {k: GetUniform(self.hprog, k.encode()) for k in ('src', 'part', 's1', 's2', 'px')}
            self.vprog = program(SCR_HARM_V, 'glint (vertical)')
            self.u_v = {k: GetUniform(self.vprog, k.encode()) for k in ('H1', 'H2', 'H3', 'part', 's1', 's2', 'w1', 'w2', 'px')}
        except OSError as e:
            print('overlay scratches unavailable: %s' % e); self.gprog = None
        self.scr_on = True           # Overlay > Scratches (on by default: the owner)
        self.scr_wear = 1.0          # the fine wear and the smaller steps: their number and depth (Overlay > Wear)
        self.scr_fine = 1.0          # Overlay > Wear type: the fine scratches (steps 1-3), number (depth x sqrt)
        self.scr_medium = 1.0        #  the medium ones (steps 4-7)
        self.scr_large = 0.1         #  the damage (steps 8-10): little by default (the owner: most people want none), up to x2
        self.scr_glint = 1.0         # how strongly they catch the light (Overlay > Glint strength)
        self.scr_scrib = 1.0         # the share of curly scratches (Overlay > Scribbles)
        self.scr_width = 1.0         # how far round from square to the light they still glint (fixed: |sin|^4)
        self.scr_reach = 1.0         # how far from the lines they glint (Overlay > Glint reach: the far kernel's share)
        self.art_glow = True         # the plastic spreads the light softly around bright lines (Overlay > Plastic glow)
        self.art_glow_k = 1.0        # how strongly (Overlay > Plastic glow strength; x1 = ART_GLOW_K)
        self._lobe = (None, None)
        self.scr_seed = 1            # New random scratches (each overlay its own variation of it)
        self.scr_tex = None; self.scr_key = None; self.scr_made_t = 0.0
        self.art = None              # the overlay: (texture, width, height) - see set_art
        self.art_on = True           # Overlay > Overlay
        self.art_tint = 1.0          # how strongly the filters colour the beam (Overlay > Tint strength)
        self.art_room = 0.0648       # the room's light on the filters (Overlay > Room light): an evening room (the owner's choice)
        self.art_print = 0.36        # the printed parts' brightness (Overlay > Printed art brightness, the owner's choice)
        self.art_parallax = True     # the sheet a little in front of the tube: from the usual viewing height the
                                     #  picture sits a little higher on it (MiSTer's "Offset: Compensated")
        self.art_dx = 0.0; self.art_dy = 0.0; self.art_scale = 1.0   # (Overlay > Adjust position and size)
        self.tex = []; self.fbs = []
        self.trail_on = True         # the phosphor trail (T)
        self.trail = 0.6             # the afterglow's time scale: 1 = as measured on a real Vectrex (F7 / F8); 0.6 the
                                     #  owner's choice side by side with the tube (2026-10-14)
        self.bloom = 0.55            # bloom strength (the owner's choice, 2026-10-08)
        self.wob_on = True           # mains hum wobble (see HUM_AXES)
        self.wob_strength = 0.1074   # rms offset, game px (the owner's choice, 2026-10-08)
        self.line_gap = 12.3         # the integrators' stop delay in cycles (the core's; Tube > Line gaps): the gaps at
                                     #  line ends and corners - 12.3 fitted to the owner's test cart photos as a
                                     #  well-tuned Vectrex; ~14-15 his wide-gap machine (Malban: 15)
        self.mains_hz = 50           # the mains frequency of the hum (MAINS_HZ)
        self.bend_on = True          # brightness bending (high voltage sag)
        self.bend_strength = 0.6     # game px at the screen edge per 0.1 beam load
        self.bend_type = 0           # index into BEND_TYPES
        self.curve_on = True         # the curved tube (C, see CURVE_SHADER; on by default: the owner's choice)
        self.halo_on = True          # the glow around resting spots (Tube > Corner glow, see BEAM_HALO_W)
        self.even_beam = False       # Tube > Even beam: an ideal vector display - every point as bright and wide as a
                                     #  line, no corner glow, overlapping light not adding up (off: the real tube)
        self.halo_k = BEAM_HALO_K    # its brightness (Tube > Corner glow strength)
        self.phos_spread = PHOS_SPREAD  # the afterglow's softness (0; not in the menu)
        self.phos_grow = PHOS_GROW      # how the afterglow joins up (Tube > Afterglow joining)
        self.phos_smooth = PHOS_SMOOTH  # the afterglow's overlap (Tube > Afterglow overlap)
        self.frame_no = 0; self.frame_t0 = 0.0
        self.passes = 2
        self.young = []              # frames still lighting up: [built, real start, real end, lit up to]
        self.last = None             # the newest frame (phosphor off: shown as it is)
        self.T_prev = None
        self.hud = None              # text in the corner (the frame rate counter)

    def _float_works(self, tw, th):
        """can a shader read a 16-bit float image? (Apple's software renderer - a Mac without a graphics
        card, a virtual machine - draws them as garbage: then the images are 8-bit, a shorter afterglow)"""
        gl = self.gl
        made = []
        def target(fmt, kind):
            t = ctypes.c_uint(); gl.glGenTextures(1, ctypes.byref(t)); gl.glBindTexture(GL_TEXTURE_2D, t.value)
            for k, v in ((GL_TEX_MIN, GL_LINEAR), (GL_TEX_MAG, GL_LINEAR), (GL_WRAP_S, GL_CLAMP_EDGE), (GL_WRAP_T, GL_CLAMP_EDGE)):
                gl.glTexParameteri(GL_TEXTURE_2D, k, v)
            gl.glTexImage2D(GL_TEXTURE_2D, 0, fmt, tw, th, 0, GL_RGBA, kind, None)
            f = ctypes.c_uint(); self.GenFB(1, ctypes.byref(f)); self.BindFB(GL_FRAMEBUFFER, f.value)
            self.FBTex(GL_FRAMEBUFFER, GL_COLOR_ATT0, GL_TEXTURE_2D, t.value, 0)
            made.append((t, f))
            return t.value, f.value
        ok = False
        try:
            gl.glViewport(0, 0, tw, th); gl.glDisable(GL_BLEND)
            a = target(GL_RGBA16F, GL_HALF_FLOAT)                      # (the window's size, drawn into as
            gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)   #  the phosphor images are)
            self._quad(0, 0.25, 0.25, 0.25, 1.0)
            b = target(0x8058, 0x1401)                                  # GL_RGBA8, unsigned byte
            self.UseProgram(self.tprog); self.Uniform1i(self.u_ttex, 0)
            self._quad(a[0])                                            # (the tone shader: 0.25 stays 0.25)
            self.UseProgram(0)
            px = (ctypes.c_ubyte * (tw * th * 4))()
            gl.glReadPixels.argtypes = [ctypes.c_int] * 4 + [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p]
            gl.glReadPixels(0, 0, tw, th, GL_RGBA, 0x1401, px)
            bad = sum(1 for i in range(0, len(px), 4 * 37) if abs(px[i] - 64) > 3 or abs(px[i + 1] - 64) > 3)
            ok = bad == 0
        except Exception:       # noqa
            ok = False
        self.BindFB(GL_FRAMEBUFFER, 0); gl.glEnable(GL_BLEND); gl.glClearColor(0, 0, 0, 1)
        for t, f in made:
            gl.glDeleteTextures(1, ctypes.byref(t)); self.DelFB(1, ctypes.byref(f))
        w, h = self.size; gl.glViewport(0, 0, w, h)
        if not ok:
            print('16-bit float images do not work here: 8-bit images (the afterglow is shorter)')
        return ok

    def _target(self, w, h, f32=False):
        gl = self.gl
        if not hasattr(self, 'float_ok'):
            gl.glGetString.restype = ctypes.c_char_p
            soft = b'Software' in (gl.glGetString(0x1F01) or b'')     # GL_RENDERER: Apple's software renderer
            if soft:                                                  #  reads them as garbage in the full picture
                print('Apple software renderer: 8-bit images (the afterglow is shorter)')
            self.float_ok = not soft and self._float_works(w, h)
        t = ctypes.c_uint(); gl.glGenTextures(1, ctypes.byref(t))
        gl.glBindTexture(GL_TEXTURE_2D, t.value)
        for k, v in ((GL_TEX_MIN, GL_LINEAR), (GL_TEX_MAG, GL_LINEAR), (GL_WRAP_S, GL_CLAMP_EDGE), (GL_WRAP_T, GL_CLAMP_EDGE)):
            gl.glTexParameteri(GL_TEXTURE_2D, k, v)
        if self.float_ok and f32:
            gl.glTexImage2D(GL_TEXTURE_2D, 0, 0x8814, w, h, 0, GL_RGBA, GL_FLOAT, None)     # GL_RGBA32F
        elif self.float_ok:
            gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, None)
        else:
            gl.glTexImage2D(GL_TEXTURE_2D, 0, 0x8058, w, h, 0, GL_RGBA, 0x1401, None)     # GL_RGBA8
        f = ctypes.c_uint(); self.GenFB(1, ctypes.byref(f))
        self.BindFB(GL_FRAMEBUFFER, f.value)
        self.FBTex(GL_FRAMEBUFFER, GL_COLOR_ATT0, GL_TEXTURE_2D, t.value, 0)
        if self.FBStatus(GL_FRAMEBUFFER) != GL_FB_COMPLETE:
            raise OSError('framebuffer incomplete')
        gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)
        self.BindFB(GL_FRAMEBUFFER, 0)
        self.tex.append(t.value); self.fbs.append(f.value)
        return t.value, f.value

    def resize(self, w, h):
        super().resize(w, h)
        w, h = self.size
        for t in self.tex:
            self.gl.glDeleteTextures(1, ctypes.byref(ctypes.c_uint(t)))
        for f in self.fbs:
            self.DelFB(1, ctypes.byref(ctypes.c_uint(f)))
        self.tex = []; self.fbs = []
        self.P = self._target(w, h)                       # the phosphor image (ping-pong with P2)
        self.P2 = self._target(w, h)
        self.F = self._target(w, h)                       # this display frame's fresh light
        self.D = self._target(w, h)                       # what the screen shows
        self.E = self._target(w, h)                       # (the curved tube: the screen before bending)
        self.young = []; self.last = None   # (built in window pixels: from now on)
        self.qw, self.qh = max(4, w // 4), max(4, h // 4)
        self.B1 = self._target(self.qw, self.qh)          # bloom ping-pong
        self.B2 = self._target(self.qw, self.qh)
        self.R = self._target(w, h) if getattr(self, 'rot', 0) else None   # (a turned screen: drawn here first)
        self.S = self._target(w, h)                       # (scratches: the screen before the overlay)
        self.B3 = self._target(self.qw, self.qh)          # (scratches: its light, blurred a little)
        self.Q0 = self._target(self.qw, self.qh)          # (its light at quarter size, sharp)
        self.H = [self._target(self.qw, self.qh, True) for _ in range(3)]   # (the glint: horizontal harmonics)
        self.V = [self._target(self.qw, self.qh, True) for _ in range(2)]   # (and both ways)

    @property
    def out(self):
        """the frame buffer the finished screen goes to: the window, or (turned) R"""
        return self.R[1] if getattr(self, 'R', None) else 0

    def _turn_out(self):
        """a turned screen: R onto the window, turned rot degrees clockwise"""
        R = getattr(self, 'R', None)
        if not R:
            return
        gl = self.gl
        rw, rh = self.real_size
        self.BindFB(GL_FRAMEBUFFER, 0); gl.glViewport(0, 0, rw, rh); gl.glDisable(GL_BLEND)
        gl.glMatrixMode(GL_PROJECTION); gl.glPushMatrix(); gl.glLoadIdentity(); gl.glOrtho(0, 1, 0, 1, -1, 1)
        gl.glMatrixMode(GL_MODELVIEW)
        gl.glEnable(GL_TEXTURE_2D); gl.glBindTexture(GL_TEXTURE_2D, R[0]); gl.glColor4f(1, 1, 1, 1)
        tc = {90: lambda X, Y: (1 - Y, X), 180: lambda X, Y: (1 - X, 1 - Y), 270: lambda X, Y: (Y, 1 - X)}[self.rot]
        gl.glBegin(GL_QUADS)
        for X, Y in ((0, 0), (1, 0), (1, 1), (0, 1)):
            gl.glTexCoord2f(*tc(X, Y)); gl.glVertex2f(X, Y)
        gl.glEnd()
        gl.glDisable(GL_TEXTURE_2D)
        gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW)
        gl.glViewport(0, 0, *self.size)

    def set_art(self, art):
        """the overlay to show: (width, height, rgba bytes top row first, sheet width) or None (see
        vart.load_overlay)"""
        gl = self.gl
        if self.art:
            gl.glDeleteTextures(1, ctypes.byref(ctypes.c_uint(self.art[0])))
            self.art = None
        if not art:
            return
        w, h, px, sheet = art
        t = ctypes.c_uint(); gl.glGenTextures(1, ctypes.byref(t))
        gl.glBindTexture(GL_TEXTURE_2D, t.value)
        for k, v in ((GL_TEX_MIN, GL_LINEAR), (GL_TEX_MAG, GL_LINEAR), (GL_WRAP_S, GL_CLAMP_EDGE), (GL_WRAP_T, GL_CLAMP_EDGE)):
            gl.glTexParameteri(GL_TEXTURE_2D, k, v)
        buf = ctypes.create_string_buffer(px, len(px))
        gl.glTexImage2D(GL_TEXTURE_2D, 0, 0x8058, w, h, 0, GL_RGBA, 0x1401, buf)     # (RGBA8)
        self.art = (t.value, w, h, sheet)

    def art_rect(self):
        """where the overlay plane goes, window px (x0, y0, x1, y1): its height the picture's (MiSTer's
        'Framing: Overlay'), its pixels square - the sheet keeps its real shape (162 x 207 mm, as 846 x 1080),
        a little narrower than the picture (0.805); nudged and scaled by art_dx / art_dy / art_scale (fractions
        of the picture height), lowered a little for the parallax"""
        s, ox, oy = self.map
        pl, pt, pr, pb = ox * s, oy * s, (ox + ALG_MAX_X) * s, (oy + ALG_MAX_Y) * s
        ph = pb - pt
        tw, th = self.art[1], self.art[2]
        k = getattr(self, 'art_scale', 1.0)
        hh = ph * k; ww = hh * tw / float(th)
        cx = (pl + pr) / 2 + self.art_dx * ph
        cy = (pt + pb) / 2 + (self.art_dy + (ART_PARALLAX if self.art_parallax else 0.0)) * ph
        return cx - ww / 2, cy - hh / 2, cx + ww / 2, cy + hh / 2

    def _art(self):
        """the overlay on the finished (bent) screen - flat, as the sheet in front of the glass"""
        if not (self.art and self.art_on):
            return
        gl = self.gl
        w, h = self.size
        x0, y0, x1, y1 = self.art_rect()
        scr = (getattr(self, 'gprog', None) and self.float_ok
               and ((self.scr_on and self._scr_ready()) or getattr(self, 'art_glow', False)))
        if scr:                                                 # (the screen before the overlay: the scratches' light)
            gl.glBindTexture(GL_TEXTURE_2D, self.S[0]); gl.glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h)
        gl.glMatrixMode(GL_PROJECTION); gl.glPushMatrix(); gl.glLoadIdentity(); gl.glOrtho(0, w, h, 0, -1, 1)
        gl.glMatrixMode(GL_MODELVIEW)
        gl.glEnable(GL_TEXTURE_2D); gl.glBindTexture(GL_TEXTURE_2D, self.art[0]); gl.glColor4f(1, 1, 1, 1)
        gl.glEnable(GL_BLEND)
        self.UseProgram(self.aprog); self.Uniform1i(self.u_a['tex'], 0)
        rl = room_light(self.art_room)                          # (the print lit by the room as well)
        self.Uniform1f(self.u_a['tint'], self.art_tint); self.Uniform1f(self.u_a['room'], rl)
        self.Uniform1f(self.u_a['print_'], self.art_print * (max(0.0, self.art_room) / ART_ROOM_MID) ** ART_PRINT_POW)
        for ps, (sf, df) in ((0.0, (0x0306, 0x0300)), (1.0, (1, 1))):    # (DST_COLOR, SRC_COLOR / ONE, ONE)
            self.Uniform1f(self.u_a['pass'], ps)
            gl.glBlendFunc(sf, df)
            gl.glBegin(GL_QUADS)
            for (u, v), (x, y) in (((0, 0), (x0, y0)), ((1, 0), (x1, y0)), ((1, 1), (x1, y1)), ((0, 1), (x0, y1))):
                gl.glTexCoord2f(u, v); gl.glVertex2f(x, y)
            gl.glEnd()
        self.UseProgram(0)
        gl.glDisable(GL_TEXTURE_2D)
        gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE)
        gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW)
        if scr:
            self._scratches(x0, y0, x1, y1)

    # ---------------------------------------------------------------- the overlay's scratches
    def _scr_ready(self):
        """the scratch textures for this overlay, seed and wear (made again when they change; while the
        Wear slider moves at most every 0.3 s)"""
        key = (self.art[0], getattr(self, 'art_name', ''), int(self.scr_seed), round(self.scr_wear, 3),
               (round(self.scr_fine, 3), round(self.scr_medium, 3), round(self.scr_large, 3)),
               round(self.scr_scrib, 3))
        if key != self.scr_key:
            now = __import__('time').perf_counter()
            if self.scr_tex and now - self.scr_made_t < 0.3:
                return True                                     # (the old ones until then)
            self._scr_make(key); self.scr_made_t = now
        return self.scr_tex is not None

    def _scr_make(self, key):
        gl = self.gl
        tw, th = int(round(2048 * 162 / 207.0)), 2048
        if not self.scr_tex:
            made = []
            for _ in range(3):
                t = ctypes.c_uint(); gl.glGenTextures(1, ctypes.byref(t)); gl.glBindTexture(GL_TEXTURE_2D, t.value)
                for k, v in ((GL_TEX_MIN, GL_LINEAR), (GL_TEX_MAG, GL_LINEAR), (GL_WRAP_S, GL_CLAMP_EDGE), (GL_WRAP_T, GL_CLAMP_EDGE)):
                    gl.glTexParameteri(GL_TEXTURE_2D, k, v)
                gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, tw, th, 0, GL_RGBA, GL_HALF_FLOAT, None)
                f = ctypes.c_uint(); self.GenFB(1, ctypes.byref(f)); self.BindFB(GL_FRAMEBUFFER, f.value)
                self.FBTex(GL_FRAMEBUFFER, GL_COLOR_ATT0, GL_TEXTURE_2D, t.value, 0)
                if self.FBStatus(GL_FRAMEBUFFER) != GL_FB_COMPLETE:
                    self.BindFB(GL_FRAMEBUFFER, self.out); print('overlay scratches: no float target'); self.gprog = None
                    return
                made.append((t.value, f.value))
            self.scr_tex = made
            NB, SEG = 4096, 24                                  # the template: 4096 scratches of 24 pieces each
            pts = []
            for j in range(NB):
                for k in range(SEG):
                    pts += (j, k / float(SEG), j, (k + 1) / float(SEG))
            self.scr_tmpl = (ctypes.c_float * len(pts))(*pts); self.scr_nb, self.scr_seg = NB, SEG
        self.scr_key = key
        # the seed: the user's, varied per overlay
        name = key[1] or str(key[0])
        sd = (int(key[2]) * 7919 + sum((i + 1) * ord(c) for i, c in enumerate(name))) % 9973 + 1.0   # (each overlay its own)
        wear, dmg = key[3], key[4]
        def amount(k):                                          # step k (1-10): (number x, depth x)
            base = SCR_BASE[0] if k <= 5 else SCR_BASE[1]
            g = 0 if k <= 3 else 1 if k <= 7 else 2
            s = base * wear * dmg[g] * SCR_GROUP[g]                 # (Wear: all; Wear type: its group)
            return s, s ** 0.5
        gl.glViewport(0, 0, tw, th); gl.glDisable(GL_TEXTURE_2D)
        gl.glMatrixMode(GL_PROJECTION); gl.glPushMatrix(); gl.glLoadIdentity()
        gl.glMatrixMode(GL_MODELVIEW); gl.glPushMatrix(); gl.glLoadIdentity()
        gl.glDisableClientState(GL_COLOR_ARRAY); gl.glEnableClientState(GL_VERTEX_ARRAY)
        gl.glVertexPointer(2, GL_FLOAT, 0, self.scr_tmpl)
        gl.glEnable(GL_BLEND); gl.glBlendFunc(GL_ONE, GL_ONE)
        gl.glLineWidth(1.0); gl.glDisable(GL_LINE_SMOOTH)
        self.UseProgram(self.gprog); u = self.u_g
        self.Uniform2f(u['scrib'], SCR_SCRIBBLE[0] * key[5], SCR_SCRIBBLE[1])
        self.Uniform4f(u['llen'], *SCR_TIERS)
        def draw(n, stepid, mode, place, L0, L1, d0, d1, wv, chan, off=0.0):
            cand = n / (SCR_HANDLED_MEAN if place else 1.0)
            whole = int(cand); cand = whole + (1 if (sd * 0.6180339 + stepid * 0.37) % 1.0 < cand - whole else 0)
            for k_, v_ in (('stepid', stepid), ('mode', mode), ('place', place), ('L0', L0), ('L1', L1), ('d0', d0),
                           ('d1', d1), ('wv', wv), ('chan', chan), ('off', off), ('seed', sd), ('dmax', SCR_HANDLED_MAX)):
                self.Uniform1f(u[k_], float(v_))
            for b in range(0, cand, self.scr_nb):
                self.Uniform1f(u['batch'], float(b // self.scr_nb)); self.Uniform1f(u['limit'], float(min(self.scr_nb, cand - b)))
                gl.glDrawArrays(GL_LINES, 0, self.scr_nb * self.scr_seg * 2)
        fine_n = SCR_FINE[0] * 162 * 207 * amount(1)[0]         # (Wear: all of them; Damage: the balance)
        fw = SCR_FINE[5] * SCR_TPM * amount(1)[1]
        for c, (t, f) in enumerate(self.scr_tex):
            self.BindFB(GL_FRAMEBUFFER, f); gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)
            if c < 2:
                draw(fine_n, 0, 0, 0, SCR_FINE[1], SCR_FINE[2], SCR_FINE[3], SCR_FINE[4], fw, c)
        for c in (2, 3):                                        # (the steps: into B (d, 2t) and C (4t))
            self.BindFB(GL_FRAMEBUFFER, self.scr_tex[c - 1][1])
            for i, (n, L0, L1, depth, half) in enumerate(SCR_STEPS):     # (weighted by their depth)
                kn, kd = amount(i + 2)
                if kn <= 0:
                    continue
                for o in range(-half, half + 1):
                    wv = depth * kd * SCR_STEP_W * SCR_TPM * (1.0 if o == 0 else 0.55)
                    draw(n * kn, i + 1, 1, 1, L0, L1, 0.75, 1.25, wv, c, o * SCR_STEP_W)
        self.UseProgram(0)
        gl.glMatrixMode(GL_MODELVIEW); gl.glPopMatrix(); gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW)
        gl.glClearColor(0, 0, 0, 1)
        self.BindFB(GL_FRAMEBUFFER, self.out); gl.glViewport(0, 0, *self.size)
        gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE)

    def _scratches(self, x0, y0, x1, y1):
        """the scratches over the overlay: their light from the screen before it (S), blurred at quarter size"""
        gl = self.gl
        w, h = self.size
        gl.glDisable(GL_BLEND)                                  # (the light: S at quarter size, blurred once (M)
        self.BindFB(GL_FRAMEBUFFER, self.Q0[1]); gl.glViewport(0, 0, self.qw, self.qh)   #  and three times (F))
        self._quad(self.S[0])
        self.BindFB(GL_FRAMEBUFFER, self.B1[1]); self._quad(self.Q0[0])
        self._harmonics()
        self.BindFB(GL_FRAMEBUFFER, self.B1[1]); gl.glViewport(0, 0, self.qw, self.qh)
        self.UseProgram(self.prog); self.Uniform1i(self.u_tex, 0)
        for it in range(3):
            self.BindFB(GL_FRAMEBUFFER, self.B2[1]); self.Uniform2f(self.u_dir, 1.0 / self.qw, 0.0); self._quad(self.B1[0])
            self.BindFB(GL_FRAMEBUFFER, self.B1[1]); self.Uniform2f(self.u_dir, 0.0, 1.0 / self.qh); self._quad(self.B2[0])
            if it == 0:
                self.UseProgram(0); self.BindFB(GL_FRAMEBUFFER, self.B3[1]); self._quad(self.B1[0])
                self.UseProgram(self.prog)
        self.UseProgram(0)
        self.BindFB(GL_FRAMEBUFFER, self.out); gl.glViewport(0, 0, w, h)
        gl.glMatrixMode(GL_PROJECTION); gl.glPushMatrix(); gl.glLoadIdentity(); gl.glOrtho(0, w, h, 0, -1, 1)
        gl.glMatrixMode(GL_MODELVIEW)
        on = bool(self.scr_on and self.scr_tex)
        st = [t for t, f in self.scr_tex] if self.scr_tex else [self.art[0]] * 3
        units = ((self.art[0], 'art'), (st[0], 'A'), (st[1], 'B'), (st[2], 'C'), (self.S[0], 'S'),
                 (self.B3[0], 'M'), (self.B1[0], 'F'), (self.V[0][0], 'V1'), (self.V[1][0], 'V2'))
        for n_, (t, name) in enumerate(units):
            self.ActiveTexture(0x84C0 + n_); gl.glBindTexture(GL_TEXTURE_2D, t)
        self.ActiveTexture(0x84C0); gl.glEnable(GL_TEXTURE_2D)
        self.UseProgram(self.rprog); u = self.u_r
        for n_, (t, name) in enumerate(units):
            self.Uniform1i(u[name], n_)
        self.Uniform1f(u['sf'], self.art[3] / float(self.art[1])); self.Uniform1f(u['ref'], SCR_REF)
        self.Uniform1f(u['glint'], self.scr_glint * SCR_BASE[2]); self.Uniform1f(u['room'], room_light(self.art_room))
        self.Uniform1f(u['scrk'], 2.5 * 0.85 * 0.5 if on else 0.0)
        self.Uniform1f(u['scron'], 1.0 if on else 0.0)
        if self._lobe[0] != self.scr_width:
            self._lobe = (self.scr_width, scr_lobe(self.scr_width))
        self.Uniform1f(u['a2'], self._lobe[1][0]); self.Uniform1f(u['a4'], self._lobe[1][1])
        self.Uniform1f(u['gk'], SCR_GLINT_K if on else 0.0)
        self.Uniform1f(u['glow'], ART_GLOW_K * self.art_glow_k if getattr(self, 'art_glow', False) else 0.0)
        self.Uniform4f(u['pen'], SCR_PEN[0], SCR_PEN[1], SCR_PEN[2], 0.0)
        self.Uniform2f(u['size'], float(w), float(h)); self.Uniform2f(u['qpx'], 1.0 / self.qw, 1.0 / self.qh)
        gl.glEnable(GL_BLEND); gl.glColor4f(1, 1, 1, 1)
        for ps, (sf_, df_) in ((0.0, (0x0306, 0)), (1.0, (1, 1))):      # (DST_COLOR, ZERO: the line gives / ONE, ONE)
            self.Uniform1f(u['pass'], ps); gl.glBlendFunc(sf_, df_)
            gl.glBegin(GL_QUADS)
            for (tu, tv), (x, y) in (((0, 0), (x0, y0)), ((1, 0), (x1, y0)), ((1, 1), (x1, y1)), ((0, 1), (x0, y1))):
                gl.glTexCoord2f(tu, tv); gl.glVertex2f(x, y)
            gl.glEnd()
        self.UseProgram(0)
        for n_ in range(len(units) - 1, -1, -1):
            self.ActiveTexture(0x84C0 + n_); gl.glBindTexture(GL_TEXTURE_2D, 0)
        gl.glDisable(GL_TEXTURE_2D)
        gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE)
        gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW)

    def _harmonics(self):
        """the light's angle harmonics for the glint and the plastic glow (Q0 -> H -> V, quarter size)"""
        (s1, w1), (s2, w2) = SCR_GLINT_SIG
        r = max(0.0, min(2.0, getattr(self, 'scr_reach', 1.0)))      # (Glint reach: from the near kernel to the far one)
        w1, w2 = w1 * (1.5 - 0.5 * r), w2 * r
        px = (1.0 / self.qw, 1.0 / self.qh)
        self.UseProgram(self.hprog); u = self.u_h
        self.Uniform1i(u['src'], 0); self.Uniform1f(u['s1'], s1); self.Uniform1f(u['s2'], s2); self.Uniform2f(u['px'], *px)
        for part in range(3):
            self.BindFB(GL_FRAMEBUFFER, self.H[part][1]); self.Uniform1f(u['part'], float(part)); self._quad(self.Q0[0])
        gl = self.gl
        self.UseProgram(self.vprog); u = self.u_v
        for n_, nm in enumerate(('H1', 'H2', 'H3')):
            self.ActiveTexture(0x84C0 + n_); gl.glBindTexture(GL_TEXTURE_2D, self.H[n_][0]); self.Uniform1i(u[nm], n_)
        self.ActiveTexture(0x84C0)
        self.Uniform1f(u['s1'], s1); self.Uniform1f(u['s2'], s2); self.Uniform1f(u['w1'], w1); self.Uniform1f(u['w2'], w2)
        self.Uniform2f(u['px'], *px)
        for part in range(2):
            self.BindFB(GL_FRAMEBUFFER, self.V[part][1]); self.Uniform1f(u['part'], float(part)); self._quad(self.H[0][0])
        for n_ in (2, 1):
            self.ActiveTexture(0x84C0 + n_); gl.glBindTexture(GL_TEXTURE_2D, 0)
        self.ActiveTexture(0x84C0)
        self.UseProgram(0)

    def _quad(self, tex, r=1.0, g=1.0, b=1.0, a=1.0):
        gl = self.gl
        gl.glMatrixMode(GL_PROJECTION); gl.glPushMatrix(); gl.glLoadIdentity()
        gl.glOrtho(0, 1, 0, 1, -1, 1)
        gl.glMatrixMode(GL_MODELVIEW)
        if tex:
            gl.glEnable(GL_TEXTURE_2D); gl.glBindTexture(GL_TEXTURE_2D, tex)
        gl.glColor4f(r, g, b, a)
        gl.glBegin(GL_QUADS)
        for x, y in ((0, 0), (1, 0), (1, 1), (0, 1)):
            gl.glTexCoord2f(x, y); gl.glVertex2f(x, y)
        gl.glEnd()
        if tex:
            gl.glDisable(GL_TEXTURE_2D)
        gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW)

    def _capsules(self, lines, gain=1.0):
        """the beam lines as Gaussian capsules (window pixels, additive), brightness and width from
        the beam energy per pixel (needs the cycle count per line; without it: a medium beam)"""
        self._draw_built(self._build(lines, gain))

    def _draw_built(self, b, age0=None, dtw=0.0, imm=False):
        """draw built capsules; with age0 (s since the batch's start): the fresh light of the lines
        drawn in the last dtw seconds, with their life left (imm: all lines, as drawn now)"""
        if not b:
            return
        v, t, c, tt, n = b
        gl = self.gl
        w, h = self.size
        gl.glMatrixMode(GL_PROJECTION); gl.glPushMatrix(); gl.glLoadIdentity()
        gl.glOrtho(0, w, 0, h, -1, 1)
        gl.glMatrixMode(GL_MODELVIEW)
        gl.glEnableClientState(GL_VERTEX_ARRAY); gl.glEnableClientState(GL_COLOR_ARRAY)
        gl.glEnableClientState(GL_TEXTURE_COORD_ARRAY)
        gl.glVertexPointer(2, GL_FLOAT, 0, v); gl.glColorPointer(4, GL_FLOAT, 0, c)
        gl.glTexCoordPointer(4, GL_FLOAT, 0, t)
        self.ClientActiveTexture(0x84C1)                   # (unit 1: each line's time in its frame)
        gl.glEnableClientState(GL_TEXTURE_COORD_ARRAY)
        gl.glTexCoordPointer(1, GL_FLOAT, 0, tt)
        self.ClientActiveTexture(0x84C0)
        self.UseProgram(self.cprog)
        self.Uniform1f(self.u_premul, 1.0 if getattr(self, 'blend_max', False) else 0.0)
        u = self.u_t
        if age0 is None:
            self.Uniform1f(u['timed'], 0.0)
        else:
            for k, x in (('timed', 2.0 if imm else 1.0), ('age0', age0), ('dtw', dtw), ('gain', PHOS_GAIN)):
                self.Uniform1f(u[k], x)
        gl.glDrawArrays(GL_QUADS, 0, n * 4)
        self.UseProgram(0)
        self.ClientActiveTexture(0x84C1); gl.glDisableClientState(GL_TEXTURE_COORD_ARRAY)
        self.ClientActiveTexture(0x84C0); gl.glDisableClientState(GL_TEXTURE_COORD_ARRAY)
        gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW)

    def _build(self, lines, gain=1.0, load=True):
        """the vertex arrays for these lines (window pixels): (v, t, c, tt, n), or None"""
        if not lines:
            return None
        halos = [(ln[0], ln[1], ln[2], ln[3], (ln[4] & 127) | 256) + tuple(ln[5:]) for ln in lines
                 if len(ln) > 5 and ln[0] == ln[2] and ln[1] == ln[3] and ln[5] >= 64 and (ln[4] & 127)] \
            if getattr(self, 'halo_on', True) and not getattr(self, 'even_beam', False) else None
        if halos:
            lines = halos + list(lines)                    # (drawn first: the spots and lines go over them)
        n = len(lines)
        w, h = self.size
        sc, ox, oy = self.map
        wob = None
        cos_, sin_ = math.cos, math.sin
        if getattr(self, 'wob_on', False) and self.wob_strength > 0:
            wob = True
            a0, ((PX, QX), (PY, QY)) = hum_coeffs(self.frame_t0 / 1.5e6, self.wob_strength, WOBBLE_SPEED, self.mains_hz)
            k50 = 2 * math.pi * self.mains_hz / 1.5e6
            def woff(t):
                a = a0 + t * k50
                s1, c1 = sin_(a), cos_(a)
                s2, c2 = 2 * s1 * c1, c1 * c1 - s1 * s1
                s3, c3 = s2 * c1 + c2 * s1, c2 * c1 - s2 * s1
                return (PX[1] * s1 + QX[1] * c1 + PX[2] * s2 + QX[2] * c2 + PX[3] * s3 + QX[3] * c3,
                        PY[1] * s1 + QY[1] * c1 + PY[2] * s2 + QY[2] * c2 + PY[3] * s3 + QY[3] * c3)
        # brightness bending: this frame's beam load (energy x intensity per frame time), smoothed
        do_load = load
        load = sum((ln[5] / 8.0) * ((ln[4] & 127) / 127.0) for ln in lines if len(ln) > 5) / 30000.0
        self.load_f = getattr(self, 'load_f', load)
        if do_load:
            self.load_f += (load - self.load_f) * 0.3
        bend = getattr(self, 'bend_on', False) and self.bend_strength > 0
        # the tube switching on / off: brightness (warm-up) and picture size (bulge, collapse)
        cgain = getattr(self, 'crt_gain', 1.0)
        cwidth = getattr(self, 'crt_width', 1.0)              # (the spot blooms as the picture collapses)
        csx, csy = getattr(self, 'crt_sx', 1.0), getattr(self, 'crt_sy', 1.0)
        cskew = getattr(self, 'crt_skew', 0.0); cshift = getattr(self, 'crt_shift', 0.0)
        cgarb = getattr(self, 'crt_garble', 0.0)
        cwarp = getattr(self, 'crt_warp', 0.0); cwph = getattr(self, 'crt_warp_ph', 0.0)
        cscale = csx != 1.0 or csy != 1.0 or cskew or cshift or cgarb or cwarp
        cxm, cym = ALG_MAX_X / 2.0, ALG_MAX_Y / 2.0
        if cgarb:
            import random as _r
            rg = _r.Random(self.frame_no * 7919 + int(cgarb))
        if bend:
            btype = BEND_TYPES[self.bend_type % len(BEND_TYPES)]
            amt = self.bend_strength * self.load_f * 10.0         # game px at the screen edge
            gs = 1.0 + (amt / 128.0 if btype != 'shift' else 0.0)
            gsh = amt * 160.0 * 0.5 if btype != 'grow' else 0.0  # (emulator units, down/right)
            cxm, cym = ALG_MAX_X / 2.0, ALG_MAX_Y / 2.0
        gp = 160.0 * sc                                    # window pixels per game pixel
        v = (ctypes.c_float * (n * 8))(); t = (ctypes.c_float * (n * 16))(); c = (ctypes.c_float * (n * 16))()
        tt = (ctypes.c_float * (n * 4))()                  # each line's time in its frame (s)
        i = j = q4 = 0
        exp, sqrt = math.exp, math.sqrt
        for ln in lines:
            x0, y0, x1, y1, b = ln[:5]
            cyc = ln[5] / 8.0 if len(ln) > 5 else 3.0      # beam energy (cycles at full level)
            if bend:
                x0 = cxm + (x0 - cxm) * gs + gsh; x1 = cxm + (x1 - cxm) * gs + gsh
                y0 = cym + (y0 - cym) * gs + gsh; y1 = cym + (y1 - cym) * gs + gsh
            if cscale:                                     # (the tube switching off: collapse)
                gx = gy = 0.0
                if cgarb:                                  # scrambled: every line thrown off on its own
                    gx = (rg.random() - 0.5) * cgarb; gy = (rg.random() - 0.5) * cgarb
                if cwarp:                                  # smooth waves across the picture
                    wx0 = cwarp * math.sin(y0 * 0.00042 + cwph); wx1 = cwarp * math.sin(y1 * 0.00042 + cwph)
                    wy0 = cwarp * 0.6 * math.sin(x0 * 0.00055 + cwph * 1.7); wy1 = cwarp * 0.6 * math.sin(x1 * 0.00055 + cwph * 1.7)
                    gx += 0.0; x0w, x1w, y0w, y1w = wx0, wx1, wy0, wy1
                else:
                    x0w = x1w = y0w = y1w = 0.0
                x0, x1 = (cxm + (x0 - cxm) * csx + cskew * (y0 - cym) + cshift + gx + x0w,
                          cxm + (x1 - cxm) * csx + cskew * (y1 - cym) + cshift + gx * 0.6 + x1w)
                y0 = cym + (y0 - cym) * csy + gy + y0w; y1 = cym + (y1 - cym) * csy - gy * 0.6 + y1w
            if wob and len(ln) > 6:                        # hum: offsets at the line's start and end
                t0 = ln[6]
                dx0, dy0 = woff(t0); dx1, dy1 = woff(t0 + cyc)
                x0 += dx0; y0 += dy0; x1 += dx1; y1 += dy1
            ax = (x0 + ox) * sc; ay = h - (y0 + oy) * sc
            bx = (x1 + ox) * sc; by = h - (y1 + oy) * sc
            dx, dy = bx - ax, by - ay
            lp = sqrt(dx * dx + dy * dy)                   # window px
            zi = (b & 127) / 127.0
            zk, zw = beam_z(zi)
            fw = (BEAM_W0 + BEAM_WZ * zi) * zw * cwidth    # thin, a little fuller when brighter
            if b & 256:                                    # a resting spot's halo (see BEAM_HALO_W)
                bl = min(1.0, max(0.0, (cyc * zk - BEAM_BLOOM_E0) / (BEAM_BLOOM_E1 - BEAM_BLOOM_E0)))
                fw = BEAM_HALO_W * zw * (1.0 + (BEAM_BLOOM_W - 1.0) * bl) * cwidth
                k = getattr(self, 'halo_k', BEAM_HALO_K) * min(1.0, cyc / BEAM_HALO_C) * (1.0 + (BEAM_BLOOM_H - 1.0) * bl)
            elif getattr(self, 'even_beam', False):       # an ideal display: lines and points alike
                k = BEAM_LINE
            elif lp / gp > 0.05:
                e = cyc / (lp / gp)                        # cycles per game pixel
                k = BEAM_LINE * (1.0 - BEAM_SPEED * exp(-e / 0.8))   # fast sweeps slightly fainter
            else:                                          # the beam resting: a spot
                # corners (16-18 cycles) and dots (60-70) at full brightness, as on the real tube
                k = BEAM_DOTMAX * min(1.0, cyc / BEAM_DOTC)
                if zi < BEAM_LOWZ:                         # a long rest crosses the cutoff earlier (beam_zdot)
                    zk = max(zk, beam_zdot(zi) * min(1.0, cyc / BEAM_DOTLOWC))
                fw = fw * BEAM_DOTW + BEAM_DOTWK * min(cyc, 80.0)
                bl = min(1.0, max(0.0, (cyc * zk - BEAM_BLOOM_E0) / (BEAM_BLOOM_E1 - BEAM_BLOOM_E0)))
                fw *= 1.0 + (BEAM_BLOOM_W - 1.0) * bl          # (a long bright rest blooms)
            sg = max(0.6, fw * gp / 2.355)                 # sigma, window px
            k *= zk * gain * BEAM_PEAK * cgain
            if b & 128:                                    # (the beam fading out: a faint trace)
                k *= BEAM_FADE
            m = 2.6 * sg + 1.0                             # quad margin
            if lp > 1e-3:
                ux, uy = dx / lp * m, dy / lp * m
            else:
                ux, uy = m, 0.0
            nx, ny = -uy, ux
            v[i] = ax - ux + nx; v[i + 1] = ay - uy + ny
            v[i + 2] = bx + ux + nx; v[i + 3] = by + uy + ny
            v[i + 4] = bx + ux - nx; v[i + 5] = by + uy - ny
            v[i + 6] = ax - ux - nx; v[i + 7] = ay - uy - ny
            i += 8
            rr = gg = bb = k; aa = min(1.0, sg / 16.0)              # pure white (owner, 2026-10-12)
            for q in range(4):
                t[j] = ax; t[j + 1] = ay; t[j + 2] = bx; t[j + 3] = by
                c[j] = rr; c[j + 1] = gg; c[j + 2] = bb; c[j + 3] = aa
                j += 4
            tl = ln[6] / 1.5e6 if len(ln) > 6 else 0.0
            tt[q4] = tt[q4 + 1] = tt[q4 + 2] = tt[q4 + 3] = tl
            q4 += 4
        return v, t, c, tt, n

    # ---- the phosphor in real time (see PHOS_GAIN)
    def hold(self):
        """how long a lit spot stays at full brightness: until the game draws it again - its longest
        recent frame (at least PHOS_HOLD) plus 1 ms; afterglow off: the same plus 2 ms, then dark at once
        (longer kept the last copy of a moving object at full brightness next to the new one).
        (A 40 fps game redraws only every 25 ms: a fixed 21 ms hold dimmed what it drew first.)"""
        per = min(0.1, max(getattr(self, 'periods', None) or [0.02]))
        if self.trail_on:
            return max(PHOS_HOLD, per + 0.001)
        return max(PHOS_HOLD_OFF, per + 0.002)

    def _phos_uniforms(self, u):
        """the afterglow curve's settings for a shader using PHOS_FUNC"""
        self.Uniform1f(u['hold'], self.hold())
        self.Uniform1f(u['tscale'], max(0.05, self.trail))
        self.Uniform1f(u['toff'], 0.0 if self.trail_on else 1.0)

    def clear(self):
        """black: no afterglow, no frames waiting"""
        gl = self.gl
        if not getattr(self, 'D', None):
            return
        for fb in (self.P[1], self.P2[1], self.F[1], self.D[1]):
            self.BindFB(GL_FRAMEBUFFER, fb); gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)
        self.BindFB(GL_FRAMEBUFFER, 0)
        self.young = []; self.last = None
    hist = property(lambda self: [], lambda self, v: self.clear())     # (older callers: hist = [] clears)

    def add_frame(self, lines, t_start, length=None):
        """a game frame that started at real time t_start (perf_counter s) and lasts `length` s; its
        lines light up at their own moments, as the beam draws them"""
        if length:                                        # (the hold follows the game's frame period)
            self.periods = (getattr(self, 'periods', []) + [length])[-12:]
        b = self._build(lines)
        self.last = b
        if b:
            self.young.append([b, t_start, t_start + max(b[3]), -1e9])

    def present(self, T, imm=None, flush=False):
        """show the screen at real time T: the light of the lines drawn since the last display frame
        (at their own moments), the phosphor image aged by the time passed. imm: lines lit now (the
        tube switching off); flush: the beam stopped - what was drawn so far is all that is left."""
        gl = self.gl
        w, h = self.size
        if not self.phosphor:                             # crisp: the newest frame as it is
            self.BindFB(GL_FRAMEBUFFER, self.out); gl.glViewport(0, 0, w, h)
            gl.glClearColor(0, 0, 0, 1); gl.glClear(GL_COLOR_BUFFER_BIT)
            gl.glEnable(GL_BLEND); gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE)
            even = getattr(self, 'even_beam', False) and self.BlendEq
            if even:                                      # (even beam: the brighter light, not the sum - max
                self.BlendEq(0x8008); self.blend_max = True   #  ignores alpha: the spot's falloff in the colour)
            self._draw_built(self._build(imm) if imm else self.last)
            if even:
                self.BlendEq(0x8006); self.blend_max = False
            self.young = []; self.T_prev = T
            self._art()
            self._hud()
            self._turn_out()
            self._swap()
            return
        dt = min(0.25, max(0.0, T - self.T_prev)) if self.T_prev is not None else 0.0
        self.T_prev = T
        # 1. the fresh light: lines add up within a frame (rgb), the newest life wins (alpha)
        self.BindFB(GL_FRAMEBUFFER, self.F[1]); gl.glViewport(0, 0, w, h)
        gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)
        gl.glEnable(GL_BLEND); self.BlendFuncSep(GL_ONE, GL_ONE, GL_ONE, GL_ONE)
        self.BlendEqSep(0x8008 if getattr(self, 'even_beam', False) else 0x8006, 0x8008)   # (add - even beam: max -, max)
        keep = []
        for f in self.young:
            b, t0, t1, done = f
            self._draw_built(b, T - t0, 1e9 if flush else T - done)
            f[3] = T
            if not flush and T < t1:
                keep.append(f)
        if imm:                                           # (imm_max: one even line, no bright joints)
            if getattr(self, 'imm_max', False): self.BlendEq(0x8008)
            self._draw_built(self._build(imm, load=False), 0.0, 1.0, imm=True)
        self.BlendEq(0x8006)                              # (and back to adding, for everything else)
        self.young = keep
        # 2. the phosphor image: aged, lit again where the fresh light is brighter
        od = getattr(self, 'decay_override', None)        # (switching off: frozen, dimmed by the curve)
        src, dst = self.P, self.P2
        self.BindFB(GL_FRAMEBUFFER, dst[1]); gl.glDisable(GL_BLEND)
        self.UseProgram(self.uprog)
        self.Uniform1i(self.u_up['oldt'], 0); self.Uniform1i(self.u_up['fresh'], 1)
        self.Uniform1f(self.u_up['kf'], 1.0 if od is not None else math.exp(-dt))
        self._phos_uniforms(self.u_up)
        self.Uniform1f(self.u_up['mul'], od if od is not None else 1.0)
        self.Uniform1f(self.u_up['spread'], getattr(self, 'phos_spread', PHOS_SPREAD) if self.trail_on else 0.0)
        self.Uniform2f(self.u_up['px'], 1.0 / max(1, w), 1.0 / max(1, h))
        self.Uniform1f(self.u_up['grow'], getattr(self, 'phos_grow', PHOS_GROW) if self.trail_on else 0.0)
        self.Uniform1f(self.u_up['gcap'], getattr(self, 'phos_gcap', PHOS_GCAP))
        self.Uniform1f(self.u_up['gage'], math.exp(-PHOS_GAGE))
        hz = 1.0 / dt if dt > 0.004 else 60.0             # (the reach per display frame: as in an 820 px high
        self.Uniform1f(self.u_up['gn'], float(max(1, min(4, round(h / 820.0 * 60.0 / hz)))))   #  window at 60 Hz)
        self.ActiveTexture(0x84C1); gl.glBindTexture(GL_TEXTURE_2D, self.F[0]); self.ActiveTexture(0x84C0)
        self._quad(src[0])
        self.UseProgram(0)
        self.P, self.P2 = dst, src
        # 3. what it shows
        self.BindFB(GL_FRAMEBUFFER, self.D[1])
        self.UseProgram(self.sprog); self.Uniform1i(self.u_stex, 0); self._phos_uniforms(self.u_sh)
        self.Uniform1f(self.u_sh['sm'], getattr(self, 'phos_smooth', PHOS_SMOOTH) * h / 820.0 if self.trail_on else 0.0)
        self.Uniform2f(self.u_sh['px'], 1.0 / max(1, w), 1.0 / max(1, h))
        self._quad(self.P[0])
        self.UseProgram(0)
        self._finish(self.D[0])

    def draw(self, lines):
        """one picture at once (callers without frame timing)"""
        self.present(__import__('time').perf_counter(), imm=lines)

    def _hud(self):
        """the frame rate counter (small green strokes in the top left corner) and the volume knob"""
        if not (self.hud or getattr(self, 'knob', None) or getattr(self, 'osd', None) or getattr(self, 'cart', None)
                or getattr(self, 'browser', None)):
            return
        gl = self.gl
        w, h = self.size
        gl.glMatrixMode(GL_PROJECTION); gl.glPushMatrix(); gl.glLoadIdentity()
        gl.glOrtho(0, w, h, 0, -1, 1)
        gl.glMatrixMode(GL_MODELVIEW)
        gl.glDisableClientState(GL_COLOR_ARRAY); gl.glDisableClientState(GL_VERTEX_ARRAY)
        gl.glEnable(GL_BLEND); gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        k = getattr(self, 'hud_k', None) or max(2.0, h / 400.0)   # px per font unit (6 units tall)
        gl.glLineWidth(max(1.2, k * 0.45))
        for i, row in enumerate(self.hud or []):          # (rows of (text, colour) pieces, stacked)
            m = max(4 * k, 0.01 * h)                      # (a little away from the corner)
            self._text_row(row, m, m + i * 9.0 * k, k)
        if getattr(self, 'knob', None):
            self._knob(self.knob)
        if getattr(self, 'osd', None):
            self._osd(self.osd)
        if getattr(self, 'cart', None):
            self._cart(self.cart)
        if getattr(self, 'browser', None):
            self._browser(self.browser)
        gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW)
        gl.glEnableClientState(GL_COLOR_ARRAY); gl.glEnableClientState(GL_VERTEX_ARRAY)

    def _text_row(self, segs, x, y, k):
        """one row of (text, colour) pieces in the stroke font, 6 k high, top left at x, y"""
        gl = self.gl
        gl.glBegin(GL_LINES)
        for text, (cr, cg, cb) in segs:
            gl.glColor4f(cr, cg, cb, 1.0)
            for ch in text.upper():
                for a, b, c, d in HUD_FONT.get(ch, []):
                    gl.glVertex2f(x + a * k, y + b * k); gl.glVertex2f(x + c * k, y + d * k)
                x += 6 * k
        gl.glEnd()

    def _osd(self, od):
        """the window's OSD: its rows stacked in an area (the strip below the picture), as large as fit"""
        gl = self.gl
        x0, y0, x1, y1 = od['area']
        rows = od['rows']
        chars = max(sum(len(t) for t, _ in r) for r in rows)
        k = od.get('k') or OSD_SCALE * min((y1 - y0) / (len(rows) * 9.0), (x1 - x0) / (6.0 * chars), (y1 - y0) / 18.0)
        gl.glLineWidth(max(1.0, k * 0.42))
        for i, r in enumerate(rows):
            self._text_row(r, x0, y0 + i * 9.0 * k, k)

    def _lines(self, segs):
        gl = self.gl
        gl.glBegin(GL_LINES)
        for x0, y0, x1, y1 in segs:
            gl.glVertex2f(x0, y0); gl.glVertex2f(x1, y1)
        gl.glEnd()

    def _rect(self, x0, y0, x1, y1):
        self._lines([(x0, y0, x1, y0), (x1, y0, x1, y1), (x1, y1, x0, y1), (x0, y1, x0, y0)])

    def _icon(self, lines, xf):
        """an icon's polylines through xf(u, v) -> window pixels"""
        gl = self.gl
        for line in lines:
            gl.glBegin(0x0003)                                  # (GL_LINE_STRIP)
            for u, v in line:
                gl.glVertex2f(*xf(u, v))
            gl.glEnd()

    def _cart(self, c):
        """the cartridge pictogram (click: the ROM browser), CARTRIDGE above it like the panel's labels"""
        gl = self.gl
        cx, cy, r = c['cx'], c['cy'], c['r']
        gl.glColor4f(0.40, 0.667, 0.533, 1.0)
        gl.glLineWidth(max(1.2, r / 45.0))
        s = 2.0 * r / 100.0                                     # (as high as the knob)
        self._icon(CART_ICON, lambda u, v: (cx + (u - 50) * s, cy + (v - 50) * s))
        if c.get('open'):
            self._rect(cx - 52 * s, cy - 56 * s, cx + 52 * s, cy + 56 * s)
        lh = 0.30 * r
        self._text_row([('CARTRIDGE', (0.40, 0.667, 0.533))], cx - (9 * 6 - 2) * lh / 12.0, cy - 1.5 * r - lh, lh / 6.0)

    def _load_icon(self, x0, y0, x1, y1):
        """the load button: LOAD_ICON turned 90 degrees left, fitted into the box"""
        s = min((x1 - x0) / 160.0, (y1 - y0) / 100.0)
        ox = (x0 + x1) / 2 - 80 * s; oy = (y0 + y1) / 2 + 50 * s
        self._icon(LOAD_ICON, lambda u, v: (ox + v * s, oy - u * s))

    def _plug(self, x0, y0, x1, y1):
        """a mains plug, cable to the left, prongs to the right, within the box"""
        w, h = x1 - x0, y1 - y0
        bx0, bx1 = x0 + 0.42 * w, x0 + 0.78 * w
        by0, by1 = y0 + 0.2 * h, y0 + 0.8 * h
        self._rect(bx0, by0, bx1, by1)
        self._lines([(bx1, y0 + 0.37 * h, x1, y0 + 0.37 * h), (bx1, y0 + 0.63 * h, x1, y0 + 0.63 * h),
                     (bx0, y0 + 0.5 * h, x0 + 0.25 * w, y0 + 0.5 * h)])
        pts = [(x0 + 0.25 * w - 0.25 * w * (i / 8.0), y0 + 0.5 * h + 0.3 * h * math.sin(math.pi * i / 8.0)) for i in range(9)]
        self._lines([(a[0], a[1], b[0], b[1]) for a, b in zip(pts, pts[1:])])

    def _browser(self, b):
        """the ROM browser: a box over the picture, the ROMs (the chosen one marked), a scroll bar and the
        plug (click: load it)"""
        gl = self.gl
        x0, y0, x1, y1 = b['rect']
        g = (0.40, 0.667, 0.533); hi = (0.62, 0.95, 0.78)
        gl.glColor4f(0.0, 0.0, 0.0, 0.88)                       # (dark behind it: the picture dims)
        gl.glBegin(GL_QUADS)
        for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            gl.glVertex2f(x, y)
        gl.glEnd()
        k, rh = b['k'], b['row_h']
        gl.glLineWidth(max(1.0, k * 0.42))
        gl.glColor4f(*g, 1.0)
        self._rect(x0, y0, x1, y1)
        self._text_row([(b['title'], g)], x0 + 2 * k * 6 / 6.0 * 2, y0 + 0.6 * rh, k)
        lx0, ly0, lx1, ly1 = b['list']
        gl.glColor4f(*g, 1.0); self._rect(lx0, ly0, lx1, ly1)
        names, top, sel, n = b['names'], b['top'], b['sel'], b['n_vis']
        cols = int((lx1 - lx0 - 4 * 6 * k) / (6 * k))
        if not names:
            for i, line in enumerate(b['empty'].split('\n')):         # (short lines: they fit the list)
                self._text_row([(line, g)], lx0 + 6 * k, ly0 + (0.3 + i) * rh, k)
        for i in range(n):
            j = top + i
            if j >= len(names):
                break
            y = ly0 + i * rh
            t = names[j] if len(names[j]) <= cols else names[j][:cols - 2] + '..'
            if j == sel:
                gl.glColor4f(*hi, 1.0); self._rect(lx0 + 0.5 * k, y + 0.1 * rh, lx1 - 2.5 * 6 * k, y + 0.95 * rh)
                self._text_row([('>' , hi), (' ' + t, hi)], lx0 + 1.5 * k, y + 0.3 * rh, k)
            else:
                self._text_row([('  ' + t, g)], lx0 + 1.5 * k, y + 0.3 * rh, k)
        if len(names) > n:                                      # the scroll bar
            sx = lx1 - 1.2 * 6 * k
            ht = (ly1 - ly0) * n / float(len(names))
            st = ly0 + (ly1 - ly0 - ht) * top / float(max(1, len(names) - n))
            gl.glColor4f(*g, 1.0); self._rect(sx, st, sx + 0.8 * 6 * k, st + ht)
        for (bx0, by0, bx1, by1), d in ((b['up'], -1), (b['dn'], 1)):   # the scroll buttons: a box, a triangle
            gl.glColor4f(*g, 1.0); self._rect(bx0, by0, bx1, by1)
            mx, w3, h3 = (bx0 + bx1) / 2, (bx1 - bx0) * 0.28, (by1 - by0) * 0.22
            my = (by0 + by1) / 2
            ay, by = my + d * h3, my - d * h3                     # (the point: up or down)
            self._lines([(mx - w3, by, mx, ay), (mx, ay, mx + w3, by), (mx + w3, by, mx - w3, by)])
        cx0, cy0, cx1, cy1 = b['close']                         # the close button: a box with an X
        self._rect(cx0, cy0, cx1, cy1)
        m = (cx1 - cx0) * 0.25
        self._lines([(cx0 + m, cy0 + m, cx1 - m, cy1 - m), (cx1 - m, cy0 + m, cx0 + m, cy1 - m)])
        px0, py0, px1, py1 = b['plug']
        gl.glColor4f(*(hi if names else g), 1.0)
        self._load_icon(px0, py0, px1, py1)
        self._text_row([('LOAD', hi if names else g)], px0 - 5.5 * 6 * k, (py0 + py1) / 2 - 3 * k, k)
        self._text_row([(b['help'], g)], x0 + 0.6 * rh, y1 - 0.85 * rh, k * 0.55)

    def _knob(self, kb):
        """the volume knob and RESET button, outlines only (the picture shows through)"""
        gl = self.gl
        cx, cy, r, rx = kb['cx'], kb['cy'], kb['r'], kb['rx']
        gl.glColor4f(0.40, 0.667, 0.533, 1.0)
        gl.glLineWidth(max(1.2, r / 45.0))
        sin, cos, pi = math.sin, math.cos, math.pi
        def at(x, y, rad, deg):                                 # (degrees clockwise from 12 o'clock)
            a = deg * pi / 180.0
            return x + rad * sin(a), y - rad * cos(a)
        def strip(pts, closed=False):
            gl.glBegin(0x0002 if closed else 0x0003)            # (GL_LINE_LOOP / GL_LINE_STRIP)
            for x, y in pts:
                gl.glVertex2f(x, y)
            gl.glEnd()
        def circle(x, y, rad):
            strip([at(x, y, rad, i * 5.0) for i in range(72)], True)
        def text(t, x, ytop, hgt):                              # (centred on x)
            k = hgt / 6.0
            x0 = x - (len(t) * 6 - 2) * k / 2.0
            gl.glBegin(GL_LINES)
            for ch in t:
                for a, b, c, d in HUD_FONT.get(ch, []):
                    gl.glVertex2f(x0 + a * k, ytop + b * k); gl.glVertex2f(x0 + c * k, ytop + d * k)
                x0 += 6 * k
            gl.glEnd()
        # the knob: its rim, the cap (a double edge), the position marker
        circle(cx, cy, r); circle(cx, cy, 0.66 * r); circle(cx, cy, 0.6 * r)
        ang = -KNOB_SWEEP + 2.0 * KNOB_SWEEP * kb['vol']
        a0 = at(cx, cy, 0.72 * r, ang); a1 = at(cx, cy, 0.94 * r, ang)
        gl.glBegin(GL_LINES); gl.glVertex2f(*a0); gl.glVertex2f(*a1); gl.glEnd()
        lh = 0.30 * r                                           # (label height)
        text('OFF/ON', cx, cy - 1.5 * r - lh, lh)
        # the volume wedge: a curved bar right of the knob, thin at the top, wide at the bottom
        inner = [at(cx, cy, 1.25 * r, 50 + i * 4.0) for i in range(21)]
        outer = [at(cx, cy, 1.25 * r + (0.03 + 0.17 * i / 20.0) * r, 50 + i * 4.0) for i in range(21)]
        strip(inner + outer[::-1], True)
        text('VOLUME', cx + 1.6 * r + (6 * 6 - 2) * lh / 12.0, cy - lh / 2.0, lh)
        # the RESET button: its ring and the dome (pressed: smaller)
        circle(rx, cy, 0.95 * r)
        circle(rx, cy, (0.68 if kb.get('pressed') else 0.78) * r)
        text('RESET', rx, cy - 1.5 * r - lh, lh)

    def _finish(self, src):
        """bloom from the picture `src`, then the screen: picture, bloom, overlay, counter"""
        gl = self.gl
        w, h = self.size
        gl.glDisableClientState(GL_COLOR_ARRAY); gl.glDisableClientState(GL_VERTEX_ARRAY)
        # 2. bloom: quarter size copy, blurred
        gl.glDisable(GL_BLEND)
        self.BindFB(GL_FRAMEBUFFER, self.B1[1]); gl.glViewport(0, 0, self.qw, self.qh)
        self._quad(src)
        self.UseProgram(self.prog); self.Uniform1i(self.u_tex, 0)
        for _ in range(self.passes):
            self.BindFB(GL_FRAMEBUFFER, self.B2[1]); self.Uniform2f(self.u_dir, 1.0 / self.qw, 0.0)
            self._quad(self.B1[0])
            self.BindFB(GL_FRAMEBUFFER, self.B1[1]); self.Uniform2f(self.u_dir, 0.0, 1.0 / self.qh)
            self._quad(self.B2[0])
        self.UseProgram(0)
        # 3. the screen: the picture plus the bloom (curved tube: first into E, bent onto the screen)
        curve = getattr(self, 'curve_on', False)
        self.BindFB(GL_FRAMEBUFFER, self.E[1] if curve else self.out); gl.glViewport(0, 0, w, h)
        gl.glClearColor(0, 0, 0, 1); gl.glClear(GL_COLOR_BUFFER_BIT)
        self.UseProgram(self.tprog); self.Uniform1i(self.u_ttex, 0)
        self._quad(src)
        self.UseProgram(0)
        gl.glEnable(GL_BLEND); gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE)
        self._quad(self.B1[0], 0.85, 0.92, 1.0, self.bloom)        # a near-white halo
        ov = getattr(self, 'overlay', None)
        if ov:                                                  # lines on top that leave no afterglow
            g0 = getattr(self, 'crt_gain', 1.0)                 # (the tube switching off: zap, collapse)
            self.crt_gain = getattr(self, 'overlay_gain', 1.0)
            smooth = getattr(self, 'overlay_smooth', False) and self.BlendEq
            if smooth:                                          # one even line: overlapping pieces take the
                self.BlendEq(0x8008); self.blend_max = True     # brighter one instead of adding up (GL_MAX)
            self._capsules(ov)
            if smooth:
                self.BlendEq(0x8006); self.blend_max = False    # (GL_FUNC_ADD)
            self.crt_gain = g0
        if curve:
            gl.glDisable(GL_BLEND)
            self.BindFB(GL_FRAMEBUFFER, self.out)
            ph = ALG_MAX_Y * self.map[0]                    # (the picture's height, window px)
            self.UseProgram(self.kprog); self.Uniform1i(self.u_k['tex'], 0)
            self.Uniform2f(self.u_k['norm'], w / (0.5 * ph), h / (0.5 * ph))
            self.Uniform1f(self.u_k['k'], CURVE_K)
            self.Uniform2f(self.u_k['ctr'], 0.5, 1.0 - (self.map[2] * self.map[0] + 0.5 * ph) / h)
            self._quad(self.E[0])
            self.UseProgram(0)
        self._art()                                             # (flat: after the bend)
        self._hud()
        self._turn_out()
        gl.glEnableClientState(GL_COLOR_ARRAY); gl.glEnableClientState(GL_VERTEX_ARRAY)
        self._swap()

def make_renderer(hwnd, widget=None):
    """the best renderer that works: OpenGL with GPU bloom, plain OpenGL, else GDI+ (Windows; elsewhere
    the caller falls back to the Tk canvas)"""
    try:
        r = GLBloomRenderer(hwnd, widget)
        return r
    except Exception as e:      # noqa
        print('GPU bloom unavailable (%s)' % e)
        if not IS_WIN:
            raise
    try:
        return GLRenderer(hwnd)
    except Exception as e:      # noqa
        print('OpenGL unavailable (%s), using GDI+' % e)
        return Renderer(hwnd)

def set_dpi_aware():
    if not IS_WIN:
        return
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)      # per monitor
    except Exception:
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except Exception:
            pass

_API = []
def _api():
    """user32 / gdi32 with 64-bit handle types, our own copies (the shared ctypes.windll functions pass
    handles as 32-bit ints; a display DC above 2^31 would not fit)"""
    if not _API:
        u, gd = ctypes.WinDLL('user32'), ctypes.WinDLL('gdi32')
        u.MonitorFromWindow.restype = W.HANDLE; u.MonitorFromWindow.argtypes = [W.HWND, W.DWORD]
        u.GetMonitorInfoW.argtypes = [W.HANDLE, ctypes.c_void_p]
        u.GetAncestor.restype = W.HWND; u.GetAncestor.argtypes = [W.HWND, ctypes.c_uint]
        u.GetWindowRect.argtypes = [W.HWND, ctypes.c_void_p]
        gd.CreateDCW.restype = W.HDC; gd.CreateDCW.argtypes = [W.LPCWSTR, W.LPCWSTR, W.LPCWSTR, ctypes.c_void_p]
        gd.GetDeviceCaps.argtypes = [W.HDC, ctypes.c_int]; gd.DeleteDC.argtypes = [W.HDC]
        dwm = ctypes.WinDLL('dwmapi')
        dwm.DwmGetWindowAttribute.argtypes = [W.HWND, W.DWORD, ctypes.c_void_p, W.DWORD]
        _API.extend([u, gd, dwm])
    return _API[0], _API[1]

class _MONINFO(ctypes.Structure):
    _fields_ = [('cbSize', W.DWORD), ('rcMonitor', W.RECT), ('rcWork', W.RECT), ('dwFlags', W.DWORD),
                ('szDevice', W.WCHAR * 32)]

def monitor_info(hwnd=None):
    """the monitor a window is on (Windows): dict(px=(w, h) physical pixels, mm=(w, h) as the monitor reports
    it (EDID; 0 if not), work=(left, top, right, bottom) the desktop without the task bar); None elsewhere"""
    if not IS_WIN:
        return None
    u, gd = _api()
    try:
        hm = u.MonitorFromWindow(hwnd or None, 2 if hwnd else 1)  # (NEAREST; without a window: PRIMARY)
        mi = _MONINFO(); mi.cbSize = ctypes.sizeof(mi)
        if not u.GetMonitorInfoW(hm, ctypes.byref(mi)):
            return None
        dc = gd.CreateDCW('DISPLAY', mi.szDevice, None, None)
        try:
            mm = (gd.GetDeviceCaps(dc, 4), gd.GetDeviceCaps(dc, 6))           # HORZSIZE, VERTSIZE
        finally:
            gd.DeleteDC(dc)
        m, w = mi.rcMonitor, mi.rcWork
        return dict(px=(m.right - m.left, m.bottom - m.top), mm=mm if min(mm) >= 100 else (0, 0),
                    work=(w.left, w.top, w.right, w.bottom))
    except Exception:       # noqa
        return None

def frame_rect(hwnd):
    """a top-level window's visible frame (left, top, right, bottom), physical pixels (without the
    invisible resize borders), or None"""
    if not IS_WIN:
        return None
    try:
        u = _api()[0]
        top = u.GetAncestor(hwnd, 2)                            # (GA_ROOT: Tk's wrapper window)
        r = W.RECT()
        if _API[2].DwmGetWindowAttribute(top, 9, ctypes.byref(r), ctypes.sizeof(r)):   # (EXTENDED_FRAME_BOUNDS)
            u.GetWindowRect(top, ctypes.byref(r))
        return r.left, r.top, r.right, r.bottom
    except Exception:       # noqa
        return None

def pixels_per_mm(hwnd=None):
    """physical pixels per millimetre of the monitor (from its reported size), or None"""
    if not IS_WIN:
        return None
    u, gd = ctypes.windll.user32, ctypes.windll.gdi32
    hdc = u.GetDC(hwnd or 0)
    try:
        mm, px = gd.GetDeviceCaps(hdc, 4), gd.GetDeviceCaps(hdc, 8)     # HORZSIZE, HORZRES
    finally:
        u.ReleaseDC(hwnd or 0, hdc)
    if mm >= 150 and px > 0:
        return px / mm
    return None
