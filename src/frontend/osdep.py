"""osdep.py - the macOS and Linux side of Vectrex_EMU (on Windows vectrex_emu.py and vecrender.py use the
Windows APIs themselves: WGL, XInput / WinMM, waveOut, GetAsyncKeyState).

    gl_context(widget)   OpenGL drawn into a Tk frame: GLX on Linux (X11 / XWayland), on macOS an NSView
                         with an NSOpenGLContext laid over the frame (clicks pass through to Tk)
    Keys                 the keyboard state from Tk's key events, as Windows virtual-key codes (the
                         settings keep those, so Vectrex_EMU.conf works on every system)
    Pads                 controllers through SDL2: a game controller (Xbox / PlayStation / Switch pads...)
                         looks like Windows' XInput pad, any other joystick like a WinMM one
    Audio                16-bit mono through SDL2's audio queue (the same calls as the waveOut player)
"""
import ctypes, ctypes.util, math, os, sys, threading, time

IS_MAC = sys.platform == 'darwin'
IS_LINUX = sys.platform.startswith('linux')
CF = ctypes.CFUNCTYPE

# ------------------------------------------------------------------ folders
def app_root(exe):
    """the folder the program sits in (its roms folder and settings next to it). macOS: the folder that
    holds Vectrex_EMU.app - also when Gatekeeper runs a downloaded app from a hidden copy (App
    Translocation): then the folder it was really unpacked to."""
    d = os.path.dirname(os.path.abspath(exe))
    if IS_MAC:
        i = d.find('.app/Contents/')
        if i >= 0:
            app = d[:i + 4]
            if '/AppTranslocation/' in app:             # (macOS answers for the app itself, not its folder)
                app = _untranslocate(app) or app
            d = os.path.dirname(app)
    return d

def translocated(path):
    """macOS runs this downloaded app from a hidden copy (it can not see the folders next to it)"""
    return IS_MAC and '/AppTranslocation/' in path

def _untranslocate(path):
    try:
        from Foundation import NSURL
        import objc
        sec = ctypes.CDLL('/System/Library/Frameworks/Security.framework/Security')
        f = sec.SecTranslocateCreateOriginalPathForURL
        f.restype = ctypes.c_void_p; f.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        url = NSURL.fileURLWithPath_(path)
        p = f(ctypes.c_void_p(objc.pyobjc_id(url)), None)
        if p:
            orig = objc.objc_object(c_void_p=p).path()
            if orig and os.path.exists(orig):
                return str(orig)
    except Exception:       # noqa
        pass
    return None

def settings_dir():
    """where the settings go when the program's own folder can not be written"""
    if IS_MAC:
        return os.path.expanduser('~/Library/Application Support/Vectrex_EMU')
    return os.path.join(os.environ.get('XDG_CONFIG_HOME') or os.path.expanduser('~/.config'), 'Vectrex_EMU')

# ------------------------------------------------------------------ OpenGL
def gl_context(widget):
    return MacGL(widget) if IS_MAC else GLX(widget)

class _XWindowAttributes(ctypes.Structure):
    _fields_ = [('x', ctypes.c_int), ('y', ctypes.c_int), ('width', ctypes.c_int), ('height', ctypes.c_int),
                ('border_width', ctypes.c_int), ('depth', ctypes.c_int), ('visual', ctypes.c_void_p),
                ('root', ctypes.c_ulong), ('c_class', ctypes.c_int), ('bit_gravity', ctypes.c_int),
                ('win_gravity', ctypes.c_int), ('backing_store', ctypes.c_int), ('backing_planes', ctypes.c_ulong),
                ('backing_pixel', ctypes.c_ulong), ('save_under', ctypes.c_int), ('colormap', ctypes.c_ulong),
                ('map_installed', ctypes.c_int), ('map_state', ctypes.c_int), ('all_event_masks', ctypes.c_long),
                ('your_event_mask', ctypes.c_long), ('do_not_propagate_mask', ctypes.c_long),
                ('override_redirect', ctypes.c_int), ('screen', ctypes.c_void_p)]

_XERR = CF(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)
_XERR_KEEP = []                                                 # (X error handlers in use: never freed)

class GLX:
    """an OpenGL context on the Tk frame's own X window (its own connection to the X server), with a
    framebuffer config of the window's visual"""
    max_swap_interval = 8
    scale = 1.0                 # (X11: Tk's sizes are pixels)
    def __init__(self, widget):
        X = ctypes.CDLL(ctypes.util.find_library('X11') or 'libX11.so.6')
        G = ctypes.CDLL('libGL.so.1')
        self.X, self.gl = X, G
        vp, ui, i = ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int
        X.XOpenDisplay.restype = vp; X.XOpenDisplay.argtypes = [ctypes.c_char_p]
        X.XDefaultScreen.argtypes = [vp]
        X.XGetWindowAttributes.argtypes = [vp, ui, ctypes.POINTER(_XWindowAttributes)]
        X.XVisualIDFromVisual.argtypes = [vp]; X.XVisualIDFromVisual.restype = ui
        X.XSync.argtypes = [vp, i]; X.XFree.argtypes = [vp]
        X.XRootWindow.argtypes = [vp, i]; X.XRootWindow.restype = ui
        G.glXChooseFBConfig.argtypes = [vp, i, ctypes.POINTER(i), ctypes.POINTER(i)]
        G.glXChooseFBConfig.restype = ctypes.POINTER(vp)
        G.glXGetFBConfigAttrib.argtypes = [vp, vp, i, ctypes.POINTER(i)]
        G.glXCreateNewContext.argtypes = [vp, vp, i, vp, i]; G.glXCreateNewContext.restype = vp
        G.glXMakeCurrent.argtypes = [vp, ui, vp]
        G.glXSwapBuffers.argtypes = [vp, ui]
        G.glXDestroyContext.argtypes = [vp, vp]
        G.glXGetProcAddressARB.argtypes = [ctypes.c_char_p]; G.glXGetProcAddressARB.restype = vp
        self.errors = []
        self._xerr = _XERR(lambda d, e: (self.errors.append(1), 0)[1])     # (an X error must not end the program)
        _XERR_KEEP.append(self._xerr)                           # (Xlib keeps calling it for the whole program - Tk's
        X.XSetErrorHandler(self._xerr)                          #  errors too: freed with a failed context, a crash)
        widget.update_idletasks()
        self.win = widget.winfo_id()
        self.dpy = X.XOpenDisplay(None)
        if not self.dpy:
            raise OSError('no X display')
        scr = X.XDefaultScreen(self.dpy)
        wa = _XWindowAttributes()
        X.XGetWindowAttributes(self.dpy, self.win, ctypes.byref(wa))
        want = X.XVisualIDFromVisual(wa.visual) if wa.visual else 0
        att = (i * 15)(0x8010, 1, 0x8011, 1, 5, 1, 8, 8, 9, 8, 10, 8, 0, 0, 0)   # window, RGBA, double buffered, 8 bits
        n = i(0)
        cfgs = G.glXChooseFBConfig(self.dpy, scr, att, ctypes.byref(n))
        if not cfgs or not n.value:
            X.XCloseDisplay.argtypes = [vp]; X.XCloseDisplay(self.dpy); self.dpy = None
            raise OSError('no GLX framebuffer config - the OpenGL driver did not load (see README: Mac and Linux notes)')
        cfg = cfgs[0]
        for k in range(n.value):                                # (the one of the window's visual)
            v = i(0); G.glXGetFBConfigAttrib(self.dpy, cfgs[k], 0x800B, ctypes.byref(v))     # GLX_VISUAL_ID
            if v.value == want:
                cfg = cfgs[k]; break
        self.ctx = G.glXCreateNewContext(self.dpy, cfg, 0x8014, None, 1)                   # GLX_RGBA_TYPE, direct
        X.XFree(cfgs)
        if not self.ctx:
            raise OSError('no GLX context')
        X.XSync(self.dpy, 0)
        if not G.glXMakeCurrent(self.dpy, self.win, self.ctx):
            raise OSError('glXMakeCurrent failed')
        X.XSync(self.dpy, 0)
        if self.errors:
            raise OSError('X error making the OpenGL context current')
        self._interval = None
        for name, kind in ((b'glXSwapIntervalEXT', 'ext'), (b'glXSwapIntervalMESA', 'mesa')):
            p = G.glXGetProcAddressARB(name)
            if p:
                if kind == 'ext':
                    f = CF(None, vp, ui, i)(p); self._interval = lambda n, f=f: f(self.dpy, self.win, n)
                else:
                    f = CF(i, ctypes.c_uint)(p); self._interval = lambda n, f=f: f(n)
                break
    def proc(self, name, res, *args):
        p = self.gl.glXGetProcAddressARB(name.encode())
        if not p:
            raise OSError('no ' + name)
        return CF(res, *args)(p)
    def swap(self):
        self.gl.glXSwapBuffers(self.dpy, self.win)
    def swap_interval(self, n):
        try:
            if self._interval: self._interval(int(n))
        except Exception:       # noqa
            pass
        return True
    def reshape(self):
        pass
    def refresh_rate(self):
        """the monitor's refresh rate (XRandR), or 0"""
        try:
            R = ctypes.CDLL(ctypes.util.find_library('Xrandr') or 'libXrandr.so.2')
            R.XRRGetScreenInfo.argtypes = [ctypes.c_void_p, ctypes.c_ulong]; R.XRRGetScreenInfo.restype = ctypes.c_void_p
            R.XRRConfigCurrentRate.argtypes = [ctypes.c_void_p]; R.XRRConfigCurrentRate.restype = ctypes.c_short
            R.XRRFreeScreenConfigInfo.argtypes = [ctypes.c_void_p]
            c = R.XRRGetScreenInfo(self.dpy, self.X.XRootWindow(self.dpy, self.X.XDefaultScreen(self.dpy)))
            if c:
                r = R.XRRConfigCurrentRate(c); R.XRRFreeScreenConfigInfo(c)
                return int(r)
        except Exception:       # noqa
            pass
        return 0
    def close(self):
        try:
            self.gl.glXMakeCurrent(self.dpy, 0, None)
            self.gl.glXDestroyContext(self.dpy, self.ctx)
            self.X.XCloseDisplay(ctypes.c_void_p(self.dpy))
        except Exception:       # noqa
            pass

_MacView = None
def _mac_view_class():
    """an NSView that never takes a click (they go to the Tk window under it)"""
    global _MacView
    if _MacView is None:
        from AppKit import NSView
        class VXGLView(NSView):
            def hitTest_(self, pt):
                return None
            def isOpaque(self):
                return True
            def acceptsFirstResponder(self):
                return False
        _MacView = VXGLView
    return _MacView

class MacGL:
    """an NSOpenGLContext (legacy profile: OpenGL 2.1, the fixed pipeline the renderer uses) over the Tk
    frame: in a borderless child window that moves with the Tk window and lets clicks through
    (VX_MACGL=child, the default), or in an NSView added to the Tk window (VX_MACGL=subview)"""
    max_swap_interval = 1
    def __init__(self, widget):
        self.mode = os.environ.get('VX_MACGL', 'child')
        import AppKit, objc     # noqa
        self.AppKit = AppKit
        self.widget = widget
        self.top = widget.winfo_toplevel()
        self.top.update()
        self.nswin = self._find_window()
        if self.nswin is None:
            raise OSError('no NSWindow for the Tk window')
        A = AppKit
        self.child = None
        if self.mode == 'child':
            self.child = A.NSWindow.alloc().initWithContentRect_styleMask_backing_defer_(self._screen_rect(), 0, 2, False)
            self.child.setIgnoresMouseEvents_(True); self.child.setOpaque_(True); self.child.setHasShadow_(False)
            self.child.setBackgroundColor_(A.NSColor.blackColor()); self.child.setReleasedWhenClosed_(False)
            self.nswin.addChildWindow_ordered_(self.child, 1)          # NSWindowAbove
            self.child.orderFront_(None)
            self.view = self.child.contentView()
        else:
            self.view = _mac_view_class().alloc().initWithFrame_(self._rect())
            self.nswin.contentView().addSubview_(self.view)
        self.scale = 1.0                # the screen's pixels per Tk point (2 on a Retina screen, see reshape)
        self._retina()
        base = [A.NSOpenGLPFADoubleBuffer, A.NSOpenGLPFAColorSize, 24, A.NSOpenGLPFAAlphaSize, 8]
        pf = None
        for extra in ([A.NSOpenGLPFAAccelerated, A.NSOpenGLPFANoRecovery], [A.NSOpenGLPFAAccelerated], []):
            pf = A.NSOpenGLPixelFormat.alloc().initWithAttributes_(base + extra + [0])   # (the graphics card first)
            if pf is not None:
                break
        if pf is None:
            raise OSError('no OpenGL pixel format')
        self.ctx = A.NSOpenGLContext.alloc().initWithFormat_shareContext_(pf, None)
        if self.ctx is None:
            raise OSError('no OpenGL context')
        self.ctx.setView_(self.view)
        self.ctx.update()
        self.ctx.makeCurrentContext()
        self.gl = ctypes.CDLL('/System/Library/Frameworks/OpenGL.framework/OpenGL')
    def _find_window(self):
        A = self.AppKit
        lib = self._libtk()                                     # (Tk's own call, Tk 8.6.10 and later)
        if lib is not None:
            try:
                f = lib.Tk_MacOSXGetNSWindowForDrawable
                f.restype = ctypes.c_void_p; f.argtypes = [ctypes.c_void_p]
                p = f(ctypes.c_void_p(int(self.top.winfo_id())))
                if p:
                    import objc
                    return objc.objc_object(c_void_p=p)
            except Exception:   # noqa
                pass
        title = self.top.title()                                # (else: the window with our title)
        app = A.NSApplication.sharedApplication()
        for w in app.windows():
            if w.title() == title and w.isVisible():
                return w
        for w in app.windows():
            if w.isVisible():
                return w
        return None
    @staticmethod
    def _libtk():
        """the Tk library this process loaded (found among the loaded images)"""
        try:
            dl = ctypes.CDLL(None)
            dl._dyld_image_count.restype = ctypes.c_uint32
            dl._dyld_get_image_name.restype = ctypes.c_char_p; dl._dyld_get_image_name.argtypes = [ctypes.c_uint32]
            for k in range(dl._dyld_image_count()):
                name = (dl._dyld_get_image_name(k) or b'').decode(errors='replace')
                base = os.path.basename(name).lower()
                if base.startswith('libtk') or base == 'tk':
                    return ctypes.CDLL(name)
        except Exception:       # noqa
            pass
        return None
    def _rect(self):
        from Foundation import NSMakeRect
        w, t = self.widget, self.top
        x = w.winfo_rootx() - t.winfo_rootx(); y = w.winfo_rooty() - t.winfo_rooty()
        ww, hh = max(1, w.winfo_width()), max(1, w.winfo_height())
        cv = self.nswin.contentView()
        if not cv.isFlipped():
            y = cv.frame().size.height - y - hh
        return NSMakeRect(x, y, ww, hh)
    def _screen_rect(self):
        cv = self.nswin.contentView()
        return self.nswin.convertRectToScreen_(cv.convertRect_toView_(self._rect(), None))
    def diag(self):
        """(for tests) where the view is and what it is"""
        cv = self.nswin.contentView()
        return 'mode %s view %s flipped %s layer %s content %s %s' % (
            self.mode, tuple(self.view.frame()), cv.isFlipped(), self.view.wantsLayer(), tuple(cv.frame()),
            tuple(self.child.frame()) if self.child else '')
    def proc(self, name, res, *args):
        try:
            f = getattr(self.gl, name)
        except AttributeError:
            raise OSError('no ' + name)
        return CF(res, *args)(ctypes.cast(f, ctypes.c_void_p).value)
    def swap(self):
        self.ctx.flushBuffer()
    def swap_interval(self, n):
        try:
            self.ctx.setValues_forParameter_([min(1, int(n))], 222)    # NSOpenGLContextParameterSwapInterval
        except Exception:       # noqa
            pass
        return True
    def _retina(self):
        """a Retina screen: the picture at its full resolution (the view's pixels, not its points)"""
        try:
            s = float((self.child or self.nswin).backingScaleFactor())
        except Exception:       # noqa
            s = 1.0
        if os.environ.get('VX_RETINA') == '0':                  # (tests: points only)
            s = 1.0
        self.view.setWantsBestResolutionOpenGLSurface_(s > 1.0)
        self.scale = max(1.0, s)
    def reshape(self):
        self._retina()                  # (the window may have moved to another screen)
        if self.child is not None:
            self.child.setFrame_display_(self._screen_rect(), True)
        else:
            self.view.setFrame_(self._rect())
        self.ctx.update()
        self.ctx.makeCurrentContext()
    def refresh_rate(self):
        try:
            s = self.nswin.screen() or self.AppKit.NSScreen.mainScreen()
            return int(s.maximumFramesPerSecond())
        except Exception:       # noqa
            return 60
    def close(self):
        try:
            self.AppKit.NSOpenGLContext.clearCurrentContext()
            self.ctx.clearDrawable()
            if self.child is not None:
                self.nswin.removeChildWindow_(self.child); self.child.orderOut_(None)
            else:
                self.view.removeFromSuperview()
        except Exception:       # noqa
            pass

# ------------------------------------------------------------------ the keyboard
# Tk key names -> Windows virtual-key codes (what Vectrex_EMU.conf keeps)
_VK = {'BackSpace': 0x08, 'Tab': 0x09, 'ISO_Left_Tab': 0x09, 'Return': 0x0D, 'KP_Enter': 0x0D, 'Shift_L': 0x10,
       'Shift_R': 0x10, 'Control_L': 0x11, 'Control_R': 0x11, 'Alt_L': 0x12, 'Alt_R': 0x12, 'Option_L': 0x12,
       'Option_R': 0x12, 'Meta_L': 0x5B, 'Meta_R': 0x5C, 'Super_L': 0x5B, 'Super_R': 0x5C, 'Command': 0x5B,
       'Pause': 0x13, 'Caps_Lock': 0x14, 'Escape': 0x1B, 'space': 0x20, 'Prior': 0x21, 'Next': 0x22,
       'End': 0x23, 'Home': 0x24, 'Left': 0x25, 'Up': 0x26, 'Right': 0x27, 'Down': 0x28, 'Insert': 0x2D,
       'Delete': 0x2E, 'KP_Multiply': 0x6A, 'KP_Add': 0x6B, 'KP_Subtract': 0x6D, 'KP_Decimal': 0x6E,
       'KP_Divide': 0x6F, 'KP_Prior': 0x21, 'KP_Next': 0x22, 'KP_End': 0x23, 'KP_Home': 0x24, 'KP_Left': 0x25,
       'KP_Up': 0x26, 'KP_Right': 0x27, 'KP_Down': 0x28, 'KP_Insert': 0x2D, 'KP_Delete': 0x2E,
       'semicolon': 0xBA, 'colon': 0xBA, 'equal': 0xBB, 'plus': 0xBB, 'comma': 0xBC, 'less': 0xBC, 'minus': 0xBD,
       'underscore': 0xBD, 'period': 0xBE, 'greater': 0xBE, 'slash': 0xBF, 'question': 0xBF, 'grave': 0xC0,
       'asciitilde': 0xC0, 'bracketleft': 0xDB, 'braceleft': 0xDB, 'backslash': 0xDC, 'bar': 0xDC,
       'bracketright': 0xDD, 'braceright': 0xDD, 'apostrophe': 0xDE, 'quotedbl': 0xDE,
       'exclam': 0x31, 'at': 0x32, 'numbersign': 0x33, 'dollar': 0x34, 'percent': 0x35, 'asciicircum': 0x36,
       'ampersand': 0x37, 'asterisk': 0x38, 'parenleft': 0x39, 'parenright': 0x30}
_VK.update({'F%d' % n: 0x6F + n for n in range(1, 25)})
_VK.update({'KP_%d' % n: 0x60 + n for n in range(10)})

def vk_of(keysym):
    """a Tk key name as a Windows virtual-key code (0: unknown)"""
    if len(keysym) == 1 and keysym.isalnum() and keysym.isascii():
        return ord(keysym.upper())
    return _VK.get(keysym, 0)

class Keys:
    """which keys are held, from Tk's key events. X11 repeats a held key as release + press pairs: a
    release counts only when no press follows within 30 ms."""
    def __init__(self):
        self.down = set()
        self.root = None
        self._rel = {}
    def attach(self, root):
        self.root = root
        root.bind_all('<KeyPress>', self._press, add='+')
        root.bind_all('<KeyRelease>', self._release, add='+')
    def _press(self, e):
        vk = vk_of(e.keysym)
        if vk:
            self._rel.pop(vk, None); self.down.add(vk)
    def _release(self, e):
        vk = vk_of(e.keysym)
        if not vk:
            return
        if IS_MAC:
            self.down.discard(vk); return
        t = time.perf_counter(); self._rel[vk] = t
        def done():
            if self._rel.get(vk) == t:
                self._rel.pop(vk, None); self.down.discard(vk)
        self.root.after(30, done)
    def clear(self):
        self.down.clear()
    def held(self, vk):
        return vk in self.down

KEYS = Keys()

# ------------------------------------------------------------------ SDL2 (controllers, sound)
_sdl = None
_sdl_lock = threading.Lock()
def sdl():
    global _sdl
    if _sdl is None:
        lib = os.path.join(getattr(sys, '_MEIPASS', ''), 'sdllib')
        if IS_MAC and getattr(sys, 'frozen', False) and os.path.isdir(lib):
            os.environ['PYSDL2_DLL_PATH'] = lib     # (the app: its own copy; from the sources: the pysdl2-dll package)
        import sdl2
        _sdl = sdl2
    return _sdl

def sdl_init(flags):
    S = sdl()
    with _sdl_lock:
        S.SDL_SetHint(b'SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS', b'1')
        S.SDL_SetHint(b'SDL_NO_SIGNAL_HANDLERS', b'1')
        return S.SDL_InitSubSystem(flags) == 0

JOY_AXES = ['X', 'Y', 'Z', 'R', 'U', 'V']

class Pads:
    """the controllers, read by SDL2 in a thread of their own (SDL's joysticks want their own thread:
    the one that opened them reads them): game controllers as 'xi<n>' (the XInput layout and names),
    other joysticks as 'joy:vendor:product[:n]' (axes X Y Z R U V, buttons J1.., the hat)"""
    def __init__(self):
        self.devs = []
        self.state = {}
        self.ok = False
        self._t = threading.Thread(target=self._run, daemon=True)
        self._t.start()
        for _ in range(50):                                     # (the first scan, before the first frame)
            if self.ok is not False: break
            time.sleep(0.01)
    def _run(self):
        try:
            S = sdl()
            if not sdl_init(S.SDL_INIT_JOYSTICK | S.SDL_INIT_GAMECONTROLLER):
                self.ok = None; return
        except Exception as e:  # noqa
            print('controllers unavailable (%s)' % e)
            if os.environ.get('VX_DEBUG'):
                import traceback; traceback.print_exc()
            self.ok = None; return
        opened = {}                                             # instance id -> (handle, is controller)
        last_scan = -9.0
        self.ok = True
        while True:
            S.SDL_GameControllerUpdate()
            now = time.perf_counter()
            if now - last_scan > 1.0:
                last_scan = now
                S.SDL_JoystickUpdate()
                live = set()
                for k in range(S.SDL_NumJoysticks()):
                    iid = S.SDL_JoystickGetDeviceInstanceID(k)
                    live.add(iid)
                    if iid in opened: continue
                    if S.SDL_IsGameController(k):
                        h = S.SDL_GameControllerOpen(k)
                        if h: opened[iid] = (h, True)
                    else:
                        h = S.SDL_JoystickOpen(k)
                        if h: opened[iid] = (h, False)
                for iid in list(opened):
                    h, gc = opened[iid]
                    js = S.SDL_GameControllerGetJoystick(h) if gc else h
                    if iid not in live or not S.SDL_JoystickGetAttached(js):
                        (S.SDL_GameControllerClose if gc else S.SDL_JoystickClose)(h)
                        del opened[iid]
            devs = []; state = {}; nxi = 0; seen = {}
            for iid in sorted(opened):
                h, gc = opened[iid]
                if gc:
                    did = 'xi%d' % nxi; nxi += 1
                    name = (S.SDL_GameControllerName(h) or b'Game controller').decode(errors='replace')
                    devs.append(dict(id=did, name=name, kind='xi', index=iid, axes=['LX', 'LY', 'RX', 'RY']))
                    state[did] = self._pad(S, h)
                else:
                    vid, pid = S.SDL_JoystickGetVendor(h), S.SDL_JoystickGetProduct(h)
                    seen[(vid, pid)] = n = seen.get((vid, pid), 0) + 1
                    did = 'joy:%04x:%04x' % (vid, pid) + ('' if n == 1 else ':%d' % n)
                    name = (S.SDL_JoystickName(h) or b'USB controller').decode(errors='replace')
                    na = min(6, max(2, S.SDL_JoystickNumAxes(h)))
                    devs.append(dict(id=did, name=name + ('' if n == 1 else ' (%d)' % n), kind='joy', index=iid,
                                     axes=JOY_AXES[:na], buttons=S.SDL_JoystickNumButtons(h),
                                     pov=S.SDL_JoystickNumHats(h) > 0))
                    state[did] = self._joy(S, h)
            self.devs, self.state = devs, state
            time.sleep(0.004)
    @staticmethod
    def _pad(S, h):
        ax = lambda a: max(-1.0, S.SDL_GameControllerGetAxis(h, a) / 32767.0)
        bt = lambda b: S.SDL_GameControllerGetButton(h, b)
        C = S
        b = {n for n, k in (('A', C.SDL_CONTROLLER_BUTTON_A), ('B', C.SDL_CONTROLLER_BUTTON_B),
                            ('X', C.SDL_CONTROLLER_BUTTON_X), ('Y', C.SDL_CONTROLLER_BUTTON_Y),
                            ('START', C.SDL_CONTROLLER_BUTTON_START), ('BACK', C.SDL_CONTROLLER_BUTTON_BACK),
                            ('LB', C.SDL_CONTROLLER_BUTTON_LEFTSHOULDER), ('RB', C.SDL_CONTROLLER_BUTTON_RIGHTSHOULDER),
                            ('LS', C.SDL_CONTROLLER_BUTTON_LEFTSTICK), ('RS', C.SDL_CONTROLLER_BUTTON_RIGHTSTICK)) if bt(k)}
        if ax(C.SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.5: b.add('LT')
        if ax(C.SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.5: b.add('RT')
        r, l = bt(C.SDL_CONTROLLER_BUTTON_DPAD_RIGHT), bt(C.SDL_CONTROLLER_BUTTON_DPAD_LEFT)
        u, d = bt(C.SDL_CONTROLLER_BUTTON_DPAD_UP), bt(C.SDL_CONTROLLER_BUTTON_DPAD_DOWN)
        pov = (float(r) - float(l), float(u) - float(d)) if (r or l or u or d) else None
        return dict(axes=dict(LX=ax(C.SDL_CONTROLLER_AXIS_LEFTX), LY=-ax(C.SDL_CONTROLLER_AXIS_LEFTY),    # (up: +, as XInput)
                              RX=ax(C.SDL_CONTROLLER_AXIS_RIGHTX), RY=-ax(C.SDL_CONTROLLER_AXIS_RIGHTY)),
                    buttons=b, pov=pov)
    @staticmethod
    def _joy(S, h):
        na = min(6, S.SDL_JoystickNumAxes(h))
        axes = {JOY_AXES[k]: max(-1.0, S.SDL_JoystickGetAxis(h, k) / 32767.0) for k in range(na)}
        axes.setdefault('X', 0.0); axes.setdefault('Y', 0.0)
        btn = {'J%d' % (k + 1) for k in range(min(32, S.SDL_JoystickNumButtons(h))) if S.SDL_JoystickGetButton(h, k)}
        pov = None
        if S.SDL_JoystickNumHats(h) > 0:
            hv = S.SDL_JoystickGetHat(h, 0)
            if hv:
                pov = (float(bool(hv & 2)) - float(bool(hv & 8)), float(bool(hv & 1)) - float(bool(hv & 4)))
        return dict(axes=axes, buttons=btn, pov=pov)
    def devices(self):
        return list(self.devs)
    def pad_state(self, d):
        return self.state.get(d['id']) if d else None

class Audio:
    """16-bit mono at `rate` through SDL2's audio queue"""
    def __init__(self, rate):
        self.rate = rate
        self.ok = False
        self.pending = []
        self.last = bytes(2)
        self.underruns = 0
        try:
            S = self.S = sdl()
            if not sdl_init(S.SDL_INIT_AUDIO):
                raise OSError(S.SDL_GetError().decode(errors='replace'))
            want = S.SDL_AudioSpec(rate, S.AUDIO_S16SYS, 1, 512)
            have = S.SDL_AudioSpec(0, 0, 0, 0)
            self.dev = S.SDL_OpenAudioDevice(None, 0, ctypes.byref(want), ctypes.byref(have), 0)
            if not self.dev:
                raise OSError(S.SDL_GetError().decode(errors='replace'))
            self.ok = True
            S.SDL_PauseAudioDevice(self.dev, 0)
            self._write(bytes(2 * rate * 40 // 1000))           # 40 ms head start: the queue never runs dry
        except Exception as e:  # noqa
            print('sound unavailable (%s)' % e)
            if os.environ.get('VX_DEBUG'):
                import traceback; traceback.print_exc()
    def queued_samples(self):
        return self.S.SDL_GetQueuedAudioSize(self.dev) // 2 if self.ok else 0
    def play(self, data):
        if not self.ok or not data:
            return
        if not self.S.SDL_GetQueuedAudioSize(self.dev):          # ran dry: bridge with 8 ms of the last level
            self.underruns += 1
            self._write(self.last * (self.rate // 125))
        self.last = data[-2:]
        self._write(data)
    def _write(self, data):
        self.S.SDL_QueueAudio(self.dev, data, len(data))
    def flush(self):
        if self.ok:
            self.S.SDL_ClearQueuedAudio(self.dev)
            self._write(bytes(2 * self.rate * 10 // 1000))
    def close(self):
        if self.ok:
            self.S.SDL_ClearQueuedAudio(self.dev)
            self.S.SDL_CloseAudioDevice(self.dev)
            self.dev = 0
