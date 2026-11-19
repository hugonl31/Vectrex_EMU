"""lrhost.py - a minimal libretro frontend for testing the Vectrex_EMU core on a Windows PC: the real core DLL, an OpenGL
context (WGL, hidden window), scripted input, every frame's pixels read back (the GPU path's, or a CPU frame). Exact and
repeatable: the same core code the iOS app and the Android core run (OpenGL 3.3 here instead of GLES 3). Two cores in
one process: copy the DLL under another name - each Host switches to its own GL context.
  The DLL: libretro/build_libretro_win.bat (Visual C++). For online play's tests (vx_net_*, netvx.c) link it with an
export list that has those names too (exports.def + vx_net_host, vx_net_join, vx_net_accept, vx_net_prepare, vx_net_go,
vx_net_leave, vx_net_chat, vx_net_pause, vx_net_room, vx_net_lan, vx_net_status, vx_net_md5_file).
  Used 2026-11-19 to measure the phosphor trail overlay on / off (4 cores: overlay x afterglow) and to test online play
core to core and against the PC app (the RAM's checksums alike at every check).

    h = Host(dll, system_dir, save_dir, options={...}, size=(w, h))
    h.load(rom)
    for f in range(n): h.run(pad=dict(up=1, b4=1), ax=(x, y)); img = h.grab()    # img: numpy uint8 (h, w, 3), top row first
"""
import ctypes, ctypes.wintypes as W, os, sys
import numpy as np

c_bool = ctypes.c_bool
ENV_T = ctypes.CFUNCTYPE(c_bool, ctypes.c_uint, ctypes.c_void_p)
VIDEO_T = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_size_t)
AUDIO_T = ctypes.CFUNCTYPE(None, ctypes.c_int16, ctypes.c_int16)
AUDIOB_T = ctypes.CFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t)
POLL_T = ctypes.CFUNCTYPE(None)
STATE_T = ctypes.CFUNCTYPE(ctypes.c_int16, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint)
RESET_T = ctypes.CFUNCTYPE(None)
GETFB_T = ctypes.CFUNCTYPE(ctypes.c_size_t)
PROC_T = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_char_p)


class HWR(ctypes.Structure):
    _fields_ = [('context_type', ctypes.c_int), ('context_reset', ctypes.c_void_p), ('get_current_framebuffer', GETFB_T),
                ('get_proc_address', PROC_T), ('depth', c_bool), ('stencil', c_bool), ('bottom_left_origin', c_bool),
                ('version_major', ctypes.c_uint), ('version_minor', ctypes.c_uint), ('cache_context', c_bool),
                ('context_destroy', ctypes.c_void_p), ('debug_context', c_bool)]


class VAR(ctypes.Structure):
    _fields_ = [('key', ctypes.c_char_p), ('value', ctypes.c_char_p)]


class GAMEINFO(ctypes.Structure):
    _fields_ = [('path', ctypes.c_char_p), ('data', ctypes.c_void_p), ('size', ctypes.c_size_t), ('meta', ctypes.c_char_p)]


class GEOM(ctypes.Structure):
    _fields_ = [('base_width', ctypes.c_uint), ('base_height', ctypes.c_uint), ('max_width', ctypes.c_uint), ('max_height', ctypes.c_uint), ('aspect_ratio', ctypes.c_float)]


class AVINFO(ctypes.Structure):
    _fields_ = [('geometry', GEOM), ('fps', ctypes.c_double), ('sample_rate', ctypes.c_double)]


# the RetroPad's ids
JP = dict(b=0, y=1, select=2, start=3, up=4, down=5, left=6, right=7, a=8, x=9, l=10, r=11, l2=12, r2=13, l3=14, r3=15)
VX_BTN = dict(b1='y', b2='x', b3='b', b4='a')        # (the core's default layout: buttons 1-4 on Y X B A)

_u32, _gdi, _gl, _k32 = ctypes.windll.user32, ctypes.windll.gdi32, ctypes.windll.opengl32, ctypes.windll.kernel32


class PFD(ctypes.Structure):
    _fields_ = [('nSize', W.WORD), ('nVersion', W.WORD), ('dwFlags', W.DWORD), ('iPixelType', ctypes.c_byte), ('cColorBits', ctypes.c_byte)] + \
               [(f, ctypes.c_byte) for f in 'a b c d e f g h i j k l m n o p q r'.split()] + [('dwLayerMask', W.DWORD), ('dwVisibleMask', W.DWORD), ('dwDamageMask', W.DWORD)]


def gl_context():
    _u32.CreateWindowExW.restype = W.HWND
    _u32.CreateWindowExW.argtypes = [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, W.HWND, W.HMENU, W.HINSTANCE, W.LPVOID]
    hwnd = _u32.CreateWindowExW(0, 'STATIC', 'lrhost', 0, 0, 0, 64, 64, None, None, None, None)
    _u32.GetDC.restype = W.HDC; _u32.GetDC.argtypes = [W.HWND]; hdc = _u32.GetDC(hwnd)
    _gdi.ChoosePixelFormat.argtypes = [W.HDC, ctypes.c_void_p]; _gdi.SetPixelFormat.argtypes = [W.HDC, ctypes.c_int, ctypes.c_void_p]
    pfd = PFD(); pfd.nSize = ctypes.sizeof(PFD); pfd.nVersion = 1; pfd.dwFlags = 0x25; pfd.cColorBits = 32
    _gdi.SetPixelFormat(hdc, _gdi.ChoosePixelFormat(hdc, ctypes.byref(pfd)), ctypes.byref(pfd))
    _gl.wglCreateContext.restype = ctypes.c_void_p; _gl.wglCreateContext.argtypes = [W.HDC]
    _gl.wglMakeCurrent.argtypes = [W.HDC, ctypes.c_void_p]
    ctx = _gl.wglCreateContext(hdc); _gl.wglMakeCurrent(hdc, ctx)
    return hdc, ctx


_gl.wglGetProcAddress.restype = ctypes.c_void_p; _gl.wglGetProcAddress.argtypes = [ctypes.c_char_p]
_k32.GetProcAddress.restype = ctypes.c_void_p; _k32.GetProcAddress.argtypes = [ctypes.c_void_p, ctypes.c_char_p]


def proc(name):
    p = _gl.wglGetProcAddress(name)
    if not p or p in (1, 2, 3, -1 & 0xffffffffffffffff):
        p = _k32.GetProcAddress(ctypes.c_void_p(_gl._handle), name)
    return p


def glf(name, res, *args):
    return ctypes.WINFUNCTYPE(res, *args)(proc(name.encode()))


class Host:
    def __init__(self, dll, system_dir, save_dir, options=None, size=(1440, 1920), hdr=False, log=False):
        self.opts = dict(options or {}); self.defaults = {}; self.updated = True; self.log = log
        self.sysdir = ctypes.c_char_p(system_dir.encode()); self.savedir = ctypes.c_char_p(save_dir.encode())
        self.hdc, self.ctx = gl_context()
        self.W, self.H = size; self.hdr = hdr
        self.glGenFramebuffers = glf('glGenFramebuffers', None, ctypes.c_int, ctypes.POINTER(ctypes.c_uint))
        self.glBindFramebuffer = glf('glBindFramebuffer', None, ctypes.c_uint, ctypes.c_uint)
        self.glFramebufferTexture2D = glf('glFramebufferTexture2D', None, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_int)
        gl = _gl
        t = ctypes.c_uint(); gl.glGenTextures(1, ctypes.byref(t)); gl.glBindTexture(0x0DE1, t.value)
        gl.glTexImage2D(0x0DE1, 0, 0x881A if hdr else 0x8058, self.W, self.H, 0, 0x1908, 0x1406 if hdr else 0x1401, None)
        gl.glTexParameteri(0x0DE1, 0x2801, 0x2600); gl.glTexParameteri(0x0DE1, 0x2800, 0x2600)
        f = ctypes.c_uint(); self.glGenFramebuffers(1, ctypes.byref(f)); self.glBindFramebuffer(0x8D40, f.value)
        self.glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, t.value, 0); self.fbo = f.value
        self.dll = ctypes.CDLL(dll)
        self.hwr = None; self.frame = None; self.fw = self.fh = 0; self.pad = {0: {}, 1: {}}; self.ax = {0: (0, 0), 1: (0, 0)}
        self.cb_env = ENV_T(self._env); self.cb_video = VIDEO_T(self._video); self.cb_audio = AUDIO_T(lambda l, r: None)
        self.cb_audiob = AUDIOB_T(lambda d, n: n); self.cb_poll = POLL_T(lambda: None); self.cb_state = STATE_T(self._state)
        self.cb_getfb = GETFB_T(lambda: self.fbo); self.cb_proc = PROC_T(lambda n: proc(n))
        d = self.dll
        d.retro_set_environment(self.cb_env); d.retro_set_video_refresh(self.cb_video); d.retro_set_audio_sample(self.cb_audio)
        d.retro_set_audio_sample_batch(self.cb_audiob); d.retro_set_input_poll(self.cb_poll); d.retro_set_input_state(self.cb_state)
        d.retro_init()

    # ---- the environment
    def _env(self, cmd, data):
        try:
            if cmd in (9, 31):                                   # GET_SYSTEM_DIRECTORY, GET_SAVE_DIRECTORY
                ctypes.cast(data, ctypes.POINTER(ctypes.c_void_p))[0] = ctypes.cast(self.sysdir if cmd == 9 else self.savedir, ctypes.c_void_p).value
                return True
            if cmd == 52: return False                           # GET_CORE_OPTIONS_VERSION: the flat list
            if cmd == 16:                                        # SET_VARIABLES
                arr = ctypes.cast(data, ctypes.POINTER(VAR)); i = 0
                while arr[i].key:
                    v = arr[i].value.decode(); self.defaults[arr[i].key.decode()] = v.split('; ', 1)[1].split('|')[0]; i += 1
                return True
            if cmd == 15:                                        # GET_VARIABLE
                v = ctypes.cast(data, ctypes.POINTER(VAR))[0]; k = v.key.decode()
                val = self.opts.get(k, self.defaults.get(k))
                if val is None: return False
                self._keep = getattr(self, '_keep', {}); self._keep[k] = ctypes.create_string_buffer(val.encode())
                ctypes.cast(data, ctypes.POINTER(ctypes.c_void_p))[1] = ctypes.addressof(self._keep[k])
                return True
            if cmd == 17:                                        # GET_VARIABLE_UPDATE
                ctypes.cast(data, ctypes.POINTER(c_bool))[0] = self.updated; self.updated = False; return True
            if cmd == 14 or cmd == 14 | 0x10000:                 # SET_HW_RENDER
                h = ctypes.cast(data, ctypes.POINTER(HWR))[0]
                if h.context_type not in (1, 3): return False
                h.get_current_framebuffer = self.cb_getfb; h.get_proc_address = self.cb_proc
                ctypes.cast(data, ctypes.POINTER(HWR))[0] = h; self.hwr = ctypes.cast(data, ctypes.POINTER(HWR))[0]
                self.reset_fn = ctypes.CFUNCTYPE(None)(h.context_reset); return True
            if cmd == 10: return True                            # SET_PIXEL_FORMAT
            if cmd == 47:                                        # GET_AUDIO_VIDEO_ENABLE
                ctypes.cast(data, ctypes.POINTER(ctypes.c_int))[0] = 3; return True
            if cmd in (32, 37, 18, 11, 35, 8, 3, 0x10000 | 70, 70): return True
            if cmd == 6 and self.log:                            # SET_MESSAGE
                print('[msg]', ctypes.cast(data, ctypes.POINTER(ctypes.c_char_p))[0]); return True
        except Exception as e:
            print('env error', cmd, e)
        return False

    def _video(self, data, w, h, pitch):
        self.fw, self.fh = w, h; self.frame = data
        if data and data != (1 << 64) - 1 and data != -1:            # (a CPU frame: XRGB8888, kept)
            a = np.ctypeslib.as_array(ctypes.cast(data, ctypes.POINTER(ctypes.c_uint8)), shape=(h, pitch))
            px = a[:, :w * 4].reshape(h, w, 4); self.soft = px[:, :, [2, 1, 0]].copy()
        else:
            self.soft = None

    def _state(self, port, device, index, id_):
        if port > 1: return 0
        if device == 1:                                          # JOYPAD
            p = self.pad[port]
            if id_ == 256: return sum(1 << JP[k] for k, v in p.items() if v)
            for k, v in p.items():
                if v and JP.get(k) == id_: return 1
            return 0
        if device == 5 and index == 0:                           # ANALOG, left stick: -32768..32767, y down
            x, y = self.ax[port]
            return int(max(-32767, min(32767, x * 256 if id_ == 0 else -y * 256)))
        return 0

    def set_options(self, **kv):
        self.opts.update({k if k.startswith('vectrex_emu_') else 'vectrex_emu_' + k: v for k, v in kv.items()}); self.updated = True

    def load(self, rom):
        _gl.wglMakeCurrent(self.hdc, self.ctx); data = open(rom, 'rb').read(); self._romdata = ctypes.create_string_buffer(data, len(data))
        gi = GAMEINFO(rom.encode(), ctypes.cast(self._romdata, ctypes.c_void_p), len(data), None)
        self.dll.retro_load_game.argtypes = [ctypes.POINTER(GAMEINFO)]; self.dll.retro_load_game.restype = c_bool
        ok = self.dll.retro_load_game(ctypes.byref(gi))
        av = AVINFO(); self.dll.retro_get_system_av_info(ctypes.byref(av)); self.av = av
        if self.hwr is not None: _gl.wglMakeCurrent(self.hdc, self.ctx); self.reset_fn()
        return ok

    def run(self, port=0, **buttons):
        """one frame; buttons: up/down/left/right, b1..b4 (the Vectrex's), any RetroPad name, ax=(x, y) -128..127"""
        if 'ax' in buttons: self.ax[port] = buttons.pop('ax')
        p = {}
        for k, v in buttons.items(): p[VX_BTN.get(k, k)] = v
        self.pad[port] = p
        _gl.wglMakeCurrent(self.hdc, self.ctx); self.dll.retro_run()

    def grab(self, float_=False):
        """the last frame the core drew, top row first"""
        w, h = self.fw, self.fh
        if not w: return None
        if getattr(self, 'soft', None) is not None: return self.soft.copy()
        _gl.wglMakeCurrent(self.hdc, self.ctx); self.glBindFramebuffer(0x8D40, self.fbo)
        _gl.glReadPixels.argtypes = [ctypes.c_int] * 4 + [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p]
        _gl.glPixelStorei(0x0D05, 1)
        if float_:
            buf = np.empty((h, w, 3), np.float32); _gl.glReadPixels(0, 0, w, h, 0x1907, 0x1406, buf.ctypes.data)
        else:
            buf = np.empty((h, w, 3), np.uint8); _gl.glReadPixels(0, 0, w, h, 0x1907, 0x1401, buf.ctypes.data)
        return buf[::-1].copy()

    def close(self):
        try: self.dll.retro_unload_game(); self.dll.retro_deinit()
        except Exception: pass

# Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md.
