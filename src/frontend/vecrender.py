"""vecrender.py - anti-aliased vector display for arena_play.py (Windows GDI+ through ctypes).

Draws the emulator's vectors into an off-screen 32-bit bitmap with GDI+ anti-aliasing, each line
twice: a wide faint glow and a bright core (the look of a vector CRT), then copies the bitmap to the
window in one call. Line widths scale with the picture, so any window size looks smooth.
"""
import ctypes, math, os, sys, time, ctypes.wintypes as W
import palette
import roomlight, casemodel, tubemap
try:                                    # (numpy: the vertex arrays of a frame in one go - _build_np; without it the
    import numpy as _np                 #  same per line in Python, slower: a dense frame of 11000 lines took 13 ms)
except Exception:   # noqa
    _np = None

IS_WIN = sys.platform == 'win32'      # (macOS / Linux: osdep.py makes the OpenGL context)

ALG_MAX_X, ALG_MAX_Y = 35000, 41000      # (the core's canvas, vecx.h: 1000 wider each side since 2026-10-27)

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

def Renderer_rest(ln):
    """a resting spot (see REST_SPEED): the beam moved slower than REST_SPEED units a cycle on average"""
    cyc = ln[5] / 8.0 if len(ln) > 5 else 0.0
    return ((ln[2] - ln[0]) ** 2 + (ln[3] - ln[1]) ** 2) ** 0.5 <= REST_SPEED * cyc or (ln[0] == ln[2] and ln[1] == ln[3])


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
        if getattr(self, 'grab_req', False):            # (the debug report's picture: this one, read back before it
            self.grab_req = False                       #  is shown - only the emulator's own image, never the screen)
            try:
                w, h = self.size
                px = (ctypes.c_ubyte * (w * h * 4))()
                self.gl.glReadPixels.argtypes = [ctypes.c_int] * 4 + [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p]
                self.gl.glReadPixels(0, 0, w, h, GL_RGBA, 0x1401, px)
                self.grab = (w, h, bytes(px))
            except Exception:   # noqa
                self.grab = None
        if getattr(self, 'snap_req', None):             # (Record > Snapshot at the window's size: this picture)
            box, self.snap_req = self.snap_req, None
            try:
                self.snap_out = (box[2], box[3], self.read_px(0, box))
            except Exception:   # noqa
                self.snap_out = (0, 0, None)
        rec = getattr(self, 'rec', None)                # (recording: the finished picture read back - before the dot)
        if rec is not None or getattr(self, '_rec_pend', None):
            try:
                self._rec_grab(rec)
            except Exception as e:   # noqa
                print('recording: picture not read (%s)' % e); self._rec_pend = None
        if getattr(self, 'rec_mark', None):                # (recording: REC below the picture - after the read-back)
            self._rec_mark(self.rec_mark)
        if IS_WIN:
            if getattr(self, 'hdr', None):              # (HDR: shown through DirectX instead - hdrwin.py)
                self._hdr_show()
            else:
                self.gdi.SwapBuffers(self.dc)
        else:
            self.ctxo.swap()

    def read_px(self, fb, box):
        """the pixels of framebuffer fb (0: the window) in box (x, y from the bottom left, w, h): bgra rows, bottom
        row first"""
        x, y, w, h = box
        if getattr(self, 'BindFB', None):
            self.BindFB(0x8D40, fb)
        rp = self.gl.glReadPixels
        rp.argtypes = [ctypes.c_int] * 4 + [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p]
        self.gl.glPixelStorei(0x0D05, 4)                # (GL_PACK_ALIGNMENT)
        px = ctypes.create_string_buffer(w * h * 4)
        rp(x, y, w, h, 0x80E1, 0x1401, px)              # (GL_BGRA, unsigned bytes)
        return px.raw

    def _rec_grab(self, rec):
        """one picture for the recording, each 1/50 s: read into a pixel buffer now, taken out of the other one (the
        picture read before) - no waiting for the graphics card"""
        now = time.perf_counter()
        pend, self._rec_pend = getattr(self, '_rec_pend', None), None
        gl = self.gl
        PACK = 0x88EB
        if not hasattr(self, '_pbo'):
            try:
                gen = self._proc('glGenBuffers', None, ctypes.c_int, ctypes.c_void_p)
                self._pb_bind = self._proc('glBindBuffer', None, ctypes.c_uint, ctypes.c_uint)
                self._pb_data = self._proc('glBufferData', None, ctypes.c_uint, ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_uint)
                self._pb_map = self._proc('glMapBuffer', ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint)
                self._pb_unmap = self._proc('glUnmapBuffer', ctypes.c_ubyte, ctypes.c_uint)
                b = (ctypes.c_uint * 2)(); gen(2, b)
                self._pbo, self._pbo_n, self._pbo_i = [b[0], b[1]], [0, 0], 0
            except Exception as e:   # noqa
                print('recording: no pixel buffers (%s): read directly' % e)
                self._pbo = None
        box = getattr(self, 'rec_box', None)
        take = rec is not None and box is not None and rec.claim(now)
        if self._pbo is None:
            if take:
                rec.put(now, self.read_px(0, box), box)
            return
        if getattr(self, 'BindFB', None):
            self.BindFB(0x8D40, 0)
        rp = gl.glReadPixels
        rp.argtypes = [ctypes.c_int] * 4 + [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p]
        gl.glPixelStorei(0x0D05, 4)
        nxt = None
        if take:
            i = self._pbo_i; x, y, w, h = box; n = w * h * 4
            self._pb_bind(PACK, self._pbo[i])
            if self._pbo_n[i] != n:
                self._pb_data(PACK, n, None, 0x88E5); self._pbo_n[i] = n      # (GL_STREAM_READ)
            rp(x, y, w, h, 0x80E1, 0x1401, None)
            nxt = (i, now, n, box); self._pbo_i = 1 - i
        if pend:
            j, t, n, pbox = pend
            self._pb_bind(PACK, self._pbo[j])
            p = self._pb_map(PACK, 0x88B8)                # (GL_READ_ONLY)
            if p:
                data = ctypes.string_at(p, n)
                self._pb_unmap(PACK)
                if rec is not None:
                    rec.put(t, data, pbox)
        self._pb_bind(PACK, 0)
        self._rec_pend = nxt

    def _rec_mark(self, m):
        """recording: REC and the time in the strip below the picture (vectrex_emu.rec_state, rows as a hint) - drawn
        after the picture was read back, so never in the video. In the turned picture's pixels, turned as the picture
        (_turn_out) when the screen is turned"""
        gl = self.gl
        rw, rh = getattr(self, 'real_size', self.size)
        if getattr(self, 'BindFB', None):
            self.BindFB(0x8D40, 0)
        if getattr(self, 'UseProgram', None):
            self.UseProgram(0)
        gl.glViewport(0, 0, rw, rh)
        gl.glMatrixMode(GL_PROJECTION); gl.glPushMatrix(); gl.glLoadIdentity(); gl.glOrtho(0, rw, rh, 0, -1, 1)
        gl.glMatrixMode(GL_MODELVIEW); gl.glPushMatrix(); gl.glLoadIdentity()
        rot = getattr(self, 'rot', 0)                     # (clockwise: the picture's (x, y) to the window's)
        gl.glTranslatef.argtypes = [ctypes.c_float] * 3; gl.glRotatef.argtypes = [ctypes.c_float] * 4
        if rot == 90:
            gl.glTranslatef(rw, 0, 0); gl.glRotatef(90, 0, 0, 1)
        elif rot == 180:
            gl.glTranslatef(rw, rh, 0); gl.glRotatef(180, 0, 0, 1)
        elif rot == 270:
            gl.glTranslatef(0, rh, 0); gl.glRotatef(270, 0, 0, 1)
        gl.glDisableClientState(GL_COLOR_ARRAY); gl.glDisableClientState(GL_VERTEX_ARRAY)
        gl.glDisable(GL_TEXTURE_2D); gl.glEnable(GL_BLEND); gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        gl.glVertex2f.argtypes = [ctypes.c_float] * 2
        y = m['cy']
        for i, t in enumerate(m['rows']):
            rk = m['k'] * m['ks'][i]
            gl.glLineWidth(max(1.2, rk * 0.45))
            x = m['cx'] - len(t) * 3.0 * rk
            if i == 0 and m.get('dot'):                   # (a red dot before REC: the group stays centred)
                d = 1.6 * 6.0 * rk; x += d / 2
                cr, cx, cy = 2.4 * rk, x - d + 2.6 * rk, y + 3.0 * rk
                gl.glColor4f(*palette.rgb(tuple(m['cols'][0])), 1.0)
                gl.glBegin(6)                             # (GL_TRIANGLE_FAN)
                gl.glVertex2f(cx, cy)
                for n in range(33):
                    gl.glVertex2f(cx + cr * math.cos(n * math.pi / 16), cy + cr * math.sin(n * math.pi / 16))
                gl.glEnd()
            self._text_row([(t, m['cols'][i])], x, y, rk)
            y += 11.0 * rk
        gl.glColor4f(1, 1, 1, 1)
        gl.glEnableClientState(GL_COLOR_ARRAY); gl.glEnableClientState(GL_VERTEX_ARRAY)
        gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW); gl.glPopMatrix()
        gl.glViewport(0, 0, *self.size)

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
        s = min(w / (VIEW_AW * ALG_MAX_Y), h / (ALG_MAX_Y * (1.0 + rs))) * getattr(self, 'view_scale', 1.0)   # (full screen: + / - size)
        ox = (w / s - ALG_MAX_X) / 2 + PIC_DX * ALG_MAX_Y          # (the glass in the window's middle)
        oy = (h / s - ALG_MAX_Y * (1.0 + rs)) / 2 + PAD_TOP * ALG_MAX_Y   # (rs: the black above and the strip below)
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

# the picture to quarter size for the bloom and the overlay's light: every one of the 4 x 4 pixels a quarter pixel
# covers counted, by four filtered reads of 2 x 2 each. Drawn straight (one read at the quarter pixel's middle) only 2 x 2
# of them counted: a thin line sliding across was caught one frame and missed the next - the glow round it blinking in
# patches as the picture moved against the overlay (head tracking, the wobble; most in a small window - the owner's
# video, 2026-10-29)
DOWN_SHADER = b"""
uniform sampler2D tex;
uniform vec2 sp;                        // (one pixel of the source)
void main() {                          // (8 x 8 pixels, weighted 1 3 3 1 per pair each way - a soft filter: a thin line
    vec2 uv = gl_TexCoord[0].st;        //  counts about the same wherever it lies against the quarter pixels. The hard
    vec4 c = vec4(0.0);                 //  4 x 4 box made the glow narrower or wider as it slid across them - the bloom
    for (int i = 0; i < 4; i++)         //  pulsed with the picture's movement, 2026-10-29)
        for (int j = 0; j < 4; j++) {
            float wi = (i == 0 || i == 3) ? 1.0 : 3.0, wj = (j == 0 || j == 3) ? 1.0 : 3.0;
            c += wi * wj * texture2D(tex, uv + vec2(float(2 * i - 3), float(2 * j - 3)) * sp);
        }
    gl_FragColor = c / 64.0;
}
"""

# several game frames in one display frame: their light averaged per pixel (sum / how many drew it). A line that moved
# between them shows as two half-bright copies - the same light as one; the brighter-of-the-two (max) kept both at full
# strength: a moving line was twice as wide and bright in some display frames, its bloom pulsing (2026-10-29)
FRESH_COV = b"""
uniform sampler2D tex;
uniform float ck;                       // (beam trace: the frame holds light, not screen values - x this first; else 1)
void main() {
    vec4 c = texture2D(tex, gl_TexCoord[0].st);
    gl_FragColor = vec4(0.25 * clamp(ck * max(c.r, max(c.g, c.b)) / 0.5, 0.0, 1.0), 0.0, 0.0, 0.0);   // (a faint edge counts for
                                        //  little: counted whole, a bright line where another frame's faint edge lay was
                                        //  halved - gaps in a moving shape, the rotor test cart, 2026-10-29)   // (quarters: an 8-bit buffer counts to 4
}
"""
FRESH_NORM = b"""
uniform sampler2D tex, cnt;
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec4 c = texture2D(tex, uv);
    gl_FragColor = vec4(c.rgb / max(1.0, 4.0 * texture2D(cnt, uv).r), c.a);
}
"""

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
#
# Tube > Beam trace (beam = 1): a piece of the spot's own path (the core's beam-trace mode). The spot, a Gaussian of
# sigma sg, moves uniformly from A to B and leaves its energy (gl_Color.rgb) spread along the way:
#   d = Q sqrt(2 pi) sg / L  exp(-v^2 / 2 sg^2)  (Phi(u / sg) - Phi((u - L) / sg))   (u along, v across the piece)
# and for a piece of zero length (the spot at rest) Q exp(-r^2 / 2 sg^2). The pieces' soft ends add up exactly to one
# even line where the spot moves on: no caps, no joins. d is the energy in the phosphor's saturation units (see
# BEAM_TRACE_CSAT); SAT_SHADER turns the frame's sum into light.
CAPSULE_SHADER = b"""
uniform vec2 fcs;                          // (Basic: the lines drawn at the phosphor's size - window px per pixel here)
uniform float premul;
uniform float timed, age0, dtw, gain, beam, hsg;   // (hsg: a spot wider than this, px, is the glass's glow - a plain Gaussian)
uniform vec3 wb;
uniform vec3 wbc;
vec3 cold(vec3 c, vec3 wb) {               // the white balance in the glow, pure white in the bright core: a cold white
    float m = max(c.r, max(c.g, c.b));      //  line that keeps the screen's full brightness (WB_CORE)
    return c * mix(wb, wbc, smoothstep(0.35, 0.9, m));
}
float Phi(float x) {                        // the normal distribution (erf: Abramowitz and Stegun 7.1.26)
    float z = abs(x) * 0.70710678, t = 1.0 / (1.0 + 0.3275911 * z);
    float e = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * exp(-z * z);
    return 0.5 + 0.5 * (x < 0.0 ? -e : e);
}
void main() {
    vec4 s = gl_TexCoord[0];
    vec2 p = gl_FragCoord.xy * fcs, a = s.xy, ba = s.zw - s.xy;
    float sg = gl_Color.a * 16.0;
    float g;
    float sw = sqrt(sg * sg + max(fcs.x * fcs.x - 1.0, 0.0) / 12.0);   // (larger pixels: the spot as wide as one of them
                                                                         //  - point samples of a thin line broke it up -
                                                                         //  its light the same: below)
    // a joint with the next / previous piece of the same stroke: cut along the bisector of the two directions
    // (gl_TexCoord[2]: the start's and the end's normals, [3].xy: their planes), so the two pieces share every pixel
    // once - no dark wedge outside a bend, no double light inside it
    vec4 cut = gl_TexCoord[2]; vec2 cc = gl_TexCoord[3].xy;
    if (dot(cut.xy, cut.xy) > 0.5 && dot(p, cut.xy) < cc.x) discard;
    if (dot(cut.zw, cut.zw) > 0.5 && dot(p, cut.zw) > cc.y) discard;
    if (beam > 0.5) {
        vec2 q = p - a; float L = length(ba);
        // the spot's profile: flatter on top and steeper at the sides than a Gaussian - exp(-(d / a)^2.6), the same width at half
        // height and the same light (the owner's photo of Vectorblade's logo: a line 0.70 / 1.00 / 1.30 / 1.70 units wide at 75 / 50 /
        // 25 / 10 % of its peak; a Gaussian of that half width 0.64 / 1.00 / 1.41 / 1.82)
        if (sg > hsg) {                     // (the glass's glow: no flat top, no edge - it fades out; the spot's profile on it was a disc that stopped)
            if (L < 1e-3) g = 4.0 * exp(-length(q) / (0.5 * sg));   // (a spot's glow: brightest at the spot, fading steadily - the same light as the Gaussian's; that one stood round a dot as an even disc with a rim, the screen's last step to black)
            else { vec2 t = ba / L; float u = dot(q, t), v = q.x * t.y - q.y * t.x;
                // (beside a line: 3 x as bright at the line as the Gaussian and down to black at the same reach, 3 sigma - the owner,
                //  2026-11-22: BT_HALO_A, BT_HALO_N; the core: gpu_phos.c PH_FS_BEAM hsh)
                g = 2.5066283 * sg / L * 3.0 * pow(max(1.0 - abs(v) / (3.0 * sg), 0.0), 3.0) * (Phi(u / sg) - Phi((u - L) / sg)); }
        } else if (L < 1e-3) g = 1.177 * exp(-pow(length(q) / (1.356 * sg), 2.6));
        else {
            vec2 t = ba / L; float u = dot(q, t), v = q.x * t.y - q.y * t.x;
            g = 2.5066283 * sg / L * 1.041 * exp(-pow(abs(v) / (1.356 * sg), 2.6)) * (Phi(u / sg) - Phi((u - L) / sg));
        }
    } else {
        float h = clamp(dot(p - a, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
        float d = length(p - a - ba * h);
        g = exp(-0.5 * d * d / (sw * sw)) * (dot(ba, ba) < 1e-6 ? (sg * sg) / (sw * sw) : sg / sw);
    }
    if (timed > 0.5) {                      // fresh phosphor light: brightness, and its life left
        float age = timed > 1.5 ? 0.0 : age0 - gl_TexCoord[1].x;
        if (age < 0.0 || age >= dtw) discard;          // (not drawn yet / already lit earlier)
        gl_FragColor = vec4(gl_Color.rgb * g * gain, exp(-age));      // (alpha: freshness, e^-age)
        return;
    }
    gl_FragColor = premul > 0.5 ? vec4(cold(gl_Color.rgb * g, wb), g) : vec4(cold(gl_Color.rgb, wb), g);   // (wb: crisp lines)
}
"""

# the beam's energy model: a line that took `cyc` cycles over `len` game pixels (160 emulator
# units) got e = cyc/len cycles of beam per pixel. Fitted to the owner's photos (2026-10-08):
#  - width (FWHM, game px) = 0.7 + 0.04 e^2: small text (e ~2.6) ~0.95 px with its rows nearly
#    touching, a big title (e ~1.3, rows 1.41 px apart) ~0.8 px with a thin dark gap,
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
# faster; a resting spot: DOT_K x dot_light(cycles) ^ (1 / 2.2), DOT_W x as wide as the lines.
BEAM_LINE = 1.15
BEAM_FADE = 0.3              # the beam fading out after /BLANK (intensity bit 7 from the core): this much of a line
# the brightness knob at the back (knob_bright, the Vectrex unit's Brightness; 0 = well set): it sets the tube's bias - a
# fixed offset on the intensity drive, the same for every line: the beam is driven at Z/127 + knob through the tube's
# curve (beam_z), its cutoff included. So the knob moves the cutoff through the intensities one by one (the test cart's
# INTENSITY steps light up from their brighter left ends), and a line's corner dots and glow go with it. /BLANK drives
# the beam at 0: with the knob up past the cutoff the blanked beam glows too - its moves, returns to the middle and rests
# (the core sends them as intensity 0 + bit 7 while the knob is up): the "spider web"
# a resting beam (a corner: ~18 cycles in one spot, ~10x a moving line's energy there) also lights a soft
# halo around itself (light scattered in the tube's glass; the owner's photos: corner dots and stars in
# a glow). It softens the dark line ends next to corners without hiding them; text has no resting spots.
BEAM_HALO_W, BEAM_HALO_K, BEAM_HALO_C = 1.6, 0.014, 18.0  # FWHM (game px), brightness (default, the owner's choice 2026-10-14; Tube > Corner glow strength), cycles for full
# a resting spot (the owner's Dot Test photos, 2026-11-21, ProRAW; as the core, libretro_vx.c): as wide as the line at
# its intensity, its light growing with the rest - A = DOT_A x DOT_T x ln(1 + cycles / DOT_T) times the line's peak (0.8x
# at 4 cycles, 2.9x at 16, 7x at 48, 13.5x at 128) - never wider; round it a faint glow (REST_HALO of that light, FWHM
# REST_HALO_W game px, its light ^ REST_HALO_P). Shots alike, a double shot as one of the summed time; joints and corners as
# isolated dots. Per look (0 Bright, 1 Vectrex: the lines' light differs), each matched to the photos' dot sizes in this
# renderer (F12 snapshots at the window's size: within 0.12 unit on average in Bright, 0.14 in Vectrex)
DOT_A, DOT_T = 0.209, 52.0
DOT_K, DOT_W, REST_HALO, REST_HALO_W, REST_HALO_P = (1.2, 3.0), (1.4, 1.6), (0.3, 0.6), (2.4, 2.4), 0.8   # (REST_HALO x 5, W 0.9 -> 2.4: the owner, 2026-11-26 - more shine round the brightest and corner dots, as on his tube)
if os.environ.get('VX_RHALO'):       # (a test: VX_RHALO=strength,width[,power] - the resting spots' glow, as REST_HALO[0] / REST_HALO_W)
    _h = [float(v) for v in os.environ['VX_RHALO'].split(',')]
    REST_HALO, REST_HALO_W = (_h[0], _h[0] * 2), (_h[1], _h[1])
    if len(_h) > 2: REST_HALO_P = _h[2]
DOT_C0 = 9.0     # a rest joined to a lit line (a joint, a corner: brightness bit 1024): the beam slows into it - this many cycles
                 # less (the photos: a 16-cycle corner 1.3x the line, an isolated 16-cycle dot 2.9x; the owner, 2026-10-26: 12 and 16
                 # only a slight bump in the line's thickness)
DOT_WL = 1.5     # the spot's width grows from the line's to DOT_W x up to this light (x the 7F line's): a faint or short rest
                 # a slight thickening, a bright long one a dot (the owner, 2026-11-21: "fainter dots are also smaller")
def mark_joined(x0, y0, x1, y1, b, e, t0):
    """brightness bit 1024 on the rests joined to a lit moving line (its end at the rest's start, or its start at the
    rest's end, in time too) and on the dwells (512): the arrays as _build_np has them (e: cycles x 8)"""
    n = len(b)
    ln = _np.sqrt((x1 - x0) ** 2 + (y1 - y0) ** 2); mov = ln > REST_SPEED * e / 8.0
    lit = ((b & 127) != 0) & ((b & 128) == 0) & ((b & 256) == 0)
    tol = 0.5 * 160.0
    att = (b & 512) != 0
    if n > 1:
        a, c = slice(None, -1), slice(1, None)        # (a: the earlier piece, c: the later one)
        meet = (_np.abs(x1[a] - x0[c]) <= tol) & (_np.abs(y1[a] - y0[c]) <= tol) & (_np.abs(t0[a] + e[a] / 8.0 - t0[c]) <= 2.0)
        att[1:] |= ~mov[1:] & mov[:-1] & lit[:-1] & meet      # (a line, then the rest)
        att[:-1] |= ~mov[:-1] & mov[1:] & lit[1:] & meet      # (the rest, then a line)
    return _np.where(att & ~mov, b | 1024, b & ~1024)
def dot_light(cyc):
    if _np is not None and hasattr(cyc, '__len__'):  # (the frame's arrays, _build_np)
        return DOT_A * DOT_T * _np.log1p(_np.maximum(_np.asarray(cyc, dtype=float), 0.0) / DOT_T)
    return DOT_A * DOT_T * math.log1p(max(cyc, 0.0) / DOT_T)
# a resting spot: a piece where the beam moved slower than REST_SPEED units a cycle on average (the spot's lag brings it
# to rest gradually - the last fraction of a unit is the rest; lines are drawn at 1 unit a cycle and more). Drawn
# where the spot came to rest
REST_SPEED = 0.5
# such a spot (brightness bit 512) is weaker than a true rest while short: the owner (2026-10-26): the 12- and 16-cycle
# rests "a slight bulge of the line", a faint glow, not a defined dot. Brightness x w^2.5, width x (1 + REST_DWELL_SOFT
# (1 - w)), w = its cycles / REST_DWELL_C (at most 1)
# (2026-10-26 again: 12 and 16 only a slight bump in the line's thickness, not a defined dot: no halo below
# REST_DWELL_HALO cycles)
REST_DWELL_C, REST_DWELL_SOFT, REST_DWELL_HALO = 28.0, 1.5, 20.0


# the beam's width with its intensity (Z/127; its brightness: TUBE_LIGHT): below intensity 63 the tube is close
# to its cutoff and the spot shrinks fast: the owner (2026-10-12): Vectorblade's background stars (intensity 15-47) are much
# thinner, smaller and fainter on the real tube than the shots (127), on the emulator nearly the same.
BEAM_LOWZ, BEAM_LOWP, BEAM_LOWW = 0.5, 1.7, 0.45  # below this z: the cutoff curve (BEAM_Z0); width x LOWW at z = 0
# the tube's cutoff, measured (testcart/bright_test, the owner's photo and count by eye 2026-10-14): the
# lines from $20 (very faint; $18 dark), then steeply brighter ($30 / $38 / $40 ~ 0.37 / 0.64 / 1 of each
# other), the dots clearly from $20: brightness ((Z - Z0) / (63.5 - Z0))^LOWP with Z0 = 27, zero below
BEAM_Z0 = 27 / 127.0


# the tube's light for its drive, measured (2026-10-25): the test cartridge's INTENSITY page (all intensities, 8 per
# bar) on the owner's Vectrex, iPhone ProRAW (linear), the red channel's light summed across each step's line, the
# camera's fall-off to the screen's edges fitted out (a factor per step position, the same in every bar). Up to 72:
# the ultra-wide camera (ISO 16; ISO 200 for the faint bars); from 72: the main camera (ISO 50, f/4, 1/25 s - its
# ISO 50 and ISO 320 photos agree within 2 %, the ultra-wide's do not: it compresses its highlights). TUBE_LIGHT[d]
# = the light at drive d (Z + the brightness knob's bias, in intensity steps; the circuit is linear in Z: the DAC,
# D302 / Q505 and the cathode, see the service manual), 1 = Z 127 at the knob's default. Dark up to 27 (the cutoff),
# measured from 30 to 108 (within 5 %), above that (the top bar was out of the photos, and the knob up) the curve
# fitted to 48-108: light ~ u / (1 + u), u = ((d - 18.5) / 81.7)^2.67 - a mild saturation (Z 127 = 2.8 x Z 72)
TUBE_LIGHT = [
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.00125, 0.00249, 0.00374,
    0.00503, 0.00664, 0.00972, 0.01133, 0.01319, 0.01537, 0.01784, 0.02081, 0.02432, 0.02838, 0.03302, 0.03824,
    0.04402, 0.05034, 0.05705, 0.06431, 0.07209, 0.08041, 0.08926, 0.09862, 0.10846, 0.11877, 0.12951, 0.14066,
    0.15217, 0.164, 0.17611, 0.18845, 0.20098, 0.21364, 0.2264, 0.2392, 0.252, 0.26475, 0.27742, 0.28997, 0.30235,
    0.31455, 0.3256, 0.3368, 0.34826, 0.36011, 0.37248, 0.38549, 0.39929, 0.41402, 0.42782, 0.44165, 0.45549,
    0.46933, 0.48316, 0.49698, 0.51077, 0.52451, 0.53821, 0.55184, 0.56541, 0.57891, 0.59232, 0.60563, 0.61885,
    0.63197, 0.64497, 0.65785, 0.67062, 0.68325, 0.69575, 0.70812, 0.72035, 0.73243, 0.74437, 0.75616, 0.7678,
    0.77928, 0.79061, 0.80178, 0.8128, 0.82365, 0.83435, 0.84489, 0.85527, 0.86548, 0.87554, 0.88544, 0.89518,
    0.90476, 0.91418, 0.92345, 0.93256, 0.94151, 0.95032, 0.95897, 0.96747, 0.97582, 0.98403, 0.99208, 1, 1.0078,
    1.0154, 1.0229, 1.0303, 1.0375, 1.0446, 1.0515, 1.0584, 1.0651, 1.0717, 1.0781, 1.0845, 1.0907, 1.0968, 1.1028,
    1.1087, 1.1144, 1.1201, 1.1256, 1.1311, 1.1364, 1.1417, 1.1468, 1.1519, 1.1568, 1.1617, 1.1665, 1.1711, 1.1757,
    1.1803, 1.1847, 1.189, 1.1933, 1.1975, 1.2016, 1.2056, 1.2095, 1.2134, 1.2172, 1.2209, 1.2246, 1.2282, 1.2317]
TUBE_LIGHT_D = len(TUBE_LIGHT) - 1


# Tube > White balance: the beam's white as a colour temperature (WHITE_LO..WHITE_HI kelvin, the slider even in mireds;
# the default 12000 K - the owner, 2026-10-27: what the real Vectrex looks like, really bluish; 9300 K before). The white on the CIE daylight
# curve against sRGB's own (6504 K: the beam's white before), as bright: gains on the beam's light only (the overlay, the
# case and the glass keep their colours)
WHITE_DEFAULT, WHITE_LO, WHITE_HI = 12000.0, 5600.0, 16000.0     # (the owner, 2026-10-27: room to go bluer than the default)
WHITE_CORE_HI = 12000.0         # (the cores' ramp keeps its old scale: at 12000 K they are fully that, as before; beyond, T itself)
# the slider (the owner, 2026-10-26): its middle neutral - the line and its glow the screen's pure white (WHITE_MID);
# towards cool (warm) the glow takes the temperature while the line's bright core stays white; only over the last
# WHITE_CORE of each half does the core follow too, reaching the end's temperature at the end. Each half even in mireds
WHITE_MID, WHITE_CORE = 6504.0, 0.3


def white_pos(T):
    """the slider's place for T kelvin: -1 (WHITE_LO, warm) .. 0 (WHITE_MID) .. 1 (WHITE_HI, cool)"""
    m, m0 = 1e6 / float(T), 1e6 / WHITE_MID
    if m <= m0:
        return min(1.0, (m0 - m) / (m0 - 1e6 / WHITE_HI))
    return -min(1.0, (m - m0) / (1e6 / WHITE_LO - m0))


def white_from_pos(f):
    """the temperature at the slider's place f (-1 .. 1)"""
    m0 = 1e6 / WHITE_MID
    m = m0 - f * (m0 - 1e6 / WHITE_HI) if f >= 0 else m0 - f * (1e6 / WHITE_LO - m0)
    return 1e6 / m


def white_core_k(T):
    """the line core's temperature for the slider at T: neutral, but over the last WHITE_CORE of a half on to T's - the
    cool half on the scale up to WHITE_CORE_HI (the look the owner chose at 12000 K), past it the core is T itself"""
    T = float(T)
    if T >= WHITE_CORE_HI:
        return T
    m, m0 = 1e6 / T, 1e6 / WHITE_MID
    if m <= m0:                                         # (cool: positions on the 6504 .. 12000 K scale)
        f = min(1.0, (m0 - m) / (m0 - 1e6 / WHITE_CORE_HI))
        if f <= 1.0 - WHITE_CORE:
            return WHITE_MID
        g = (f - (1.0 - WHITE_CORE)) / WHITE_CORE
        return 1e6 / (m0 - g * (m0 - 1e6 / WHITE_CORE_HI))
    f = white_pos(T)
    if abs(f) <= 1.0 - WHITE_CORE:
        return WHITE_MID
    g = (abs(f) - (1.0 - WHITE_CORE)) / WHITE_CORE
    return white_from_pos(-g)


# WB_CORE: the white balance is a tint of the light around the lines - the glow, the bloom, the afterglow - while their
# bright cores stay the screen's own pure white (cold() in the shaders): a screen is brightest at full R, G and B, and
# a colder white made by lowering red would cost ~13 % of the brightness at 9300 K (~18 % at 12000 K). The eye reads
# the line from its glow: cold, at full brightness (the owner, 2026-10-26)
def white_gain(T):
    """display-space (r, g, b) gains for the colour temperature T kelvin (1, 1, 1 at 6504 K), equally bright"""
    def lin(T):
        T = min(max(float(T), 4000.0), 25000.0)
        if T <= 7000: x = -4.6070e9 / T ** 3 + 2.9678e6 / T ** 2 + 0.09911e3 / T + 0.244063
        else: x = -2.0064e9 / T ** 3 + 1.9018e6 / T ** 2 + 0.24748e3 / T + 0.237040
        y = -3.0 * x * x + 2.870 * x - 0.275; X, Z = x / y, (1 - x - y) / y
        return (3.2406 * X - 1.5372 - 0.4986 * Z, -0.9689 * X + 1.8758 + 0.0415 * Z, 0.0557 * X - 0.2040 + 1.0570 * Z)
    c, c0 = lin(T), lin(6504.0)
    c = [a / b for a, b in zip(c, c0)]
    L = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
    return tuple(max(0.0, v / L) ** (1 / 2.2) for v in c)


def tube_light(d):
    """the tube's light at drive d (intensity steps, may be fractional; TUBE_LIGHT)"""
    d = min(max(d, 0.0), float(TUBE_LIGHT_D)); i = min(int(d), TUBE_LIGHT_D - 1)
    return TUBE_LIGHT[i] + (TUBE_LIGHT[i + 1] - TUBE_LIGHT[i]) * (d - i)


def beam_z(zi):
    """(brightness factor, width factor) for the drive zi = Z / 127 (+ the knob's bias): the brightness as a
    display value (the screen shows light^(1/2.2)), from the measured tube (TUBE_LIGHT)"""
    k = tube_light(zi * 127.0) ** (1 / 2.2)
    if zi >= BEAM_LOWZ:
        return k, 1.0
    return k, BEAM_LOWW + (1.0 - BEAM_LOWW) * max(0.0, zi) / BEAM_LOWZ


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

# Tube > Beam trace: the phosphor lit by the energy the spot leaves (the core's beam-trace mode: the spot's own path,
# see vecx.c bp_step). Per pixel the frame's energy d (in units of BEAM_TRACE_CSAT cycles of a Z 127 beam resting on
# one spot's area) gives light 1 - exp(-d): linear for lines, saturating where the spot rests - the one constant fits
# the owner's rest-dot measurement (testcart/lag_test: a rest of N cycles ~ 31 (1 - e^-(N-5)/69) cycles of line light,
# within 1-2; 2026-10-25). Shown as light^(1/2.2) x BEAM_TRACE_GAIN (a typical line as bright as in the line renderer).
# The spot: a Gaussian, BEAM_TRACE_SG x the line renderer's (display) width - the light's profile, not its square root
BEAM_TRACE_CSAT = 8.5
BEAM_TRACE_GAIN = 3.0
BEAM_TRACE_SG = 0.674
# The phosphor's light along the beam's path (the owner's Dot Test photos, 2026-11-21, ProRAW, linear light; pages 1 and 3):
# two parts. A fast one that saturates at once - a fixed light for every unit of phosphor the spot sweeps over (BT_A a
# unit of length) - and a slow one growing with the beam's time on the place (BT_B a cycle; flattening gently with the
# dose: BT_D0 cycles at intensity 127). Measured: a line's light a unit = 0.72 + 0.21 x (cycles a unit) at 1.0 / 1.6 / 3.2
# cycles a unit; a resting spot 0.2-0.25 a cycle, linear to ~48 cycles, 0.7 of that at 128. By intensity: the lines' light
# is the tube's (TUBE_LIGHT, from lines); the slow part falls faster - x TUBE_LIGHT^BT_IP (dots at 5F / 40 / 30: 0.52 /
# 0.12 / 0.019 of 7F's, the tube's light 0.67 / 0.27 / 0.08) - so the fast part is what is left of a line's light at the
# speed the tube's light was measured at (BT_UREF). A piece of the path (length l units, t cycles):
#   light = a(z) (l + BT_WF) + (l + BT_W) / BT_W x BT_B x BT_D0 ln(1 + i(z) tau / BT_D0),   tau = t BT_W / (l + BT_W)
# (tau: the cycles a point stays under the spot, BT_W its footprint), spread along the piece as the spot's Gaussian
# (CAPSULE_SHADER, beam). No dot, halo or dwell rules: a rest, a slow corner, a line's speed follow from the path.
# Units: the reference line (Draw_Line_d, 1.59 cycles a unit, intensity 127) has light 1.054 a unit of length
BT_A, BT_B, BT_D0, BT_IP = 0.70, 0.29, 150.0, 1.5
BT_W, BT_WF, BT_UREF = 0.30, 0.10, 1.6
BT_UNITS = 160.0 / 134.0                    # (Vectrex units - Moveto at scale $7F - a game px)
BT_EXPO = 0.20                              # (the Vectrex look: a typical game line's flat top at white, hardly above - Vectorblade's logo,
                                            #  intensity 79: its lines 0.6 of the photo's ceiling, unclipped; driven above white the tone step
                                            #  flattens a line's top and its skirt shows: "an out of focus photo", the owner)
def bt_light(tl, l, t):
    """a piece's light: tl = the tube's light of its intensity (0..1), l = its length (units), t = cycles (arrays)"""
    iz = tl ** BT_IP
    az = _np.maximum(0.0, (BT_A + BT_B * BT_UREF) * tl - BT_B * BT_UREF * iz)
    tau = t * BT_W / (l + BT_W)
    return az * (l + BT_WF) + (l + BT_W) / BT_W * BT_B * BT_D0 * _np.log1p(iz * tau / BT_D0)
# The law as the Photo Test measured it (2026-11-21, f/4, unclipped; tools/photo_match README): the light a unit of line
# against the beam's dwell there (cycles a unit) at intensities $30 / $40 / $5F / $7F (BT2_L, one scale; ~dwell^0.65 at
# $7F and ^0.9 at $40 up to 8 cycles a unit, ^0.8 beyond at every intensity), by intensity at 8.3 cycles a unit
# $30 / $3F / $5F / $6F / $7F = 0.022 / 0.12 / 0.51 / 0.76 / 1 (BT2_TLK: against TUBE_LIGHT). A resting dot of 20 cycles has
# the light of 2.0 units of the 8.3 line and the peak of a line that slow: the same law with the dwell taken over the
# spot's footprint (BT2_W units). So: along each run of lit pieces the beam's time within BT2_W of every place gives
# its dwell, the law its light - a rest, a slow start, a line's end follow; separate runs add (measured: repeats and
# crossings add). VX_BT_LAW=1: the law before (bt_light)
BT2_W = 1.0
BT2_D = _np.log(_np.array([1.048, 1.386, 2.080, 2.773, 4.159, 5.546, 8.319, 16.64, 33.28, 48.0, 128.0, 1024.0]))
def _bt2_row(v, k=1.0):
    v = [x * k for x in v]; v += [v[-1] * (48.0 / 33.28) ** 0.8]; v += [v[-1] * (128.0 / 48.0) ** 0.64]; v += [v[-1] * 8.0 ** 0.3]   # (rests: lag_test / Dot Test - flattening)
    return _np.log(_np.array(v))
BT2_Z = _np.array([48.0, 64.0, 95.0, 127.0])
BT2_L = _np.stack([_bt2_row([0.0333, 0.0428, 0.0619, 0.08, 0.11, 0.14, 0.21, 0.40, 0.73], 0.194), _bt2_row([0.21, 0.27, 0.39, 0.51, 0.72, 0.90, 1.25, 2.21, 3.70], 0.194),
                   _bt2_row([0.24, 0.30, 0.41, 0.50, 0.66, 0.79, 1.05, 1.79, 3.20]), _bt2_row([0.49, 0.59, 0.78, 0.94, 1.21, 1.43, 1.87, 3.24, 5.75])])
BT2_K = 1.054 / 0.647                       # (the reference line - 1.59 cycles a unit, $7F - keeps its light: the exposure stands)
BT2_TLK = ((0.0, 0.25), (48.0, 0.25), (63.0, 0.48), (95.0, 0.76), (111.0, 0.89), (127.0, 1.0), (255.0, 1.0))   # (the Photo Test's page 4 / TUBE_LIGHT)
def _bt2_tl(q):
    return _np.interp(q, _np.arange(TUBE_LIGHT_D + 1.0), TUBE_LIGHT) * _np.interp(q, [a_ for a_, b_ in BT2_TLK], [b_ for a_, b_ in BT2_TLK])
def bt2_light(z, d):
    """the light a unit of path: z = the drive (intensity steps, arrays), d = the dwell there (cycles a unit)"""
    z = _np.asarray(z, dtype=float); ld = _np.log(_np.maximum(d, 1e-3))
    j = _np.clip(_np.searchsorted(BT2_D, ld) - 1, 0, len(BT2_D) - 2); f = (ld - BT2_D[j]) / (BT2_D[j + 1] - BT2_D[j])   # (below the first: its slope on)
    rows = BT2_L[:, j] + (BT2_L[:, j + 1] - BT2_L[:, j]) * f          # (4 x n: each intensity's curve at d)
    zc = _np.clip(z, BT2_Z[0], BT2_Z[-1]); i = _np.clip(_np.searchsorted(BT2_Z, zc) - 1, 0, len(BT2_Z) - 2); g = (zc - BT2_Z[i]) / (BT2_Z[i + 1] - BT2_Z[i])
    n = _np.arange(len(ld)); out = _np.exp(rows[i, n] + (rows[i + 1, n] - rows[i, n]) * g)
    out = _np.where(z < BT2_Z[0], out * _bt2_tl(z) / _bt2_tl(BT2_Z[0]), out); out = _np.where(z > BT2_Z[-1], out * _bt2_tl(z) / _bt2_tl(BT2_Z[-1]), out)
    return BT2_K * out
def bt2_prepare(L, col=None, dk=1.0):
    """the beam's pieces (rows x0 y0 x1 y1 flags energy t0, in time) -> the pieces to draw and each one's light: along
    every run of lit pieces that join in place and time the dwell at a place = the beam's cycles within BT2_W of it
    (along the path) / BT2_W; a long piece gives its two ends (BT2_W / 2) as pieces of their own; a run's two ends get a
    cap (zero length, the footprint's half beyond the end). dk: the dwell from the cycles x this - a frame squeezed into a
    refresh (Smooth motion: its times and cycles x k) lit as drawn, x 1 / k; its times and places as they are"""
    np = _np; U = 134.0; W = BT2_W
    b = L[:, 4].astype(np.int64); z = (b & 127).astype(float)
    lit = (z > 0) & ((b & 128) == 0)
    x0, y0, x1, y1, t0 = L[:, 0], L[:, 1], L[:, 2], L[:, 3], L[:, 6]; t = L[:, 5] / 8.0
    l = np.sqrt((x1 - x0) ** 2 + (y1 - y0) ** 2) / U
    out_rows, out_light, out_src = [], [], []
    o = np.nonzero(~lit)[0]                                # (the others - the dark beam's glow, fades: piece by piece)
    if len(o):
        out_rows.append(L[o]); out_light.append((l[o] + W) * bt2_light(z[o], t[o] * dk / (l[o] + W))); out_src.append(o)
    k = np.nonzero(lit)[0]
    if len(k):
        X0, Y0, X1, Y1, T0, T, Ln, Z, B = x0[k], y0[k], x1[k], y1[k], t0[k], t[k], l[k], z[k], b[k]; n = len(k)
        cont = np.zeros(n, dtype=bool)
        if n > 1:
            cont[1:] = ((np.abs(X0[1:] - X1[:-1]) <= 3.0) & (np.abs(Y0[1:] - Y1[:-1]) <= 3.0) & (np.abs(T0[1:] - (T0[:-1] + T[:-1])) <= 1.5) & (B[1:] == B[:-1]) & (k[1:] == k[:-1] + 1))
        gap = np.where(cont, 0.0, 4.0 * W); S0 = np.cumsum(gap + np.concatenate([[0.0], Ln[:-1]])); S1 = S0 + Ln
        Tc = np.concatenate([[0.0], np.cumsum(T)[:-1]])
        xs = np.empty(2 * n); ys = np.empty(2 * n); xs[0::2] = S0; xs[1::2] = S1; ys[0::2] = Tc * dk; ys[1::2] = (Tc + T) * dk
        def t_hi(s):                                       # (the beam's cycles up to s, a rest at s counted)
            i = np.clip(np.searchsorted(xs, s, side='right') - 1, 0, 2 * n - 2); dxs = xs[i + 1] - xs[i]
            v = ys[i] + np.where(dxs > 1e-9, np.clip((s - xs[i]) / np.where(dxs > 1e-9, dxs, 1.0), 0.0, 1.0), 0.0) * (ys[i + 1] - ys[i])
            return np.where(s >= xs[-1], ys[-1], np.where(s < xs[0], ys[0], v))
        def t_lo(s):                                       # (.. a rest at s not counted)
            i = np.clip(np.searchsorted(xs, s, side='left'), 1, 2 * n - 1); dxs = xs[i] - xs[i - 1]
            v = ys[i - 1] + np.where(dxs > 1e-9, np.clip((s - xs[i - 1]) / np.where(dxs > 1e-9, dxs, 1.0), 0.0, 1.0), 1.0) * (ys[i] - ys[i - 1])
            return np.where(s <= xs[0], ys[0], np.where(s > xs[-1], ys[-1], v))
        dwell = lambda s: (t_hi(s + W / 2) - t_lo(s - W / 2)) / W
        mv = Ln > 1e-6; lg = mv & (Ln > 1.5 * W)
        def emit(m, f0, f1):                               # (the pieces m from the share f0 to f1 of their length)
            sm = S0[m] + Ln[m] * (f0 + f1) / 2.0; R = np.empty((int(m.sum()), 7))
            R[:, 0] = X0[m] + (X1[m] - X0[m]) * f0; R[:, 1] = Y0[m] + (Y1[m] - Y0[m]) * f0; R[:, 2] = X0[m] + (X1[m] - X0[m]) * f1; R[:, 3] = Y0[m] + (Y1[m] - Y0[m]) * f1
            R[:, 4] = B[m]; R[:, 5] = T[m] * (f1 - f0) * 8.0; R[:, 6] = T0[m] + T[m] * f0
            out_rows.append(R); out_light.append(Ln[m] * (f1 - f0) * bt2_light(Z[m], dwell(sm))); out_src.append(k[m])
        sh = mv & ~lg
        if sh.any(): emit(sh, 0.0, 1.0)
        if lg.any():
            h = (W / 2) / Ln[lg]; emit(lg, 0.0, h); emit(lg, h, 1.0 - h); emit(lg, 1.0 - h, 1.0)
        first = ~cont; last = np.concatenate([~cont[1:], [True]])
        for m, px, py, s, tt in ((first, X0, Y0, S0 - W / 4, T0), (last, X1, Y1, S1 + W / 4, T0 + T)):
            R = np.empty((int(m.sum()), 7)); R[:, 0] = R[:, 2] = px[m]; R[:, 1] = R[:, 3] = py[m]; R[:, 4] = B[m]; R[:, 5] = 8.0; R[:, 6] = tt[m]
            out_rows.append(R); out_light.append((W / 2) * bt2_light(Z[m], dwell(s[m]))); out_src.append(k[m])
    if not out_rows: return L[:0], np.zeros(0), (col[:0] if col is not None else None)
    src = np.concatenate(out_src)
    return np.concatenate(out_rows), np.concatenate(out_light), (col[src] if col is not None else None)
# The line build in C (native/vxbuild.c -> vxbuild.dll, native\build_win.bat): _build_np's beam trace - bt2_prepare, the
# tube's map, the vertex arrays - piece by piece, the same arithmetic (a picture of ~1700 pieces: 2.6 ms in numpy).
# Loaded once, when the first picture is built; not there, not loadable or VX_NO_NATIVE=1: the numpy way (macOS and
# Linux have no library yet). Its tables and constants are handed over from here
_vxb = None                                 # (None: not tried yet; False: the numpy way)
_vxb_lock = None
def _vxb_off(why):
    """the C line build switched off, said once"""
    global _vxb
    if _vxb is not False: print('line build: numpy (%s)' % why)
    _vxb = False
def _vxb_get():
    """the C line build's library with its tables set, or False"""
    global _vxb, _vxb_lock
    if _vxb is not None:
        return _vxb
    if _np is None or os.environ.get('VX_NO_NATIVE', '') not in ('', '0'):
        _vxb = False
        return _vxb
    try:
        import threading
        name = 'vxbuild.dll' if IS_WIN else ('libvxbuild.dylib' if sys.platform == 'darwin' else 'libvxbuild.so')
        path = os.path.join(getattr(sys, '_MEIPASS', os.path.dirname(os.path.abspath(__file__))), 'native', name)
        if not os.path.exists(path):
            raise OSError('no %s' % name)
        lib = ctypes.CDLL(path)
        vp, ci = ctypes.c_void_p, ctypes.c_int
        lib.vxb_tables.argtypes = [vp, ci, vp, vp, ci, vp, ci, vp, ci, vp, vp, vp, ci, ci]; lib.vxb_tables.restype = ci
        lib.vxb_pieces.argtypes = [vp, ci, ci, vp]; lib.vxb_pieces.restype = ci
        lib.vxb_load.argtypes = []; lib.vxb_load.restype = vp
        lib.vxb_fill.argtypes = [vp] * 7 + [ctypes.POINTER(ctypes.c_double)]; lib.vxb_fill.restype = ci
        try:                                    # (the cores it shares a picture's pieces out over; tests: VX_BUILD_THREADS=1)
            lib.vxb_threads.argtypes = [ci]; lib.vxb_threads.restype = ci
            if os.environ.get('VX_BUILD_THREADS'): lib.vxb_threads(int(os.environ['VX_BUILD_THREADS']))
        except AttributeError:
            pass
        try:                                    # (Smooth motion's work, native/vxsmooth.c: vectrex_emu.py sm_frame_c, sm_merge)
            cd = ctypes.c_double
            lib.vxs_frame.argtypes = [vp, ci, vp, ci, ci, cd, cd, cd, cd, ctypes.POINTER(cd), vp, ci, vp, ci, vp, vp, vp, vp, cd, cd, cd]
            lib.vxs_frame.restype = ci
            lib.vxs_pair.argtypes = [vp, vp]; lib.vxs_pair.restype = ci
            lib.vxs_merge.argtypes = [vp, ci, ci, vp]; lib.vxs_merge.restype = ci
            lib.vxs = True
        except AttributeError:                  # (an older library: the numpy way for that)
            lib.vxs = False
        try:                                    # (the hand-over's pairing: vectrex_emu.py hold_pairs)
            cd = ctypes.c_double
            lib.vxs_hold.argtypes = [vp, ci, vp, ci, ci, cd, cd, cd, cd, cd, vp, vp]; lib.vxs_hold.restype = ci
            lib.vxs_hold_ok = bool(lib.vxs)
        except AttributeError:
            lib.vxs_hold_ok = False
        f8 = lambda a: _np.ascontiguousarray(a, dtype=_np.float64)
        tl, kx, ky = f8(TUBE_LIGHT), f8([a_ for a_, b_ in BT2_TLK]), f8([b_ for a_, b_ in BT2_TLK])
        d, z, l_, tx, ty = f8(BT2_D), f8(BT2_Z), f8(BT2_L), f8(tubemap.DX), f8(tubemap.DY)
        p = lambda a: a.ctypes.data
        if lib.vxb_tables(p(tl), len(tl), p(kx), p(ky), len(kx), p(d), len(d), p(z), len(z), p(l_), p(tx), p(ty),
                          tubemap.NU, tubemap.NV) != _VXB_NP:
            raise ValueError('tables')
        _vxb_lock = threading.Lock()
        _vxb = lib
    except Exception as e:   # noqa
        _vxb_off(e)
    return _vxb
_VXB_NP = 99                              # (the parameters a picture: vxbuild.c's P_COUNT, the order in Renderer._build_c)
class _VxbOut:
    """the C line build's vertex arrays (V T C TT T2 T3: 68 floats a piece) in one block, a block used again once nothing holds any
    of its arrays any more - the picture gone from the young frames, the hand-over, the afterglow. A new block each picture cost
    its first touch: Vectrexians' 28000 pieces, 7.6 MB, 0.6 ms of the build's 2 ms just for Windows to hand out fresh memory.
    The arrays are the same; only where they lie is not new"""
    SPARE = 3                               # (blocks nothing holds, kept for the next pictures)
    def __init__(self):
        self.blocks = []                    # ([floats, array]: nothing else holding the array - its count as a new one's - it is free)
        b = [0, _np.empty(1, dtype=_np.float32)] if _np is not None else [0, object()]
        self.rc_free = sys.getrefcount(b[1])   # (the count of an array held by its pair alone, seen the same way)
    def arrays(self, n):
        """V, T, C, TT, T2, T3 for n pieces (numpy float32, contiguous: views of a block)"""
        need = 68 * n
        free = [b for b in self.blocks if sys.getrefcount(b[1]) == self.rc_free]   # (any view of it, any ctypes array on
        fit = [b for b in free if b[0] >= need]                                    #  one - the picture's - counts)
        if fit:
            blk = min(fit, key=lambda b: b[0])
        else:
            m = 68 * (n + n // 4 + 1024)    # (a little room: the next pictures are a little bigger or smaller)
            blk = [m, _np.empty(m, dtype=_np.float32)]; self.blocks.append(blk)
        rest = sorted((b for b in free if b is not blk), key=lambda b: b[0])
        if len(rest) > self.SPARE:          # (more free than kept: the smallest go)
            drop = rest[:len(rest) - self.SPARE]
            self.blocks = [b for b in self.blocks if not any(b is d for d in drop)]
        a = blk[1]; o = 0; out = []
        for k in (8, 16, 16, 4, 16, 8):
            out.append(a[o:o + k * n]); o += k * n
        return out
_vxb_out = _VxbOut()
BT_HALO_F, BT_HALO_S = 0.008, 2.5           # (the glass: this share of every piece's light spread wide - sigma, units: a flat, wide veil.
                                            #  Vectorblade's logo: 0.4-0.7 % of a line's level 2 units from it, and no band close to the line)
TINT_GLOW = True                          # (the vectors' colour by their profile, a white core with a blue glow - SAT_SHADER tm; the default since 2026-11-26)
BT_GLOW = (0.62, 0.80, 1.0)              # (the glow's tint against the core's - the owner, 2026-11-22: next to the tube the vectors looked greenish;
                                        #  the faint light - the lines' skirts, the glass's glow - bluer, the bright core the tube's white)
LIB_ART = 0.8                           # (the library's pictures at 80 % brightness - the owner: 70 % on 2026-11-22 (the eyes stay used to the vectors' level), 80 % on 2026-11-23)
BL_GAIN = 1.5; BL_WIDTH = 1.3             # (the Bright look: blook)
LOOK_S = {'vectrex': 0.0, 'medium': 0.5, 'bright': 1.0}   # (Tube > Look: its strength - Medium halfway by eye: the geometric mean of
                                                        #  the two, 1.22 x the light, 1.14 x the width; bgain / bwidth, the core's look_gain)
BT_SPOT = 1.25                              # (the spot's width x this: the lines' cross-section in the owner's photos, cut along the normal -
                                            #  2026-11-22, the owner: 1.85 matched the photos - their lens' blur with it; the BIOS's small text
                                            #  must show its rows as the tube does: 1.5; Last Human's smaller font too, but only barely (the owner): 1.25)
                                            #  Vectorblade's logo (intensity 79) 1.00 units at half height, 0.70 at 75 %, 1.30 at 25 %, 1.70 at 10 %:
                                            #  a Gaussian; the Dot Test's reference line (intensity 127) 1.03-1.16; ours 0.54 x this at 79)
BT_TINT = (0.65, 0.86, 1.0)                 # (the beam on the phosphor: one white for cores and glow alike - the tube's own, from the owner's
                                            #  photos with the camera's white balance (three sessions: red 0.62-0.66 of blue, green 0.83-0.89 as
                                            #  display values, lines and glow alike): a blueish white, blue full so no brightness is given away)
BT_KMAX = 512.0                             # (a piece's light goes to the GPU as a colour, clamped at 1: sent / BT_KMAX, SAT_SHADER x BT_KMAX)
SAT_SHADER = b"""
uniform sampler2D tex;
uniform float k;
uniform vec3 wb;
uniform vec3 wbc;
uniform float gs;                       // (the Bright look's lift taken out: the split by the Vectrex look's screen value)
uniform int tm;                         // (1: the vectors' colour by their profile - TINT_GLOW; libretro gpu_phos.c PH_FS_SAT tm 4)
uniform vec2 px;                        // (a texel)
uniform vec3 tcore, thalo;
uniform vec2 thr;
vec3 cold(vec3 c, vec3 wb) {               // the glow's tint in the faint light, the tube's white in the bright core (BT_GLOW)
    float m = max(c.r, max(c.g, c.b)) * gs;
    return c * mix(wb, wbc, smoothstep(0.35, 0.9, m));
}
float lum(vec2 uv) { return dot(max(texture2D(tex, uv).rgb, vec3(0.0)), vec3(1.0 / 3.0)); }
void main() {
    vec4 c = texture2D(tex, gl_TexCoord[0].st);
    vec3 l = max(c.rgb, vec3(0.0));          // (the pieces' light, added up: the phosphor's law is in each piece - bt2_light)
    if (tm == 1) {                           // (every vector the same at every brightness: a white core, a blue halo where
        vec2 uv = gl_TexCoord[0].st;         //  its light falls off - by the pixel's place in its own line's profile)
        float lc = dot(l, vec3(1.0 / 3.0)), e = 0.0;
        for (int i = -4; i <= 4; i += 2) { e = max(e, lum(uv + vec2(float(i) * px.x, 0.0))); e = max(e, lum(uv + vec2(0.0, float(i) * px.y))); }
        float r = lc / max(e, 1e-6), rid = 0.0, bv = -1.0;   // (a crest across the direction its light falls off most: a core)
        for (int j = 0; j < 4; j++) {
            vec2 d = j == 0 ? vec2(2.0, 0.0) : j == 1 ? vec2(0.0, 2.0) : j == 2 ? vec2(2.0, 2.0) : vec2(2.0, -2.0);
            float a = lum(uv + d * px), bb = lum(uv - d * px), dv = lc - min(a, bb);
            if (dv > bv) { bv = dv; rid = 1.0 - smoothstep(0.8, 1.0, max(a, bb) / max(lc, 1e-6)); }
        }
        rid *= step(0.05, r);
        gl_FragColor = vec4(pow(k * l, vec3(1.0 / 2.2)) * mix(thalo, tcore, max(smoothstep(thr.x, thr.y, r), rid)), c.a);
        return;
    }
    gl_FragColor = vec4(cold(pow(k * l, vec3(1.0 / 2.2)), wb), c.a);
}
"""

# the picture to the screen with a soft shoulder: linear up to 0.6, then rolling off towards 1, so
# bright slow shapes (the ship at full intensity) keep their detail instead of clipping white
TONE_SHADER = b"""
uniform sampler2D tex;
uniform float hk;
uniform float vl;                       // (the Vectrex look: the lines scaled in light by vl, clipping only at white / the peak)
uniform float bgh;                      // (Bright on an HDR screen: the ordinary screen's curve, x the screen's top; Medium: x its square
                                        //  root, the rest given back above it as the Vectrex look's, up to the top)
void main() {
    vec3 c = texture2D(tex, gl_TexCoord[0].st).rgb;
    if (bgh > 0.0) { vec3 d = c / bgh; vec3 e = 0.55 + 0.45 * (1.0 - exp(-(d - 0.55) / 0.6)); vec3 o = mix(d, e, step(0.55, d)) * bgh;
        if (hk > bgh) { vec3 x = max(c - o, 0.0); float m = hk - bgh; o += x / pow(1.0 + pow(x / m, vec3(8.0)), vec3(0.125)); }
        gl_FragColor = vec4(o, 1.0); return; }
    if (vl > 0.0) { vec3 l = pow(max(c, 0.0), vec3(2.2)) * vl; float p = pow(max(hk, 1.0), 2.2);
        l = l / pow(1.0 + pow(l / p, vec3(6.0)), vec3(1.0 / 6.0)); gl_FragColor = vec4(pow(l, vec3(1.0 / 2.2)), 1.0); return; }
    vec3 k = 0.55 + 0.45 * (1.0 - exp(-(c - 0.55) / 0.6));
    vec3 o = mix(c, k, step(0.55, c));
    if (hk > 1.0) { vec3 x = max(c - o, 0.0); float m = hk - 1.0;
        o += x / pow(1.0 + pow(x / m, vec3(8.0)), vec3(0.125)); }
    gl_FragColor = vec4(o, 1.0);
}
"""
# (an HDR screen, hk > 1: the light the shoulder took away given back above white, softly up to the screen's peak - as the
#  core's HDR: its knee as always, then up to the top; hk is the top as a screen value - 1.0 an ordinary screen, as before)

# the picture to an HDR screen (hdrwin.py): the screen values made linear by the exact sRGB curve (as Windows shows an
# ordinary picture), scaled to the SDR white Windows uses (scRGB: 1.0 = 80 nits) - above 1.0 the same curve: HDR
HDR_OUT_SHADER = b"""
uniform sampler2D tex;
uniform float sc;
void main() {
    vec3 v = max(texture2D(tex, gl_TexCoord[0].st).rgb, 0.0);
    vec3 l = mix(pow((v + 0.055) / 1.055, vec3(2.4)), v / 12.92, step(v, vec3(0.04045)));
    gl_FragColor = vec4(l * sc, 1.0);
}
"""

# the tube geometry (Tube > Tube geometry): where the beam lands on the owner's Vectrex - measured 2026-10-27 from the
# owner's exact trace of the Test Cartridge grid on a real tube (tubemap.py: the emulator's grid warped onto the traced
# one, every line within the trace's own precision). Strength (Tube geometry...): 0 = the straight part only (its size
# and place: overlays keep their place), 1 = the owner's tube. The shader no longer bends the picture (the lines are
# bent before they are drawn); tube_coeffs below is its identity.
# Real sizes (the owner's measurements, 2026-10-27): the overlay sheet 162 x 207 mm lies exactly on the case's matte
# frame (its outer edge, corners r 8.63 mm), the frame 3.5 mm wide; inside it the glossy walls down to the glass. The
# renderer's unit stays half a picture height: the sheet ART_TUBE_K units high = 103.5 mm, so 1 unit = MM_UNIT mm.
TUBE_TERMS = ((0, 0), (0, 1), (0, 2), (0, 3), (1, 0), (1, 1), (1, 2), (2, 0), (2, 1), (3, 0))   # (u power, v power)
# The glass's outline: the owner's traced edge where the glass meets the case (the blue line's centre), its radius every
# degree from the horizontal axis (0 .. 90, half picture heights), the four corners averaged (they agree within
# 0.19 mm), mirrored into all four. 145.7 x 188.5 mm, centred on the frame (within 0.06 mm).
GLASS_LUT = (
    0.65021, 0.65031, 0.65061, 0.65111, 0.65180, 0.65270, 0.65380, 0.65510, 0.65660, 0.65827,
    0.66006, 0.66200, 0.66407, 0.66634, 0.66886, 0.67156, 0.67442, 0.67754, 0.68090, 0.68446,
    0.68825, 0.69224, 0.69646, 0.70092, 0.70564, 0.71062, 0.71588, 0.72144, 0.72731, 0.73349,
    0.73996, 0.74677, 0.75393, 0.76145, 0.76935, 0.77766, 0.78638, 0.79551, 0.80504, 0.81492,
    0.82509, 0.83554, 0.84638, 0.85779, 0.86987, 0.88252, 0.89563, 0.90879, 0.92163, 0.93367,
    0.94436, 0.95326, 0.96003, 0.96443, 0.96651, 0.96643, 0.96426, 0.96040, 0.95554, 0.95002,
    0.94396, 0.93765, 0.93130, 0.92502, 0.91889, 0.91294, 0.90721, 0.90170, 0.89641, 0.89139,
    0.88664, 0.88217, 0.87800, 0.87412, 0.87052, 0.86714, 0.86393, 0.86089, 0.85802, 0.85538,
    0.85298, 0.85078, 0.84884, 0.84717, 0.84572, 0.84444, 0.84337, 0.84251, 0.84187, 0.84149,
    0.84136,
)
GLASS_AW, GLASS_AH = GLASS_LUT[0], GLASS_LUT[90]          # the glass's half width, half height (units)
GLASS_DX, GLASS_DY = 0.0, 0.0                             # (the glass, the frame and the overlay share one centre; the
                                                          #  picture sits off it by the map's own offset, tubemap.LIN)
# the room light's reflections and the phosphor layer behind the glass were made for the earlier glass outline: the
# shader looks them up in its coordinates (gsc: x and y scaled so the glass's edges meet), so they keep their look
GLASS_REF_AW, GLASS_REF_AH = 0.7114, 0.9889
GLASS_N = 7.17                          # (that outline's superellipse power: the room light's maps)
GLASS_EDGE = 0.025 * GLASS_REF_AH / GLASS_AH   # the soft edge (a share of the radius: the same ~2.8 mm as before)
GLASS_GREY = 0.20                       # the glass in room light without an overlay: grey x Room light (0 .. 1) -
                                        #  at the default hardly noticeable; with an overlay always black (contrast)
ART_TUBE_K = 0.924                      # the overlay sheet's half height, units (its 207 mm: the scale of everything)
MM_UNIT = 103.5 / ART_TUBE_K            # real mm per unit (112.0)
TUBE_SEG = 1200.0                       # (lines longer than this, emulator units, are bent in pieces)
TUBE_LIN = tubemap.LIN                  # the straight part: x = a u + b, y = c v + d (units)

# The glass's curve, seen straight on (Tube > Glass curvature): the owner's photo of the grid was taken straight on with
# a long lens, and the tube's face curves away from the camera towards its edges - so there the squares look narrower
# (left and right) and lower (top and bottom), as they do to a player in front of it. A point on the glass a distance s
# from the middle along it shows at R sin(s / R). Fitted to the traced grid (2026-10-29): the face curves more across
# than up and down - R 97 mm across (the columns' spread 8 % -> 3 %), 205 mm up and down (3 % -> 2.5 %). Strength g:
# 0 = as seen (the photo, the overlays' fit), 1 = the squares even (the projection undone: R / g). Separate from the
# analog circuits' geometry (the core) and the measured map (Tube geometry)
GLASS_RX_MM, GLASS_RY_MM = 97.0, 205.0
GLASS_T_MAX = 0.95                      # (past 95 % of the radius: straight on with the slope there - no infinity)

def glass_flat(X, Y, g, np=None):
    """the glass's projection undone with strength g: X, Y round the frame's centre (units, scalars or numpy)"""
    if g <= 0:
        return X, Y
    out = []
    for P, R0 in ((X, GLASS_RX_MM), (Y, GLASS_RY_MM)):
        R = R0 / g / MM_UNIT                          # (the radius in units)
        tm = GLASS_T_MAX; slope = 1.0 / math.sqrt(1 - tm * tm)
        if np is not None:
            t = P / R; tc = np.clip(t, -tm, tm)
            out.append(R * (np.arcsin(tc) + (t - tc) * slope))
        else:
            t = P / R; tc = min(max(t, -tm), tm)
            out.append(R * (math.asin(tc) + (t - tc) * slope))
    return out[0], out[1]

UW = ALG_MAX_X / float(ALG_MAX_Y)        # (half the canvas's width in half heights: the picture's side edge in u)
def unit_warp(w, u, v):
    """one Vectrex's picture shape (vectrex_emu.unit_warp: keystone, pincushion, linearity as fractions) on the ideal
    picture (u, v: half heights round its centre, v down) - before the tube's own map; floats or numpy arrays.
    Keystone: the width grows linearly down the picture (keyx = bottom against top), the height across it. Pincushion:
    the corners pulled out, so the sides bow in (+), or barrel (-): pinx the share at the corners. Linearity: the slope
    1 + linx at the right edge, 1 - linx at the left (one side stretched, the other squeezed), the same down"""
    kx, ky, px, py, lx, ly = w
    a, b = u / UW, v                                     # (-1..1 across and down)
    U = u * (1.0 + 0.5 * kx * b) * (1.0 + px * b * b) + 0.5 * lx * UW * a * a
    V = v * (1.0 + 0.5 * ky * a) * (1.0 + py * a * a) + 0.5 * ly * b * b
    return U, V

def tube_map(k, g=0.0, w=None):
    """the tube's map, emulator units -> emulator units (round the frame's centre at the canvas's middle): the
    straight part plus k x the measured imperfections, then the glass's curve undone with strength g. w: the unit's
    own picture shape first (unit_warp)"""
    la, lb, lc, ld = TUBE_LIN
    X0, Y0, U = ALG_MAX_X / 2.0, ALG_MAX_Y / 2.0, ALG_MAX_Y / 2.0
    imp = tubemap.imp
    def f(x, y):
        u = (x - X0) / U; v = (y - Y0) / U
        if w:
            u, v = unit_warp(w, u, v)
        dx, dy = imp(u, v) if k else (0.0, 0.0)
        X, Y = glass_flat(la * u + lb + k * dx, lc * v + ld + k * dy, g)
        return X0 + X * U, Y0 + Y * U
    return f

def tube_coeffs(k):
    """the shader's own bend (none now: the lines are bent before they are drawn): the identity"""
    return ([1.0 if t == (1, 0) else 0.0 for t in TUBE_TERMS], [1.0 if t == (0, 1) else 0.0 for t in TUBE_TERMS])
ART_PARALLAX = 0.008    # the overlay's parallax (Overlay > Parallax): the picture behind the glass this share of the picture
                        #  height x ART_TUBE_K higher than the sheet - the sheet itself stays on the case's frame (2026-10-28)
LIGHT_GLSL = b"""
// the room's light (see roomlight.py: the same geometry)
const float HZ = 0.80, CH = 2.4, FIX_W = 0.30, FIX_L = 1.2;
const vec3 LIGHTC = vec3(1.0, 0.94, 0.894);
uniform float lca, lsa;
float ceiling(vec2 p, float soft) {
    // (anti-aliased: every edge softened by at least this pixel's own size there - fwidth - so the far, squeezed
    //  cells are as smooth as the near ones; no early returns before the derivatives)
    float den = max(HZ - p.y, 0.05), vis = step(0.05, HZ - p.y);
    float D = CH / den, D0 = CH / HZ;
    float X = p.x * D / 2.0, Zr = D0 - D;
    float Xr = lca * X - lsa * Zr, Z = lsa * X + lca * Zr;
    float wx = soft + 0.8 * fwidth(Xr), wz = soft + 0.8 * fwidth(Z);
    float ins = (1.0 - smoothstep(FIX_W - wx, FIX_W + wx, abs(Xr))) * (1.0 - smoothstep(FIX_L - wz, FIX_L + wz, abs(Z)));
    float fr = max(smoothstep(FIX_W - 0.05 - wx, FIX_W - 0.05 + wx, abs(Xr)), smoothstep(FIX_L - 0.05 - wz, FIX_L - 0.05 + wz, abs(Z)));
    float cx = (Xr + FIX_W) / (2.0 * FIX_W) * 2.0, cz = (Z + FIX_L) / (2.0 * FIX_L) * 9.0;
    float fx = fract(cx), fz = fract(cz);
    float bx = min(wx / FIX_W, 0.45), bz = min(wz / 0.267, 0.45);
    float blade = max(smoothstep(0.44 - bx, 0.44 + bx, abs(fx - 0.5)), smoothstep(0.42 - bz, 0.42 + bz, abs(fz - 0.5)));
    float cell = 0.85 + 0.15 * pow(max(1.0 - abs(fx - 0.5) * 2.0, 0.0), 0.6);
    float val = mix(mix(cell, 0.18, blade), 0.12, fr);
    float fall = clamp(pow(D0 / D, 0.7), 0.75, 1.25) / 1.25;
    return vis * ins * val * fall;
}
vec2 reflp(vec2 uv, float curv) {
    vec3 n = normalize(vec3(curv * uv, 1.0));
    float dn = -n.z;
    vec3 r = vec3(-2.0 * dn * n.x, -2.0 * dn * n.y, -1.0 - 2.0 * dn * n.z);
    return uv * 0.9 + r.xy / r.z;
}
"""
CURVE_SHADER = b"""
uniform sampler2D tex, lmap;
uniform vec2 norm, ctr;
uniform float cx[10], cy[10];
uniform float pull, glass, aw, ah, gn, edge, grey, lk, bscale, pxs;
uniform vec2 goff, gsc;                  // (gsc: the glass's size / the earlier outline's - the room light's maps)
uniform float glut[91];                 // the glass's radius every degree (GLASS_LUT)
uniform vec3 lpos;
uniform vec4 lmr;
uniform vec2 hsh;                       // (head tracking: the room light's reflections moved, units - refl_shift)
uniform vec2 gsh;                       // (and the phosphor layer's glow moved on further, the maps' units - phos_shift)
uniform float cse, croom, crefl, cwear;  // the plastic case round the glass (casemodel): on, the room's light on it,
                                        //  its reflections' and its wear's strength (the deck's Case strip)
uniform float clut[91];                 // its opening's radius every degree (CASE_LUT)
uniform vec3 lc;                        // the ceiling light, cm (x right, y up, z to the viewer)
uniform sampler2D smap;                 // its scratches (casemodel.scratch_map): R the tube's glint, G the room's
uniform vec4 smr;                       // (the map's bounds: x0, x1, y0, y1)
""" + LIGHT_GLSL + b"""
float hash(vec2 p) {                                     // (no sine: no repeats or bands from lost precision)
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float bumps(vec2 p) { return (vnoise(p - 0.35) - vnoise(p + 0.35)) * 4.0; }   // (smooth bumps, lit from the top left)
// the outlines: a radius every degree, Catmull-Rom between them (smooth: the case's light shows no steps)
vec2 crom(float p0, float p1, float p2, float p3, float f) {             // (value, slope per degree)
    float a = -p0 + p2, b = 2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3, c = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
    return 0.5 * vec2(2.0 * p1 + f * (a + f * (b + f * c)), a + f * (2.0 * b + 3.0 * f * c));
}
int mi(int i) { return i < 0 ? -i : (i > 90 ? 180 - i : i); }           // (both outlines mirror at 0 and 90 degrees)
vec2 gR(float a) { float i = min(floor(a), 89.0); int k = int(i);
    return crom(glut[mi(k - 1)], glut[k], glut[k + 1], glut[mi(k + 2)], a - i); }
vec2 cR(float a) { float i = min(floor(a), 89.0); int k = int(i);
    return crom(clut[mi(k - 1)], clut[k], clut[k + 1], clut[mi(k + 2)], a - i); }
float sdist(vec2 d, vec2 R) {                                            // (signed distance from the outline, units)
    float dr = R.y * 57.29578;
    return (length(d) - R.x) * R.x / sqrt(R.x * R.x + dr * dr);
}
float ang(vec2 d) { return degrees(atan(abs(d.y), abs(d.x) + 1e-9)); }
float mfade(vec2 mc) {                                   // (the room light's maps read past their edge - head tracking: their
    vec2 o = max(-mc, mc - 1.0);                         //  edge carries on, fading out softly over a fifth of the map; inside
    float d = length(max(o, 0.0));                       //  nothing changes)
    return exp(-d * d / 0.012);
}
float sdG(vec2 d) { return sdist(d, gR(ang(d))); }
float sdC(vec2 d) {                                      // (the case's opening: a rounded rectangle, exact - casemodel's
    vec2 q = abs(d) - vec2(0.69188 - 0.045826, 0.89275 - 0.045826);   //  155 x 200 mm, corners r 5.13 mm: smooth corners)
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - 0.045826;
}
vec2 fwd(vec2 p, out mat2 J) {
    float u = p.x, v = p.y, u2 = u * u, v2 = v * v;
    float t[10]; float tu[10]; float tv[10];
    t[0] = 1.0;    tu[0] = 0.0;      tv[0] = 0.0;
    t[1] = v;      tu[1] = 0.0;      tv[1] = 1.0;
    t[2] = v2;     tu[2] = 0.0;      tv[2] = 2.0 * v;
    t[3] = v2 * v; tu[3] = 0.0;      tv[3] = 3.0 * v2;
    t[4] = u;      tu[4] = 1.0;      tv[4] = 0.0;
    t[5] = u * v;  tu[5] = v;        tv[5] = u;
    t[6] = u * v2; tu[6] = v2;       tv[6] = 2.0 * u * v;
    t[7] = u2;     tu[7] = 2.0 * u;  tv[7] = 0.0;
    t[8] = u2 * v; tu[8] = 2.0 * u * v; tv[8] = u2;
    t[9] = u2 * u; tu[9] = 3.0 * u2; tv[9] = 0.0;
    vec2 f = vec2(0.0); vec2 fu = vec2(0.0); vec2 fv = vec2(0.0);
    for (int i = 0; i < 10; i++) {
        f += vec2(cx[i], cy[i]) * t[i]; fu += vec2(cx[i], cy[i]) * tu[i]; fv += vec2(cx[i], cy[i]) * tv[i];
    }
    float a = max((1.0 + u / 0.85) * 0.5, 0.0), b = max((1.0 - v) * 0.5, 0.0);   // (the corner's pull)
    f.y -= pull * a * a * b * b;
    fu.y -= pull * a * b * b / 0.85;
    fv.y += pull * a * a * b;
    J = mat2(fu, fv);
    return f;
}
// ---- the plastic case (casemodel; the owner's previews 2026-10-24): what it shows at this point gd (from the
//      glass's centre, units, y down). Lengths below in "ref px": the 1839 px high picture of the previews.
const float U = 919.67, FRONT_W = 0.03125;        // (FRONT_W: casemodel's - 3.5 mm, the owner's measurement)
vec3 tex9(vec2 q, float rad) {                       // (the vectors round q (units), a soft ring rad ref px wide)
    vec2 o = vec2(rad / U) / norm, s = ctr + vec2(q.x, -q.y) / norm;
    vec3 a = texture2D(tex, s).rgb * 2.0;
    a += texture2D(tex, s + vec2(o.x, 0.0)).rgb + texture2D(tex, s - vec2(o.x, 0.0)).rgb;
    a += texture2D(tex, s + vec2(0.0, o.y)).rgb + texture2D(tex, s - vec2(0.0, o.y)).rgb;
    a += 0.7 * (texture2D(tex, s + o).rgb + texture2D(tex, s - o).rgb
              + texture2D(tex, s + vec2(o.x, -o.y)).rgb + texture2D(tex, s - vec2(o.x, -o.y)).rgb);
    return a / 8.8;
}
vec3 disc(vec2 q, float rad) {                       // (an even soft disc: a line becomes one solid glowing band,
    vec2 s = ctr + vec2(q.x, -q.y) / norm;           //  not its thin core and halo apart; 40 taps on a turned spiral)
    float rot = 6.2832 * hash(gl_FragCoord.xy);
    vec3 a = vec3(0.0); float ws = 0.0;
    for (int i = 0; i < 10; i++) {
        float f = (float(i) + 0.5) / 10.0, r = rad * sqrt(f), an = rot + 2.39996 * float(i);
        float w = exp(-1.5 * f);
        a += w * texture2D(tex, s + vec2(cos(an), sin(an)) * r / U / norm).rgb; ws += w;
    }
    return a / ws;
}
vec3 plastic(vec2 gd, vec2 tgt) {
    float pu = pxs;                                                      // (one window pixel, units)
    float dg = sdG(gd), dc = -sdC(gd);
    vec2 rp = gd * U;
    // (where the glossy wall meets the matte front the moulding is not perfect: the edge wanders 1 - 3 px)
    float de = dc + pu * 1.6 * (0.5 - 0.65 * vnoise(rp / 9.0 + 41.0) - 0.35 * vnoise(rp / 2.5 + 13.0));
    float wa = clamp(dg / pu + 0.5, 0.0, 1.0) * clamp(de / pu + 0.5, 0.0, 1.0);
    float fa = clamp(0.5 - de / pu, 0.0, 1.0);                           // (beyond: the matte outer frame)
    float wave = 2.0 * vnoise(rp / 110.0) - 1.0 + 0.5 * (2.0 * vnoise(rp / 40.0 + 17.0) - 1.0);
    float flow = 2.0 * vnoise(rp / 14.0 + 5.0) - 1.0;
    vec3 col = vec3(0.0);
    if (wa > 0.0) {
        // the wall in 3D (the owner's photos): straight lines from the case's straight outer edge (in front, H
        // towards the viewer) down to the glass's curved edge, one per direction from the centre - straight along
        // the outer edge, bending smoothly to meet the tube; its true direction everywhere (no seams)
        float th = atan(gd.y, gd.x), a = ang(gd), sg = sign(gd.x * gd.y);
        vec2 G = gR(a), C = cR(a);
        float Rg = G.x, D = max(C.x - G.x, 1e-4), rho = length(gd);
        float dRg = G.y * 57.29578 * sg, dD = (C.y - G.y) * 57.29578 * sg;
        float u = clamp((rho - Rg) / D, 0.0, 1.0);                       // (0 at the glass .. 1 at the outer edge)
        // its depth: ~1.2 cm at the sides and top / bottom; deeper where the wall is wider - the tube's face curves
        // back at its corners - so the corners stay steep and mirror the glass too (smooth max). Its angle as before
        // the measured geometry (2026-10-27: 68.5 deg at the sides, 65.5 top / bottom, 63.5 in the corners)
        float k = 1.9712 * D - 0.09864, q = sqrt(k * k + 0.0004);
        float H = 0.10123 + 0.5 * (k + q), dH = 0.9856 * (1.0 + k / q) * dD;
        float cs = cos(th), sn = sin(th);
        vec3 Pu = vec3(D * cs, D * sn, H);
        float dr = dRg + u * dD;
        vec3 Pt = vec3(dr * cs - rho * sn, dr * sn + rho * cs, u * dH);
        vec3 N = normalize(cross(Pt, Pu)); N *= sign(N.z);                // (x right, y down, z to the viewer)
        // (flat walls, the owner's grid photo 2026-11-20: each wall at one angle, square to the case's straight outer edge - the
        //  grid mirrored evenly, its lines straight; near the glass the wall follows the glass's curved edge)
        vec2 e2 = vec2(0.002, 0.0);
        vec2 qc = abs(gd) - vec2(0.69188 - 0.045826, 0.89275 - 0.045826), sgn = vec2(gd.x < 0.0 ? -1.0 : 1.0, gd.y < 0.0 ? -1.0 : 1.0);
        vec2 pc = normalize(exp(clamp((qc - max(qc.x, qc.y)) / 0.045, -30.0, 0.0))) * sgn;
        vec2 pgl = normalize(vec2(sdG(gd + e2.xy) - sdG(gd - e2.xy), sdG(gd + e2.yx) - sdG(gd - e2.yx)) + 1e-6);
        vec2 pd = normalize(mix(pgl, pc, smoothstep(0.0, 0.3 * pc.x * pc.x + 1e-3, u)));   // (only the side walls bend to the glass)
        float phi = radians(mix(65.5, 68.5, smoothstep(0.3, 0.9, abs(pd.x))));
        N = normalize(vec3(-pd * sin(phi), cos(phi)));
        vec2 n = normalize(gd);                                           // (outwards, in the picture)
        float wave2 = 2.0 * vnoise(rp / 70.0 + 31.0) - 1.0 + 0.4 * (2.0 * vnoise(rp / 26.0 + 9.0) - 1.0);
        N = normalize(N + vec3(0.006 * wave + 0.003 * flow, 0.006 * wave2 + 0.003 * flow, 0.0));   // (the moulding: nearly flat - the grid photo mirrors straight)
        float lip = smoothstep(0.93, 1.0, u);                                // (the rounded outer edge; at the glass a dark gap)
        N = normalize(mix(N, vec3(0.0, 0.0, 1.0), 0.85 * lip));
        float t = u;
        float dpos = (rho - Rg) * U, dout = max(de, 0.0) * U;
        // the vectors mirrored in it: the eye's ray off the wall onto the glass (nearest the sharpest, inverse square)
        vec3 P = vec3(tgt, H * u), E = vec3(0.0, 0.0, 50.0 / 11.0);
        vec3 Rr = reflect(normalize(P - E), N);
        vec3 refl = vec3(0.0); float gm = 0.0, back = 0.0;
        if (Rr.z < -0.02) {
            vec2 mq = P.xy + Rr.xy * (-P.z / Rr.z);
            back = length(mq - P.xy) * U;
            gm = clamp(0.5 - sdG(mq - goff) / pu, 0.0, 1.0);             // (only the glass's light)
            refl = max(disc(mq, 1.6) * gm - 0.02, 0.0);
        }
        float fall = 1.0 / (1.0 + pow(back / 130.0, 2.0));
        float side = abs(n.x), bot = max(n.y, 0.0), top = max(-n.y, 0.0);
        float strength = crefl * 1.5 * (0.14 * pow(side, 1.5) + 0.15 * bot + 0.14 * top) * fall * (1.0 + 0.06 * flow)
            * (1.0 - smoothstep(0.84, 0.94, u));   // (none on the rounded outer edge: it turns away from the screen - a real
                                                                 //  case shows no lines there; the owner's marks 2026-11-20)
        // the room's light: the same surface (y up for the light's cm)
        vec3 Nu = vec3(N.x, -N.y, N.z);
        vec3 Pc = vec3(tgt.x * 11.0, -tgt.y * 11.0, H * u * 11.0);
        vec3 V = normalize(vec3(0.0, 0.0, 50.0) - Pc), L = normalize(lc - Pc);
        float lit = croom * pow(clamp((dot(Nu, L) - 0.55) / 0.45, 0.0, 1.0), 1.4);  // (facing the light: bottom, right)
        float ndh = clamp(dot(Nu, normalize(V + L)), 0.0, 1.0);
        float spec = croom * 0.35 * pow(ndh, 60.0);
        float base = 0.006 + 0.002 * flow + 0.010 * lit + spec;
        // the glass's glare, a faint glow onto the plastic next to it
        float bl = 0.0;
        if (lk > 0.0) {
            vec2 eq = tgt - n * dpos / U;
            vec2 eqn = (eq - hsh) / gsc;                                     // (the maps' glass; head tracking: moved)
            vec2 mq = vec2((eqn.x - lmr.x) / (lmr.y - lmr.x), (eqn.y - lmr.z) / (lmr.w - lmr.z));
            float mr = texture2D(lmap, mq).r * mfade(mq);
            bl = lk * mr * (0.03 * exp(-dpos / 55.0) * (0.25 + 0.75 * pow(top, 0.7)) + 0.035 * exp(-dpos / 16.5) * pow(top, 0.7));
        }
        // the scratches: they glint where light crosses them - the tube's (nearby) and the room's
        vec2 sq = vec2((gd.x - smr.x) / (smr.y - smr.x), (smr.w - gd.y) / (smr.w - smr.z));
        vec2 sc = texture2D(smap, sq).rg;
        float lv = clamp(3.0 * dot(tex9(tgt - n * (dpos + 25.0) / U, 30.0), vec3(0.333)), 0.0, 1.0);
        float scr = cwear * 0.9 * (0.5 * sc.r * lv + sc.g * (0.6 * lit + 0.8 * bl * 30.0));
        col = vec3(base) + refl * strength * vec3(0.85, 0.92, 1.0) + 0.35 * scr * vec3(0.95, 0.97, 1.0)
            + bl * vec3(0.97, 0.98, 1.0) + exp(-pow(dout / 2.0, 2.0)) * 0.26 * lit;
        col *= mix(0.2, 1.0, smoothstep(0.0, 0.06, u));                 // (the dark gap where it meets the glass)
        col *= wa;
    }
    // beyond: 5 mm of the case's flat front, facing the viewer - very matte ABS with its fine sand texture
    float fr = fa * smoothstep(0.002, 0.007, FRONT_W + dc);   // (its outer edge: a soft fade over ~0.5 mm)
    if (fr > 0.0) {
        // (the owner's close-up: a mid grey, an even fine grain, tiny bright specks that catch the light - some a
        //  little coloured - and a slightly lighter rounded edge where it drops into the glossy wall)
        vec2 cell = floor(rp / 1.1);
        // (studio-equipment matte, like a Yamaha HS8: an even, very fine micro-grain - no bumps, no glitter)
        float grain = (vnoise(rp / 2.0 + 5.3) - 0.5) * 0.13 + (hash(cell) - 0.5) * 0.05;
        // (where the overlays slide in and scrape: a smoother, slightly shinier band halfway across, in faint
        //  lengthwise marks, its strength varying along the length and less in the corners)
        float acr = -de * U, mid = 0.5 * FRONT_W * U;                     // (ref px from the inner edge; the middle)
        float band = exp(-pow((acr - mid - 6.0 * (vnoise(rp / 300.0 + 4.0) - 0.5)) / 3.0, 2.0));
        float marks = 0.5 + 0.5 * vnoise(vec2(acr / 0.9, 0.0) + rp / 500.0);
        float cornw = 1.0 - 0.85 * exp(-pow((ang(gd) - 55.0) / 9.0, 2.0));
        float sidew = mix(1.0, 0.3, smoothstep(45.0, 65.0, ang(gd)));   // (mostly on the left and right, less top / bottom)
        float wear = min(1.0, cwear) * band * marks * cornw * sidew * smoothstep(0.58, 0.82, vnoise(rp / 340.0 + 17.0));   // (in stretches, longer gaps)
        grain *= 1.0 - 0.65 * wear;
        float mott = (vnoise(rp / 55.0 + 23.0) - 0.5) * 0.04;
        float tone = 0.010 + 0.09 * croom;
        float sp = hash(cell + 91.7), spark = step(0.982, sp) * (0.5 + hash(cell + 3.3));
        vec3 tint = mix(vec3(1.0, 0.92, 0.75), vec3(0.75, 0.9, 1.0), hash(cell + 7.9));
        float rim = exp(-pow(max(-de, 0.0) * U / 3.0, 2.0)) * 0.2;      // (the rounded edge into the wall)
        float gloss = smoothstep(0.2, 0.85, 0.6 * vnoise(rp / 420.0 + 83.0) + 0.4 * vnoise(rp / 900.0 + 37.0));   // (how shiny: smooth, over 10 - 20 cm)
        vec3 Pc = vec3(tgt.x * 11.0, -tgt.y * 11.0, 1.4);
        vec3 Hf = normalize(normalize(vec3(0.0, 0.0, 50.0) - Pc) + normalize(lc - Pc));
        float sheen = croom * (0.014 + 0.07 * gloss) * pow(Hf.z, 6.0) * (1.0 + 3.0 * grain);   // (a satin sheen, glinting on the grain)
        float drift = 0.85 + 0.3 * vnoise(rp / 700.0 + 29.0);              // (the plane's lightness: slow, smooth)
        sheen *= 0.2 + 1.6 * vnoise(rp / 600.0 + 74.0);                  // (the glint: slow, strong variation)
        sheen *= mix(1.2, 0.45, smoothstep(45.0, 65.0, ang(gd)) * clamp(-gd.y / 0.3, 0.0, 1.0));   // (more on the long sides, less on top)
        sheen += croom * 0.024 * wear * pow(Hf.z, 6.0);                  // (the scraped band: its own strength)
        col += (vec3(drift * tone * (1.0 + grain + mott + rim)) * vec3(0.98, 0.99, 1.02) + sheen
              + croom * (0.01 + 0.04 * gloss) * spark * tint) * fr;
    }
    return col + (hash(gl_FragCoord.xy) - hash(gl_FragCoord.xy + 7.31)) / 255.0;   // (dithered: no bands)
}
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec2 tgt = (uv - ctr) * norm; tgt.y = -tgt.y;          // (on the tube: half picture heights, y down)
    vec2 p = tgt; mat2 J;
    for (int i = 0; i < 6; i++) {                            // (where the ideal picture had this point)
        vec2 f = fwd(p, J);
        float det = J[0][0] * J[1][1] - J[1][0] * J[0][1];
        p -= (1.0 / det) * mat2(J[1][1], -J[0][1], -J[1][0], J[0][0]) * (f - tgt);
    }
    vec2 s = ctr + vec2(p.x, -p.y) / norm;
    vec3 c = (s.x < 0.0 || s.x > 1.0 || s.y < 0.0 || s.y > 1.0) ? vec3(0.0) : texture2D(tex, s).rgb;
    if (glass > 0.5) {                                       // (the glass ends: a superellipse)
        vec2 gd = tgt - goff;
        float ga = degrees(atan(abs(gd.y), abs(gd.x) + 1e-9));
        float r = length(gd) / gR(ga).x;                      // (Catmull-Rom: a smooth edge)
        float g = smoothstep(0.0, 1.0, clamp((1.0 - r) / edge, 0.0, 1.0));   // (a soft edge)
        vec3 add = vec3(0.0);
        vec2 tq = tgt / gsc;                                 // (the room light's maps: made for the earlier glass outline)
        if (grey > 0.0 || lk > 0.0) {                        // (the glass in the room's light: no overlay)
            float gr = bumps(gl_FragCoord.xy * bscale);      // (the phosphor layer's fine orange-peel bumps)
            add += grey * (1.0 - 0.25 * tq.y) * (1.0 + 0.005 * gr) * vec3(0.92, 0.95, 1.0);
            if (lk > 0.0) {
                vec2 tl = tq - hsh / gsc;                    // (head tracking: the reflection moved with the head)
                vec2 mc = vec2((tl.x - lmr.x) / (lmr.y - lmr.x), (tl.y - lmr.z) / (lmr.w - lmr.z));
                vec4 m = texture2D(lmap, mc) * mfade(mc);    // (moved past the map's edge: its edge fading out - clamped, the
                                                             //  edge row stretched to a shaft; cut short, a hard edge, 2026-10-29)
                vec2 tgl = tl - gsh;                         // (the phosphor layer's glow: inwards of the reflection by the
                vec2 mcg = vec2((tgl.x - lmr.x) / (lmr.y - lmr.x), (tgl.y - lmr.z) / (lmr.w - lmr.z));   //  glass's
                m.g = texture2D(lmap, mcg).g * mfade(mcg);                                           //  thickness where
                                                                                                     //  it is now)
                float sharp = ceiling((reflp(tl, 0.2) - lpos.xy) / lpos.z, 0.012);   // (a mirror; 1 % blur, anti-aliased)
                add += lk * (m.g * (1.0 + 0.018 * gr) * vec3(0.95, 0.96, 1.0) + 0.88 * (0.98 * sharp + m.r) * LIGHTC);   // (m.g: the phosphor's light, roomlight.maps)
            }
        }
        c = sqrt(c * c + add * add) * g;                    // (the room's light added as light, gamma 2: the phosphor's
                                                             //  faint light the same at any room light, 2026-11-15)
        if (cse > 0.5) c += plastic(gd, tgt);
    }
    gl_FragColor = vec4(c, 1.0);
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
GLOW_SMOOTH_S = 0.0                       # (the light it and the glints gather: not smoothed in time any more, 2026-11-22 - at 0.15 s
                                          #  the plastic's glow trailed 40-150 px behind a moving line; without it neither flickers
                                          #  while the vectors are steady: the Glow Test cart, the owner's choice)
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
ROOM_MAX = 2 * 0.0648                   # the Room light slider's right end (twice the default)
def light_level(r):
    """the room's light for the glass and the light's reflections: hardly any at the default, 1 at the slider's end"""
    return min(1.0, max(0.0, r / ROOM_MAX)) ** 3

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
uniform vec4 rlp;                       // (the room's light on the sheet: -, -, px per unit, strength)
uniform vec2 rpt[6];                    // (its area: six points, window px, y up)
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
    if (rlp.w > 0.0) {                                   // (the ceiling light on the sheet - an area, six points: every
        vec3 lcol = vec3(1.0, 0.94, 0.894) * mix(vec3(1.0), trans, 0.6);   //  scratch close to it, further out only the bigger)
        float bc = b.z / max(b.y, 1e-6), bs = b.w / max(b.y, 1e-6);
        for (int i = 0; i < 6; i++) {
            vec2 dv = (rpt[i] - gl_FragCoord.xy) / rlp.z; dv.y = -dv.y;
            float dl = length(dv * vec2(1.0 / 1.25, 1.0));
            float ph2 = atan(dv.y, dv.x + 1e-7), c2l = cos(2.0 * ph2), s2l = sin(2.0 * ph2);
            if (D > 0.0) {
                float acf = pow(clamp(0.5 - 0.5 * (a.y * c2l + a.z * s2l) / D, 0.0, 1.0), 2.0);
                c += rlp.w / 4.0 * (0.9 * exp(-dl / 0.05) + 0.3 * exp(-dl / 0.11)) * m * acf * 2.5 * lcol;
            }
            if (b.y > 0.0) {
                float acr = pow(clamp(0.5 - 0.5 * (bc * c2l + bs * s2l), 0.0, 1.0), 3.0);
                float farl = 0.5 * exp(-dl / 0.06) + 0.25 * exp(-dl / 0.10) + 0.18 * exp(-dl / 0.24) + 0.07 * exp(-dl / 0.55);
                c += rlp.w / 4.0 * farl * dd * acr * 6.0 * lcol;
            }
        }
    }
    // the damage cut through the colour layer at its widest: the tube's light comes through untinted (white)
    float cut = smoothstep(pen.x, pen.y, cc.z);
    c += cut * pen.z * (0.5 * near + 0.8 * mid + 0.6 * far) * (vec3(1.0) - trans * 0.5) + cut * room * 0.5;   // (it scatters the light around it)
    gl_FragColor = vec4(c, 1.0);
}
"""

# the room's light on an overlay (Light menu): a flat sheet - the fixture's mirror image a little blurred (plexiglass),
# the plastic brighter around it, and (mode 2, into the scratches' light) a soft glow their glints catch
OV_LIGHT_SHADER = b"""
uniform sampler2D art;
uniform float mode, k, wear;
uniform vec3 lpos;
uniform vec2 hsh;                                           // (head tracking: the reflection moved, units)
uniform vec4 pic;                                           // (the picture's centre x, y from the top, half height, window h)
""" + LIGHT_GLSL + b"""
void main() {
    float a = texture2D(art, gl_TexCoord[0].st).a;
    float sheet = step(0.004, a);                           // (on the sheet only)
    vec2 uv = vec2(gl_FragCoord.x - pic.x, (pic.w - gl_FragCoord.y) - pic.y) / pic.z - hsh;
    vec2 p = (uv * 0.9 - lpos.xy) / lpos.z;
    if (mode < 0.5) {
        vec2 c = lpos.xy / 0.9;
        vec2 d = (uv - c) * vec2(1.0 / 1.25, 1.0);
        float s = (70.0 + 110.0 * wear) / 917.0, d2 = dot(d, d);
        float glow = 0.30 * exp(-d2 / (2.0 * s * s)) + 0.05 * exp(-d2 / (18.0 * s * s));
        gl_FragColor = vec4(vec3(6.0 * glow * k * sheet), 1.0);
    } else if (mode < 1.5)
        gl_FragColor = vec4(LIGHTC * k * ceiling(p, 0.02) * sheet, 1.0);
    else {
        float d = length((uv - lpos.xy / 0.9) * vec2(1.0 / 1.25, 1.0));
        gl_FragColor = vec4(LIGHTC * k * ceiling(p, 0.15) * sheet, 1.0);
    }
}
"""

# the finished screen onto the window: it is drawn in 16-bit floats (the overlay's passes - filter, print, the
# room's light, its reflection - blend without rounding), here rounded to the window's 8 bits with a dither of
# less than one step, so soft light falls off smoothly, without bands
DITHER_SHADER = b"""
uniform sampler2D tex;
float h(vec2 p) { vec3 q = fract(vec3(p.xyx) * 0.1031); q += dot(q, q.yzx + 33.33); return fract((q.x + q.y) * q.z); }
void main() {
    vec3 c = texture2D(tex, gl_TexCoord[0].st).rgb;
    float n = h(gl_FragCoord.xy) - h(gl_FragCoord.xy + 17.31);     // (triangular, -1 .. 1 step)
    gl_FragColor = vec4(c + n / 255.0, 1.0);
}
"""

ART_SHADER = b"""
uniform sampler2D tex, scr;
uniform float tint, room, print_;
uniform vec2 ssz;
void main() {
    vec4 o = texture2D(tex, gl_TexCoord[0].st);
    float filt = step(0.004, o.a) * (1.0 - step(0.996, o.a)), opq = step(0.996, o.a);
    vec3 hue = o.rgb / max(max(max(o.r, o.g), o.b), 0.02);       // (the filter's colour at full strength)
    vec3 m = mix(vec3(1.0), hue * 1.15, tint * filt) * (1.0 - opq);
    vec3 d = texture2D(scr, gl_FragCoord.xy / ssz).rgb * m;      // the picture through the sheet
    vec3 L = o.rgb * (filt * room + opq * print_);               // the room's light on it
    gl_FragColor = vec4(sqrt(d * d + L * L), 1.0);              // (added as light, gamma 2 - not as screen values: the
}                                                                //  trail looks the same at any room light, 2026-11-15)
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
# The load: a picture's beam time x its intensity's share, from the core's lines as they come - each line once, not the
# pieces drawn (the beam trace adds a glass's-glow copy of every lit piece and a cap at each run's end: counted, they
# made the load ~1.9 x, screen 1 0.696). Its unit: the Test Cartridge's grid (screen 1) 0.366, as when the tube map was
# measured (tubemap: / 1.0172) - the line renderer then counted its resting spots' halos too, 1.056 x the lines alone
# (the Full Grid Tests the same 1.056).
BEND_LOAD_CYC = 30000.0 / 1.0556

def beam_load(L, k=1.0):
    """a picture's beam load for the brightness bending (BEND_LOAD_CYC): its lines' cycles x intensity share; L the
    core's lines (an array n x 7+, or tuples - those without energy left out); k: their times squeezed by that (Smooth
    motion's refresh) - the game frame's own load all the same"""
    k = k if k and k > 0 else 1.0
    if _np is not None and getattr(L, 'ndim', 0) == 2:
        return float(_np.sum(L[:, 5] / 8.0 * ((L[:, 4].astype(_np.int64) & 127) / 127.0))) / BEND_LOAD_CYC / k
    return sum((ln[5] / 8.0) * ((int(ln[4]) & 127) / 127.0) for ln in L if len(ln) > 5) / BEND_LOAD_CYC / k

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
PHOS_A = ((0.1404, 0.0454), (0.0582, 0.350))   # the measured afterglow (share, time constant s). The phone apps use another shape
                                                #  (gpu_phos.c vx_phone_trail: a quicker drop, a longer faint end - the owner, 2026-11-22;
                                                #  the PC's is right as it is)
TRAIL_K = 1.5                                   # its brightness on the screen (the owner, 2026-11-15: the trail on black as
                                                #  it looked through an overlay - the light now added as light everywhere)
PHOS_ACAP = 1.0                 # the afterglow's brightest: a line's (the corners' extra light only while lit - SHOW_SHADER)
# the afterglow's faint end lifted where no overlay is shown: g x (1 + A / (1 + g / G0)) - up to A x G0 more, reached already
# where it is faint. On black the trail so looks as it does through an overlay, whose plastic glow and scratches (added as
# screen values) lift it there; measured with the RetroArch core on Armor Attack's jeep, overlay on / off (the owner,
# 2026-11-19). The core lifts it always and adds the sheet's own light to the trail as light instead - the same look
TRAIL_LIFT_A, TRAIL_LIFT_G0 = 3.0, 0.00625

# A game may draw its picture in two halves, one on each frame (a menu that draws half its lines each frame: they share nothing
# with the one before and nearly everything with the one before that). On the tube both halves keep glowing;
# with a hold of one frame each half went dark between its redraws - the picture blinked. frame_keys: a frame's
# lit lines as a set of rounded endpoints; frame_alternates: this frame is the other half of the one before.
ALT_MAX_LINES = 6000                    # (denser frames - 3D UVMC2 games - are not checked: they draw whole pictures)
def frame_keys(lines):
    if _np is None or lines is None or len(lines) == 0 or len(lines) > ALT_MAX_LINES:
        return None
    try:
        A = getattr(lines, 'np', None)                    # (the core's frame: its array made already - not the tuples
        if A is None or len(A) != len(lines):            #  built and turned back into an array, ~1.5 ms a heavy frame)
            A = _np.asarray(lines, dtype=_np.float64)
        if A.ndim != 2 or A.shape[1] < 5:
            return None
        lit = (A[:, 4].astype(_np.int64) & 127) > 0
        q = (A[lit, :4] // 64).astype(_np.int64)
        return _np.unique(((q[:, 0] * 1024 + q[:, 1]) * 1024 + q[:, 2]) * 1024 + q[:, 3])
    except (ValueError, TypeError):
        return None
def frame_overlap(a, b):
    if a is None or b is None or len(a) == 0:
        return None
    return float(_np.isin(a, b, assume_unique=True).mean())
def frame_alternates(st, lines):
    """this frame's lines are the other half of the previous frame's picture (keeps the last two frames' keys on
    st): little in common with the previous frame, much with the one before"""
    k = frame_keys(lines)
    k1, k2 = getattr(st, 'keys_prev', None), getattr(st, 'keys_prev2', None)
    st.keys_prev2, st.keys_prev = k1, k
    o1, o2 = frame_overlap(k, k1), frame_overlap(k, k2)
    return o1 is not None and o2 is not None and o1 < 0.5 and o2 > o1 + 0.3


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
""" % (PHOS_A[0][0] * TRAIL_K, PHOS_A[0][1], PHOS_A[1][0] * TRAIL_K, PHOS_A[1][1])
# The phosphor in two layers (the owner, 2026-10-29: trails that overlap must add up, not cut each other):
#  - the lit layer (P, UPDATE_SHADER): a spot at full brightness while the beam's flash is held (hold()) - its light and
#    freshness e^-age. Past its hold it leaves this layer.
#  - the afterglow (G1, G2, GLOW_SHADER): the two parts of the measured afterglow (PHOS_A), each an energy per pixel that
#    fades at its own rate. Every flash excites them to the measured curve; overlapping flashes keep each other's light.
#    (One brightness and age per pixel before: a new line over an old trail took the trail's light onto its own,
#    faster, curve - the older copy faded early where a newer one had crossed it, the trail stepped and cut.)
# The screen shows the brighter of the two (SHOW_SHADER).
UPDATE_SHADER = b"""
uniform sampler2D oldt, fresh, relm;
uniform float kf, mul, rel, relon, rmin;
""" + PHOS_FUNC + b"""
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec4 o = texture2D(oldt, uv);
    o.a *= kf; o.rgb *= mul;
    if (o.a <= exp(-hold)) o = vec4(0.0);               // (past its hold: the afterglow layers carry it on)
    vec4 f = texture2D(fresh, uv);
    float fb = lum(f.rgb) * bright(f.a), ob = lum(o.rgb) * bright(o.a);
    // the hand-over (the screen's own rate without Smooth motion: _present, relm): the old copy of a line that moved, in the
    // picture its new copy is drawn in - where most of the light held here is that copy's, and lit a while ago (rmin: not a
    // line this game frame drew). It goes out where the new copy does not light the spot again, as at the end of its hold - the
    // lighting itself as without it (as a whole new picture's - all of the new light - the new copy's soft glow over the old
    // copy took its light off the afterglow there: Test Cart page 1's slow bars' trails 5 % fainter)
    float hm = relon > 0.5 ? lum(texture2D(relm, uv).rgb) : 0.0;
    bool ho = hm > 0.0 && o.a > 0.0 && o.a <= exp(-rmin) && hm >= 0.5 * lum(o.rgb);
    // the beam lights the spot again if the fresh light is about as bright as what is held there (only strictly
    // brighter: an unchanged line drawn again kept its first lighting's age and went dark at the hold - every still line
    // blinked; ~0.7: a line the wobble shifts a little is drawn again too)
    bool lit = f.a > 0.0 && fb > 0.0 && (rel > 0.5 || fb >= 0.7 * min(ob, 1.0));   // (a whole new picture: all of its light,
                                                        //  however faint - its soft glow kept only where brighter than
                                                        //  the copy before, the old copy's afterglow showed through: the
                                                        //  new line's glow cut, 2026-10-29)
    if (!lit && ho)                                     // (the hand-over: let go)
        o = vec4(0.0);
    if (!lit && rel > 0.5)                              // a whole new picture (Smooth motion, G-Sync) that did not draw
        o = vec4(0.0);                                  //  this spot again: no longer held - a line that moved (a rotor's
                                                        //  blade) flashes once, razor sharp, then only glows away; a
                                                        //  still line is drawn again in every picture (the owner,
                                                        //  2026-10-29)
    gl_FragColor = lit ? f : o;
}
"""
# the afterglow, one display frame on: each part fades (kd = e^-dt/tau, the power-off curve: mul); a flash that leaves
# the lit layer now (its hold over, or a whole new picture that did not draw it again) starts its afterglow here - the
# part's share at its age, added to what still glows (light adds up: a new line's glow over an old trail, two trails
# crossing). Started while the flash is still lit, a still line drawn again and again would add to itself. The light at
# most a line's: the beam's corners (it dwells there) fade as their line does - kept brighter, every copy a fast-turning
# shape left ended in dots (PHOS_ACAP)
GLOW_COMMON = b"""
uniform sampler2D oldp, newp;
uniform float mul, acap, hold, dt;
uniform vec2 amp, tau;                                  // (the two parts: fast, slow)
float lum(vec3 c) { return dot(c, vec3(0.3, 0.5, 0.2)); }
vec4 leaving(vec2 uv) {                                 // a flash that starts fading here now: its light, its age
    vec4 o = texture2D(oldp, uv), n = texture2D(newp, uv);
    if (!(o.a > 0.0 && -log(o.a) < hold)) return vec4(0.0);
    vec3 c = o.rgb * mul * min(1.0, acap / max(lum(o.rgb), 1e-4));
    if (n.a > 0.0 && -log(n.a) < hold) c = max(c - n.rgb, vec3(0.0));   // (taken over: the light it does not replace)
    return vec4(c, -log(o.a) + dt);
}
void excite(vec2 uv, out vec3 e1, out vec3 e2) {       // the excitation of both parts here, as a flash starts fading
    vec4 l = leaving(uv);
    e1 = l.rgb * amp.x * exp(-l.a / tau.x); e2 = l.rgb * amp.y * exp(-l.a / tau.y);
}
"""
# Ultra: both parts in full colour, written at once (two outputs)
GLOW_SHADER = GLOW_COMMON + b"""
uniform sampler2D oldg1, oldg2;
uniform vec2 kd;
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec3 e1, e2; excite(uv, e1, e2);
    vec3 g1 = texture2D(oldg1, uv).rgb * kd.x * mul + e1, g2 = texture2D(oldg2, uv).rgb * kd.y * mul + e2;
    vec4 n = texture2D(newp, uv);
    if (n.a > 0.0 && -log(n.a) < hold) {                // (lit again: its own afterglow under it goes - a late game
        g1 = max(g1 - n.rgb * amp.x, vec3(0.0));        //  frame let a still line start fading, lit again on top it
        g2 = max(g2 - n.rgb * amp.y, vec3(0.0));        //  flashed a quarter brighter for a moment)
    }
    gl_FragData[0] = vec4(g1, 1.0); gl_FragData[1] = vec4(g2, 1.0);
}
"""
# High, Medium, Low (and a driver without two outputs): one image - the fast part in colour, the slow part as brightness
# (its colour the fast part's, white without it: only a 3D Imager's colours fade to white in the slow tail)
GLOW_PACKED = GLOW_COMMON + b"""
uniform sampler2D oldg1;
uniform vec2 kd;
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec3 e1, e2; excite(uv, e1, e2);
    vec4 t = texture2D(oldg1, uv);
    vec3 g1 = t.rgb * kd.x * mul + e1; float g2 = t.a * kd.y * mul + lum(e2);
    vec4 n = texture2D(newp, uv);
    if (n.a > 0.0 && -log(n.a) < hold) {
        g1 = max(g1 - n.rgb * amp.x, vec3(0.0)); g2 = max(g2 - lum(n.rgb) * amp.y, 0.0);
    }
    gl_FragColor = vec4(g1, g2);
}
"""
# what the phosphor shows now: the lit layer's light while held, else the afterglow (alpha: the bloom weight)
SHOW_SHADER = b"""
uniform sampler2D tex, g1, g2;
uniform vec3 wb;
""" + PHOS_FUNC + b"""
uniform vec3 wbc;
uniform float acap, lifta;
vec3 cold(vec3 c, vec3 wb) {               // the white balance in the glow, pure white in the bright core: a cold white
    float m = max(c.r, max(c.g, c.b));      //  line that keeps the screen's full brightness (WB_CORE)
    return c * mix(wb, wbc, smoothstep(0.35, 0.9, m));
}
uniform float pkd;                          // (packed: GLOW_PACKED - not 'packed', a reserved word in Mesa's GLSL)
vec3 glow(vec2 uv) {                        // a pixel's afterglow (GLOW_SHADER: both parts; GLOW_PACKED: in one image)
    if (toff > 0.5) return vec3(0.0);
    vec4 t = texture2D(g1, uv);
    if (pkd < 0.5) return t.rgb + texture2D(g2, uv).rgb;
    float l = lum(t.rgb);
    return t.rgb + t.a * (l > 1e-5 ? t.rgb / l : vec3(1.0));
}
void main() {
    vec2 uv = gl_TexCoord[0].st;
    vec4 p = texture2D(tex, uv);
    vec3 lit = (p.a > 0.0 && -log(p.a) < hold) ? p.rgb : vec3(0.0);
    vec3 g = glow(uv);
    g *= 1.0 + lifta / (1.0 + g / 0.00625);   // (the faint end lifted with no overlay shown: TRAIL_LIFT_A, TRAIL_LIFT_G0)
    gl_FragColor = vec4(cold(lit + g, wb), 0.7);   // (they add up: a new line's glow over a trail - the brighter of the two
                                                   //  only, its glow ended in an edge there. White balance: see cold)
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
# knob with a position marker, OFF/ON above it, the volume wedge and VOL to its right; the RESET
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
# the Vectrex controller from above (a joystick in port 2): the case with the cable notch, the stick (rings), the four buttons
JOY_ICON = [
    [(54.6, 36.4), (100.6, 36.4), (101.5, 36.5), (102.3, 36.9), (103.0, 37.4), (103.6, 38.2), (103.9, 39.0), (104.0, 39.9), (104.0, 72.1), (103.9, 73.0), (103.6, 73.8), (103.0, 74.5), (102.3, 75.1), (101.5, 75.4), (100.6, 75.5), (-0.6, 75.5), (-1.5, 75.4), (-2.3, 75.1), (-3.0, 74.5), (-3.6, 73.8), (-3.9, 73.0), (-4.0, 72.1), (-4.0, 39.9), (-3.9, 39.0), (-3.6, 38.2), (-3.0, 37.4), (-2.3, 36.9), (-1.5, 36.5), (-0.6, 36.4), (45.4, 36.4)],
    [(45.4, 36.4), (45.6, 37.6), (46.0, 38.7), (46.7, 39.7), (47.7, 40.4), (48.8, 40.9), (50.0, 41.0), (51.2, 40.9), (52.3, 40.4), (53.3, 39.7), (54.0, 38.7), (54.4, 37.6), (54.6, 36.4)],
    [(48.4, 39.0), (48.4, 26.1), (48.6, 25.3), (49.2, 24.7), (50.0, 24.5), (50.8, 24.7), (51.4, 25.3), (51.6, 26.1), (51.6, 39.0)],
    [(27.9, 56.0), (27.6, 58.6), (26.6, 61.0), (25.0, 63.1), (22.9, 64.7), (20.4, 65.8), (17.8, 66.1), (15.2, 65.8), (12.7, 64.7), (10.6, 63.1), (9.0, 61.0), (8.0, 58.6), (7.7, 56.0), (8.0, 53.4), (9.0, 50.9), (10.6, 48.8), (12.7, 47.2), (15.2, 46.2), (17.8, 45.9), (20.4, 46.2), (22.9, 47.2), (25.0, 48.8), (26.6, 50.9), (27.6, 53.4), (27.9, 56.0)],
    [(25.3, 56.0), (25.0, 57.9), (24.3, 59.7), (23.1, 61.3), (21.5, 62.5), (19.7, 63.2), (17.8, 63.5), (15.9, 63.2), (14.1, 62.5), (12.5, 61.3), (11.3, 59.7), (10.6, 57.9), (10.3, 56.0), (10.6, 54.0), (11.3, 52.2), (12.5, 50.7), (14.1, 49.5), (15.9, 48.8), (17.8, 48.5), (19.7, 48.8), (21.5, 49.5), (23.1, 50.7), (24.3, 52.2), (25.0, 54.0), (25.3, 56.0)],
    [(21.0, 56.0), (20.9, 56.8), (20.6, 57.6), (20.1, 58.3), (19.4, 58.8), (18.6, 59.1), (17.8, 59.2), (17.0, 59.1), (16.2, 58.8), (15.5, 58.3), (15.0, 57.6), (14.7, 56.8), (14.6, 56.0), (14.7, 55.1), (15.0, 54.4), (15.5, 53.7), (16.2, 53.2), (17.0, 52.9), (17.8, 52.8), (18.6, 52.9), (19.4, 53.2), (20.1, 53.7), (20.6, 54.4), (20.9, 55.1), (21.0, 56.0)],
    [(50.7, 56.0), (50.4, 57.8), (49.5, 59.4), (48.0, 60.6), (46.3, 61.2), (44.5, 61.2), (42.8, 60.6), (41.3, 59.4), (40.4, 57.8), (40.1, 56.0), (40.4, 54.2), (41.3, 52.6), (42.8, 51.4), (44.5, 50.8), (46.3, 50.8), (48.0, 51.4), (49.5, 52.6), (50.4, 54.2), (50.7, 56.0)],
    [(49.1, 56.0), (48.7, 57.6), (47.7, 58.9), (46.2, 59.6), (44.6, 59.6), (43.1, 58.9), (42.1, 57.6), (41.7, 56.0), (42.1, 54.4), (43.1, 53.1), (44.6, 52.4), (46.2, 52.4), (47.7, 53.1), (48.7, 54.4), (49.1, 56.0)],
    [(65.6, 56.0), (65.3, 57.8), (64.4, 59.4), (63.0, 60.6), (61.3, 61.2), (59.4, 61.2), (57.7, 60.6), (56.3, 59.4), (55.4, 57.8), (55.1, 56.0), (55.4, 54.2), (56.3, 52.6), (57.7, 51.4), (59.4, 50.8), (61.3, 50.8), (63.0, 51.4), (64.4, 52.6), (65.3, 54.2), (65.6, 56.0)],
    [(64.0, 56.0), (63.7, 57.6), (62.6, 58.9), (61.2, 59.6), (59.5, 59.6), (58.1, 58.9), (57.0, 57.6), (56.7, 56.0), (57.0, 54.4), (58.1, 53.1), (59.5, 52.4), (61.2, 52.4), (62.6, 53.1), (63.7, 54.4), (64.0, 56.0)],
    [(80.6, 56.0), (80.3, 57.8), (79.4, 59.4), (77.9, 60.6), (76.2, 61.2), (74.4, 61.2), (72.7, 60.6), (71.2, 59.4), (70.3, 57.8), (70.0, 56.0), (70.3, 54.2), (71.2, 52.6), (72.7, 51.4), (74.4, 50.8), (76.2, 50.8), (77.9, 51.4), (79.4, 52.6), (80.3, 54.2), (80.6, 56.0)],
    [(79.0, 56.0), (78.6, 57.6), (77.6, 58.9), (76.1, 59.6), (74.5, 59.6), (73.0, 58.9), (72.0, 57.6), (71.6, 56.0), (72.0, 54.4), (73.0, 53.1), (74.5, 52.4), (76.1, 52.4), (77.6, 53.1), (78.6, 54.4), (79.0, 56.0)],
    [(95.5, 56.0), (95.2, 57.8), (94.3, 59.4), (92.9, 60.6), (91.2, 61.2), (89.3, 61.2), (87.6, 60.6), (86.2, 59.4), (85.3, 57.8), (85.0, 56.0), (85.3, 54.2), (86.2, 52.6), (87.6, 51.4), (89.3, 50.8), (91.2, 50.8), (92.9, 51.4), (94.3, 52.6), (95.2, 54.2), (95.5, 56.0)],
    [(93.9, 56.0), (93.6, 57.6), (92.5, 58.9), (91.1, 59.6), (89.4, 59.6), (88.0, 58.9), (86.9, 57.6), (86.6, 56.0), (86.9, 54.4), (88.0, 53.1), (89.4, 52.4), (91.1, 52.4), (92.5, 53.1), (93.6, 54.4), (93.9, 56.0)],
]
# the VecVoice (port 2): its rounded box with a speaker, sound waves to the right
VECLINK_ICON = [                                  # (the VecLink cable: a plug at each end, the cable between)
    [(0.0, 38.0), (22.0, 41.0), (22.0, 59.0), (0.0, 62.0), (0.0, 38.0)],
    [(100.0, 38.0), (78.0, 41.0), (78.0, 59.0), (100.0, 62.0), (100.0, 38.0)],
    [(22.0, 50.0), (25.5, 54.3), (29.0, 58.4), (32.5, 62.2), (36.0, 65.6), (39.5, 68.3), (43.0, 70.3), (46.5, 71.6), (50.0, 72.0), (53.5, 71.6), (57.0, 70.3), (60.5, 68.3), (64.0, 65.6), (67.5, 62.2), (71.0, 58.4), (74.5, 54.3), (78.0, 50.0)],
    [(6.0, 46.0), (6.0, 54.0)], [(11.0, 46.0), (11.0, 54.0)], [(16.0, 46.0), (16.0, 54.0)],
    [(84.0, 46.0), (84.0, 54.0)], [(89.0, 46.0), (89.0, 54.0)], [(94.0, 46.0), (94.0, 54.0)],
]

PORT2_ICON = [
    [(10.0, 18.0), (8.4, 18.2), (7.0, 18.8), (5.8, 19.8), (4.8, 21.0), (4.2, 22.4), (4.0, 24.0), (4.0, 76.0), (4.2, 77.6), (4.8, 79.0), (5.8, 80.2), (7.0, 81.2), (8.4, 81.8), (10.0, 82.0), (56.0, 82.0), (57.6, 81.8), (59.0, 81.2), (60.2, 80.2), (61.2, 79.0), (61.8, 77.6), (62.0, 76.0), (62.0, 24.0), (61.8, 22.4), (61.2, 21.0), (60.2, 19.8), (59.0, 18.8), (57.6, 18.2), (56.0, 18.0), (10.0, 18.0)],
    [(46.0, 50.0), (45.6, 53.4), (44.3, 56.5), (42.2, 59.2), (39.5, 61.3), (36.4, 62.6), (33.0, 63.0), (29.6, 62.6), (26.5, 61.3), (23.8, 59.2), (21.7, 56.5), (20.4, 53.4), (20.0, 50.0), (20.4, 46.6), (21.7, 43.5), (23.8, 40.8), (26.5, 38.7), (29.6, 37.4), (33.0, 37.0), (36.4, 37.4), (39.5, 38.7), (42.2, 40.8), (44.3, 43.5), (45.6, 46.6), (46.0, 50.0)],
    [(37.0, 50.0), (36.5, 52.0), (35.0, 53.5), (33.0, 54.0), (31.0, 53.5), (29.5, 52.0), (29.0, 50.0), (29.5, 48.0), (31.0, 46.5), (33.0, 46.0), (35.0, 46.5), (36.5, 48.0), (37.0, 50.0)],
    [(72.5, 38.7), (73.6, 40.4), (74.4, 42.2), (75.1, 44.1), (75.6, 46.0), (75.9, 48.0), (76.0, 50.0), (75.9, 52.0), (75.6, 54.0), (75.1, 55.9), (74.4, 57.8), (73.6, 59.6), (72.5, 61.3)],
    [(80.8, 33.1), (82.3, 35.6), (83.6, 38.3), (84.7, 41.1), (85.4, 44.0), (85.9, 47.0), (86.0, 50.0), (85.9, 53.0), (85.4, 56.0), (84.7, 58.9), (83.6, 61.7), (82.3, 64.4), (80.8, 66.9)],
    [(89.0, 27.4), (91.1, 30.8), (92.8, 34.4), (94.2, 38.2), (95.2, 42.1), (95.8, 46.0), (96.0, 50.0), (95.8, 54.0), (95.2, 57.9), (94.2, 61.8), (92.8, 65.6), (91.1, 69.2), (89.0, 72.6)],
]
# the VecVox (port 2): a head in profile, speaking (the SpeakJet: the more advanced module)
VOICE_ICON = [
    [(24.0, 96.0), (24.1, 95.0), (24.2, 93.8), (24.4, 92.3), (24.7, 90.7), (24.9, 88.9), (25.1, 87.0), (25.2, 85.2), (25.2, 83.3), (25.2, 81.6), (25.0, 80.0), (24.7, 78.5), (24.2, 77.1), (23.6, 75.7), (23.0, 74.4), (22.2, 73.0), (21.5, 71.6), (20.8, 70.3), (20.1, 68.9), (19.5, 67.5), (19.0, 66.0), (18.5, 64.5), (18.1, 63.0), (17.7, 61.4), (17.2, 59.8), (16.9, 58.2), (16.6, 56.6), (16.3, 55.0), (16.1, 53.4), (16.0, 51.7), (16.0, 50.0), (16.1, 48.3), (16.2, 46.5), (16.4, 44.6), (16.7, 42.8), (17.1, 40.9), (17.5, 39.0), (18.0, 37.2), (18.6, 35.4), (19.2, 33.6), (20.0, 32.0), (20.9, 30.4), (21.8, 28.8), (22.9, 27.2), (24.0, 25.7), (25.2, 24.2), (26.5, 22.8), (27.9, 21.4), (29.2, 20.2), (30.6, 19.0), (32.0, 18.0), (33.4, 17.1), (35.0, 16.3), (36.6, 15.6), (38.2, 14.9), (39.9, 14.4), (41.6, 13.9), (43.2, 13.6), (44.9, 13.3), (46.5, 13.1), (48.0, 13.0), (49.5, 13.0), (51.0, 13.1), (52.5, 13.3), (54.0, 13.6), (55.5, 14.0), (56.9, 14.5), (58.3, 15.0), (59.6, 15.6), (60.9, 16.3), (62.0, 17.0), (63.1, 17.8), (64.1, 18.7), (65.0, 19.8), (65.9, 20.8), (66.8, 22.0), (67.5, 23.2), (68.2, 24.4), (68.9, 25.6), (69.5, 26.8), (70.0, 28.0), (70.4, 29.2), (70.7, 30.3), (70.9, 31.5), (71.0, 32.7), (71.1, 33.9), (71.2, 35.2), (71.3, 36.4), (71.4, 37.6), (71.7, 38.8), (72.0, 40.0), (72.5, 41.2), (73.1, 42.5), (73.9, 43.9), (74.7, 45.2), (75.5, 46.5), (76.3, 47.8), (77.0, 49.0), (77.5, 50.1), (77.9, 51.1), (78.0, 52.0), (77.9, 52.7), (77.5, 53.3), (76.9, 53.8), (76.1, 54.1), (75.3, 54.4), (74.5, 54.7), (73.7, 55.0), (72.9, 55.3), (72.4, 55.6), (72.0, 56.0), (71.8, 56.5), (71.8, 57.0), (71.9, 57.5), (72.0, 58.0), (72.2, 58.5), (72.5, 59.0), (72.7, 59.5), (72.9, 60.0), (73.0, 60.5), (73.0, 61.0), (72.9, 61.4), (72.7, 61.8), (72.5, 62.2), (72.3, 62.6), (72.0, 62.9), (71.7, 63.3), (71.5, 63.7), (71.3, 64.1), (71.1, 64.5), (71.0, 65.0), (71.0, 65.5), (71.1, 66.1), (71.3, 66.7), (71.5, 67.3), (71.8, 67.9), (72.0, 68.6), (72.1, 69.2), (72.2, 69.8), (72.2, 70.4), (72.0, 71.0), (71.7, 71.6), (71.3, 72.1), (70.8, 72.7), (70.3, 73.3), (69.7, 73.8), (69.0, 74.3), (68.3, 74.8), (67.6, 75.3), (66.8, 75.7), (66.0, 76.0), (65.1, 76.2), (64.1, 76.2), (63.0, 76.0), (61.8, 75.8), (60.6, 75.6), (59.5, 75.5), (58.4, 75.5), (57.4, 75.7), (56.6, 76.2), (56.0, 77.0), (55.6, 78.3), (55.4, 80.0), (55.3, 82.0), (55.3, 84.2), (55.4, 86.6), (55.5, 88.9), (55.7, 91.1), (55.8, 93.1), (56.0, 94.8), (56.0, 96.0)],
    [(58.0, 38.0), (58.6, 37.9), (59.4, 37.6), (60.3, 37.4), (61.3, 37.1), (62.2, 37.0), (63.0, 37.0), (63.7, 37.2), (64.3, 37.5), (64.8, 37.9), (65.3, 38.4), (65.7, 38.7), (66.0, 39.0)],
    [(82.6, 59.5), (83.0, 60.2), (83.4, 60.9), (83.6, 61.6), (83.8, 62.4), (84.0, 63.2), (84.0, 64.0), (84.0, 64.8), (83.8, 65.6), (83.6, 66.4), (83.4, 67.1), (83.0, 67.8), (82.6, 68.5)],
    [(88.4, 55.5), (89.2, 56.8), (89.8, 58.2), (90.3, 59.6), (90.7, 61.0), (90.9, 62.5), (91.0, 64.0), (90.9, 65.5), (90.7, 67.0), (90.3, 68.4), (89.8, 69.8), (89.2, 71.2), (88.4, 72.5)],
    [(94.2, 51.6), (95.3, 53.5), (96.3, 55.4), (97.0, 57.5), (97.6, 59.6), (97.9, 61.8), (98.0, 64.0), (97.9, 66.2), (97.6, 68.4), (97.0, 70.5), (96.3, 72.6), (95.3, 74.5), (94.2, 76.4)],
]
# the 3D Imager (port 2): the goggles, the colour disc on top - its black half hatched, its sectors
IMAGER_ICON = [
    [(80.0, 38.0), (79.6, 42.7), (78.5, 47.3), (76.7, 51.6), (74.3, 55.6), (71.2, 59.2), (67.6, 62.3), (63.6, 64.7), (59.3, 66.5), (54.7, 67.6), (50.0, 68.0), (45.3, 67.6), (40.7, 66.5), (36.4, 64.7), (32.4, 62.3), (28.8, 59.2), (25.7, 55.6), (23.3, 51.6), (21.5, 47.3), (20.4, 42.7), (20.0, 38.0), (20.4, 33.3), (21.5, 28.7), (23.3, 24.4), (25.7, 20.4), (28.8, 16.8), (32.4, 13.7), (36.4, 11.3), (40.7, 9.5), (45.3, 8.4), (50.0, 8.0), (54.7, 8.4), (59.3, 9.5), (63.6, 11.3), (67.6, 13.7), (71.2, 16.8), (74.3, 20.4), (76.7, 24.4), (78.5, 28.7), (79.6, 33.3), (80.0, 38.0)],
    [(54.0, 38.0), (53.7, 39.5), (52.8, 40.8), (51.5, 41.7), (50.0, 42.0), (48.5, 41.7), (47.2, 40.8), (46.3, 39.5), (46.0, 38.0), (46.3, 36.5), (47.2, 35.2), (48.5, 34.3), (50.0, 34.0), (51.5, 34.3), (52.8, 35.2), (53.7, 36.5), (54.0, 38.0)],
    [(50, 8), (50, 68)],
    [(50, 38), (76, 23)],
    [(50, 38), (76, 53)],
    [(30, 15), (42, 34)],
    [(24, 26), (38, 42)],
    [(22, 40), (36, 50)],
    [(28, 56), (40, 58)],
    [(6.0, 66.0), (6.3, 63.6), (7.1, 61.6), (8.5, 60.0), (10.4, 58.9), (12.9, 58.2), (16.0, 58.0), (84.0, 58.0), (87.1, 58.2), (89.6, 58.9), (91.5, 60.0), (92.9, 61.6), (93.7, 63.6), (94.0, 66.0), (94.0, 84.0), (93.7, 86.4), (92.9, 88.4), (91.5, 90.0), (89.6, 91.1), (87.1, 91.8), (84.0, 92.0), (62.0, 92.0), (60.1, 91.8), (58.4, 91.3), (57.0, 90.5), (55.8, 89.3), (54.8, 87.8), (54.0, 86.0), (52.7, 84.3), (51.3, 83.3), (50.0, 83.0), (48.7, 83.3), (47.3, 84.3), (46.0, 86.0), (45.2, 87.8), (44.2, 89.3), (43.0, 90.5), (41.6, 91.3), (39.9, 91.8), (38.0, 92.0), (16.0, 92.0), (12.9, 91.8), (10.4, 91.1), (8.5, 90.0), (7.1, 88.4), (6.3, 86.4), (6.0, 84.0), (6.0, 66.0)],
    [(40.0, 74.0), (39.7, 75.8), (38.9, 77.5), (37.6, 79.0), (35.9, 80.3), (33.8, 81.2), (31.4, 81.8), (29.0, 82.0), (26.6, 81.8), (24.2, 81.2), (22.1, 80.3), (20.4, 79.0), (19.1, 77.5), (18.3, 75.8), (18.0, 74.0), (18.3, 72.2), (19.1, 70.5), (20.4, 69.0), (22.1, 67.7), (24.2, 66.8), (26.6, 66.2), (29.0, 66.0), (31.4, 66.2), (33.8, 66.8), (35.9, 67.7), (37.6, 69.0), (38.9, 70.5), (39.7, 72.2), (40.0, 74.0)],
    [(82.0, 74.0), (81.7, 75.8), (80.9, 77.5), (79.6, 79.0), (77.9, 80.3), (75.8, 81.2), (73.4, 81.8), (71.0, 82.0), (68.6, 81.8), (66.2, 81.2), (64.1, 80.3), (62.4, 79.0), (61.1, 77.5), (60.3, 75.8), (60.0, 74.0), (60.3, 72.2), (61.1, 70.5), (62.4, 69.0), (64.1, 67.7), (66.2, 66.8), (68.6, 66.2), (71.0, 66.0), (73.4, 66.2), (75.8, 66.8), (77.9, 67.7), (79.6, 69.0), (80.9, 70.5), (81.7, 72.2), (82.0, 74.0)],
]
# the light pen (port 2): the pen, its tip, its cord and the light at its tip
LPEN_ICON = [
    [(78.0, 8.0), (92.0, 22.0), (36.0, 78.0), (22.0, 64.0), (78.0, 8.0)],
    [(22.0, 64.0), (12.0, 88.0), (36.0, 78.0)],
    [(15.0, 81.0), (19.0, 85.0)],
    [(85.0, 15.0), (87.6, 12.5), (90.1, 10.6), (92.3, 9.1), (94.2, 8.2), (96.0, 7.9), (97.6, 8.1), (98.9, 8.8), (100.0, 10.0)],
    [(60.0, 30.0), (70.0, 40.0)],
    [(2.0, 96.0), (8.0, 92.0)],
    [(4.0, 100.0), (10.0, 94.0)],
]
CART_ICON = [[(3, 0), (97, 0), (97, 37), (100, 40), (100, 99), (99, 100), (1, 100), (0, 99), (0, 40), (3, 37), (3, 0)],
             [(6, 0), (6, 36.5)], [(94, 0), (94, 36.5)], [(3, 36.5), (97, 36.5)],
             [(50, 6), (60, 14), (55, 14), (55, 20), (45, 20), (45, 14), (40, 14), (50, 6)],
             [(9, 44), (91, 44), (94, 47), (94, 93), (91, 96), (9, 96), (6, 93), (6, 47), (9, 44)]] + _TICKS + LOGO_ICON
# the load button: the slot (its opening as wide as the cartridge's insert end), an arrow into it, the
# cartridge 60 below - drawn turned 90 degrees left (the slot on the left, as on the Vectrex's side)
LOAD_ICON = [[(-2, 0), (102, 0), (102, 18), (97, 18), (97, 22), (3, 22), (3, 18), (-2, 18), (-2, 0)],
             [(50, 52), (50, 30)], [(43, 37), (50, 30), (57, 37)]] +             [[(u, v + 60) for u, v in line] for line in CART_ICON]
METER_K = 0.7           # the OSD's CPU / GPU meters: rows below its text, this much smaller
METER_ROWS = 0.25 + 2 * METER_K   # (the room they take, in text rows)
METER_THIN_H = 4.0      # (a low window: the meters as two thin lines under the text - the room they take, in text units)
OSD_SCALE = 0.3         # the window OSD's text: 30 % of what would fill the strip (the owner)
KNOB_SWEEP = 150.0      # degrees either side of 12 o'clock: 0 % to 100 %
# The window round the glass: the plastic case (its walls and 5 mm of its flat front, casemodel) and FRAME_M of black,
# the same left, right and above, and below it when there is no strip; centred on the glass, so the picture sits
# a little left of the window's middle (PIC_DX) and its top may be above the window's (PAD_TOP < 0).
ART_AW = 162.0 / 207.0 * ART_TUBE_K                                     # the overlay's half width, units (81 mm)
FRAME_X = casemodel.CASE_LUT[0] + casemodel.FRONT_W                      # (the case's outer edge from the glass's centre,
FRAME_Y = casemodel.CASE_LUT[90] + casemodel.FRONT_W                     #  half picture heights: its walls and matte front)
FRAME_M = 0.009 * 0.924 / 1.094                                         # (the black round it: the same on every side - as large
                                                                        #  a share of the case as before the real sizes, 2026-10-28)
GLASS_PAD = (FRAME_X + FRAME_M - GLASS_AW) / 2
VIEW_AW = FRAME_X + FRAME_M
PIC_DX = -GLASS_DX / 2                                                  # (picture centre - window centre, picture heights)
PAD_TOP = (FRAME_Y + FRAME_M) / 2 - 0.5 - GLASS_DY / 2
PAD_BOT = (FRAME_Y + FRAME_M) / 2 - 0.5 + GLASS_DY / 2                  # (the strip starts below this)
KNOB_STRIP = 3.7 * KNOB_R   # the strip below the picture: a little black space, the labels, the knob, black space

class GLBloomRenderer(GLRenderer):
    """The phosphor on the graphics card. The picture lives in a phosphor image (16-bit float,
    see PHOS_A): every spot is lit by the beam and fades as measured on a real Vectrex."""
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
        gl.glTranslatef.argtypes = [ctypes.c_float] * 3          # (head tracking: the glowing picture moved, _finish)
        self.Uniform1f = proc('glUniform1f', None, i, ctypes.c_float)
        self.Uniform1fv = proc('glUniform1fv', None, i, i, ctypes.c_void_p)
        self.Uniform3f = proc('glUniform3f', None, i, ctypes.c_float, ctypes.c_float, ctypes.c_float)
        self.Uniform2fv = proc('glUniform2fv', None, i, i, ctypes.c_void_p)
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
        try:
            self.covprog = program(FRESH_COV, 'fresh coverage'); self.avgprog = program(FRESH_NORM, 'fresh average')
            self.u_c = GetUniform(self.covprog, b'tex'); self.u_ck = GetUniform(self.covprog, b'ck'); self.u_n = (GetUniform(self.avgprog, b'tex'), GetUniform(self.avgprog, b'cnt'))
        except Exception:   # noqa: (without them: added up, as before; names of their own - cprog is the capsules')
            self.covprog = None
        try:
            self.downprog = program(DOWN_SHADER, 'down4')   # (its own name: dprog is the dither's - it took this one's place,
            self.u_down = (GetUniform(self.downprog, b'tex'), GetUniform(self.downprog, b'sp'))   #  2026-10-29)
        except Exception:   # noqa: (without it: the straight copy, as before)
            self.downprog = None
        self.cprog = program(CAPSULE_SHADER, 'capsule')
        self.u_premul = GetUniform(self.cprog, b'premul')
        self.u_t = {k: GetUniform(self.cprog, k.encode()) for k in ('timed', 'age0', 'dtw', 'gain', 'beam', 'hsg', 'wb', 'wbc', 'fcs')}
        self.satprog = program(SAT_SHADER, 'beam trace phosphor')
        self.u_sat = {k: GetUniform(self.satprog, k.encode()) for k in ('tex', 'k', 'wb', 'wbc', 'gs', 'tm', 'px', 'tcore', 'thalo', 'thr')}
        self.ClientActiveTexture = proc('glClientActiveTexture', None, u)
        self.MultiTexCoord4f = proc('glMultiTexCoord4f', None, u, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float)
        self.ActiveTexture = proc('glActiveTexture', None, u)
        try: self.BlendColor = proc('glBlendColor', None, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float)
        except Exception: self.BlendColor = None   # noqa
        try: self.GenMip = proc('glGenerateMipmap', None, u)    # (the overlay's smaller copies: see set_art)
        except Exception: self.GenMip = None   # noqa
        self.BlendEqSep = proc('glBlendEquationSeparate', None, u, u)
        self.BlendFuncSep = proc('glBlendFuncSeparate', None, u, u, u, u)
        self.uprog = program(UPDATE_SHADER, 'phosphor update')
        self.u_up = {k: GetUniform(self.uprog, k.encode()) for k in ('oldt', 'fresh', 'kf', 'mul', 'hold', 'tscale', 'toff', 'rel',
                                                                     'relm', 'relon', 'rmin')}
        GK = ('oldg1', 'oldg2', 'oldp', 'newp', 'kd', 'mul', 'amp', 'tau', 'acap', 'hold', 'dt')
        try:                                            # (Ultra: both afterglow parts at once - two outputs)
            self.DrawBuffers = proc('glDrawBuffers', None, i, ctypes.POINTER(u))
            self.glowprog = program(GLOW_SHADER, 'phosphor afterglow')   # (its own name: gprog is taken)
            self.u_gl = {k: GetUniform(self.glowprog, k.encode()) for k in GK}
        except Exception as e:   # noqa: (without: the packed afterglow everywhere)
            print('afterglow in full colour unavailable (%s): packed' % e); self.glowprog = None
        self.glowpack = program(GLOW_PACKED, 'phosphor afterglow, packed')
        self.u_gp = {k: GetUniform(self.glowpack, k.encode()) for k in GK}
        self.sprog = program(SHOW_SHADER, 'phosphor show')
        self.u_stex = GetUniform(self.sprog, b'tex')
        self.u_sh = {k: GetUniform(self.sprog, k.encode()) for k in ('hold', 'tscale', 'toff', 'wb', 'wbc', 'acap', 'g1', 'g2', 'pkd', 'lifta')}
        try:                                                    # (max blending: overlaps do not add up)
            self.BlendEq = proc('glBlendEquation', None, u)
        except OSError:
            self.BlendEq = None
        # the graphics card's own time for a picture (GL_TIME_ELAPSED queries, read a few frames later so nothing
        # waits for them): Auto quality tells a slow graphics card from a slow processor by it (gpu_ms; None: unknown)
        self.gpu_ms = None
        try:
            self.GenQ = proc('glGenQueries', None, i, ctypes.POINTER(u))
            self.BeginQ = proc('glBeginQuery', None, u, u)
            self.EndQ = proc('glEndQuery', None, u)
            self.QueryIV = proc('glGetQueryObjectiv', None, u, u, ctypes.POINTER(i))
            try:
                self.QueryU64 = proc('glGetQueryObjectui64v', None, u, u, ctypes.POINTER(ctypes.c_uint64))
            except OSError:
                self.QueryU64 = proc('glGetQueryObjectui64vEXT', None, u, u, ctypes.POINTER(ctypes.c_uint64))
            qs = (u * 4)(); self.GenQ(4, qs)
            self.gpu_q = list(qs); self.gpu_q_busy = []
        except (OSError, AttributeError):
            self.gpu_q = None
        self.kprog = program(CURVE_SHADER, 'curve')
        self.dprog = program(DITHER_SHADER, 'dither'); self.u_dtex = GetUniform(self.dprog, b'tex')
        self.u_k = {k: GetUniform(self.kprog, k.encode()) for k in ('tex', 'norm', 'ctr', 'cx', 'cy', 'pull', 'glass', 'aw', 'ah', 'goff', 'gsc', 'glut', 'gn', 'edge', 'grey', 'lmap', 'lk', 'bscale', 'pxs', 'lpos', 'lmr', 'hsh', 'gsh', 'lca', 'lsa', 'cse', 'croom', 'crefl', 'cwear', 'clut', 'lc', 'smap', 'smr')}
        self.tprog = program(TONE_SHADER, 'tone')
        self.u_ttex = GetUniform(self.tprog, b'tex'); self.u_thk = GetUniform(self.tprog, b'hk'); self.u_tvl = GetUniform(self.tprog, b'vl'); self.u_tbgh = GetUniform(self.tprog, b'bgh')
        self.hdr = None; self.hdr_hk = 1.0                      # (HDR: hdr_start - the picture through DirectX, scRGB)
        try:
            self.hdrprog = program(HDR_OUT_SHADER, 'HDR output')   # (not hprog: the glint's)
            self.u_hsc = GetUniform(self.hdrprog, b'sc')
        except OSError:
            self.hdrprog = None
        self._bind_raw = self.BindFB
        self.BindFB = lambda t, f: self._bind_raw(t, self.hdr_w[1] if f == 0 and self.hdr else f)   # (HDR: the window is a half-float target)
        self.aprog = program(ART_SHADER, 'overlay')
        self.u_a = {k: GetUniform(self.aprog, k.encode()) for k in ('tex', 'scr', 'ssz', 'tint', 'room', 'print_')}
        try:                                                    # (the overlay's scratches; without them if the
            self.gprog = program_vf(SCR_GEN_VS, SCR_GEN_FS, 'scratch maker')    #  card can not)
            self.u_g = {k: GetUniform(self.gprog, k.encode()) for k in
                        ('batch', 'limit', 'seed', 'stepid', 'mode', 'place', 'L0', 'L1', 'd0', 'd1', 'wv', 'off', 'chan', 'dmax', 'scrib', 'llen')}
            self.lprog = program(OV_LIGHT_SHADER, 'light on the overlay')
            self.u_l = {k: GetUniform(self.lprog, k.encode()) for k in ('art', 'mode', 'k', 'wear', 'lpos', 'hsh', 'pic', 'lca', 'lsa')}
            self.rprog = program(SCR_SHADE, 'scratches')
            self.u_r = {k: GetUniform(self.rprog, k.encode()) for k in
                        ('art', 'A', 'B', 'C', 'S', 'M', 'F', 'V1', 'V2', 'pass', 'sf', 'ref', 'glint', 'room', 'scrk',
                         'scron', 'a2', 'a4', 'gk', 'glow', 'pen', 'rlp', 'rpt', 'size', 'qpx')}
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
        self.wob_on = True           # mains hum wobble (see HUM_AXES)
        self.wob_strength = 0.1074   # rms offset, game px (the owner's choice, 2026-10-08)
        self.line_gap = 7.0          # the integrators' stop delay in cycles (the core's; Tube > Line gaps): the gaps at
                                     #  line ends and corners - 7 fitted to the owner's test cart photos with the core's
                                     #  lag of the spot (2026-10-25; was 12.3 without it); wide-gap machines more
        self.mains_hz = 50           # the mains frequency of the hum (MAINS_HZ)
        self.bend_on = True          # brightness bending (high voltage sag)
        self.bend_strength = 0.6     # game px at the screen edge per 0.1 beam load
        self.bend_type = 0           # index into BEND_TYPES
        self.curve_on = True         # the tube geometry (Tube > Tube geometry, see CURVE_SHADER; on by default)
        self.geom_k = 1.0            # its strength (Tube > Tube geometry strength: 1 = the owner's Vectrex)
        self.glass_curve = 1.0       # the glass's curve (Tube > Glass curvature: 1 = as on the owner's Vectrex, 0 = undone,
                                     #  even squares - the owner, 2026-10-29; before: glass_k, the amount undone)
        self.head_on = False         # head tracking (Overlay > Head tracking: a phone app's head position, headtrack.py)
        self.head_smooth_x = 30.0    # the head's position smoothed, ms, per axis (the Head tab; the owner: left / right
        self.head_smooth_z = 80.0    #  30, up / down 80, near / far 80 - heads move mostly across)
        self.head_smooth_y = 80.0
        self.head_tilt = 0.0         # the phone's tilt, degrees (up at the face: +): its up / down from near / far taken out
        self.head_pic_on = True      # the picture behind the overlay slides against it (the Head tab)
        self.head_k = 1.0            # its strength (x1: 2 mm at the head's full reach from the usual distance)
        self.head_glass_on = True    # the room light's reflections on the glass follow the head
        self.head_glass_k = 0.3      # their strength (x1 = as far as a real mirror image moves; the owner: 30 % by default)
        self.head_ov_on = True       # the room light's reflections on the overlay follow the head
        self.head_ov_k = 0.3         # their strength (the same)
        self.head_src = None         # (the receiver, the app's: headtrack.HeadTrack)
        self.glass_on = True         # the glass's rounded edge (Tube > Glass edge)
        self.glass_room = True       # the glass grey in room light, without an overlay (Light > Glass catches room light)
        self.case_on = True          # the plastic case round the glass, without an overlay (casemodel; deck: Case)
        self.case_refl = 1.0         # (its reflections of the picture: strength)
        self.case_wear = 1.0         # (its scratches and the overlay-rubbed band on its front: strength)
        self.case_scr = None         # (its scratch map: texture, bounds)
        self.light_on = True         # the ceiling light reflected in the glass (Light > Light reflection on the glass)
        self.light_k = 1.0           #  its strength
        self.light_x = 0.0           # Light > Light position: left (0) .. right (1)
        self.light_y = 0.273         #  highest - 5 % of it visible - (0) .. lowest - its middle a quarter down - (1)
        self.light_ov = True         # the light on an overlay (Light > Light reflection on the overlay)
        self.light_ov_k = 1.0        #  its strength
        self.light_tex = None; self.light_key = None; self.light_made_t = 0.0
        self.halo_on = True          # the glow around resting spots (Tube > Corner glow, see BEAM_HALO_W)
        self.look2 = 'medium'        # Tube > Look: 'vectrex' (the measured picture), 'medium' (halfway - the PC's default, the owner 2026-11-26) or 'bright' (the tube's light lifted, the beam wider - blook)
        self.white_k = WHITE_DEFAULT # the beam's white, kelvin (see white_gain) - fixed: the White balance slider gone, 2026-11-26
        self.beam_trace = True       # (2026-11-22: the picture is the beam's path lighting the phosphor - always; the line renderer's rules are not used any more): the spot's own path lights the phosphor by the time it spent
                                     #  (BEAM_TRACE_CSAT; the core's beam-trace mode) - off: the line renderer
        self.even_beam = False       # Tube > Even beam: an ideal vector display - every point as bright and wide as a
                                     #  line, no corner glow, overlapping light not adding up (off: the real tube)
        self.halo_k = BEAM_HALO_K    # its brightness (Tube > Corner glow strength)
        self.dot_k = 1.0             # the resting spots' brightness, x DOT_K (Tube > Corner dot brightness)
        self.dither_on = True        # the picture dithered from 16 bits to the window's 8 (no bands in soft glows)

        self.frame_no = 0; self.frame_t0 = 0.0
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
            self.gl_renderer = (gl.glGetString(0x1F01) or b'').decode(errors='replace')   # (the chip: App.gl_name)
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
        if not hasattr(self, 'tex_wh'):
            self.tex_wh = {}
        self.tex_wh[t.value] = (w, h)                     # (a texel's size: SAT_SHADER's profile)
        return t.value, f.value

    def resize(self, w, h):
        self._win_wh = (w, h)                             # (the window as Tk gave it: a reallocation calls resize again)
        super().resize(w, h)
        if getattr(self, 'hdr', None):
            self._hdr_target()
        w, h = self.size
        ps = min(1.0, max(0.25, float(getattr(self, 'phos_scale', 1.0))))
        pw, ph = max(4, int(round(w * ps))), max(4, int(round(h * ps)))
        self.psize = (pw, ph); self._ps_alloc = ps      # (the phosphor passes' size: P, P2, D, E)
        self._lh_alloc = lh = bool(getattr(self, 'line_half', False))
        fw, fh = (pw, ph) if lh else (w, h)               # (Basic: the lines drawn at that size too - line_half)
        self.fsize = (fw, fh)
        for t in self.tex:
            self.gl.glDeleteTextures(1, ctypes.byref(ctypes.c_uint(t)))
        for f in self.fbs:
            self.DelFB(1, ctypes.byref(ctypes.c_uint(f)))
        self.tex = []; self.fbs = []
        self.P = self._target(pw, ph)                     # the phosphor image (ping-pong with P2; phos_scale)
        self.P2 = self._target(pw, ph)
        self.AG = [[self._target(pw, ph), self._target(pw, ph)] for _ in PHOS_A]   # (the afterglow's parts, each ping-pong;
        self.ag_i = 0; self.ag_mode = None; self.AGfb = None                       #  packed: the first pair only)
        if getattr(self, 'glowprog', None):              # (Ultra: a framebuffer with both parts' images, per side)
            try:
                fbs = []
                for k in (0, 1):
                    f = ctypes.c_uint(); self.GenFB(1, ctypes.byref(f)); self.BindFB(GL_FRAMEBUFFER, f.value)
                    self.FBTex(GL_FRAMEBUFFER, GL_COLOR_ATT0, GL_TEXTURE_2D, self.AG[0][k][0], 0)
                    self.FBTex(GL_FRAMEBUFFER, GL_COLOR_ATT0 + 1, GL_TEXTURE_2D, self.AG[1][k][0], 0)
                    self.fbs.append(f.value); fbs.append(f.value)
                    if self.FBStatus(GL_FRAMEBUFFER) != GL_FB_COMPLETE:
                        raise OSError('incomplete')
                self.AGfb = fbs
            except Exception as e:   # noqa
                print('afterglow in full colour unavailable (%s): packed' % e); self.AGfb = None
            self.BindFB(GL_FRAMEBUFFER, 0)
        self.F = self._target(fw, fh)                     # this display frame's fresh light (the window's size: the lines;
        self.F2 = self._target(fw, fh)                    #  Basic: the phosphor's). (Beam trace: the fresh light as light)
        self.F3 = self._target(fw, fh)                    # (a second game frame in one display frame: see _present)
        self.Fc = self._target(fw, fh, True)              # (how many of them lit each pixel)
        self.F4 = self._target(fw, fh)                    # (their light averaged)
        self.E = self._target(fw, fh)                     # (Smooth motion: the game frame's own lines - where the afterglow
        self.E2 = self._target(fw, fh)                    #  starts, see _present; E2: as light, beam trace)
        self.HM = self._target(fw, fh)                    # (the hand-over: old copies of moved lines - _present; HM2: as
        self.HM2 = self._target(fw, fh)                   #  light, beam trace)
        self.D = self._target(pw, ph)                     # what the screen shows
        self.E = self._target(pw, ph)                     # (the curved tube: the screen before bending)
        self.young = []; self.last = None   # (built in window pixels: from now on)
        self.qw, self.qh = max(4, w // 4), max(4, h // 4)
        self.B1 = self._target(self.qw, self.qh)          # bloom ping-pong
        self.B2 = self._target(self.qw, self.qh)
        self.R = self._target(w, h) if getattr(self, 'rot', 0) or getattr(self, 'float_ok', False) else None   # (the screen drawn
                                                           #  here first: 16-bit, turned or not - then onto the window)
        self.S = self._target(w, h)                       # (scratches: the screen before the overlay)
        self.B3 = self._target(self.qw, self.qh)          # (scratches: its light, blurred a little)
        self.Q0 = self._target(self.qw, self.qh)          # (its light at quarter size, sharp)
        self.Q1 = self._target(self.qw, self.qh)          # (the same, smoothed in time: the glint and the plastic glow)
        self._q1_t = None
        self.H = [self._target(self.qw, self.qh, True) for _ in range(3)]   # (the glint: horizontal harmonics)
        self.V = [self._target(self.qw, self.qh, True) for _ in range(2)]   # (and both ways)

    @property
    def out(self):
        """the frame buffer the finished screen goes to: the window, or (turned) R"""
        return self.R[1] if getattr(self, 'R', None) and not self._no_r() else 0

    def _no_r(self):
        """the dither off (Quality Low), the screen not turned: no 16-bit copy dithered onto the window - drawn there
        directly"""
        return not getattr(self, 'dither_on', True) and not getattr(self, 'rot', 0)

    def _turn_out(self):
        """the screen in R onto the window - turned rot degrees clockwise if it is, dithered to the window's 8 bits"""
        R = getattr(self, 'R', None)
        if not R or self._no_r():
            return
        gl = self.gl
        rw, rh = self.real_size
        self.BindFB(GL_FRAMEBUFFER, 0); gl.glViewport(0, 0, rw, rh); gl.glDisable(GL_BLEND)
        gl.glMatrixMode(GL_PROJECTION); gl.glPushMatrix(); gl.glLoadIdentity(); gl.glOrtho(0, 1, 0, 1, -1, 1)
        gl.glMatrixMode(GL_MODELVIEW)
        gl.glEnable(GL_TEXTURE_2D); gl.glBindTexture(GL_TEXTURE_2D, R[0]); gl.glColor4f(1, 1, 1, 1)
        tc = {0: lambda X, Y: (X, Y), 90: lambda X, Y: (1 - Y, X), 180: lambda X, Y: (1 - X, 1 - Y),
              270: lambda X, Y: (Y, 1 - X)}[getattr(self, 'rot', 0)]
        dp = getattr(self, 'dprog', None)
        if dp:
            self.UseProgram(dp); self.Uniform1i(self.u_dtex, 0)
        gl.glBegin(GL_QUADS)
        for X, Y in ((0, 0), (1, 0), (1, 1), (0, 1)):
            gl.glTexCoord2f(*tc(X, Y)); gl.glVertex2f(X, Y)
        gl.glEnd()
        if dp:
            self.UseProgram(0)
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
        if getattr(self, 'GenMip', None):                   # (its smaller copies, each pixel averaged: in a small window
            try:                                            #  the sheet shrunk smoothly - not a few of its pixels picked,
                self.GenMip(GL_TEXTURE_2D)                  #  a grain the glow lit up unevenly as the picture moved
                gl.glTexParameteri(GL_TEXTURE_2D, GL_TEX_MIN, 0x2703)   #  behind it - the owner's video, 2026-10-29;
            except Exception:   # noqa                      #  GL_LINEAR_MIPMAP_LINEAR)
                gl.glTexParameteri(GL_TEXTURE_2D, GL_TEX_MIN, GL_LINEAR)
        self.art = (t.value, w, h, sheet)

    def art_rect(self):
        """where the overlay plane goes, window px (x0, y0, x1, y1): its height the picture's (MiSTer's
        'Framing: Overlay') x ART_TUBE_K - the sheet's real shape and size, 162 x 207 mm, exactly on the case's
        matte frame (the owner, 2026-10-27; the image stretched to it if its own shape differs); nudged and scaled by art_dx / art_dy / art_scale (fractions
        of the picture height), lowered a little for the parallax"""
        s, ox, oy = self.map
        pl, pt, pr, pb = ox * s, oy * s, (ox + ALG_MAX_X) * s, (oy + ALG_MAX_Y) * s
        ph = pb - pt
        tw, th = self.art[1], self.art[2]
        k = 1.0                                                 # (the sheet always on the frame: its position and size
        hh = ph * k; ww = hh * 162.0 / 207.0 * tw / float(self.art[3])   # (the image's sheet part - an .art plane's centre
                                                                #  846 of 1360 columns - the real sheet: 162 x 207 mm)
        cx = (pl + pr) / 2 + GLASS_DX / 2 * ph                  #  sliders move the picture instead - pic_adj; centred on the glass)
        ww *= ART_TUBE_K; hh *= ART_TUBE_K                      # (the picture on the tube is smaller: the sheet
                                                                #  too, in its own proportions - MiSTer's art)
        cy = (pt + pb) / 2 + GLASS_DY / 2 * ph
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
        LL = light_level(getattr(self, 'art_room', ART_ROOM_MID))
        ovl = LL * getattr(self, 'light_ov_k', 1.0) if (getattr(self, 'light_ov', True) and getattr(self, 'lprog', None)) else 0.0
        if scr:                                                 # (the screen before the overlay: the scratches' light)
            gl.glBindTexture(GL_TEXTURE_2D, self.S[0]); gl.glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h)
            if ovl > 0 and getattr(self, 'scr_on', False):     # (the room's light among it: their glints around it)
                self.BindFB(GL_FRAMEBUFFER, self.S[1])
                gl.glMatrixMode(GL_PROJECTION); gl.glPushMatrix(); gl.glLoadIdentity(); gl.glOrtho(0, w, h, 0, -1, 1)
                gl.glMatrixMode(GL_MODELVIEW)
                self._ov_light(2, x0, y0, x1, y1, 0.6 * ovl)
                gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW)
                self.BindFB(GL_FRAMEBUFFER, self.out)
        if not scr:                                             # (the screen before the overlay: the sheet's shader reads it)
            gl.glBindTexture(GL_TEXTURE_2D, self.S[0]); gl.glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h)
        gl.glMatrixMode(GL_PROJECTION); gl.glPushMatrix(); gl.glLoadIdentity(); gl.glOrtho(0, w, h, 0, -1, 1)
        gl.glMatrixMode(GL_MODELVIEW)
        gl.glEnable(GL_TEXTURE_2D); gl.glColor4f(1, 1, 1, 1)
        self.ActiveTexture(0x84C1); gl.glBindTexture(GL_TEXTURE_2D, self.S[0]); self.ActiveTexture(0x84C0)
        gl.glBindTexture(GL_TEXTURE_2D, self.art[0])
        gl.glDisable(GL_BLEND)                                  # (one pass: the picture through the sheet and the room's
        self.UseProgram(self.aprog); self.Uniform1i(self.u_a['tex'], 0)   #  light on it, added as light)
        self.Uniform1i(self.u_a['scr'], 1); self.Uniform2f(self.u_a['ssz'], float(w), float(h))
        rl = room_light(self.art_room)                          # (the print lit by the room as well)
        self.Uniform1f(self.u_a['tint'], self.art_tint); self.Uniform1f(self.u_a['room'], rl)
        self.Uniform1f(self.u_a['print_'], self.art_print * (max(0.0, self.art_room) / ART_ROOM_MID) ** ART_PRINT_POW)
        gl.glBegin(GL_QUADS)
        for (u, v), (x, y) in (((0, 0), (x0, y0)), ((1, 0), (x1, y0)), ((1, 1), (x1, y1)), ((0, 1), (x0, y1))):
            gl.glTexCoord2f(u, v); gl.glVertex2f(x, y)
        gl.glEnd()
        self.UseProgram(0)
        self.ActiveTexture(0x84C1); gl.glBindTexture(GL_TEXTURE_2D, 0); self.ActiveTexture(0x84C0)
        gl.glEnable(GL_BLEND)
        gl.glDisable(GL_TEXTURE_2D)
        if ovl > 0:                                             # (the room's light on the sheet)
            self._ov_light(0, x0, y0, x1, y1, ovl)
            self._ov_light(1, x0, y0, x1, y1, 0.45 * ovl)
        gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE)
        gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW)
        if scr:
            self._scratches(x0, y0, x1, y1)

    # ---------------------------------------------------------------- the room's light (roomlight.py)
    def _light_ready(self):
        """the light's soft maps for this position (made again when it moves; while the pad is dragged at most every
        0.1 s): a small float texture, R the glass's own soft light, G the phosphor layer's"""
        key = (round(self.light_x, 3), round(self.light_y, 3))
        if key != self.light_key:
            now = __import__('time').perf_counter()
            if self.light_tex and now - self.light_made_t < 0.1:
                return True
            R, G = roomlight.maps(key[0], key[1], GLASS_REF_AW, GLASS_N)   # (the earlier outline: the shader's gsc)
            w, h = roomlight.MAP_W, roomlight.MAP_H
            px = (ctypes.c_float * (w * h * 4))()
            for i in range(w * h):
                px[4 * i] = R[i]; px[4 * i + 1] = G[i]; px[4 * i + 3] = 1.0
            gl = self.gl
            if not self.light_tex:
                t = ctypes.c_uint(); gl.glGenTextures(1, ctypes.byref(t)); self.light_tex = t.value
            gl.glBindTexture(GL_TEXTURE_2D, self.light_tex)
            for k, v in ((GL_TEX_MIN, GL_LINEAR), (GL_TEX_MAG, GL_LINEAR), (GL_WRAP_S, GL_CLAMP_EDGE), (GL_WRAP_T, GL_CLAMP_EDGE)):
                gl.glTexParameteri(GL_TEXTURE_2D, k, v)
            gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F if self.float_ok else 0x8058, w, h, 0, GL_RGBA, GL_FLOAT, px)
            self.light_key = key; self.light_made_t = now
        return True

    def _case_scratches(self):
        """the case's scratch map (made once; a black pixel without numpy): (texture, bounds)"""
        if self.case_scr is None:
            gl = self.gl
            m = casemodel.scratch_map(GLASS_LUT)
            if m is None:
                m = (1, 1, bytes(4), (-1.0, 1.0, -1.0, 1.0))
            w, h, px, bounds = m
            t = ctypes.c_uint(); gl.glGenTextures(1, ctypes.byref(t))
            gl.glBindTexture(GL_TEXTURE_2D, t.value)
            for k, v in ((GL_TEX_MIN, GL_LINEAR), (GL_TEX_MAG, GL_LINEAR), (GL_WRAP_S, GL_CLAMP_EDGE), (GL_WRAP_T, GL_CLAMP_EDGE)):
                gl.glTexParameteri(GL_TEXTURE_2D, k, v)
            buf = ctypes.create_string_buffer(px, len(px))
            gl.glTexImage2D(GL_TEXTURE_2D, 0, 0x8058, w, h, 0, GL_RGBA, 0x1401, buf)     # (RGBA8)
            self.case_scr = (t.value, bounds)
        return self.case_scr

    def _ov_light(self, mode, x0, y0, x1, y1, k):
        """the light on an overlay, over the sheet: mode 0 its plastic brighter around the light (multiplied),
        1 the light's reflection (added), 2 a soft version into the scratches' light (S)"""
        gl = self.gl
        w, h = self.size
        s, ox, oy = self.map
        _, (fx, fy) = roomlight.place(self.light_x, self.light_y)
        u = self.u_l
        self.UseProgram(self.lprog)
        self.Uniform1i(u['art'], 0); self.Uniform1f(u['mode'], float(mode)); self.Uniform1f(u['k'], k)
        self.Uniform3f(u['lpos'], fx, fy, roomlight.FLAT_SIZE)
        self.Uniform2f(u['hsh'], *self.refl_shift('ov'))
        self.Uniform1f(u['lca'], math.cos(roomlight.ANG)); self.Uniform1f(u['lsa'], math.sin(roomlight.ANG))
        self.Uniform4f(u['pic'], (ox + ALG_MAX_X / 2.0) * s, (oy + ALG_MAX_Y / 2.0) * s, ALG_MAX_Y * s / 2.0, float(h))
        self.Uniform1f(u['wear'], max(0.0, min(2.0, getattr(self, 'scr_wear', 1.0))))
        gl.glEnable(GL_TEXTURE_2D); gl.glBindTexture(GL_TEXTURE_2D, self.art[0]); gl.glColor4f(1, 1, 1, 1)
        gl.glEnable(GL_BLEND)
        gl.glBlendFunc(*((0x0306, 1) if mode == 0 else (1, 1)))      # (DST_COLOR, ONE: x (1 + src) / ONE, ONE)
        gl.glBegin(GL_QUADS)
        for (tu, tv), (x, y) in (((0, 0), (x0, y0)), ((1, 0), (x1, y0)), ((1, 1), (x1, y1)), ((0, 1), (x0, y1))):
            gl.glTexCoord2f(tu, tv); gl.glVertex2f(x, y)
        gl.glEnd()
        self.UseProgram(0); gl.glDisable(GL_TEXTURE_2D)

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
        self._down(self.S[0])
        self.BindFB(GL_FRAMEBUFFER, self.B1[1]); self._quad(self.Q0[0])
        now = time.perf_counter()                               # (Q1: the light the plastic scatters, smoothed over ~80 ms -
        dt = now - self._q1_t if self._q1_t is not None else 1.0   #  each display frame's light added up (a line the wobble
        self._q1_t = now                                        #  moved lit at its old and new place for a moment) made its
        a = 1.0 - math.exp(-max(0.0, min(dt, 1.0)) / max(GLOW_SMOOTH_S, 1e-6))   #  wide glow flare frame to frame - the owner,
        self.BindFB(GL_FRAMEBUFFER, self.Q1[1]); gl.glEnable(GL_BLEND)  #  2026-10-29)
        if getattr(self, 'BlendColor', None):                   # (a constant share: CONSTANT_ALPHA, ONE_MINUS_CONSTANT_ALPHA)
            self.BlendColor(0.0, 0.0, 0.0, a); gl.glBlendFunc(0x8003, 0x8004)
        else:
            gl.glBlendFunc(GL_ONE, GL_ZERO)                     # (without it: as before, unsmoothed)
        self._quad(self.Q0[0])
        gl.glDisable(GL_BLEND)
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
        ovl = light_level(getattr(self, 'art_room', ART_ROOM_MID)) * getattr(self, 'light_ov_k', 1.0) \
            if (on and getattr(self, 'light_ov', True) and getattr(self, 'lprog', None)) else 0.0
        s_, ox_, oy_ = self.map
        _, (fx_, fy_) = roomlight.place(self.light_x, self.light_y)
        hh_ = ALG_MAX_Y * s_ / 2.0
        self.Uniform4f(u['rlp'], 0.0, 0.0, hh_, ovl)
        if ovl > 0:
            key = (round(self.light_x, 3), round(self.light_y, 3))
            if getattr(self, '_rpt_key', None) != key:
                self._rpt_key = key; self._rpt = roomlight.flat_points(*key)
            cx_, cy_ = (ox_ + ALG_MAX_X / 2.0) * s_, (oy_ + ALG_MAX_Y / 2.0) * s_
            pts = []
            su_, sv_ = self.refl_shift('ov')              # (head tracking: moved with the reflection)
            for pu, pv in self._rpt:
                pts += [cx_ + (pu + su_) * hh_, h - (cy_ + (pv + sv_) * hh_)]
            self.Uniform2fv(u['rpt'], 6, (ctypes.c_float * 12)(*pts))
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
            self.BindFB(GL_FRAMEBUFFER, self.H[part][1]); self.Uniform1f(u['part'], float(part)); self._quad(self.Q1[0])
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

    def _down(self, tex, size=None):
        """the picture `tex` (its size: the window's, or size) into the bound quarter-size target, every pixel counted
        (DOWN_SHADER)"""
        if getattr(self, 'downprog', None):
            w, h = size or self.size
            self.UseProgram(self.downprog); self.Uniform1i(self.u_down[0], 0); self.Uniform2f(self.u_down[1], 1.0 / max(1, w), 1.0 / max(1, h))
            self._quad(tex)
            self.UseProgram(0)
        else:
            self._quad(tex)

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
        v, t, c, tt, n = b[:5]
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
        cuts = len(b) > 8 and b[7] is not None             # (units 2, 3: the joints' cuts - see CAPSULE_SHADER)
        if cuts:
            self.ClientActiveTexture(0x84C2); gl.glEnableClientState(GL_TEXTURE_COORD_ARRAY); gl.glTexCoordPointer(4, GL_FLOAT, 0, b[7])
            self.ClientActiveTexture(0x84C3); gl.glEnableClientState(GL_TEXTURE_COORD_ARRAY); gl.glTexCoordPointer(2, GL_FLOAT, 0, b[8])
        else:
            for unit in (0x84C2, 0x84C3):
                self.ClientActiveTexture(unit); gl.glDisableClientState(GL_TEXTURE_COORD_ARRAY)
            self.MultiTexCoord4f(0x84C2, 0.0, 0.0, 0.0, 0.0); self.MultiTexCoord4f(0x84C3, 0.0, 0.0, 0.0, 0.0)
        self.ClientActiveTexture(0x84C0)
        self.UseProgram(self.cprog)
        bt = len(b) > 6 and b[6]                           # (built for the beam trace)
        self.Uniform1f(self.u_premul, 1.0 if (getattr(self, 'blend_max', False) or bt) else 0.0)
        u = self.u_t
        self.Uniform2f(u['fcs'], *getattr(self, 'fcs', (1.0, 1.0)))
        self.Uniform1f(u['beam'], 1.0 if bt else 0.0)
        self.Uniform1f(u['hsg'], 0.5 * BT_HALO_S * 160.0 * (self.map[0] if hasattr(self, 'map') else 1.0) / BT_UNITS)
        self.Uniform3f(u['wb'], *(self._wb() if (age0 is None and not bt) else (1.0, 1.0, 1.0)))
        self.Uniform3f(u['wbc'], *(self._wbc() if (age0 is None and not bt) else (1.0, 1.0, 1.0)))   # (crisp lines here; the
                                                                                                 #  rest: SHOW_SHADER / _sat)
        if age0 is None:
            self.Uniform1f(u['timed'], 0.0)
        else:
            for k, x in (('timed', 2.0 if imm else 1.0), ('age0', age0), ('dtw', dtw), ('gain', getattr(self, 'frame_w', 1.0) if bt else PHOS_GAIN * getattr(self, 'frame_w', 1.0))):
                self.Uniform1f(u[k], x)
        # (in pieces: the Raspberry Pi 5's Mesa / V3D driver crashed inside one glDrawArrays of 13788 client-array
        #  vertices, 2026-10-27 - gdb backtrace in libgallium; a few calls more cost nothing elsewhere)
        step = self.DRAW_CHUNK * 4
        for k in range(0, n * 4, step):
            gl.glDrawArrays(GL_QUADS, k, min(step, n * 4 - k))
        self.UseProgram(0)
        if cuts:
            for unit in (0x84C2, 0x84C3):
                self.ClientActiveTexture(unit); gl.glDisableClientState(GL_TEXTURE_COORD_ARRAY)
        self.ClientActiveTexture(0x84C1); gl.glDisableClientState(GL_TEXTURE_COORD_ARRAY)
        self.ClientActiveTexture(0x84C0); gl.glDisableClientState(GL_TEXTURE_COORD_ARRAY)
        gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW)

    @staticmethod
    def _rest(ln):
        return Renderer_rest(ln)

    @staticmethod
    def _joins(lines):
        """the beam drawing one stroke in several pieces (the core cuts a line where the speed changes - the spot's
        lag turns corners in curves of pieces): per line 1 = its start continues the piece before, 2 = its end
        continues into the next (the same point, the next moment). Those ends are drawn open, so a stroke is one
        smooth line - no round ends overlapping into dots at the joins. Resting spots (zero length) are not opened"""
        n = len(lines); out = [0] * n
        for i in range(1, n):
            a, c = lines[i - 1], lines[i]
            if len(a) < 7 or len(c) < 7 or (a[4] & 256) or (c[4] & 256): continue
            if abs(c[0] - a[2]) > 1.5 or abs(c[1] - a[3]) > 1.5: continue
            if abs(c[6] - (a[6] + a[5] / 8.0)) > 1.5: continue
            if not Renderer_rest(c): out[i] |= 1
            if not Renderer_rest(a): out[i - 1] |= 2
        for i, ln in enumerate(lines):
            if Renderer_rest(ln): out[i] |= 4                # (bit 2: a resting spot)
        return out

    def _tube_lines(self, lines, joins=None):
        """Tube geometry: every line through the owner's tube map (tubemap), long ones in pieces so they
        bend; each piece keeps its share of the beam's energy and its moment in the frame"""
        k = getattr(self, 'geom_k', 1.0) if getattr(self, 'curve_on', False) else 0.0
        g = self.glass_undo(k)                           # (the glass's curve undone: Tube > Glass curvature)
        w = getattr(self, 'unit_warp', None)             # (this Vectrex's picture shape)
        if getattr(self, '_tube_k', None) != (k, g, w):  # (its own name: under _tube_f it hid the method _build_np
            self._tube_k = (k, g, w); self._tube_fn = tube_map(k, g, w)  #  calls - after the first switch-on animation every frame
        f = self._tube_fn                                  #  took this slow way: Vectorblade at 2 fps after a game change)
        ps, pdx, pdy = self.pic_adj()
        if (ps, pdx, pdy) != (1.0, 0.0, 0.0):
            XM, YM = ALG_MAX_X / 2.0, ALG_MAX_Y / 2.0
            f0 = f; f = lambda x, y: (lambda p: (XM + (p[0] - XM) * ps + pdx, YM + (p[1] - YM) * ps + pdy))(f0(x, y))
        out = []; sqrt = math.sqrt; opens = []           # (opens: a piece's ends that join the next piece)
        for li, ln in enumerate(lines):
            x0, y0, x1, y1 = ln[:4]
            L = sqrt((x1 - x0) ** 2 + (y1 - y0) ** 2)
            if L <= TUBE_SEG or (k <= 0 and g <= 0 and not w):   # (straight: the ends only)
                a = f(x0, y0); b = f(x1, y1) if L > 0 else a
                out.append((a[0], a[1], b[0], b[1]) + tuple(ln[4:])); opens.append(joins[li] if joins else (4 if Renderer_rest(ln) else 0))
                continue
            m = int(L / TUBE_SEG) + 1; rest = tuple(ln[7:])
            e = ln[5] if len(ln) > 5 else None; t0 = ln[6] if len(ln) > 6 else None
            cyc = (e / 8.0) if e is not None else 0.0
            pa = f(x0, y0)
            for i in range(1, m + 1):
                s = i / float(m)
                pb = f(x0 + (x1 - x0) * s, y0 + (y1 - y0) * s)
                piece = (pa[0], pa[1], pb[0], pb[1], ln[4])
                if e is not None:
                    piece += (e / m,)
                    if t0 is not None:
                        piece += (t0 + cyc * (i - 1) / m,) + rest
                out.append(piece); pa = pb
                opens.append((1 if i > 1 else (joins[li] & 1 if joins else 0)) | (2 if i < m else (joins[li] & 2 if joins else 0)))
        self._tube_open = opens
        return out

    def _build(self, lines, gain=1.0, load=True):
        """the vertex arrays for these lines (window pixels): (v, t, c, tt, n), or None"""
        if lines is None or len(lines) == 0:                 # (a list, or a numpy array: Smooth motion)
            return None
        _bt = bool(getattr(self, 'beam_trace', False)) and hasattr(self, 'satprog')
        if _np is not None and not getattr(self, 'np_off', False) and not any(   # (beam trace: the switch-on bulge - the size alone - in _build_np too)
                getattr(self, a, d) != d for a, d in ((() if _bt else (('crt_sx', 1.0), ('crt_sy', 1.0))) + (('crt_skew', 0.0), ('crt_shift', 0.0),
                                                      ('crt_garble', 0.0), ('crt_warp', 0.0)))):
            try:                                           # (the tube switching on or off - collapse, garble: below)
                if _bt and _vxb is not False:              # (the beam trace: in C when its library is there)
                    b = self._build_c(lines, gain, load)
                    if b is not None:
                        return b
                return self._build_np(lines, gain, load)
            except (ValueError, TypeError, IndexError):
                pass                                       # (lines of other shapes: the per-line way)
        if hasattr(lines, 'tolist'):                       # (a numpy array - Smooth motion - for the per-line way: rows,
            lines = [tuple(r[:4]) + (int(r[4]),) + tuple(r[5:]) for r in lines.tolist()]   #  the brightness a whole number)
        if _bt: gain = gain * 0.0035                       # (beam trace, the tube collapsing: these are screen values, the picture takes light - about the same look)
        return self._build_py(lines, gain, load)

    def _pic_load(self, lines):
        """the brightness bending's load of the picture being built: beam_load of its lines, or load_pic - Smooth motion's,
        the game frame's own (the lines it hands over are squeezed into the refresh, an alternate frame's other half added)"""
        ld = getattr(self, 'load_pic', None)
        return beam_load(lines) if ld is None else ld

    def _build_c(self, lines, gain=1.0, load=True):
        """_build_np's beam trace in C (native/vxbuild.c: the same arithmetic piece by piece, the same arrays back).
        None: not for these lines or settings, or no library - the numpy way then"""
        lib = _vxb_get()
        if not lib or os.environ.get('VX_BT_LAW', '2') == '1' or os.environ.get('VX_BT_DUMP'):
            return None
        np = _np
        try:
            src = getattr(lines, 'np', None)               # (the core's frame as an array: with the 3D Imager's colours)
            L = np.ascontiguousarray(src if src is not None and len(src) == len(lines) else lines, dtype=np.float64)
        except (ValueError, TypeError, IndexError):
            return None                                    # (lines of other shapes: _build_np says so)
        try:
            if L.ndim != 2 or L.shape[1] < 7 or not len(L):
                return None
            if L.shape[1] > 7 and self.imager_eye >= 0:     # (the 3D Imager, one eye: the other half turn's lines out - as _build_np)
                c8 = L[:, 7].astype(np.int64)
                if (c8 & 7).any():
                    L = np.ascontiguousarray(L[((c8 & 8) != 0) == (self.imager_eye == 1)])
                    if not len(L):
                        return None
            k = getattr(self, 'geom_k', 1.0) if getattr(self, 'curve_on', False) else 0.0
            w = getattr(self, 'unit_warp', None)
            wv = tuple(float(x) for x in w) if w else (0.0,) * 6
            if len(wv) != 6:
                return None
            ps, pdx, pdy = self.pic_adj()
            wob = bool(getattr(self, 'wob_on', False) and self.wob_strength > 0)
            a0, k50, hum = 0.0, 0.0, (0.0,) * 12
            if wob:                                        # (the hum: its phase and weights now)
                a0, ((PX, QX), (PY, QY)) = hum_coeffs(self.frame_t0 / 1.5e6, self.wob_strength, WOBBLE_SPEED, self.mains_hz)
                k50 = 2 * math.pi * self.mains_hz / 1.5e6
                hum = tuple(PX[1:4]) + tuple(QX[1:4]) + tuple(PY[1:4]) + tuple(QY[1:4])
            _hf, _hs = [float(v) for v in os.environ.get('VX_BT_HALO', '%g,%g' % (BT_HALO_F, BT_HALO_S)).split(',')]   # (tests)
            rgb = [1.0] * 24
            for code, c3 in self.IMAGER_RGB.items():
                rgb[3 * code:3 * code + 3] = c3
            sc, ox, oy = self.map
            P = getattr(self, '_vxb_p', None)
            if P is None:
                P = self._vxb_p = np.zeros(_VXB_NP)
            P[:] = ((BT2_W, BT2_K, 1.0 if BT_HALO_F > 0 else 0.0, k, self.glass_undo(k), 1.0 if w else 0.0) + wv + tuple(TUBE_LIN)
                    + (ALG_MAX_X / 2.0, ALG_MAX_Y / 2.0, ALG_MAX_Y / 2.0, UW, tubemap.U0, tubemap.V0, tubemap.SU, tubemap.SV,
                       MM_UNIT, GLASS_RX_MM, GLASS_RY_MM, GLASS_T_MAX, ps, pdx, pdy, TUBE_SEG, 0.0, 1.0, 0.0,
                       1.0 if wob else 0.0, a0, k50) + hum
                    + (getattr(self, 'crt_sx', 1.0), getattr(self, 'crt_sy', 1.0), self.size[1], sc, ox, oy,
                       getattr(self, 'crt_gain', 1.0) * getattr(self, 'hv_light', 1.0),
                       getattr(self, 'crt_width', 1.0) * getattr(self, 'hv_width', 1.0), getattr(self, 'knob_bright', 0.0), gain,
                       BEAM_LOWZ, BEAM_LOWW, BEAM_W0, BEAM_WZ, BEAM_TRACE_SG, float(os.environ.get('VX_BT_SPOT', BT_SPOT)), _hf, _hs,
                       BT_UNITS, BT_KMAX, BEAM_FADE, 1.0 if self.blook() else 0.0, self.bwidth(), 1.0 if L.shape[1] > 7 else 0.0)
                    + tuple(rgb) + (float(getattr(self, 'dwell_k', 1.0)),))   # (dwell_k: see bt2_prepare)
            with _vxb_lock:                                # (the library keeps the pieces between its calls)
                n = lib.vxb_pieces(L.ctypes.data, len(L), L.shape[1], P.ctypes.data)
                if n <= 0:
                    return None
                # brightness bending: this picture's beam load (the core's lines: beam_load), smoothed
                ld_ = self._pic_load(L)
                lf = getattr(self, 'load_f', ld_)
                if load:
                    lf += (ld_ - lf) * 0.3
                if getattr(self, 'bend_on', False) and self.bend_strength > 0:
                    btype = BEND_TYPES[self.bend_type % len(BEND_TYPES)]
                    amt = self.bend_strength * lf * 10.0
                    P[32:35] = (1.0, 1.0 + (amt / 128.0 if btype != 'shift' else 0.0), amt * 160.0 * 0.5 if btype != 'grow' else 0.0)
                V, T, C, TT, T2, T3 = _vxb_out.arrays(n)   # (memory a picture before had: see _VxbOut)
                tmax = ctypes.c_double(0.0)
                if lib.vxb_fill(P.ctypes.data, V.ctypes.data, T.ctypes.data, C.ctypes.data, TT.ctypes.data, T2.ctypes.data,
                                T3.ctypes.data, ctypes.byref(tmax)) != n:
                    return None
            self._tube_open = np.zeros(n, dtype=np.int64)  # (the soft ends add up: none open)
            self.load_f = lf
            cv = lambda a: (ctypes.c_float * a.size).from_buffer(a)   # (the arrays as they are: no copy)
            return cv(V), cv(T), cv(C), cv(TT), n, tmax.value, True, cv(T2), cv(T3)
        except Exception as e:   # noqa
            _vxb_off(e)
            return None

    def par_dy(self):
        """the overlay's parallax as the picture's shift (emulator units, up): the picture behind the glass seen a
        little higher than the sheet on the frame (Overlay > Parallax, an overlay shown)"""
        if self.art and getattr(self, 'art_on', False) and getattr(self, 'art_parallax', False):
            return -ART_PARALLAX * ART_TUBE_K * ALG_MAX_Y
        return 0.0

    def pic_adj(self):
        """the picture behind an overlay: (scale, dx, dy) - emulator units round the frame's centre. The sheet lies
        on the case's frame; its sliders (Overlay left / right, up / down, size) line it up with the game by moving
        and sizing the picture the other way (the owner, 2026-10-28), plus the parallax. No overlay: (1, 0, 0) - the
        picture as measured"""
        if not (self.art and getattr(self, 'art_on', False)):
            return 1.0, 0.0, 0.0
        k = getattr(self, 'art_scale', 1.0) or 1.0
        return (1.0 / k, -getattr(self, 'art_dx', 0.0) * ALG_MAX_Y,
                -getattr(self, 'art_dy', 0.0) * ART_TUBE_K * ALG_MAX_Y + self.par_dy())

    def head_px(self):
        """head tracking: how far the glowing picture slides against the sheet now, window px (x right, y down) - the
        whole phosphor image, afterglow and bloom with it, moved where it is put on the screen (_finish). Moving the
        lines themselves left their afterglow behind: a faint glow beside each line, flickering with the head's every
        small move (the owner's video, 2026-10-29). Only behind an overlay: without one nothing to slide against"""
        src = getattr(self, 'head_src', None)
        if not (src is not None and getattr(self, 'head_on', False) and getattr(self, 'head_pic_on', True)
                and self.art and getattr(self, 'art_on', False) and hasattr(self, 'map')):
            return 0.0, 0.0
        mx, my = src.offset(getattr(self, 'head_k', 1.0))
        e = ALG_MAX_Y / 2.0 / MM_UNIT * self.map[0]       # (window px a mm)
        return mx * e, my * e

    def _tube_f(self, x, y, k):
        """the tube's map (_tube_lines): a point of the integrators (the lines' units) where it shows on the curved
        tube (still the lines' units), k the geometry's strength (0: none) - numpy arrays (tubemap)"""
        np = _np
        la, lb, lc, ld = TUBE_LIN
        XM, YM, U = ALG_MAX_X / 2.0, ALG_MAX_Y / 2.0, ALG_MAX_Y / 2.0
        u = (np.asarray(x, dtype=np.float64) - XM) / U; v = (np.asarray(y, dtype=np.float64) - YM) / U
        w = getattr(self, 'unit_warp', None)
        if w:                                             # (this Vectrex's own picture shape: vectrex_emu.unit_warp)
            u, v = unit_warp(w, u, v)
        X = la * u + lb; Y = lc * v + ld
        if k:
            dx, dy = tubemap.imp_np(np, u, v)
            X = X + k * dx; Y = Y + k * dy
        X, Y = glass_flat(X, Y, self.glass_undo(k), np)  # (the glass's curve undone: Glass curvature)
        ps, pdx, pdy = self.pic_adj()                     # (behind an overlay: its sliders and the parallax)
        return XM + X * U * ps + pdx, YM + Y * U * ps + pdy

    REFL_L_MM = 1500.0          # (the ceiling light's mirror image: about as far behind the glass as the light is in front)

    def refl_shift(self, on='glass'):
        """head tracking: how far the room light's reflections move on the glass and the overlay now (units, v down).
        A mirror image lies as far behind the glass as the light is in front of it - far: it slides almost as much as
        the head moves (x L / (D + L)), the same way, unlike the picture just behind the sheet"""
        src = getattr(self, 'head_src', None)
        if src is None or not getattr(self, 'head_on', False):
            return 0.0, 0.0
        hv = src.head_view()
        if hv is None:
            return 0.0, 0.0
        hx, hy, dk = hv
        if not getattr(self, 'head_%s_on' % on, True):
            return 0.0, 0.0
        f = getattr(self, 'head_%s_k' % on, 0.3) * self.REFL_L_MM / (600.0 + self.REFL_L_MM) * dk / MM_UNIT   # (on: 'glass' /
                                                                #  'ov'; nearer / further: headtrack.refl_k)
        return hx * f, -hy * f

    def phos_shift(self, hs):
        """head tracking: the phosphor layer's glow, behind the thick glass, sits inwards of the glass reflection by the
        glass's thickness there - none in the middle, INNER_OFF at the edge (roomlight.maps). The maps were made for the
        light's own place; moved with the head (hs, units), the glow keeps to that rule where the reflection is now: the
        extra move, the maps' units (the owner, 2026-10-29: down low it belongs above the light, as it is below it on
        top)"""
        if not (hs[0] or hs[1]):
            return 0.0, 0.0
        key = (round(self.light_x, 3), round(self.light_y, 3))
        if getattr(self, '_pc_key', None) != key:
            (lx, ly), _ = roomlight.place(self.light_x, self.light_y)
            self._pc_key = key; self._pc = roomlight.centre(lx, ly, roomlight.GLASS_CURV)
        gx, gy = GLASS_AW / GLASS_REF_AW, GLASS_AH / GLASS_REF_AH
        def off(u, v):                                  # (the inward offset at (u, v): maps' units)
            r = (abs(u / GLASS_REF_AW) ** GLASS_N + abs(v) ** GLASS_N) ** (1.0 / GLASS_N)
            o = roomlight.INNER_OFF * min(1.0, r) ** 2; n = max(1e-6, math.hypot(u, v))
            return o * u / n, o * v / n
        cu, cv = self._pc
        su, sv = hs[0] / gx, hs[1] / gy
        o0, o1 = off(cu, cv), off(cu + su, cv + sv)     # (towards the middle by the glass's thickness wherever the
        return o0[0] - o1[0], o0[1] - o1[1]             #  reflection is: on it in the middle, the same distance inwards at
                                                        #  every edge - the owner looked again, 2026-10-29)

    def window_to_vectrex(self, px, py):
        """a window pixel (the picture not turned) as the integrators' place there - the lines' units, the tube's map
        undone (a few steps: the map is close to the identity): where the light pen points"""
        sc, ox, oy = self.map
        w, h = self.size
        u, v = px / sc - ox, py / sc - oy                 # (the projection's y runs down, as the lines')
        k = getattr(self, 'geom_k', 1.0) if getattr(self, 'curve_on', False) else 0.0
        x, y = _np.array([u], dtype=float), _np.array([v], dtype=float)
        ps = self.pic_adj()[0]
        gx, gy = 1.0 / (TUBE_LIN[0] * ps), 1.0 / (TUBE_LIN[2] * ps)   # (steps scaled by the map's own size: exact in a few)
        for _ in range(8):
            fx, fy = self._tube_f(x, y, k)
            x = x + (u - fx) * gx; y = y + (v - fy) * gy
        return float(x[0]), float(y[0])

    # the 3D Imager's filters (imager.c: the colour code of each line, an 8th column): the phosphor's white light through
    # red, green and blue gels - the blue a little dimmer, as through a real one
    IMAGER_RGB = {1: (1.0, 0.12, 0.10), 2: (0.15, 1.0, 0.25), 4: (0.25, 0.45, 1.0)}
    imager_eye = -1     # (the 3D Imager: -1 both eyes' pictures, 1 the left eye's only, 0 the right eye's - bit 8 of the
                        #  colour code: the disc's second half turn, the left eye's; Tony on Facebook, 2026-11-24)

    def _build_np(self, lines, gain=1.0, load=True):
        """_build_py with numpy: the same arithmetic on all lines at once (checked equal to it, float32). Beam trace on:
        the core's pieces of the spot's path, each its energy (see CAPSULE_SHADER)"""
        np = _np
        src = getattr(lines, 'np', None)                   # (the core's frame as an array: with the 3D Imager's colours)
        L = np.asarray(src if src is not None and len(src) == len(lines) else lines, dtype=np.float64)
        if L.ndim != 2 or L.shape[1] < 7:
            raise ValueError('lines')
        col = L[:, 7].astype(np.int64) if L.shape[1] > 7 else None
        L = L[:, :7]
        if col is not None and self.imager_eye >= 0 and len(col) and (col & 7).any():   # (one eye: the other half turn's lines out)
            keep = ((col & 8) != 0) == (self.imager_eye == 1)
            L = L[keep]; col = col[keep]
        ld_ = self._pic_load(L)                                  # (brightness bending: the core's lines, before any are added)
        b = L[:, 4].astype(np.int64)
        if len(b) and not getattr(self, 'beam_trace', False):   # (rests joined to lines: see DOT_C0)
            b = mark_joined(L[:, 0], L[:, 1], L[:, 2], L[:, 3], b, L[:, 5], L[:, 6]); L[:, 4] = b
        beam = bool(getattr(self, 'beam_trace', False)) and hasattr(self, 'satprog')
        bl = None
        if beam and os.environ.get('VX_BT_LAW', '2') != '1' and len(L):   # (the measured law: bt2_prepare)
            if os.environ.get('VX_BT_DUMP'): np.save(os.environ['VX_BT_DUMP'], L)
            L, bl, col = bt2_prepare(L, col, float(getattr(self, 'dwell_k', 1.0))); b = L[:, 4].astype(np.int64)
        if beam:                                           # (no joins or rest rules: the light does it; the glass's glow: every
            hm = ((b & 127) != 0) & ((b & 128) == 0)       #  lit piece once more, wide and faint - bit 256)
            if hm.any() and BT_HALO_F > 0:
                H = L[hm].copy(); H[:, 4] = b[hm] | 256
                L = np.concatenate([H, L]); b = L[:, 4].astype(np.int64)
                if col is not None:
                    col = np.concatenate([col[hm], col])
                if bl is not None: bl = np.concatenate([bl[hm], bl])
        elif getattr(self, 'halo_on', True) and not getattr(self, 'even_beam', False):
            hm = (np.sqrt((L[:, 2] - L[:, 0]) ** 2 + (L[:, 3] - L[:, 1]) ** 2) <= REST_SPEED * L[:, 5] / 8.0) & (L[:, 5] >= 64) & ((b & 127) != 0) & ~(((b & 512) != 0) & (L[:, 5] < 8.0 * REST_DWELL_HALO))
            if hm.any():                                   # (the resting spots' halos, drawn first)
                H = L[hm].copy(); H[:, 4] = (b[hm] & (127 | 1024)) | 256
                L = np.concatenate([H, L]); b = L[:, 4].astype(np.int64)
                if col is not None:
                    col = np.concatenate([col[hm], col])
        x0, y0, x1, y1, e, t0 = L[:, 0], L[:, 1], L[:, 2], L[:, 3], L[:, 5], L[:, 6]
        # one stroke in several pieces: the joined ends open (_joins)
        jo = np.zeros(len(L), dtype=np.int64)
        zl = np.sqrt((x1 - x0) ** 2 + (y1 - y0) ** 2) <= REST_SPEED * e / 8.0     # (resting spots: see REST_SPEED)
        if beam:
            zl = (x0 == x1) & (y0 == y1)                   # (a piece of zero length: the spot at rest)
            if len(L) > 1 and bl is None:                                 # (rests in one place, one after the other: one rest of the summed
                nx_ = zl[1:] & zl[:-1] & (x0[1:] == x0[:-1]) & (y0[1:] == y0[:-1]) & (b[1:] == b[:-1]) & (np.abs(t0[1:] - (t0[:-1] + e[:-1] / 8.0)) <= 1.5)   #  time - the law is not additive)
                if nx_.any():
                    e = e.copy(); keep = np.ones(len(L), dtype=bool)
                    for j in np.nonzero(nx_)[0][::-1]:      # (from the back: a run adds up into its first piece)
                        e[j] += e[j + 1]; keep[j + 1] = False
                    L = L[keep].copy(); L[:, 5] = e[keep]; b = b[keep]
                    if col is not None: col = col[keep]
                    x0, y0, x1, y1, e, t0 = L[:, 0], L[:, 1], L[:, 2], L[:, 3], L[:, 5], L[:, 6]
                    jo = np.zeros(len(L), dtype=np.int64); zl = (x0 == x1) & (y0 == y1)
        elif len(L) > 1:
            nh = (b & 256) == 0
            cont = ((np.abs(x0[1:] - x1[:-1]) <= 1.5) & (np.abs(y0[1:] - y1[:-1]) <= 1.5)
                    & (np.abs(t0[1:] - (t0[:-1] + e[:-1] / 8.0)) <= 1.5) & nh[1:] & nh[:-1])
            jo[1:] |= np.where(cont & ~zl[1:], 1, 0)
            jo[:-1] |= np.where(cont & ~zl[:-1], 2, 0)
        # the tube's map, long lines in pieces (_tube_lines)
        k = getattr(self, 'geom_k', 1.0) if getattr(self, 'curve_on', False) else 0.0
        Ls = np.sqrt((x1 - x0) ** 2 + (y1 - y0) ** 2)
        bend = k > 0 or self.glass_undo(k) > 0 or bool(getattr(self, 'unit_warp', None))   # (the unit's shape bends lines too)
        m = np.where((Ls > TUBE_SEG) & bend, (Ls / TUBE_SEG).astype(np.int64) + 1, 1)
        n0 = len(L)
        idx = np.repeat(np.arange(n0), m)
        starts = np.cumsum(m) - m
        i = np.arange(len(idx)) - np.repeat(starts, m) + 1
        mm = m[idx].astype(np.float64)
        s0, s1 = (i - 1) / mm, i / mm
        X0, Y0, X1, Y1 = x0[idx], y0[idx], x1[idx], y1[idx]
        ax_, ay_ = X0 + (X1 - X0) * s0, Y0 + (Y1 - Y0) * s0
        bx_ = np.where(mm == 1, X1, X0 + (X1 - X0) * s1); by_ = np.where(mm == 1, Y1, Y0 + (Y1 - Y0) * s1)
        f = lambda x, y: self._tube_f(x, y, k)
        x0, y0 = f(ax_, ay_)
        x1, y1 = f(bx_, by_)
        same = zl[idx]                                    # (a resting spot: where the spot came to rest)
        x0 = np.where(same, x1, x0); y0 = np.where(same, y1, y0)
        e = e[idx] / mm
        if bl is not None: bl = bl[idx] / mm
        t0 = t0[idx] + (L[idx, 5] / 8.0) * (i - 1) / mm
        b = b[idx]
        op = np.where(i > 1, 1, jo[idx] & 1) | np.where(i < mm, 2, jo[idx] & 2) | np.where(same, 4, 0)
        if beam:
            op = np.zeros(len(idx), dtype=np.int64)        # (the soft ends add up: none open)
        self._tube_open = op
        n = len(x0)
        w, h = self.size
        sc, ox, oy = self.map
        cyc = e / 8.0
        # brightness bending: this picture's beam load (ld_, above), smoothed
        self.load_f = getattr(self, 'load_f', ld_)
        if load:
            self.load_f += (ld_ - self.load_f) * 0.3
        if getattr(self, 'bend_on', False) and self.bend_strength > 0:
            btype = BEND_TYPES[self.bend_type % len(BEND_TYPES)]
            amt = self.bend_strength * self.load_f * 10.0
            gs = 1.0 + (amt / 128.0 if btype != 'shift' else 0.0)
            gsh = amt * 160.0 * 0.5 if btype != 'grow' else 0.0
            cxm, cym = ALG_MAX_X / 2.0, ALG_MAX_Y / 2.0
            x0 = cxm + (x0 - cxm) * gs + gsh; x1 = cxm + (x1 - cxm) * gs + gsh
            y0 = cym + (y0 - cym) * gs + gsh; y1 = cym + (y1 - cym) * gs + gsh
        if getattr(self, 'wob_on', False) and self.wob_strength > 0:     # hum: offsets at each end
            a0, ((PX, QX), (PY, QY)) = hum_coeffs(self.frame_t0 / 1.5e6, self.wob_strength, WOBBLE_SPEED, self.mains_hz)
            k50 = 2 * math.pi * self.mains_hz / 1.5e6
            def woff(t):
                a = a0 + t * k50
                s1, c1 = np.sin(a), np.cos(a)
                s2, c2 = 2 * s1 * c1, c1 * c1 - s1 * s1
                s3, c3 = s2 * c1 + c2 * s1, c2 * c1 - s2 * s1
                return (PX[1] * s1 + QX[1] * c1 + PX[2] * s2 + QX[2] * c2 + PX[3] * s3 + QX[3] * c3,
                        PY[1] * s1 + QY[1] * c1 + PY[2] * s2 + QY[2] * c2 + PY[3] * s3 + QY[3] * c3)
            dx0, dy0 = woff(t0); dx1, dy1 = woff(t0 + cyc)
            x0 = x0 + dx0; y0 = y0 + dy0; x1 = x1 + dx1; y1 = y1 + dy1
        _csx, _csy = getattr(self, 'crt_sx', 1.0), getattr(self, 'crt_sy', 1.0)       # (beam trace: the switch-on bulge)
        if _csx != 1.0 or _csy != 1.0:
            _cx, _cy = ALG_MAX_X / 2.0, ALG_MAX_Y / 2.0
            x0 = _cx + (x0 - _cx) * _csx; x1 = _cx + (x1 - _cx) * _csx; y0 = _cy + (y0 - _cy) * _csy; y1 = _cy + (y1 - _cy) * _csy
        cgain = getattr(self, 'crt_gain', 1.0) * getattr(self, 'hv_light', 1.0)        # (R525: the high voltage's light and
        cwidth = getattr(self, 'crt_width', 1.0) * getattr(self, 'hv_width', 1.0)      #  spot, vectrex_emu.unit_picture)
        gp = 160.0 * sc
        ax = (x0 + ox) * sc; ay = h - (y0 + oy) * sc
        bx = (x1 + ox) * sc; by = h - (y1 + oy) * sc
        dx, dy = bx - ax, by - ay
        lp = np.sqrt(dx * dx + dy * dy)
        kb = getattr(self, 'knob_bright', 0.0)
        web = (b & 255) == 128                             # (the blanked beam, see knob_bright)
        zi = np.maximum(0.0, (b & 127) / 127.0 + kb)       # the drive: intensity + the knob's bias
        hi = zi >= BEAM_LOWZ                               # (above 1, the knob up: on along the same line)
        zk = np.interp(zi * 127.0, np.arange(TUBE_LIGHT_D + 1.0), TUBE_LIGHT) ** (1 / 2.2)   # the measured tube (beam_z)
        zw = np.where(hi, 1.0, BEAM_LOWW + (1.0 - BEAM_LOWW) * zi / BEAM_LOWZ)
        fw = (BEAM_W0 + BEAM_WZ * zi) * zw * cwidth
        halo = (b & 256) != 0
        moving = ~same & ~halo                             # (a spot only where the beam stood still)
        rest = ~halo & ~moving
        kk = np.zeros(n)
        if halo.any():
            lk = 0
            fw = np.where(halo, REST_HALO_W[lk] * zw * cwidth, fw)
            ce = np.where((b & 1024) != 0, cyc - DOT_C0, cyc)
            kk = np.where(halo, getattr(self, 'halo_k', BEAM_HALO_K) / BEAM_HALO_K * (REST_HALO[lk] * dot_light(ce)) ** REST_HALO_P, kk)
        if getattr(self, 'even_beam', False):
            kk = np.where(halo, kk, BEAM_LINE)
        else:
            with np.errstate(divide='ignore', invalid='ignore'):
                ee = np.where(moving, cyc / np.where(moving, lp / gp, 1.0), 0.0)
            kk = np.where(moving, BEAM_LINE * (1.0 - BEAM_SPEED * np.exp(-ee / 0.8)), kk)
            if rest.any():
                al = dot_light(np.where((b & 1024) != 0, cyc - DOT_C0, cyc))   # (joined: DOT_C0 less)
                tl = zk ** 2.2                             # (the intensity's light, x the 7F line's)
                kk = np.where(rest, DOT_K[0] * getattr(self, 'dot_k', 1.0) * al ** (1 / 2.2), kk)
                rd = np.maximum(0.0, (zi - BEAM_Z0DOT) / (BEAM_LOWZ - BEAM_Z0DOT))
                zdot = (0.55 + 0.45 * BEAM_LOWZ) * rd ** BEAM_DOTLOWP
                zk = np.where(rest & (zi < BEAM_LOWZ), np.maximum(zk, zdot * np.minimum(1.0, cyc / BEAM_DOTLOWC)), zk)
                fw = np.where(rest, fw * (1.0 + (DOT_W[0] - 1.0) * np.minimum(1.0, al * tl / DOT_WL)), fw)   # (see DOT_WL)
        sg = np.maximum(0.6, fw * gp / 2.355)
        kk = kk * zk * gain * BEAM_PEAK * cgain
        kk = np.where(((b & 128) != 0) & ~web, kk * BEAM_FADE, kk)
        mq = 2.6 * sg + 1.0
        if beam:                                           # the spot's energy: the tube's light for the drive x cycles
            fwb = (BEAM_W0 + BEAM_WZ * zi) * zw * cwidth
            upx = gp / BT_UNITS                            # (window px a unit)
            hal = (b & 256) != 0                           # (the glass's glow of a piece)
            sg = np.maximum(0.5, BEAM_TRACE_SG * fwb * gp / 2.355 * float(os.environ.get('VX_BT_SPOT', BT_SPOT)))
            _hf, _hs = [float(v) for v in os.environ.get('VX_BT_HALO', '%g,%g' % (BT_HALO_F, BT_HALO_S)).split(',')]   # (tests)
            sg = np.where(hal, _hs * upx, sg)
            kk = ((bl if bl is not None else bt_light(np.interp(zi * 127.0, np.arange(TUBE_LIGHT_D + 1.0), TUBE_LIGHT), lp / upx, cyc))
                  / (2.0 * math.pi * (sg / upx) ** 2) * np.where(hal, _hf, 1.0 - _hf) * gain * cgain / BT_KMAX)   # (a rest's peak for that light: the shader spreads it)
            kk = np.where(((b & 128) != 0) & ~web, kk * BEAM_FADE, kk)
            if self.blook(): sg = np.where(hal, sg, sg * self.bwidth())   # (Bright, Medium: the beam wider, its peak kept - more light from a thin line)
            mq = 3.5 * sg + 1.0
        moved = lp > 1e-3
        lps = np.where(moved, lp, 1.0)
        ux = np.where(moved, dx / lps * mq, mq); uy = np.where(moved, dy / lps * mq, 0.0)
        nx, ny = -uy, ux
        # the joints' cuts (see CAPSULE_SHADER): the bisector of this piece's and the neighbour's direction; a sharp
        # turn (more than ~120 degrees) or a piece without length keeps its round end
        dxu = np.where(moved, dx / lps, 0.0); dyu = np.where(moved, dy / lps, 0.0)
        op = op.copy()
        n0x = np.zeros(n); n0y = np.zeros(n); n1x = np.zeros(n); n1y = np.zeros(n)
        if n > 1:
            sx_, sy_ = dxu[:-1] + dxu[1:], dyu[:-1] + dyu[1:]
            sl = np.sqrt(sx_ * sx_ + sy_ * sy_)
            ok = (sl > 1.0) & moved[:-1] & moved[1:] & ((op[1:] & 1) != 0) & ((op[:-1] & 2) != 0)
            bxn = np.where(ok, sx_ / np.where(ok, sl, 1.0), 0.0); byn = np.where(ok, sy_ / np.where(ok, sl, 1.0), 0.0)
            n0x[1:] = bxn; n0y[1:] = byn; n1x[:-1] = bxn; n1y[:-1] = byn
            # (an open end without a cut - its neighbour not open, a sharp turn: closed, round)
            op[1:] = np.where(((op[1:] & 1) != 0) & ~ok, op[1:] & ~1, op[1:])
            op[:-1] = np.where(((op[:-1] & 2) != 0) & ~ok, op[:-1] & ~2, op[:-1])
        if (op & 1).any() or (op & 2).any():
            pass
        sa = np.ones(n); sb = np.ones(n)                   # (the quads run past joints too: the cuts end them)
        V = np.empty((n, 8), dtype=np.float32)
        V[:, 0] = ax - ux * sa + nx; V[:, 1] = ay - uy * sa + ny
        V[:, 2] = bx + ux * sb + nx; V[:, 3] = by + uy * sb + ny
        V[:, 4] = bx + ux * sb - nx; V[:, 5] = by + uy * sb - ny
        V[:, 6] = ax - ux * sa - nx; V[:, 7] = ay - uy * sa - ny
        ex = np.where(moved, dx / lps * 1e4, 0.0); ey = np.where(moved, dy / lps * 1e4, 0.0)
        A2x = np.where(op & 1, ax - ex, ax); A2y = np.where(op & 1, ay - ey, ay)
        B2x = np.where(op & 2, bx + ex, bx); B2y = np.where(op & 2, by + ey, by)
        T = np.empty((n, 4, 4), dtype=np.float32)
        T[:, :, 0] = A2x[:, None]; T[:, :, 1] = A2y[:, None]; T[:, :, 2] = B2x[:, None]; T[:, :, 3] = B2y[:, None]
        T2 = np.empty((n, 4, 4), dtype=np.float32)
        T2[:, :, 0] = n0x[:, None]; T2[:, :, 1] = n0y[:, None]; T2[:, :, 2] = n1x[:, None]; T2[:, :, 3] = n1y[:, None]
        T3 = np.zeros((n, 4, 2), dtype=np.float32)
        T3[:, :, 0] = (n0x * ax + n0y * ay)[:, None]; T3[:, :, 1] = (n1x * bx + n1y * by)[:, None]
        C = np.empty((n, 4, 4), dtype=np.float32)
        if col is not None:                                # (the 3D Imager: each line through its filter)
            cc = col[idx] & 7
            rgb = np.ones((n, 3))
            for code, (r_, g_, b_) in self.IMAGER_RGB.items():
                m_ = cc == code
                if m_.any():
                    rgb[m_] = (r_, g_, b_)
            C[:, :, 0] = (kk * rgb[:, 0])[:, None]; C[:, :, 1] = (kk * rgb[:, 1])[:, None]; C[:, :, 2] = (kk * rgb[:, 2])[:, None]
        else:
            C[:, :, 0] = kk[:, None]; C[:, :, 1] = kk[:, None]; C[:, :, 2] = kk[:, None]
        C[:, :, 3] = np.minimum(1.0, sg / 16.0)[:, None]
        TT = np.repeat((t0 / 1.5e6).astype(np.float32), 4)
        cv = lambda a: (ctypes.c_float * a.size).from_buffer(a)   # (the arrays as they are: no copy)
        return (cv(V.reshape(-1)), cv(T.reshape(-1)), cv(C.reshape(-1)), cv(TT), n, float(TT.max()) if n else 0.0, beam,
                cv(T2.reshape(-1)), cv(T3.reshape(-1)))

    def _build_py(self, lines, gain=1.0, load=True):
        """the vertex arrays for these lines, line by line (the reference for _build_np, and the way when the
        tube switches on or off)"""
        if not lines:
            return None
        ld = self._pic_load(lines)                         # (brightness bending: the core's lines, before the halos)
        if _np is not None and len(lines) and all(len(ln) > 6 for ln in lines):   # (rests joined to lines: see DOT_C0)
            A = _np.asarray([ln[:7] for ln in lines], dtype=float)
            bj = mark_joined(A[:, 0], A[:, 1], A[:, 2], A[:, 3], A[:, 4].astype(_np.int64), A[:, 5], A[:, 6])
            lines = [tuple(ln[:4]) + (int(bj[i]),) + tuple(ln[5:]) for i, ln in enumerate(lines)]
        halos = [(ln[0], ln[1], ln[2], ln[3], (ln[4] & (127 | 1024)) | 256) + tuple(ln[5:]) for ln in lines
                 if len(ln) > 5 and Renderer_rest(ln) and ln[5] >= 64 and (ln[4] & 127)
                 and not (ln[4] & 512 and ln[5] < 8.0 * REST_DWELL_HALO)] \
            if getattr(self, 'halo_on', True) and not getattr(self, 'even_beam', False) else None
        if halos:
            lines = halos + list(lines)                    # (drawn first: the spots and lines go over them)
        lines = self._tube_lines(lines, self._joins(lines))
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
        # brightness bending: this picture's beam load (ld, above: beam_load), smoothed
        self.load_f = getattr(self, 'load_f', ld)
        if load:
            self.load_f += (ld - self.load_f) * 0.3
        bend = getattr(self, 'bend_on', False) and self.bend_strength > 0
        # the tube switching on / off: brightness (warm-up) and picture size (bulge, collapse)
        cgain = getattr(self, 'crt_gain', 1.0) * getattr(self, 'hv_light', 1.0)   # (R525: the high voltage, see unit_picture)
        kb = getattr(self, 'knob_bright', 0.0)            # (the brightness knob: see knob_bright)
        cwidth = getattr(self, 'crt_width', 1.0) * getattr(self, 'hv_width', 1.0)   # (the spot blooms as the picture collapses)
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
        ends = []                                          # (per piece: its ends, direction, open ends - for the cuts)
        opens = getattr(self, '_tube_open', None)
        if opens is not None and len(opens) != len(lines): opens = None
        for li, ln in enumerate(lines):
            x0, y0, x1, y1, b = ln[:5]
            zlen = bool(opens[li] & 4) if opens else Renderer_rest(ln)   # (a resting spot: see REST_SPEED)
            if zlen:
                x0, y0 = x1, y1                            # (where the spot came to rest)
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
            web = (b & 255) == 128                         # (the blanked beam, see knob_bright)
            zi = max(0.0, (b & 127) / 127.0 + kb)          # the drive: intensity + the knob's bias
            zk, zw = beam_z(zi)
            fw = (BEAM_W0 + BEAM_WZ * zi) * zw * cwidth    # thin, a little fuller when brighter
            if b & 256:                                    # a resting spot's halo (see BEAM_HALO_W)
                lk = 0
                fw = REST_HALO_W[lk] * zw * cwidth
                k = getattr(self, 'halo_k', BEAM_HALO_K) / BEAM_HALO_K * float(REST_HALO[lk] * dot_light(cyc - DOT_C0 if b & 1024 else cyc)) ** REST_HALO_P
            elif getattr(self, 'even_beam', False):       # an ideal display: lines and points alike
                k = BEAM_LINE
            elif not zlen:
                e = cyc / (lp / gp)                        # cycles per game pixel
                k = BEAM_LINE * (1.0 - BEAM_SPEED * exp(-e / 0.8))   # fast sweeps slightly fainter
            else:                                          # the beam resting: a spot
                al = float(dot_light(cyc - DOT_C0 if b & 1024 else cyc)); tl = zk ** 2.2   # (joined: DOT_C0 less)
                k = DOT_K[0] * getattr(self, 'dot_k', 1.0) * al ** (1 / 2.2)   # (the Dot Test photos)
                if zi < BEAM_LOWZ:                         # a long rest crosses the cutoff earlier (beam_zdot)
                    zk = max(zk, beam_zdot(zi) * min(1.0, cyc / BEAM_DOTLOWC))
                fw *= 1.0 + (DOT_W[0] - 1.0) * min(1.0, al * tl / DOT_WL)   # (see DOT_WL)
            sg = max(0.6, fw * gp / 2.355)                 # sigma, window px
            k *= zk * gain * BEAM_PEAK * cgain
            if b & 128 and not web:                        # (the beam fading out: a faint trace)
                k *= BEAM_FADE
            m = 2.6 * sg + 1.0                             # quad margin
            if lp > 1e-3:
                ux, uy = dx / lp * m, dy / lp * m
            else:
                ux, uy = m, 0.0
            nx, ny = -uy, ux
            op = opens[li] if opens else 0
            sa = sb = 1.0                                  # (the quads run past joints too: the cuts end them, see below)
            ends.append((ax, ay, bx, by, dx / lp if lp > 1e-3 else 0.0, dy / lp if lp > 1e-3 else 0.0, lp > 1e-3, op))
            v[i] = ax - ux * sa + nx; v[i + 1] = ay - uy * sa + ny
            v[i + 2] = bx + ux * sb + nx; v[i + 3] = by + uy * sb + ny
            v[i + 4] = bx + ux * sb - nx; v[i + 5] = by + uy * sb - ny
            v[i + 6] = ax - ux * sa - nx; v[i + 7] = ay - uy * sa - ny
            i += 8
            rr = gg = bb = k; aa = min(1.0, sg / 16.0)              # pure white (owner, 2026-10-12)
            if op:                                         # (and the line runs on through the joint: no fall-off)
                ex, ey = (dx / lp * 1e4, dy / lp * 1e4) if lp > 1e-3 else (0.0, 0.0)
                ax2, ay2 = (ax - ex, ay - ey) if op & 1 else (ax, ay)
                bx2, by2 = (bx + ex, by + ey) if op & 2 else (bx, by)
            else:
                ax2, ay2, bx2, by2 = ax, ay, bx, by
            for q in range(4):
                t[j] = ax2; t[j + 1] = ay2; t[j + 2] = bx2; t[j + 3] = by2
                c[j] = rr; c[j + 1] = gg; c[j + 2] = bb; c[j + 3] = aa
                j += 4
            tl = ln[6] / 1.5e6 if len(ln) > 6 else 0.0
            tt[q4] = tt[q4 + 1] = tt[q4 + 2] = tt[q4 + 3] = tl
            q4 += 4
        # the joints' cuts (as _build_np): the bisector at each joint of two open, moving pieces; an open end without
        # one goes back to its round end (its quad and its segment)
        t2 = (ctypes.c_float * (n * 16))(); t3 = (ctypes.c_float * (n * 8))()
        cut = [[0.0, 0.0, 0.0, 0.0] for _ in range(n)]
        for k in range(n - 1):
            a_, b_ = ends[k], ends[k + 1]
            if not (a_[7] & 2 and b_[7] & 1): continue
            sx_, sy_ = a_[4] + b_[4], a_[5] + b_[5]; sl = sqrt(sx_ * sx_ + sy_ * sy_)
            if not (a_[6] and b_[6] and sl > 1.0): continue
            cut[k][2], cut[k][3] = sx_ / sl, sy_ / sl; cut[k + 1][0], cut[k + 1][1] = sx_ / sl, sy_ / sl
        for k in range(n):
            ax, ay, bx, by, ux_, uy_, mv, op = ends[k]
            c0 = cut[k][0] * ax + cut[k][1] * ay; c1 = cut[k][2] * bx + cut[k][3] * by
            for q in range(4):
                t2[k * 16 + q * 4:k * 16 + q * 4 + 4] = cut[k]
                t3[k * 8 + q * 2] = c0; t3[k * 8 + q * 2 + 1] = c1
            lost0 = (op & 1) and cut[k][0] == 0.0 and cut[k][1] == 0.0
            lost1 = (op & 2) and cut[k][2] == 0.0 and cut[k][3] == 0.0
            if lost0 or lost1:                             # (back to a round end: the segment stops at the end point)
                for q in range(4):
                    if lost0: t[k * 16 + q * 4] = ax; t[k * 16 + q * 4 + 1] = ay
                    if lost1: t[k * 16 + q * 4 + 2] = bx; t[k * 16 + q * 4 + 3] = by
        return v, t, c, tt, n, (max(tt) if n else 0.0), False, t2, t3

    # ---- the phosphor in real time (see PHOS_GAIN)
    HOLD_MAX = 0.15                                       # (a game that stops drawing: its picture fades after this)
    HOLD_EPS = 0.004                                      # (the margin: frame times jitter a little, and the phosphor
                                                          #  image keeps a spot's age to ~0.5 ms - 16-bit float)
    def blook(self):
        """the Bright look (Tube > Look, 2026-11-22: for a small screen whose thin lines give little light): the tube's light lifted and
        the beam BL_WIDTH x wider. An ordinary screen: its values x BL_GAIN before the roll-off - the faint lines come up, the top is
        pressed into white. An HDR screen: the ordinary screen's whole picture, roll-off and all, x the screen's top (TONE_SHADER: bgh).
        True for Medium too: halfway (look_s 0.5) - every lift the geometric mean of the Vectrex look's and Bright's"""
        return self.look_s() > 0.0
    def look_s(self):
        """the look's strength: 0 Vectrex, 0.5 Medium, 1 Bright (LOOK_S)"""
        return LOOK_S.get(getattr(self, 'look2', 'vectrex'), 0.0)
    def bgain(self):
        """the Bright look's lift of the tube's light as a screen value (1.0: the Vectrex look; Medium: its square root)"""
        s = self.look_s(); g = self.hdr_hk if getattr(self, 'hdr_hk', 1.0) > 1.0 else BL_GAIN
        return g if s >= 1.0 else g ** s if s > 0.0 else 1.0
    def bwidth(self):
        """the beam's width x this (Bright BL_WIDTH, Medium its square root, Vectrex 1)"""
        s = self.look_s()
        return BL_WIDTH if s >= 1.0 else BL_WIDTH ** s if s > 0.0 else 1.0
    def hold(self):
        """how long a lit spot stays at full brightness: until the game has drawn its next picture. A real tube
        keeps a slow game's picture as bright as a fast one's (the eye averages the flashes); here a spot stays
        lit until the latest picture that has started by now (T_now) is completely drawn - for a picture drawn
        in two halves on alternate frames, until the half before it is. So a game that slows down, draws its
        frames in pieces or late, keeps its brightness; a moving object's old copy goes out once the new picture
        is drawn. At most HOLD_MAX (a stalled game fades)."""
        h = PHOS_HOLD if self.trail_on else PHOS_HOLD_OFF
        T = getattr(self, 'T_now', None)
        lit = getattr(self, 'lit', None)
        if T is None or not lit:
            return self._hold_static()
        cur = [f for f in lit if f[0] <= T]
        if not cur:
            return self._hold_static()
        # the time from the previous picture's first line to the latest one's (two steps for a picture in halves): a
        # line of the previous picture stays lit until the latest picture has drawn it again (at the same moment
        # in it) - and its old copy goes out then (the wobble moves the new one a little: holding the old copy
        # longer showed both, a flicker)
        step = 2 if any(f[2] for f in cur[-3:]) else 1
        per = cur[-1][0] - cur[-1 - step][0] if len(cur) > step else 0.0
        # the newest picture's lines: lit until the next picture starts, however late (a slow or stalled frame)
        k = len(cur) - 1
        if cur[k][2] and k > 0:                            # (the second half: hold the first half's lines too)
            k -= 1
        return max(h, min(self.HOLD_MAX, max(per + self.HOLD_EPS, (T - cur[k][0]) + self.HOLD_EPS)))
    def _hold_static(self):
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
        for fb in (self.P[1], self.P2[1], self.F[1], self.D[1]) + tuple(t[1] for pr in getattr(self, 'AG', []) for t in pr):
            self.BindFB(GL_FRAMEBUFFER, fb); gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)
        self.BindFB(GL_FRAMEBUFFER, 0)
        self.young = []; self.last = None; self._hand_prev = []
    hist = property(lambda self: [], lambda self, v: self.clear())     # (older callers: hist = [] clears)

    def add_frame(self, lines, t_start, length=None, period=None):
        t = time.perf_counter()
        try:
            return self._add_frame(lines, t_start, length, period)
        finally:                                        # (the processor's time for a game frame's lines: build_ms)
            ms = 1000.0 * (time.perf_counter() - t)
            self.build_ms = ms if getattr(self, 'build_ms', None) is None else 0.9 * self.build_ms + 0.1 * ms

    def _add_frame(self, lines, t_start, length=None, period=None):
        """a game frame that started at real time t_start (perf_counter s) and lasts `length` s; its
        lines light up at their own moments, as the beam draws them. The hold follows how long until the game
        draws these lines again: its frame period - or two, for a game that draws its picture in two halves on
        alternate frames (period: given by the caller instead)"""
        if period is None and length:
            period = length
            alt = frame_alternates(self, lines)
            if alt:
                period = length + getattr(self, 'len_prev', length)
            self.len_prev = length
        if period:                                        # (the hold follows the game's frame period)
            self.periods = (getattr(self, 'periods', []) + [period])[-12:]
        self.last_raw = (lines, length, period)            # (Record > Snapshot: drawn again at a bigger size)
        b = self._build(lines)
        self.last = b
        if b:
            # (when this picture starts, how long its drawing takes, and whether it is the second half of a picture
            # drawn on two frames: for the hold - see hold())
            tdraw = b[5] if len(b) > 5 else max(b[3])
            try:
                tmin = float(_np.frombuffer(b[3], dtype=_np.float32).min()) if _np is not None else min(b[3])
            except (ValueError, TypeError):
                tmin = 0.0
            alt = bool(period and length and period > length * 1.5)
            # (its first line's moment, its drawing span, second half)
            self.lit = (getattr(self, 'lit', []) + [(t_start + tmin, max(0.0, tdraw - tmin), alt)])[-8:]
            hp = getattr(self, 'hand_pairs', None)          # (the hand-over: see _hand_build)
            hb = self._hand_build(lines, alt, length, hp) if hp is not None and length else None
            if hp is None or not length:
                self._hand_prev = []
            self.young.append([b, t_start, t_start + (b[5] if len(b) > 5 else max(b[3])), -1e9, hb])
            if os.environ.get('VX_TEST_DOUBLE'): self.young.append(list(self.young[-1]))   # (a test: every frame twice in a picture - the averaging)
        else:
            self._hand_prev = []

    # ---- the hand-over: the screen's own rate (vsync, not G-Sync), Smooth motion off. A line stays lit until the next game frame
    # has drawn it again, plus HOLD_EPS (the frame times jitter: a still line went dark a moment before its redraw). A line that
    # moved was so lit twice, its old copy beside the new one for those 4 ms - and with 50 game frames on 60 refreshes a refresh
    # falls within 4 ms after a redraw about one time in four: moving things doubled (the owner: Vectrexians' aliens, Mine Storm's
    # mothership, the Ship Test). A whole picture letting go of what it did not draw (2026-11-22, whole_plain) took the rows a game
    # leaves out of a frame with it - Vectrexians'. So line by line: the old copy of a line that moved goes out in the picture its new
    # copy is drawn in - its partner found as Smooth motion pairs strokes (App.hold_pairs: certain partners only), the old copy's
    # pieces drawn as a mask at their partner's moment (_present, HM; UPDATE_SHADER ho). A line without a partner - left out of
    # the next frame, or not certain - keeps its hold, margin and all; a line drawn again in the same place is lit again as before
    HAND_MAX = 2                                       # (the frames before paired with a new one: 2 for a picture in halves)
    def _hand_build(self, lines, alt, length, hp):
        """the hand-over's mask for this game frame: the pieces of the frame(s) before that moved and are drawn again in this one,
        built where they were drawn (their own frame's hum), their moments their partners' in this frame. ([built...], rmin) or
        None. hp: App.hold_pairs"""
        np = _np
        if np is None:
            return None
        try:
            src = getattr(lines, 'np', None)
            B = np.asarray(src if src is not None and len(src) == len(lines) else lines, dtype=np.float64)
        except (ValueError, TypeError):
            self._hand_prev = []
            return None
        prev = getattr(self, '_hand_prev', None) or []
        self._hand_prev = [(B, getattr(self, 'frame_t0', 0.0))] + prev[:self.HAND_MAX - 1]
        if B.ndim != 2 or B.shape[1] < 7 or not len(B):
            return None
        sc = self.map[0] if hasattr(self, 'map') else 0.0
        dmin = 1.0 / sc if sc > 0 else 24.0               # (moved a window pixel or more: a line moved less is lit again in place)
        out = []
        f0 = self.frame_t0
        try:
            for A, ft0 in (prev[:2] if alt else prev[:1]):
                if A.ndim != 2 or A.shape[1] != B.shape[1] or not len(A):
                    continue
                r = hp(A, B, dmin)
                if r is None:
                    continue
                R, sh = r                                 # (the lines, how much later their partners are drawn - cycles)
                o = np.argsort(R[:, 6], kind='stable'); R = np.ascontiguousarray(R[o]); sh = sh[o] / 1.5e6
                self.frame_t0 = ft0                       # (built where it was drawn: its frame's hum)
                hb = self._build(R, load=False)
                if not hb:
                    continue
                # (each built piece's moment: its line's, found by its line's start - a line's pieces lie between its start and the
                #  next line's - moved by that line's shift)
                tt = np.frombuffer(hb[3], dtype=np.float32)
                k = np.clip(np.searchsorted((R[:, 6] / 1.5e6).astype(np.float32), tt, 'right') - 1, 0, len(R) - 1)
                tt += sh[k].astype(np.float32)
                out.append(hb)
        except Exception as e:   # noqa
            if not getattr(self, '_hand_said', False):
                self._hand_said = True; print('hand-over: %s' % e)
            return None
        finally:
            self.frame_t0 = f0
        return (out, 0.5 * length) if out else None

    # Quality profiles (the control deck, Vectrex > Quality): each sets these switches - the costly refinements - and
    # leaves the rest of the look alone; a switch changed by hand makes the profile Custom (the app's
    # current_profile). Chosen by hand, or suggested by Test this PC (pctest.py) - nothing switches by itself while
    # playing. Ultra - everything on (the defaults). High - the room's light and its reflections off, the afterglow
    # packed in one image. Medium - also the tube's curved geometry and glass edge, the overlay's scratches and plastic
    # glow, the case's reflections and the dither, the phosphor at three-quarter size. Low - Medium, the phosphor
    # image (the afterglow, the display pass) at half the window's size while the lines themselves are still drawn at
    # full size (phos_scale: the GPU cost of those passes is per pixel), and 30 pictures a second instead of 60
    # (half_rate). Basic - Low, the lines at the phosphor's size too (line_half).
    PROFILE_KEYS = ('curve_on', 'glass_on', 'glass_room', 'light_on', 'light_ov', 'art_glow', 'scr_on',
                    'art_parallax', 'case_refl', 'dither_on', 'phos_scale', 'half_rate', 'phos_full', 'line_half')
    _MEDIUM = dict(curve_on=False, glass_on=False, glass_room=False, light_on=False, light_ov=False,
                   art_glow=False, scr_on=False, case_refl=0.0, dither_on=False, phos_full=False)   # (parallax kept)
    PROFILE_OFF = {'high': dict(light_on=False, light_ov=False, glass_room=False, phos_full=False),
                   'medium': dict(_MEDIUM, phos_scale=0.75),     # (three-quarter phosphor: Pi 5 V3D at 1080p 62 -> 37 ms
                                                                    #  a picture, 16 -> 27 a second; a slightly softer glow)
                   'low': dict(_MEDIUM, phos_scale=0.5, half_rate=True),
                   'basic': dict(_MEDIUM, phos_scale=0.5, half_rate=True, line_half=True)}   # (Low, the lines at the
                                                                    #  phosphor's half size too: Pi 5, Vectrexians)
    phos_scale = 1.0                 # the phosphor passes' size as a share of the window's (see PROFILE_OFF)
    def glass_undo(self, k=None):
        """how much of the glass's curve is undone: 0 as seen on a Vectrex (Glass curvature 1), 1 even squares (0) - only
        with the tube's map on (k > 0; None: as set), which has the curve in it. Without it the picture is flat and
        nothing is undone: undoing a curve that is not there stretched the picture's edges (the owner, 2026-11-25:
        Glass curvature 0 has to mean no curve)"""
        if k is None:
            k = getattr(self, 'geom_k', 1.0) if getattr(self, 'curve_on', False) else 0.0
        if k <= 0:
            return 0.0
        return max(0.0, min(1.0, 1.0 - float(getattr(self, 'glass_curve', 1.0))))
    line_half = False                # Basic: the lines drawn at the phosphor's size too (softer; a quarter of the work)
    phos_full = True                 # Ultra's fidelity: the afterglow in full colour, both parts (else packed: half the
                                     #  memory and traffic, a 3D Imager's colours fade to white in the slow tail -
                                     #  GLOW_PACKED), and several game frames in one picture averaged per pixel (else
                                     #  each at 1 / n: see _present)
    DRAW_CHUNK = int(os.environ.get('VECTREX_DRAW_CHUNK', '2048'))   # capsules per draw call (see _draw_built)
    half_rate = False                # 30 pictures a second on a 60 Hz screen (the app's apply_rate reads it)
    quality = 2
    def _gpu_begin(self):
        q = getattr(self, 'gpu_q', None)
        if not q:
            return None
        busy = self.gpu_q_busy
        while busy:                                     # (the oldest finished: its time)
            ok = ctypes.c_int(0)
            self.QueryIV(busy[0], 0x8867, ctypes.byref(ok))      # (GL_QUERY_RESULT_AVAILABLE)
            if not ok.value:
                break
            ns = ctypes.c_uint64(0)
            self.QueryU64(busy[0], 0x8866, ctypes.byref(ns))     # (GL_QUERY_RESULT)
            ms = ns.value / 1e6
            self.gpu_ms = ms if self.gpu_ms is None else 0.9 * self.gpu_ms + 0.1 * ms
            q.append(busy.pop(0))
        if not q:
            return None
        qid = q.pop(0)
        self.BeginQ(0x88BF, qid)                        # (GL_TIME_ELAPSED)
        return qid

    def _gpu_end(self, qid):
        if qid is not None:
            self.EndQ(0x88BF)
            self.gpu_q_busy.append(qid)

    def present(self, T, imm=None, flush=False):
        ps = min(1.0, max(0.25, float(getattr(self, 'phos_scale', 1.0))))
        if getattr(self, 'D', None) and getattr(self, '_win_wh', None) and (abs(ps - getattr(self, '_ps_alloc', 1.0)) > 1e-6
                                                                          or bool(getattr(self, 'line_half', False)) != getattr(self, '_lh_alloc', False)):
            self.resize(*self._win_wh)                    # (Quality changed the phosphor's size: new targets)
        self._showing_last = bool(getattr(self, 'frozen', False) and not flush and getattr(self, 'last_ok', False))   # (paused: see put_last)
        self._t_pres = time.perf_counter()
        self._q_cur = self._gpu_begin()
        try:
            return self._present(T, imm, flush)
        finally:
            q, self._q_cur = getattr(self, '_q_cur', None), None
            self._gpu_end(q)

    def _swap(self):
        q, self._q_cur = getattr(self, '_q_cur', None), None   # (the picture's work ends here: not the wait for the
        self._gpu_end(q)                                        #  refresh)
        t = getattr(self, '_t_pres', None)
        if t is not None:                                       # (the processor's time for the picture: cpu_ms)
            ms = 1000.0 * (time.perf_counter() - t)
            self.cpu_ms = ms if getattr(self, 'cpu_ms', None) is None else 0.9 * self.cpu_ms + 0.1 * ms
            self._t_pres = None
        if getattr(self, '_showing_last', False):
            self.put_last(getattr(self, 'frozen_box', None))   # (paused: the picture's part as it was shown, over what was drawn anew)
            self.keep_picture('SHOWN')                          # (what is on the screen now - the snapshot's; LAST stays the tube's picture)
        else:
            self.keep_picture()
        return super()._swap()

    # ---------------------------------------------------------------- the picture last shown, kept
    # Pause and the snapshot need the picture exactly as it was shown (the owner, 2026-11-22): drawn again, a picture differs -
    # the lines' fresh light, their bloom and the glass's glow are in the frames just drawn, and those are gone a moment later
    # (paused, the bloom faded away; a snapshot came some frames after the key). So every finished picture is copied into
    # LAST before it is shown (the window's size; an HDR picture as half floats).
    def keep_picture(self, name='LAST'):
        try:
            gl = self.gl
            w, h = getattr(self, 'real_size', self.size)
            key = (w, h, bool(getattr(self, 'hdr', None)))
            if getattr(self, '_key_' + name, None) != key:
                old = getattr(self, name, None)
                if old:
                    gl.glDeleteTextures(1, ctypes.byref(ctypes.c_uint(old[0]))); self.DelFB(1, ctypes.byref(ctypes.c_uint(old[1])))
                setattr(self, name, self._target(w, h)); setattr(self, '_key_' + name, key)
                if not hasattr(gl.glCopyTexSubImage2D, '_set'):
                    gl.glCopyTexSubImage2D.argtypes = [ctypes.c_uint] + [ctypes.c_int] * 7
            self.BindFB(GL_FRAMEBUFFER, 0)
            self.ActiveTexture(0x84C0); gl.glBindTexture(GL_TEXTURE_2D, getattr(self, name)[0])
            gl.glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h)
            gl.glBindTexture(GL_TEXTURE_2D, 0)
            if name == 'LAST': self.last_ok = True
            self.shown = getattr(self, name)
        except Exception as e:   # noqa
            if not getattr(self, '_last_said', False):
                self._last_said = True; print('the last picture not kept (%s)' % e)
            self.last_ok = False

    def put_last(self, box):
        """paused: the part box (x, y from the bottom left, w, h) of the picture last shown put over the window's - the tube's
        picture stands as it was, the deck round it is drawn anew (PAUSED, the knob); no box: nothing (the ROM browser lies over it)"""
        if not (box and getattr(self, 'last_ok', False) and getattr(self, 'LAST', None)):
            return
        gl = self.gl
        w, h = getattr(self, 'real_size', self.size)
        self.BindFB(GL_FRAMEBUFFER, 0); gl.glViewport(0, 0, w, h)
        self.UseProgram(0); gl.glDisable(GL_BLEND); self.ActiveTexture(0x84C0)
        gl.glEnable(0x0C11); gl.glScissor(int(box[0]), int(box[1]), int(box[2]), int(box[3]))   # (GL_SCISSOR_TEST)
        try:
            self._quad(self.LAST[0])
        finally:
            gl.glDisable(0x0C11)

    def read_last(self, box):
        """the picture last shown, the part box (x, y from the bottom left, w, h): bgra rows, bottom row first - None if not kept"""
        sh = getattr(self, 'shown', None)
        if not (getattr(self, 'last_ok', False) and sh):
            return None
        try:
            px = self.read_px(sh[1], box)
        finally:
            self.BindFB(GL_FRAMEBUFFER, 0)
        return px

    # ---------------------------------------------------------------- HDR (Windows): hdrwin.py
    def hdr_start(self, force=False, frac=1.0):
        """the picture through DirectX in scRGB when the window's monitor has HDR on (force: also without - the tests);
        frac: how much of the screen's headroom above white (HDR brightness). Returns a line for the log"""
        if not IS_WIN:
            return 'HDR: not on this system'
        if self.hdr or not self.hdrprog:
            return ''
        try:
            import hdrwin
            r = W.RECT(); ctypes.windll.user32.GetWindowRect(W.HWND(self.hwnd), ctypes.byref(r))
            mons = [m for m in hdrwin.probe((r.left, r.top, r.right, r.bottom)) if 'name' in m]
            m = mons[0] if mons else None
            if not m or not (m['hdr'] or force):
                return 'HDR: the monitor shows no HDR (Windows: Settings > Display > Use HDR)'
            rw, rh = self.real_size
            self.hdr = hdrwin.Out(self, self.hwnd, rw, rh)
            self.hdr_sc = m['sdr_white'] / 80.0 if m['hdr'] else 1.0
            room = max(1.0, m['peak'] / m['sdr_white']) if m['hdr'] else 1.0
            self.hdr_hk = 1.055 * (1.0 + (room - 1.0) * frac) ** (1 / 2.4) - 0.055 if room > 1.05 and frac > 0.01 else 1.0
            self.hdr_w = None; self._hdr_target()
            return 'HDR: on (%s, peak %.0f nits, SDR white %.0f nits: %.1fx above white, top %.2f)%s' % (
                m['name'], m['peak'], m['sdr_white'], room, self.hdr_hk, '' if m['hdr'] else ' - forced on an SDR screen')
        except Exception as e:   # noqa
            if self.hdr:
                try:
                    self.hdr.close()
                except Exception:   # noqa
                    pass
            self.hdr = None; self.hdr_hk = 1.0
            return 'HDR: not available (%s)' % e

    def hdr_stop(self):
        if self.hdr:
            self.hdr.close(); self.hdr = None; self.hdr_hk = 1.0

    def _hdr_target(self):
        """the window's picture drawn here (half floats: room above white); the HDR output at the window's size"""
        gl = self.gl; rw, rh = self.real_size
        if getattr(self, 'hdr_w', None):
            gl.glDeleteTextures(1, ctypes.byref(ctypes.c_uint(self.hdr_w[0]))); self.DelFB(1, ctypes.byref(ctypes.c_uint(self.hdr_w[1])))
        t = ctypes.c_uint(); gl.glGenTextures(1, ctypes.byref(t)); gl.glBindTexture(GL_TEXTURE_2D, t.value)
        for k, v in ((GL_TEX_MIN, 0x2600), (GL_TEX_MAG, 0x2600), (GL_WRAP_S, GL_CLAMP_EDGE), (GL_WRAP_T, GL_CLAMP_EDGE)):   # (GL_NEAREST: 1:1)
            gl.glTexParameteri(GL_TEXTURE_2D, k, v)
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, rw, rh, 0, GL_RGBA, GL_HALF_FLOAT, None)
        f = ctypes.c_uint(); self.GenFB(1, ctypes.byref(f)); self._bind_raw(GL_FRAMEBUFFER, f.value)
        self.FBTex(GL_FRAMEBUFFER, GL_COLOR_ATT0, GL_TEXTURE_2D, t.value, 0)
        self.hdr_w = (t.value, f.value)
        self.hdr.size(rw, rh)
        self.BindFB(GL_FRAMEBUFFER, 0)

    def _hdr_show(self):
        """the window's picture onto the HDR screen: made linear, scaled, into DirectX's texture - shown"""
        gl = self.gl; rw, rh = self.real_size
        self._bind_raw(GL_FRAMEBUFFER, self.hdr.fbo.value); gl.glViewport(0, 0, rw, rh); gl.glDisable(GL_BLEND)
        self.UseProgram(self.hdrprog); self.Uniform1f(self.u_hsc, self.hdr_sc)   # (the texture: unit 0)
        gl.glMatrixMode(GL_PROJECTION); gl.glPushMatrix(); gl.glLoadIdentity(); gl.glOrtho(0, 1, 0, 1, -1, 1); gl.glMatrixMode(GL_MODELVIEW)
        gl.glEnable(GL_TEXTURE_2D); gl.glBindTexture(GL_TEXTURE_2D, self.hdr_w[0]); gl.glColor4f(1, 1, 1, 1)
        gl.glBegin(GL_QUADS)                                    # (DirectX's rows from the top: the picture turned over)
        for x, y in ((0, 0), (1, 0), (1, 1), (0, 1)):
            gl.glTexCoord2f(x, 1 - y); gl.glVertex2f(x, y)
        gl.glEnd(); gl.glDisable(GL_TEXTURE_2D)
        gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW)
        self.UseProgram(0); gl.glEnable(GL_BLEND)
        self.hdr.show(False)
        self.BindFB(GL_FRAMEBUFFER, 0); gl.glViewport(0, 0, *self.size)

    def _present(self, T, imm=None, flush=False):
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
            bb = self._build(imm) if imm else self.last
            if bb and len(bb) > 6 and bb[6]:              # (beam trace: the energy summed, then as light)
                fw, fh = getattr(self, 'fsize', (w, h))   # (Basic: F at the phosphor's size - drawn to its size)
                self.BindFB(GL_FRAMEBUFFER, self.F[1]); gl.glViewport(0, 0, fw, fh)
                gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)
                gl.glBlendFunc(GL_ONE, GL_ONE)
                self.fcs = (w / float(fw), h / float(fh))
                self._draw_built(bb)
                self.fcs = (1.0, 1.0)
                gl.glDisable(GL_BLEND)
                self.BindFB(GL_FRAMEBUFFER, self.out); gl.glViewport(0, 0, w, h)
                self._sat(self.F[0], self._wb())
                bb = None
            elif even:                                    # (even beam: the brighter light, not the sum - max
                self.BlendEq(0x8008); self.blend_max = True   #  ignores alpha: the spot's falloff in the colour)
            if bb:
                self._draw_built(bb)
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
        self.T_now = T
        # 1. the fresh light: lines add up within a frame (rgb), the newest life wins (alpha)
        fw, fh = getattr(self, 'fsize', (w, h))           # (Basic: drawn at the phosphor's size - CAPSULE_SHADER's fcs)
        self.fcs = (w / float(fw), h / float(fh))
        # 0. the hand-over (see _hand_build): the old copies of the lines whose new copies this picture draws - a mask, HM
        hand = [f for f in self.young if len(f) > 4 and f[4]] if (getattr(self, 'HM', None) and not flush) else []
        hm_rmin = None
        if hand:
            self.BindFB(GL_FRAMEBUFFER, self.HM[1]); gl.glViewport(0, 0, fw, fh)
            gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)
            gl.glEnable(GL_BLEND); self.BlendFuncSep(GL_ONE, GL_ONE, GL_ONE, GL_ONE); self.BlendEqSep(0x8006, 0x8008)
            for f in hand:
                for hb in f[4][0]:
                    self._draw_built(hb, T - f[1], T - f[3])
            hm_rmin = min(f[4][1] for f in hand)
        self.BindFB(GL_FRAMEBUFFER, self.F[1]); gl.glViewport(0, 0, fw, fh)
        gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)
        gl.glEnable(GL_BLEND); self.BlendFuncSep(GL_ONE, GL_ONE, GL_ONE, GL_ONE)
        _eb = getattr(self, 'even_beam', False) and not (getattr(self, 'beam_trace', False) and hasattr(self, 'satprog'))   # (beam trace: light adds, always)
        self.BlendEqSep(0x8008 if _eb else 0x8006, 0x8008)   # (add - even beam: max -, max)
        keep = []
        eq = 0x8008 if _eb else 0x8006
        avg = len(self.young) > 1 and getattr(self, 'max_frames', True) and getattr(self, 'covprog', None)
        if avg and not getattr(self, 'phos_full', True):  # (High, Medium, Low: not averaged per pixel - four passes at the
            avg = False                                   #  window's size, on the Pi 5 43 ms of Low's 59 a picture; each
            self.frame_w = 1.0 / len(self.young)          #  frame's light at 1 / n instead: a still line as bright, only
                                                          #  what one frame alone draws dimmer - 2026-10-29)
        if avg:                                           # (several game frames in this display frame - 30 pictures a
            self.BindFB(GL_FRAMEBUFFER, self.Fc[1])       #  second, a 50 Hz game on a 60 Hz screen: each frame's light
            gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)   #  on its own, summed, then averaged per pixel -
        for n, f in enumerate(self.young):                #  FRESH_COV / FRESH_NORM. Added up, every line drawn again
            b, t0, t1, done = f[:4]                       #  glowed twice as bright; the brighter of the two kept a line
            if avg:                                       #  that moved at both places at full strength. Within a frame
                self.BindFB(GL_FRAMEBUFFER, self.F3[1])   #  lines still add up - the owner, 2026-10-29)
                gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)
                self.BlendEqSep(eq, 0x8008); self.BlendFuncSep(GL_ONE, GL_ONE, GL_ONE, GL_ONE)
            self._draw_built(b, T - t0, 1e9 if flush else T - done)
            if avg:
                self.UseProgram(0)
                self.BindFB(GL_FRAMEBUFFER, self.F[1])    # (the sum; the newest freshness)
                self.BlendEqSep(0x8006, 0x8008); self.BlendFuncSep(GL_ONE, GL_ONE, GL_ONE, GL_ONE)
                self._quad(self.F3[0])
                self.BindFB(GL_FRAMEBUFFER, self.Fc[1])   # (and who drew where)
                self.BlendEqSep(0x8006, 0x8006)
                self.UseProgram(self.covprog); self.Uniform1i(self.u_c, 0)
                self.Uniform1f(self.u_ck, float(os.environ.get('VX_BT_CK', 8.0)) * BT_EXPO * BT_SPOT * BT_KMAX if (getattr(self, 'beam_trace', False) and hasattr(self, 'satprog')) else 1.0)
                self._quad(self.F3[0]); self.UseProgram(0)
            f[3] = T
            if not flush and T < t1:
                keep.append(f)
        self.frame_w = 1.0
        if avg:
            self.BindFB(GL_FRAMEBUFFER, self.F[1])
            self.BlendEqSep(eq, 0x8008); self.BlendFuncSep(GL_ONE, GL_ONE, GL_ONE, GL_ONE)
        if imm:                                           # (imm_max: one even line, no bright joints)
            if getattr(self, 'imm_max', False): self.BlendEq(0x8008)
            self._draw_built(self._build(imm, load=False), 0.0, 1.0, imm=True)
        self.BlendEq(0x8006)                              # (and back to adding, for everything else)
        self.young = keep
        fresh = self.F[0]
        if avg:                                           # (the sum / how many drew each pixel)
            gl.glDisable(GL_BLEND)
            self.BindFB(GL_FRAMEBUFFER, self.F4[1])
            self.UseProgram(self.avgprog); self.Uniform1i(self.u_n[0], 0); self.Uniform1i(self.u_n[1], 1)
            self.ActiveTexture(0x84C1); gl.glBindTexture(GL_TEXTURE_2D, self.Fc[0]); self.ActiveTexture(0x84C0)
            self._quad(self.F[0]); self.UseProgram(0)
            self.ActiveTexture(0x84C1); gl.glBindTexture(GL_TEXTURE_2D, 0); self.ActiveTexture(0x84C0)
            fresh = self.F4[0]
        if getattr(self, 'beam_trace', False) and hasattr(self, 'satprog'):   # (beam trace: the energy as light)
            self.BindFB(GL_FRAMEBUFFER, self.F2[1]); gl.glDisable(GL_BLEND)
            self._sat(fresh); fresh = self.F2[0]
        hm_tex = None
        if hm_rmin is not None:                           # (the hand-over's mask, as light - as the fresh light)
            hm_tex = self.HM[0]
            if getattr(self, 'beam_trace', False) and hasattr(self, 'satprog'):
                self.BindFB(GL_FRAMEBUFFER, self.HM2[1]); gl.glDisable(GL_BLEND)
                self._sat(self.HM[0]); hm_tex = self.HM2[0]
        # Smooth motion's afterglow (2026-11-22, as the core's gpu_phos.c ph_ex_mode): the in-between pictures move a line several
        # places a game frame, and every place it left started an afterglow - 2.4 trail lines at 120 Hz where a Vectrex has one.
        # So the afterglow starts only where the game itself drew: once a game frame the app hands in that frame's own lines
        # (ex_built), drawn here into E, and the afterglow starts from that instead of from the lit layer; the other refreshes none
        ex_tex = None
        if getattr(self, 'ex_mode', False) and getattr(self, 'E', None):
            self.BindFB(GL_FRAMEBUFFER, self.E[1]); gl.glViewport(0, 0, fw, fh)
            gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)
            ex_tex = self.E[0]; exb = getattr(self, 'ex_built', None)
            if exb:
                gl.glEnable(GL_BLEND); self.BlendFuncSep(GL_ONE, GL_ONE, GL_ONE, GL_ONE); self.BlendEqSep(eq, 0x8008)
                self._draw_built(exb, 0.0, 1.0, imm=True)
                self.BlendEq(0x8006); gl.glDisable(GL_BLEND)
                if getattr(self, 'beam_trace', False) and hasattr(self, 'satprog'):
                    self.BindFB(GL_FRAMEBUFFER, self.E2[1]); self._sat(self.E[0]); ex_tex = self.E2[0]
        self.fcs = (1.0, 1.0)
        # 2. the phosphor image: aged, lit again where the fresh light is brighter
        od = getattr(self, 'decay_override', None)        # (switching off: frozen, dimmed by the curve)
        src, dst = self.P, self.P2
        pw, ph = getattr(self, 'psize', (w, h))
        self.BindFB(GL_FRAMEBUFFER, dst[1]); gl.glViewport(0, 0, pw, ph); gl.glDisable(GL_BLEND)
        self.UseProgram(self.uprog)
        self.Uniform1i(self.u_up['oldt'], 0); self.Uniform1i(self.u_up['fresh'], 1)
        self.Uniform1f(self.u_up['kf'], 1.0 if od is not None else math.exp(-dt))
        self._phos_uniforms(self.u_up)
        self.Uniform1f(self.u_up['mul'], od if od is not None else 1.0)
        self.Uniform1f(self.u_up['rel'], 1.0 if (getattr(self, 'whole_pictures', False) and getattr(self, 'phos_release', True)
                                                 and od is None) else 0.0)   # (see UPDATE_SHADER: set by the app per picture)
        self.Uniform1i(self.u_up['relm'], 2)
        self.Uniform1f(self.u_up['relon'], 1.0 if (hm_tex is not None and od is None) else 0.0)
        self.Uniform1f(self.u_up['rmin'], hm_rmin if hm_rmin is not None else 1.0)
        self.ActiveTexture(0x84C2); gl.glBindTexture(GL_TEXTURE_2D, hm_tex or 0)
        self.ActiveTexture(0x84C1); gl.glBindTexture(GL_TEXTURE_2D, fresh); self.ActiveTexture(0x84C0)
        self._quad(src[0])
        self.UseProgram(0)
        if hm_tex is not None:
            self.ActiveTexture(0x84C2); gl.glBindTexture(GL_TEXTURE_2D, 0); self.ActiveTexture(0x84C0)
        self.P, self.P2 = dst, src
        # 2b. the afterglow: both parts fade, flashes leaving the lit layer excite them - Ultra: in full colour, both at
        # once (GLOW_SHADER, two outputs); the other profiles: packed in one image (GLOW_PACKED). Afterglow off: skipped
        full = bool(getattr(self, 'phos_full', True) and getattr(self, 'AGfb', None))
        mode = ('full' if full else 'packed') if self.trail_on else 'off'
        if mode != self.ag_mode:                          # (switched: what the images held means something else - black)
            for pr in self.AG:
                for t in pr:
                    self.BindFB(GL_FRAMEBUFFER, t[1]); gl.glClearColor(0, 0, 0, 0); gl.glClear(GL_COLOR_BUFFER_BIT)
            self.ag_mode = mode
        a0, a1 = self.ag_i, 1 - self.ag_i
        if mode != 'off':
            u = self.u_gl if full else self.u_gp; ts = max(0.05, self.trail)
            self.UseProgram(self.glowprog if full else self.glowpack)
            self.Uniform1i(u['oldg1'], 0); self.Uniform1i(u['oldg2'], 3); self.Uniform1i(u['oldp'], 1); self.Uniform1i(u['newp'], 2)
            self.ActiveTexture(0x84C1); gl.glBindTexture(GL_TEXTURE_2D, ex_tex if (ex_tex is not None and od is None) else self.P2[0])      # (the lit layer before this frame - Smooth motion: the game frame's own lines
            self.ActiveTexture(0x84C2); gl.glBindTexture(GL_TEXTURE_2D, self.P[0])       #  and after it)
            if full:
                self.ActiveTexture(0x84C3); gl.glBindTexture(GL_TEXTURE_2D, self.AG[1][a0][0])
            self.ActiveTexture(0x84C0)
            (fa, ta), (fs, tsl) = PHOS_A
            tk = TRAIL_K
            self.Uniform2f(u['amp'], fa * tk, fs * tk); self.Uniform2f(u['tau'], ta * ts, tsl * ts)
            self.Uniform2f(u['kd'], *((1.0, 1.0) if od is not None else (math.exp(-dt / (ta * ts)), math.exp(-dt / (tsl * ts)))))
            self.Uniform1f(u['hold'], self.hold()); self.Uniform1f(u['dt'], 0.0 if od is not None else dt)
            self.Uniform1f(u['mul'], od if od is not None else 1.0)
            self.Uniform1f(u['acap'], getattr(self, 'phos_acap', PHOS_ACAP) * self.bgain())
            if full:
                self.BindFB(GL_FRAMEBUFFER, self.AGfb[a1]); gl.glViewport(0, 0, pw, ph)
                bufs = (ctypes.c_uint * 2)(GL_COLOR_ATT0, GL_COLOR_ATT0 + 1); self.DrawBuffers(2, bufs)
            else:
                self.BindFB(GL_FRAMEBUFFER, self.AG[0][a1][1]); gl.glViewport(0, 0, pw, ph)
            self._quad(self.AG[0][a0][0])
            self.UseProgram(0)
            self.ActiveTexture(0x84C3); gl.glBindTexture(GL_TEXTURE_2D, 0); self.ActiveTexture(0x84C0)
            self.ag_i = a0 = a1
        # 3. what it shows
        self.BindFB(GL_FRAMEBUFFER, self.D[1]); gl.glViewport(0, 0, pw, ph)
        self.ActiveTexture(0x84C1); gl.glBindTexture(GL_TEXTURE_2D, self.AG[0][a0][0])
        self.ActiveTexture(0x84C2); gl.glBindTexture(GL_TEXTURE_2D, self.AG[1][a0][0]); self.ActiveTexture(0x84C0)
        self.UseProgram(self.sprog); self.Uniform1i(self.u_stex, 0); self._phos_uniforms(self.u_sh)
        self.Uniform1i(self.u_sh['g1'], 1); self.Uniform1i(self.u_sh['g2'], 2); self.Uniform1f(self.u_sh['pkd'], 0.0 if full else 1.0)
        btm = getattr(self, 'beam_trace', False) and hasattr(self, 'satprog')   # (the beam on the phosphor: its white is in the light - BT_TINT)
        self.Uniform3f(self.u_sh['wb'], *((1.0, 1.0, 1.0) if btm else self._wb()))
        self.Uniform3f(self.u_sh['wbc'], *((1.0, 1.0, 1.0) if btm else self._wbc()))
        self.Uniform1f(self.u_sh['acap'], getattr(self, 'phos_acap', PHOS_ACAP) * self.bgain())
        self.Uniform1f(self.u_sh['lifta'], 0.0 if (self.art and self.art_on) else TRAIL_LIFT_A)   # (an overlay shown: its own look)

        self._quad(self.P[0])
        self.UseProgram(0)
        for unit in (0x84C1, 0x84C2):
            self.ActiveTexture(unit); gl.glBindTexture(GL_TEXTURE_2D, 0)
        self.ActiveTexture(0x84C0)
        self._finish(self.D[0])

    def _wb(self):
        """the White balance gains for the glow (cached by temperature)"""
        k = float(getattr(self, 'white_k', WHITE_DEFAULT))
        if getattr(self, '_wb_k', None) != k:
            self._wb_k = k; self._wb_g = white_gain(k); self._wb_c = white_gain(white_core_k(k))
        return self._wb_g

    def _wbc(self):
        """the White balance gains for the lines' bright cores (white_core_k)"""
        self._wb()
        return self._wb_c

    def _sat(self, tex, wb=(1.0, 1.0, 1.0)):
        """the beam trace's energy (tex) as light, into the bound frame buffer (SAT_SHADER; wb: crisp, the white balance)"""
        self.UseProgram(self.satprog); self.Uniform1i(self.u_sat['tex'], 0)
        ex = float(os.environ.get('VX_BT_EXPO', BT_EXPO))    # (the exposure: light -> the screen; VX_BT_EXPO: tests)
        ex *= float(os.environ.get('VX_BT_SPOT', BT_SPOT))   # (a wider spot: its peak lower - the reference line stays at white)
        if self.blook(): ex *= self.bgain() ** 2.2   # (Bright, Medium: the light lifted here)
        k = float(getattr(self, 'white_k', WHITE_DEFAULT))
        t0 = white_gain(WHITE_DEFAULT); tk = white_gain(k)                      # (the tube's white as the owner's photos show it at the default
        t = tuple(a * b / c for a, b, c in zip(BT_TINT, tk, t0)); t = tuple(v / max(t) for v in t)   #  setting - BT_TINT - moved with the White balance setting)
        self.Uniform1f(self.u_sat['k'], ex * BT_KMAX); self.Uniform3f(self.u_sat['wb'], *(a * b for a, b in zip(t, BT_GLOW)))   # (the glow bluer than the core)
        self.Uniform1f(self.u_sat['gs'], 1.0 / self.bgain())
        self.Uniform3f(self.u_sat['wbc'], *t)
        # (TINT_GLOW - the owner, 2026-11-25, matching the tube with the cart: the vectors' colour by their own profile - a white
        #  core, the halo the glow's tint twice, 2 x its light, extra wide; as the libretro core's test option "blue glow 2.0x
        #  extra wide" - the one vector colour since 2026-11-26)
        on = TINT_GLOW
        self.Uniform1i(self.u_sat['tm'], 1 if on else 0)
        if on:
            w_, h_ = getattr(self, 'tex_wh', {}).get(tex, self.size)
            self.Uniform2f(self.u_sat['px'], 1.0 / max(1, w_), 1.0 / max(1, h_))
            wt = tuple(a / c for a, c in zip(tk, t0)); wt = tuple(v / max(wt) for v in wt)   # (the screen's white, moved with the White balance)
            self.Uniform3f(self.u_sat['tcore'], *wt)
            self.Uniform3f(self.u_sat['thalo'], *(a * g * g * 2.0 for a, g in zip(t, BT_GLOW)))
            self.Uniform2f(self.u_sat['thr'], 0.25, 0.9)
        self._quad(tex)
        self.UseProgram(0)

    def draw(self, lines):
        """one picture at once (callers without frame timing)"""
        self.present(__import__('time').perf_counter(), imm=lines)

    def _hud(self):
        """the frame rate counter (small green strokes in the top left corner) and the volume knob"""
        if not (self.hud or getattr(self, 'knob', None) or getattr(self, 'osd', None) or getattr(self, 'cart', None)
                or getattr(self, 'browser', None) or getattr(self, 'hint', None) or getattr(self, 'port2', None)
                or getattr(self, 'port1', None)):
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
            self._listed('knob', self.knob, lambda: self._knob(self.knob))
        if getattr(self, 'osd', None):
            self._listed('osd', self.osd, lambda: self._osd(self.osd))
        if getattr(self, 'cart', None):
            self._listed('cart', self.cart, lambda: self._cart(self.cart))
        if getattr(self, 'port2', None):
            self._listed('port2', self.port2, lambda: self._port2(self.port2))
        if getattr(self, 'port1', None):                    # (online: controller port 1, right of the cartridge)
            self._listed('port1', self.port1, lambda: self._port2(self.port1))
        if getattr(self, 'browser', None):
            self._browser(self.browser)
        if getattr(self, 'hint', None):                   # (a hint in the middle of the picture: centred rows)
            h = self.hint; hk = h['k']; a = h.get('a', 1.0)
            col = (0.40 * a, 0.667 * a, 0.533 * a)
            gl.glLineWidth(max(1.2, hk * 0.45))
            y = h['cy']
            for i, t in enumerate(h['rows']):
                rk = hk * (h['ks'][i] if 'ks' in h else 1.0)   # (each row its own size: a big PAUSED)
                gl.glLineWidth(max(1.2, rk * 0.45))
                rc = tuple(c * a for c in h['cols'][i]) if 'cols' in h else col   # (each row its own colour)
                self._text_row([(t, rc)], h['cx'] - len(t) * 3.0 * rk, y, rk)
                y += 11.0 * rk
            if 'bar' in h:                                # (a bar: how far, e.g. the 3D Imager's disc up to speed)
                w = 22.0 * 6.0 * hk; x0 = h['cx'] - w / 2; y0 = y + 2.0 * hk; y1 = y0 + 4.0 * hk
                gl.glColor4f(*palette.rgb(tuple(col)), 1.0)
                self._rect(x0, y0, x0 + w, y1)
                f = max(0.0, min(1.0, h['bar']))
                if f > 0:
                    gl.glBegin(0x0007)                    # (GL_QUADS)
                    for px, py in ((x0, y0), (x0 + w * f, y0), (x0 + w * f, y1), (x0, y1)):
                        gl.glVertex2f(px, py)
                    gl.glEnd()
                y = y1 + 5.0 * hk
            for t in h.get('rows2', []):
                k2 = 0.8 * hk
                self._text_row([(t, col)], h['cx'] - len(t) * 3.0 * k2, y, k2)
                y += 11.0 * k2
        gl.glMatrixMode(GL_PROJECTION); gl.glPopMatrix(); gl.glMatrixMode(GL_MODELVIEW)
        gl.glEnableClientState(GL_COLOR_ARRAY); gl.glEnableClientState(GL_VERTEX_ARRAY)

    def _listed(self, name, state, draw):
        """the strip's knob, OSD, cartridge and port pictogram: each drawn once into an OpenGL display list and played
        back while it looks the same (their hundreds of strokes from Python every refresh took ~1 ms - a slow PC's
        emulation shares the processor with it)"""
        gl = self.gl
        key = (repr(state), self.size, getattr(self, 'hud_k', None))
        cache = self.__dict__.setdefault('_lists', {})
        old = cache.get(name)
        if old and old[0] == key:
            gl.glCallList(old[1])
            return
        if old:
            lst = old[1]
        else:
            gl.glGenLists.restype = ctypes.c_uint
            lst = gl.glGenLists(1)
            if not lst:                                 # (no lists: drawn directly)
                draw()
                return
        gl.glNewList(lst, 0x1301)                       # (GL_COMPILE_AND_EXECUTE)
        try:
            draw()
        finally:
            gl.glEndList()
        cache[name] = (key, lst)

    def _text_row(self, segs, x, y, k):
        """one row of (text, colour) pieces in the stroke font, 6 k high, top left at x, y"""
        gl = self.gl
        gl.glBegin(GL_LINES)
        for text, col_ in segs:
            cr, cg, cb = palette.rgb(tuple(col_[:3]))        # (the scheme: blue or green)
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
        if od.get('meters') and od.get('thin'):
            self._meters_thin(od['meters'], x0, y0 + len(rows) * 9.0 * k, x0 + (chars * 6.0 - 1.0) * k, k)
        elif od.get('meters'):
            self._meters(od['meters'], x0, y0 + (len(rows) + 0.25) * 9.0 * k, x0 + (chars * 6.0 - 1.0) * k, k * METER_K)

    def _meters_thin(self, meters, x, y, x_end, k):
        """a low window's CPU and GPU meters: two thin lines under the OSD's text (CPU first), as wide as it - the share
        filled in its colour (green below 90 %, orange to 100, red above: over its time) on a dim line"""
        gl = self.gl
        th = max(1.0, 1.0 * k)
        for i, (name, pct, hist) in enumerate(meters):
            ty = y + (0.5 + i * 2.0) * k
            gl.glColor4f(*(palette.rgb((0.40, 0.667, 0.533)) + (0.30,)))
            gl.glBegin(0x0007)
            for px, py in ((x, ty), (x_end, ty), (x_end, ty + th), (x, ty + th)):
                gl.glVertex2f(px, py)
            gl.glEnd()
            if pct is not None:
                f = max(0.0, min(1.0, pct / 100.0))
                c = palette.rgb((0.40, 0.667, 0.533)) if pct < 90 else (0.93, 0.60, 0.20) if pct <= 100 else (0.93, 0.33, 0.33)
                gl.glColor4f(c[0], c[1], c[2], 1.0)
                gl.glBegin(0x0007)
                for px, py in ((x, ty), (x + (x_end - x) * f, ty), (x + (x_end - x) * f, ty + th), (x, ty + th)):
                    gl.glVertex2f(px, py)
                gl.glEnd()

    def _meters(self, meters, x, y, x_end, k):
        """the OSD's CPU and GPU meters: a bar (green below 90 %, orange to 100, red above: over its time) with the last 10 seconds as
        a line in it, and the share in figures - one per row below the OSD's text, smaller than it"""
        gl = self.gl
        gl.glLineWidth(max(1.0, k * 0.42))
        bw = (x_end - x) - (4 + 5) * 6.0 * k                    # (the bar: what the name and the figure leave - the rows
        if bw < 12.0 * k:                                       #  as wide as the OSD's text above them)
            return
        g = palette.rgb((0.40, 0.667, 0.533))
        def col(p):
            return g if p < 90 else (0.93, 0.60, 0.20) if p <= 100 else (0.93, 0.33, 0.33)
        for i, (name, pct, hist) in enumerate(meters):
            ty = y + i * 9.0 * k
            self._text_row([(name, g)], x, ty, k)
            bx0, by0 = x + 4 * 6.0 * k, ty
            bx1, by1 = bx0 + bw, ty + 6.0 * k
            gl.glColor4f(*g, 1.0)
            self._rect(bx0, by0, bx1, by1)
            if pct is not None:
                f = max(0.0, min(1.0, pct / 100.0))
                c = col(pct)
                if f > 0:
                    gl.glColor4f(c[0], c[1], c[2], 0.75)
                    gl.glBegin(0x0007)                          # (GL_QUADS)
                    for px, py in ((bx0 + k, by0 + k), (bx0 + k + (bw - 2 * k) * f, by0 + k),
                                   (bx0 + k + (bw - 2 * k) * f, by1 - k), (bx0 + k, by1 - k)):
                        gl.glVertex2f(px, py)
                    gl.glEnd()
            fig = '--' if pct is None else '%d%%' % min(999, pct)       # (right-aligned, as the OSD's values)
            self._text_row([(fig, g if pct is None else col(pct))], x_end - (len(fig) * 6.0 - 1.0) * k, ty, k)

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
        gl.glColor4f(*(palette.rgb((0.40, 0.667, 0.533)) + (1.0,)))
        gl.glLineWidth(max(1.2, r / 45.0))
        s = 2.0 * r / 100.0                                     # (as high as the knob)
        out = c.get('out', 0.0)
        if out <= 0.0:
            self._icon(CART_ICON, lambda u, v: (cx + (u - 50) * s, cy + (v - 50) * s))
        else:                                                   # out of the slot: the load symbol (the slot, the cartridge
            s2 = 2.0 * r / 160.0                                #  below it, an arrow up: put it back), sliding there
            top = cy - r
            self._icon([LOAD_ICON[0]], lambda u, v: (cx + (u - 50) * s2, top + v * s2))
            if out >= 1.0:
                self._icon(LOAD_ICON[1:3], lambda u, v: (cx + (u - 50) * s2, top + v * s2))
            dy = 60 * out * s2
            self._icon(LOAD_ICON[3:], lambda u, v: (cx + (u - 50) * s2, top + (v - 60) * s2 + dy))
        if c.get('open'):
            self._rect(cx - 52 * s, cy - 56 * s, cx + 52 * s, cy + 56 * s)
        lh = 0.30 * r
        self._text_row([('CARTRIDGE', (0.40, 0.667, 0.533))], cx - (9 * 6 - 2) * lh / 12.0, cy - 1.5 * r - lh, lh / 6.0)

    def _port2(self, c):
        """what is in controller port 2 - the controller (seen from above), the VecVox (a box with a speaker), the
        VecVoice (a speaking head), the 3D Imager (the goggles with their disc) or the light pen - with P2 PORT above it like the
        panel's labels"""
        gl = self.gl
        cx, cy, r = c['cx'], c['cy'], c['r']
        gl.glColor4f(*(palette.rgb((0.40, 0.667, 0.533)) + (1.0,)))
        gl.glLineWidth(max(1.2, r / 45.0))
        s = 2.0 * r / 100.0
        oy = cy if c.get('kind') == 'vecvoice' else cy - 0.12 * r  # (the VecVoice's box centred on the cartridge; the others
                                                                #  a little higher, close under P2 PORT)
        s *= {'vecvoice': 1.35, 'vecvox': 1.2}.get(c.get('kind'), 1.1)   # (about as large as the cartridge)
        ox = cx + (10 * s if c.get('kind') == 'vecvoice' else 0)  # (the VecVoice: its box and waves a little right)
        P = lambda u, v: (ox + (u - 50) * s, oy + (v - 50) * s)
        # (the VecVox - a SpeakJet, the more advanced module - the speaking head; the VecVoice the box with waves: the
        #  owner, 2026-10-27)
        self._icon({'vecvoice': PORT2_ICON, 'vecvox': VOICE_ICON, 'imager': IMAGER_ICON, 'lightpen': LPEN_ICON, 'veclink': VECLINK_ICON}.get(c.get('kind'), JOY_ICON), P)
        t = c['label']
        lh = min(0.30 * r, 2.6 * r * 6.0 / max(1, len(t) * 6 - 2))   # (at most as wide as CARTRIDGE: a long player name
        self._text_row([(t, (0.40, 0.667, 0.533))], cx - (len(t) * 6 - 2) * lh / 12.0, cy - 1.5 * r - 0.30 * r, lh / 6.0)   #  smaller, the same line)

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

    def _library(self, b):
        """the game library over the picture's place (library.py: as the phone apps') - a head with the title, the looks,
        the tiles a row and the categories; below it the tiles, each its picture (a texture, made here from what the
        library's reader hands over in b['new']) and its name, the chosen one framed bright"""
        gl = self.gl
        g = b['g']; x0, y0, x1, y1 = g['rect']; u, k = g['u'], g['k']
        H = lambda c: palette.rgb(tuple(int(c[i:i + 2], 16) / 255.0 for i in (1, 3, 5)))
        hi, hi2, dim, fg = H('#88ccbb'), H('#bbffee'), H('#7f978b'), H('#c8d6cf')
        BG_, TOP, PAN, BODY_, SEL_, EDGE_, EDGE2_ = (H(c) for c in ('#05080a', '#0a1210', '#0b1511', '#0f1c17', '#24443a', '#1d3329', '#2f5544'))
        tints = {'UVMC2': (H('#66a4b8'), H('#8ac8dc')), 'PiTrex': (H('#aa9c88'), H('#d0c0aa'))}
        def fill(ax0, ay0, ax1, ay1, c, a=1.0):
            gl.glColor4f(*c, a); gl.glBegin(GL_QUADS)
            for x, y in ((ax0, ay0), (ax1, ay0), (ax1, ay1), (ax0, ay1)):
                gl.glVertex2f(x, y)
            gl.glEnd()
        def frame(ax0, ay0, ax1, ay1, c):
            gl.glColor4f(*c, 1.0); self._rect(ax0, ay0, ax1, ay1)
        def text(t, x, y, kk, c):
            gl.glLineWidth(max(1.0, kk * 0.42)); self._text_row([(t, c)], x, y, kk)
        def ctext(t, cx, cy, kk, c):
            text(t, cx - len(t) * 3.0 * kk, cy - 3.0 * kk, kk, c)
        def button(box, word, on, kk=None):
            kk = kk or k
            fill(box[0], box[1], box[2], box[3], SEL_ if on else BODY_); gl.glLineWidth(1.0); frame(box[0], box[1], box[2], box[3], EDGE2_)
            ctext(word, (box[0] + box[2]) / 2, (box[1] + box[3]) / 2, kk, hi2 if on else hi)
        # the pictures handed over: textures (kept while the program runs - a few hundred small ones)
        tex = self.__dict__.setdefault('_lib_tex', {})
        for p, (w, h, px) in (b.get('new') or {}).items():
            try:
                t = ctypes.c_uint(); gl.glGenTextures(1, ctypes.byref(t)); gl.glBindTexture(GL_TEXTURE_2D, t.value)
                for kk_, v in ((GL_TEX_MIN, GL_LINEAR), (GL_TEX_MAG, GL_LINEAR), (GL_WRAP_S, GL_CLAMP_EDGE), (GL_WRAP_T, GL_CLAMP_EDGE)):
                    gl.glTexParameteri(GL_TEXTURE_2D, kk_, v)
                gl.glPixelStorei(0x0CF5, 1)                             # (GL_UNPACK_ALIGNMENT)
                gl.glTexImage2D(GL_TEXTURE_2D, 0, 0x8058, w, h, 0, GL_RGBA, 0x1401, px)
                tex[p] = (t.value, w, h)
            except Exception as e:   # noqa
                print('library: picture not shown (%s)' % e)
        gl.glBindTexture(GL_TEXTURE_2D, 0)
        fill(x0, y0, x1, y1, BG_)
        fill(x0, y0, x1, g['head'], TOP)
        gl.glLineWidth(1.0); gl.glColor4f(*EDGE_, 1.0); self._lines([(x0, g['head'], x1, g['head'])])
        text(b['title'], g['title'][0], g['title'][1], 1.25 * k, hi)
        cb = g['close']; button(cb, 'X', False)
        for box, word, v in g['looks']:
            button(box, word, v == b['look'], 0.85 * k)
        text('PER ROW', g['per'][0], g['per'][1], 0.8 * k, dim)
        for box, word, v in g['cols']:
            button(box, word, v == b['cols'], 0.8 * k)
        for box, word, v in g['cats']:
            button(box, word, v == b['cat'], 0.85 * k)
        text(b['help'], g['help'][0], g['help'][1], 0.6 * k, dim)
        # the tiles: cut at the grid's edges
        gx0, gy0, gx1, gy1 = g['grid']
        rh = getattr(self, 'real_size', self.size)[1]
        gl.glEnable(0x0C11); gl.glScissor(int(x0), int(rh - gy1), int(x1 - x0), int(gy1 - gy0))
        try:
            if not b['tiles']:
                for i, line in enumerate(b['empty'].split('\n')):
                    text(line, gx0 + u, gy0 + (1.0 + 1.8 * i) * u, k, dim)
            for t in b['tiles']:
                tx, ty, tw, th = t['x'], t['y'], t['w'], t['h']
                if b['text_only']:
                    rowh = g['tile'][3]
                    if t['sel']: fill(tx, ty, tx + tw, ty + rowh - 2, SEL_)
                    # (UVMC2 games and PiTrex programs tinted, the PiTrex itself with its raspberry - as the simple list)
                    tc = tints.get(t['kind'])
                    c_ = (hi2 if t['sel'] else hi) if t.get('boot') else tc[1 if t['sel'] else 0] if tc else hi2 if t['sel'] else fg
                    ty_ = ty + (rowh - 6 * k) / 2 - 1; ind = 2.4 * 6 * k if t.get('boot') else 0.0
                    text(t['name'][:int(tw / (6 * k)) - 5].upper(), tx + 0.8 * u + ind, ty_, k, c_)
                    if t.get('boot'):
                        P_ = lambda a_, b_: (tx + 0.8 * u + a_ * k, ty_ + b_ * k)
                        pts = [P_(5 + 2.6 * math.cos(2 * math.pi * q / 14), 3.6 + 2.4 * math.sin(2 * math.pi * q / 14)) for q in range(15)]
                        seg = [(a_[0], a_[1], b_[0], b_[1]) for a_, b_ in zip(pts, pts[1:])]
                        for (ax, ay), (bx_, by_) in (((5, 1.2), (2.8, -0.2)), ((5, 1.2), (7.2, -0.2)), ((2.8, -0.2), (4.2, 0.6)),
                                                     ((7.2, -0.2), (5.8, 0.6)), ((3.6, 3.4), (6.4, 3.4)), ((5, 1.6), (5, 5.6))):
                            seg.append((*P_(ax, ay), *P_(bx_, by_)))
                        gl.glColor4f(*c_, 1.0); self._lines(seg)
                    continue
                im = tex.get(t['img']) if t['img'] else None
                if im:
                    iw, ih = im[1], im[2]; sc = min(tw / float(iw), th / float(ih)); dw, dh = iw * sc, ih * sc
                    ix, iy = tx + (tw - dw) / 2, ty + (th - dh) / 2
                    gl.glDisable(GL_BLEND)                               # (the picture as it is: the PNG reader gives a picture without alpha an overlay's 51 - it showed dim)
                    gl.glEnable(GL_TEXTURE_2D); gl.glBindTexture(GL_TEXTURE_2D, im[0]); gl.glColor4f(LIB_ART, LIB_ART, LIB_ART, 1.0)
                    gl.glBegin(GL_QUADS)
                    for (a_, c_), (px_, py_) in (((0, 0), (ix, iy)), ((1, 0), (ix + dw, iy)), ((1, 1), (ix + dw, iy + dh)), ((0, 1), (ix, iy + dh))):
                        gl.glTexCoord2f(a_, c_); gl.glVertex2f(px_, py_)
                    gl.glEnd()
                    gl.glBindTexture(GL_TEXTURE_2D, 0); gl.glDisable(GL_TEXTURE_2D); gl.glEnable(GL_BLEND)
                else:                                                   # (no picture in this look, or not read yet: the name's tile)
                    fill(tx, ty, tx + tw, ty + th, PAN); gl.glLineWidth(1.0); frame(tx, ty, tx + tw, ty + th, EDGE2_)
                    words = t['name'].upper().split(); kk = min(k, tw / (6.0 * 12)); per = max(4, int(tw / (6 * kk)) - 2); lines = ['']
                    for w_ in words:
                        if lines[-1] and len(lines[-1]) + 1 + len(w_) > per: lines.append('')
                        lines[-1] = (lines[-1] + ' ' + w_).strip()[:per]
                    for i, ln in enumerate(lines[:4]):
                        ctext(ln, tx + tw / 2, ty + th / 2 + (i - (min(len(lines), 4) - 1) / 2.0) * 9 * kk, kk, hi if not t['img'] else dim)
                if t['sel']:
                    gl.glLineWidth(max(2.0, 0.22 * u)); frame(tx - 0.2 * u, ty - 0.2 * u, tx + tw + 0.2 * u, ty + th + 0.2 * u, hi2)
                kn = min(0.8 * k, tw / (6.0 * 16)); nm = t['name'].upper()[:max(3, int(tw / (6 * kn)))]
                ctext(nm, tx + tw / 2, ty + th + 1.2 * u, kn, hi2 if t['sel'] else fg)
        finally:
            gl.glDisable(0x0C11)
        sc_, tot = b['scroll']                                           # (how far down: a bar at the right edge)
        if tot > (gy1 - gy0) + 1:
            bh = (gy1 - gy0) * (gy1 - gy0) / tot; by = gy0 + (gy1 - gy0 - bh) * sc_ / max(1.0, tot - (gy1 - gy0))
            fill(x1 - 0.5 * u, by, x1 - 0.2 * u, by + bh, EDGE2_)
        gl.glLineWidth(1.0); frame(x0, y0, x1, y1, EDGE_)

    def _browser(self, b):
        """the ROM browser over the picture, in the dark style of the windows (Config, Controls, Help): a title
        band, the list on the strip colour, the chosen row a filled bar, filled buttons, the keys in a foot band"""
        if b.get('lib'):
            return self._library(b)
        gl = self.gl
        x0, y0, x1, y1 = b['rect']
        H = lambda c: palette.rgb(tuple(int(c[i:i + 2], 16) / 255.0 for i in (1, 3, 5)))
        g, hi, dim = H('#66aa88'), H('#88ccbb'), H('#4a7a62')
        BG_, TOP, PAN, BODY_, SEL_, EDGE_, EDGE2_ = (H(c) for c in ('#05080a', '#0a1210', '#0b1511', '#0f1c17',
                                                                     '#24443a', '#1d3329', '#2f5544'))
        def fill(ax0, ay0, ax1, ay1, c, a=1.0):
            gl.glColor4f(*c, a); gl.glBegin(GL_QUADS)
            for x, y in ((ax0, ay0), (ax1, ay0), (ax1, ay1), (ax0, ay1)):
                gl.glVertex2f(x, y)
            gl.glEnd()
        def frame(ax0, ay0, ax1, ay1, c):
            gl.glColor4f(*c, 1.0); self._rect(ax0, ay0, ax1, ay1)
        k, rh = b['k'], b['row_h']
        lx0, ly0, lx1, ly1 = b['list']
        gl.glLineWidth(max(1.0, k * 0.30))
        fill(x0, y0, x1, y1, BG_, 0.96)                         # (the whole box: nearly opaque)
        fill(x0, y0, x1, ly0 - 0.35 * rh, TOP)                  # (the title band)
        fill(x0, b['up'][1] - 0.35 * rh, x1, y1, TOP)           # (the foot band: buttons, LOAD, the keys)
        frame(x0, y0, x1, y1, EDGE_)
        gl.glColor4f(*EDGE_, 1.0)
        self._lines([(x0, ly0 - 0.35 * rh, x1, ly0 - 0.35 * rh), (x0, b['up'][1] - 0.35 * rh, x1, b['up'][1] - 0.35 * rh)])
        gl.glLineWidth(max(1.0, k * 0.42))
        self._text_row([(b['title'], hi)], x0 + 2 * k * 6 / 6.0 * 2, y0 + 0.6 * rh, k)
        # UVMC2 games and PiTrex programs: the list's green with a blue / a red tint; their legend in the title band
        tint = {'um2': (H('#66a4b8'), H('#8ac8dc')), 'pit': (H('#aa9c88'), H('#d0c0aa'))}   # (the PiTrex loader: green, as a folder)
        kinds_all = b.get('kinds', [])
        kl, lxr = k * 0.62, b['close'][0] - 1.2 * 6 * k
        for kind, word in reversed([e for e in (('um2', 'UVMC2'), ('pit', 'PITREX')) if e[0] in kinds_all]):
            tx = lxr - len(word) * 6 * kl
            self._text_row([(word, tint[kind][0])], tx, y0 + 0.6 * rh + (k - kl) * 3, kl)
            lxr = tx - 2.5 * 6 * kl
        fill(lx0, ly0, lx1, ly1, BODY_)                         # (the list)
        frame(lx0, ly0, lx1, ly1, EDGE2_)
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
            kind = b.get('kinds', ['rom'] * len(names))[j]
            ind = '    ' if kind in ('dir', 'up', 'pitrex', 'recent') else ''   # (room for the folder / back / PiTrex / Recent pictogram)
            t = names[j] if len(ind + names[j]) <= cols else names[j][:cols - 2 - len(ind)] + '..'
            c = tint[kind][1 if j == sel else 0] if kind in tint else hi if j == sel else g
            if j == sel:                                        # (the chosen row: a filled bar)
                fill(lx0 + 0.6 * k, y + 0.08 * rh, lx1 - 2.2 * 6 * k, y + 0.97 * rh, SEL_)
            self._text_row([('  ' + ind + t, c)], lx0 + 1.5 * k, y + 0.3 * rh, k)
            if kind in ('dir', 'up', 'pitrex', 'recent'):      # (a folder: its outline with a tab; BACK: an arrow; the PiTrex: a raspberry; Recent: a clock)
                ix, iy = lx0 + 1.5 * k + 2 * 6 * k, y + 0.3 * rh
                P = lambda a, b_: (ix + a * k, iy + b_ * k)
                gl.glColor4f(*c, 1.0)
                if kind == 'recent':
                    pts = [P(5 + 3.0 * math.cos(2 * math.pi * q / 16), 3 + 3.0 * math.sin(2 * math.pi * q / 16)) for q in range(17)]
                    seg = [(a[0], a[1], b_[0], b_[1]) for a, b_ in zip(pts, pts[1:])]
                    seg += [(*P(5, 3), *P(5, 1)), (*P(5, 3), *P(6.6, 3.9))]
                    self._lines(seg)
                    continue
                if kind == 'pitrex':
                    pts = [P(5 + 2.6 * math.cos(2 * math.pi * q / 14), 3.6 + 2.4 * math.sin(2 * math.pi * q / 14)) for q in range(15)]
                    seg = [(a[0], a[1], b_[0], b_[1]) for a, b_ in zip(pts, pts[1:])]
                    for (ax, ay), (bx_, by_) in (((5, 1.2), (2.8, -0.2)), ((5, 1.2), (7.2, -0.2)), ((2.8, -0.2), (4.2, 0.6)),
                                                 ((7.2, -0.2), (5.8, 0.6)), ((3.6, 3.4), (6.4, 3.4)), ((5, 1.6), (5, 5.6))):
                        seg.append((*P(ax, ay), *P(bx_, by_)))
                    self._lines(seg)
                    continue
                if kind == 'dir':
                    pts = [P(0, 6), P(0, 1), P(1, 0), P(4, 0), P(5, 1), P(10, 1), P(10, 6), P(0, 6)]
                else:
                    pts = [P(10, 3), P(0, 3), P(3, 0), P(0, 3), P(3, 6)]
                self._lines([(a[0], a[1], b_[0], b_[1]) for a, b_ in zip(pts, pts[1:])])
        if len(names) > n:                                      # the scroll bar: a slim filled thumb
            sx = lx1 - 1.0 * 6 * k
            ht = (ly1 - ly0) * n / float(len(names))
            st = ly0 + (ly1 - ly0 - ht) * top / float(max(1, len(names) - n))
            fill(sx, st + 0.2 * k, sx + 0.55 * 6 * k, st + ht - 0.2 * k, EDGE2_)
        for (bx0, by0, bx1, by1), d in ((b['up'], -1), (b['dn'], 1)):   # the scroll buttons: filled, a triangle
            fill(bx0, by0, bx1, by1, PAN); frame(bx0, by0, bx1, by1, EDGE2_)
            mx, w3, h3 = (bx0 + bx1) / 2, (bx1 - bx0) * 0.28, (by1 - by0) * 0.22
            my = (by0 + by1) / 2
            ay, by = my + d * h3, my - d * h3                     # (the point: up or down)
            gl.glColor4f(*g, 1.0)
            self._lines([(mx - w3, by, mx, ay), (mx, ay, mx + w3, by), (mx + w3, by, mx - w3, by)])
        cx0, cy0, cx1, cy1 = b['close']                         # the close button: filled, an X
        fill(cx0, cy0, cx1, cy1, PAN); frame(cx0, cy0, cx1, cy1, EDGE2_)
        m = (cx1 - cx0) * 0.28
        gl.glColor4f(*g, 1.0)
        self._lines([(cx0 + m, cy0 + m, cx1 - m, cy1 - m), (cx1 - m, cy0 + m, cx0 + m, cy1 - m)])
        px0, py0, px1, py1 = b['plug']
        gl.glColor4f(*(hi if names else dim), 1.0)
        self._load_icon(px0, py0, px1, py1)
        self._text_row([('LOAD', hi if names else dim)], px0 - 5.5 * 6 * k, (py0 + py1) / 2 - 3 * k, k)
        self._text_row([(b['help'], dim)], x0 + 0.6 * rh, y1 - 0.85 * rh, k * 0.55)

    def _knob(self, kb):
        """the volume knob and RESET button, outlines only (the picture shows through)"""
        gl = self.gl
        cx, cy, r, rx = kb['cx'], kb['cy'], kb['r'], kb['rx']
        gl.glColor4f(*(palette.rgb((0.40, 0.667, 0.533)) + (1.0,)))
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
        # the knob: its rim, the cap with a ring of fine grip dots round it, a small triangle pointing at the volume
        circle(cx, cy, r); circle(cx, cy, 0.6 * r)
        dr = max(0.5, 0.018 * r)
        for i in range(48):
            x, y = at(cx, cy, 0.645 * r, i * 7.5)
            strip([(x + dr * cos(k * pi / 3), y + dr * sin(k * pi / 3)) for k in range(6)], True)
        ang = -KNOB_SWEEP + 2.0 * KNOB_SWEEP * kb['vol']
        tip = at(cx, cy, 0.93 * r, ang)
        bx, by = at(cx, cy, 0.76 * r, ang); px, py = at(0.0, 0.0, 0.08 * r, ang + 90.0)
        strip([tip, (bx + px, by + py), (bx - px, by - py)], True)
        lh = 0.30 * r                                           # (label height)
        text(kb.get('label', 'OFF/ON'), cx, cy - 1.5 * r - lh, lh)
        if kb.get('hint'):                                      # (the pointer on the knob: how to turn it)
            text(kb['hint'], cx, cy - 1.5 * r - 2.6 * lh, 0.75 * lh)
        # the volume wedge: a curved bar right of the knob, thin at the top, wide at the bottom
        inner = [at(cx, cy, 1.25 * r, 50 + i * 4.0) for i in range(21)]
        outer = [at(cx, cy, 1.25 * r + (0.03 + 0.17 * i / 20.0) * r, 50 + i * 4.0) for i in range(21)]
        strip(inner + outer[::-1], True)
        text('VOL', cx + 1.6 * r + (3 * 6 - 2) * lh / 12.0, cy - lh / 2.0, lh)
        # the RESET button: its ring and the dome (pressed: smaller)
        circle(rx, cy, 0.95 * r)
        circle(rx, cy, (0.68 if kb.get('pressed') else 0.78) * r)
        text('RESET', rx, cy - 1.5 * r - lh, lh)

    def _finish(self, src):
        """the picture `src` onto the screen: picture, overlay, counter"""
        gl = self.gl
        w, h = self.size
        gl.glDisableClientState(GL_COLOR_ARRAY); gl.glDisableClientState(GL_VERTEX_ARRAY)
        gl.glDisable(GL_BLEND)
        # the screen: the picture (curved tube: first into E, bent onto the screen)
        curve = getattr(self, 'curve_on', False) or getattr(self, 'glass_on', False)   # (the glass pass: the glass edge)
        pw, ph = getattr(self, 'psize', (w, h))
        self.BindFB(GL_FRAMEBUFFER, self.E[1] if curve else self.out); gl.glViewport(0, 0, *((pw, ph) if curve else (w, h)))
        gl.glClearColor(0, 0, 0, 1); gl.glClear(GL_COLOR_BUFFER_BIT)
        hx, hy = self.head_px()                                 # (head tracking: the whole glowing picture moved)
        if hx or hy:
            gl.glMatrixMode(GL_MODELVIEW); gl.glPushMatrix(); gl.glTranslatef(hx / float(w), -hy / float(h), 0.0)
        self.UseProgram(self.tprog); self.Uniform1i(self.u_ttex, 0); self.Uniform1f(self.u_thk, self.hdr_hk)
        self.Uniform1f(self.u_tbgh, self.bgain() if (self.blook() and self.hdr_hk > 1.0) else 0.0)
        self.Uniform1f(self.u_tvl, 0.0)
        self._quad(src)
        self.UseProgram(0)
        gl.glEnable(GL_BLEND); gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE)
        if hx or hy:
            gl.glMatrixMode(GL_MODELVIEW); gl.glPopMatrix()
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
            self.BindFB(GL_FRAMEBUFFER, self.out); gl.glViewport(0, 0, w, h)
            ph = ALG_MAX_Y * self.map[0]                    # (the picture's height, window px)
            self.UseProgram(self.kprog); self.Uniform1i(self.u_k['tex'], 0)
            self.Uniform2f(self.u_k['norm'], w / (0.5 * ph), h / (0.5 * ph))
            self.Uniform2f(self.u_k['ctr'], (self.map[1] * self.map[0] + 0.5 * ALG_MAX_X * self.map[0]) / w,
                           1.0 - (self.map[2] * self.map[0] + 0.5 * ph) / h)
            kx, ky = tube_coeffs(0.0)                           # (the lines are bent before they are drawn,
            self.Uniform1fv(self.u_k['cx'], 10, (ctypes.c_float * 10)(*kx))     #  _tube_lines: here the
            self.Uniform1fv(self.u_k['cy'], 10, (ctypes.c_float * 10)(*ky))     #  glass only)
            self.Uniform1f(self.u_k['pull'], 0.0)
            self.Uniform1f(self.u_k['glass'], 1.0 if getattr(self, 'glass_on', True) else 0.0)
            self.Uniform1f(self.u_k['aw'], GLASS_AW); self.Uniform1f(self.u_k['gn'], GLASS_N)
            self.Uniform1f(self.u_k['ah'], GLASS_AH)
            self.Uniform2f(self.u_k['goff'], GLASS_DX, GLASS_DY + 2.0 * self.par_dy() / ALG_MAX_Y)   # (the glass with the picture)
            self.Uniform2f(self.u_k['gsc'], GLASS_AW / GLASS_REF_AW, GLASS_AH / GLASS_REF_AH)
            self.Uniform1fv(self.u_k['glut'], 91, (ctypes.c_float * 91)(*GLASS_LUT))
            self.Uniform1f(self.u_k['edge'], GLASS_EDGE)
            art_shown = bool(getattr(self, 'art', None) and getattr(self, 'art_on', False))
            catch = getattr(self, 'glass_room', True) and not art_shown
            LL = light_level(getattr(self, 'art_room', ART_ROOM_MID))          # (the room's light, 0 .. 1)
            self.Uniform1f(self.u_k['grey'], GLASS_GREY * LL if catch else 0.0)
            lk = LL * getattr(self, 'light_k', 1.0) if (catch and getattr(self, 'light_on', True) and self._light_ready()) else 0.0
            self.Uniform1f(self.u_k['lk'], lk)
            self.Uniform1f(self.u_k['bscale'], 1834.0 / max(1.0, ph) / 3.2)   # (soft, smooth bumps)
            self.Uniform1f(self.u_k['pxs'], 2.0 / max(1.0, ph))          # (one window pixel, half picture heights)
            if lk > 0:
                (lx, ly), _ = roomlight.place(self.light_x, self.light_y)
                self.Uniform3f(self.u_k['lpos'], lx, ly, roomlight.GLASS_SIZE)
                hs_ = self.refl_shift('glass')
                self.Uniform2f(self.u_k['hsh'], *hs_)
                self.Uniform2f(self.u_k['gsh'], *self.phos_shift(hs_))
                self.Uniform4f(self.u_k['lmr'], roomlight.MAP_U[0], roomlight.MAP_U[1], roomlight.MAP_V[0], roomlight.MAP_V[1])
                self.Uniform1f(self.u_k['lca'], math.cos(roomlight.ANG)); self.Uniform1f(self.u_k['lsa'], math.sin(roomlight.ANG))
                self.ActiveTexture(0x84C1); gl.glBindTexture(GL_TEXTURE_2D, self.light_tex); self.Uniform1i(self.u_k['lmap'], 1)
                self.ActiveTexture(0x84C0)
            case = getattr(self, 'case_on', True) and not art_shown and getattr(self, 'glass_on', True)
            self.Uniform1f(self.u_k['cse'], 1.0 if case else 0.0)
            if case:
                self.Uniform1f(self.u_k['croom'], LL if getattr(self, 'glass_room', True) else 0.0)
                self.Uniform1f(self.u_k['crefl'], getattr(self, 'case_refl', 1.0))
                self.Uniform1f(self.u_k['cwear'], getattr(self, 'case_wear', 1.0))
                self.Uniform1fv(self.u_k['clut'], 91, (ctypes.c_float * 91)(*casemodel.CASE_LUT))
                (lx, ly), _ = roomlight.place(self.light_x, self.light_y)
                self.Uniform3f(self.u_k['lc'], *casemodel.light_pos(lx, ly))
                st, sb = self._case_scratches()
                self.ActiveTexture(0x84C2); gl.glBindTexture(GL_TEXTURE_2D, st); self.Uniform1i(self.u_k['smap'], 2)
                self.ActiveTexture(0x84C0)
                self.Uniform4f(self.u_k['smr'], *sb)
            self._quad(self.E[0])
            self.UseProgram(0)
            self.ActiveTexture(0x84C2); gl.glBindTexture(GL_TEXTURE_2D, 0)
            self.ActiveTexture(0x84C1); gl.glBindTexture(GL_TEXTURE_2D, 0); self.ActiveTexture(0x84C0)
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
