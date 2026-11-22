"""hdrwin.py - HDR for the desktop app on Windows: the picture shown through a DirectX 11 swap chain in Windows' extended
linear colour space (scRGB, half floats - 1.0 is the SDR white of the desktop's HDR mode scaled to 80 nits), with the
OpenGL renderer drawing into a texture shared with DirectX (WGL_NV_DX_interop2: NVIDIA, AMD and Intel drivers).
probe() tells what the monitor can (HDR on, its peak, the SDR white level Windows uses for ordinary pictures); Out is
the output: begin() before the frame's drawing (OpenGL draws into its framebuffer instead of the window), end() shows
it. Everything is optional - without HDR the app draws to its window as before.
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import ctypes
import ctypes.wintypes as W

HR = ctypes.HRESULT if hasattr(ctypes, 'HRESULT') else ctypes.c_long


class GUID(ctypes.Structure):
    _fields_ = [('a', ctypes.c_uint32), ('b', ctypes.c_uint16), ('c', ctypes.c_uint16), ('d', ctypes.c_ubyte * 8)]


def guid(s):
    s = s.strip('{}').replace('-', '')
    g = GUID(int(s[0:8], 16), int(s[8:12], 16), int(s[12:16], 16))
    for i in range(8):
        g.d[i] = int(s[16 + 2 * i:18 + 2 * i], 16)
    return g


IID_IDXGIDevice = guid('54ec77fa-1377-44e6-8c32-88fd5f44c84c')
IID_IDXGIFactory2 = guid('50c83a1c-e072-4c48-87b0-3630fa36a6d0')
IID_IDXGISwapChain3 = guid('94d99bdb-f1f8-4ab0-b236-7da0170edab1')
IID_ID3D11Texture2D = guid('6f15aaf2-d208-4e89-9ab4-489535d34f9c')
IID_IDXGIOutput6 = guid('068346e8-aaec-4b84-add7-137f513f77a1')


def call(obj, idx, res, *types):
    """a COM method: the idx-th entry of obj's table"""
    vt = ctypes.cast(ctypes.cast(obj, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
    return ctypes.WINFUNCTYPE(res, ctypes.c_void_p, *types)(vt[idx])


def qi(obj, iid):
    p = ctypes.c_void_p()
    hr = call(obj, 0, HR, ctypes.POINTER(GUID), ctypes.POINTER(ctypes.c_void_p))(obj, ctypes.byref(iid), ctypes.byref(p))
    return p if hr >= 0 and p.value else None


def release(obj):
    if obj and getattr(obj, 'value', obj):
        call(obj, 2, ctypes.c_ulong)(obj)


class SAMPLE(ctypes.Structure):
    _fields_ = [('Count', ctypes.c_uint), ('Quality', ctypes.c_uint)]


class TEX2D(ctypes.Structure):
    _fields_ = [('Width', ctypes.c_uint), ('Height', ctypes.c_uint), ('MipLevels', ctypes.c_uint), ('ArraySize', ctypes.c_uint),
                ('Format', ctypes.c_uint), ('SampleDesc', SAMPLE), ('Usage', ctypes.c_uint), ('BindFlags', ctypes.c_uint),
                ('CPUAccessFlags', ctypes.c_uint), ('MiscFlags', ctypes.c_uint)]


class SCDESC1(ctypes.Structure):
    _fields_ = [('Width', ctypes.c_uint), ('Height', ctypes.c_uint), ('Format', ctypes.c_uint), ('Stereo', W.BOOL),
                ('SampleDesc', SAMPLE), ('BufferUsage', ctypes.c_uint), ('BufferCount', ctypes.c_uint), ('Scaling', ctypes.c_uint),
                ('SwapEffect', ctypes.c_uint), ('AlphaMode', ctypes.c_uint), ('Flags', ctypes.c_uint)]


class OUTDESC1(ctypes.Structure):
    _fields_ = [('DeviceName', ctypes.c_wchar * 32), ('Desktop', W.RECT), ('Attached', W.BOOL), ('Rotation', ctypes.c_uint),
                ('Monitor', ctypes.c_void_p), ('BitsPerColor', ctypes.c_uint), ('ColorSpace', ctypes.c_uint),
                ('Prim', ctypes.c_float * 8), ('MinLum', ctypes.c_float), ('MaxLum', ctypes.c_float), ('MaxFullFrameLum', ctypes.c_float)]


F16 = 10                                    # DXGI_FORMAT_R16G16B16A16_FLOAT
CS_SCRGB, CS_HDR10 = 1, 12                  # DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, ..._G2084_NONE_P2020


def _device():
    d3d = ctypes.windll.d3d11
    dev, ctx, lvl = ctypes.c_void_p(), ctypes.c_void_p(), ctypes.c_uint()
    f = d3d.D3D11CreateDevice
    f.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint,
                  ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_void_p)]
    hr = f(None, 1, None, 0x20, None, 0, 7, ctypes.byref(dev), ctypes.byref(lvl), ctypes.byref(ctx))   # (HARDWARE, BGRA_SUPPORT, SDK 7)
    if hr < 0:
        raise OSError('D3D11CreateDevice 0x%08x' % (hr & 0xffffffff))
    return dev, ctx


def _factory(dev):
    dx = qi(dev, IID_IDXGIDevice)
    ad = ctypes.c_void_p(); call(dx, 7, HR, ctypes.POINTER(ctypes.c_void_p))(dx, ctypes.byref(ad))       # GetAdapter
    fa = ctypes.c_void_p(); call(ad, 6, HR, ctypes.POINTER(GUID), ctypes.POINTER(ctypes.c_void_p))(ad, ctypes.byref(IID_IDXGIFactory2), ctypes.byref(fa))
    release(dx)
    return ad, fa


def _sdr_white(monitor_name):
    """Windows' SDR white level for that display (nits) - the brightness of ordinary pictures in its HDR mode"""
    class LUID(ctypes.Structure):
        _fields_ = [('lo', ctypes.c_uint32), ('hi', ctypes.c_int32)]

    class PATH(ctypes.Structure):          # DISPLAYCONFIG_PATH_INFO (72 bytes): only the target's adapter and id are read
        _fields_ = [('s_luid', LUID), ('s_id', ctypes.c_uint32), ('s_mode', ctypes.c_uint32), ('s_flags', ctypes.c_uint32),
                    ('t_luid', LUID), ('t_id', ctypes.c_uint32), ('t_mode', ctypes.c_uint32), ('t_tech', ctypes.c_uint32),
                    ('t_rot', ctypes.c_uint32), ('t_scale', ctypes.c_uint32), ('t_rate_n', ctypes.c_uint32), ('t_rate_d', ctypes.c_uint32),
                    ('t_order', ctypes.c_uint32), ('t_avail', W.BOOL), ('t_flags', ctypes.c_uint32), ('flags', ctypes.c_uint32)]

    class HDR(ctypes.Structure):           # DISPLAYCONFIG_DEVICE_INFO_HEADER + SDRWhiteLevel
        _fields_ = [('type', ctypes.c_uint32), ('size', ctypes.c_uint32), ('luid', LUID), ('id', ctypes.c_uint32), ('level', ctypes.c_uint32)]

    class SRC(ctypes.Structure):           # the source's GDI name, to match the monitor
        _fields_ = [('type', ctypes.c_uint32), ('size', ctypes.c_uint32), ('luid', LUID), ('id', ctypes.c_uint32), ('name', ctypes.c_wchar * 32)]

    u = ctypes.windll.user32
    np_, nm = ctypes.c_uint32(), ctypes.c_uint32()
    if u.GetDisplayConfigBufferSizes(2, ctypes.byref(np_), ctypes.byref(nm)):      # QDC_ONLY_ACTIVE_PATHS
        return None
    paths = (PATH * np_.value)(); modes = (ctypes.c_ubyte * (64 * max(1, nm.value)))()
    if u.QueryDisplayConfig(2, ctypes.byref(np_), paths, ctypes.byref(nm), modes, None):
        return None
    for p in paths[:np_.value]:
        s = SRC(1, ctypes.sizeof(SRC), p.s_luid, p.s_id)                            # DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME
        if u.DisplayConfigGetDeviceInfo(ctypes.byref(s)) or (monitor_name and s.name != monitor_name):
            continue
        h = HDR(11, ctypes.sizeof(HDR), p.t_luid, p.t_id, 0)                        # ..._GET_SDR_WHITE_LEVEL
        if not u.DisplayConfigGetDeviceInfo(ctypes.byref(h)) and h.level:
            return h.level / 1000.0 * 80.0
    return None


def probe(window_rect=None):
    """the displays: [(name, HDR on, peak nits, SDR white nits)] - the one under window_rect first"""
    out = []
    try:
        dev, ctx = _device(); ad, fa = _factory(dev)
        i = 0
        while True:
            o = ctypes.c_void_p()
            if call(ad, 7, HR, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p))(ad, i, ctypes.byref(o)) < 0:
                break
            o6 = qi(o, IID_IDXGIOutput6); d = OUTDESC1()
            if o6 and call(o6, 27, HR, ctypes.POINTER(OUTDESC1))(o6, ctypes.byref(d)) >= 0:
                out.append({'name': d.DeviceName, 'hdr': d.ColorSpace == CS_HDR10, 'peak': d.MaxLum, 'full': d.MaxFullFrameLum,
                            'sdr_white': _sdr_white(d.DeviceName) or 80.0,
                            'rect': (d.Desktop.left, d.Desktop.top, d.Desktop.right, d.Desktop.bottom)})
            release(o6); release(o); i += 1
        release(fa); release(ad); release(ctx); release(dev)
    except (OSError, AttributeError) as e:
        out.append({'error': str(e)})
    if window_rect:
        cx, cy = (window_rect[0] + window_rect[2]) // 2, (window_rect[1] + window_rect[3]) // 2
        out.sort(key=lambda m: 0 if 'rect' in m and m['rect'][0] <= cx < m['rect'][2] and m['rect'][1] <= cy < m['rect'][3] else 1)
    return out


class Out:
    """the window's picture through DirectX in scRGB: OpenGL draws into fbo (a half-float texture shared with DirectX)"""
    def __init__(self, r, hwnd, w, h):
        self.r, self.hwnd = r, hwnd                  # (r: the renderer - its GL calls and proc lookup)
        self.dev, self.ctx = _device()
        self.ad, self.fa = _factory(self.dev)
        d = SCDESC1(w, h, F16, 0, SAMPLE(1, 0), 0x20, 2, 0, 4, 0, 0)               # (render target output; 2 buffers; STRETCH; FLIP_DISCARD)
        self.sc1 = ctypes.c_void_p()
        hr = call(self.fa, 15, HR, ctypes.c_void_p, W.HWND, ctypes.POINTER(SCDESC1), ctypes.c_void_p, ctypes.c_void_p,
                  ctypes.POINTER(ctypes.c_void_p))(self.fa, self.dev, W.HWND(hwnd), ctypes.byref(d), None, None, ctypes.byref(self.sc1))
        if hr < 0:
            raise OSError('CreateSwapChainForHwnd 0x%08x' % (hr & 0xffffffff))
        self.sc = qi(self.sc1, IID_IDXGISwapChain3)
        if not self.sc or call(self.sc, 38, HR, ctypes.c_uint)(self.sc, CS_SCRGB) < 0:   # SetColorSpace1
            raise OSError('no scRGB swap chain')
        p = r.gl.wglGetProcAddress
        def wf(name, res, *args):
            a = p(name)
            if not a:
                raise OSError('no ' + name.decode() + ' (the graphics driver shares no textures with DirectX)')
            return ctypes.WINFUNCTYPE(res, *args)(a)
        self.Open = wf(b'wglDXOpenDeviceNV', ctypes.c_void_p, ctypes.c_void_p)
        self.Close = wf(b'wglDXCloseDeviceNV', W.BOOL, ctypes.c_void_p)
        self.Reg = wf(b'wglDXRegisterObjectNV', ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint)
        self.Unreg = wf(b'wglDXUnregisterObjectNV', W.BOOL, ctypes.c_void_p, ctypes.c_void_p)
        self.Lock = wf(b'wglDXLockObjectsNV', W.BOOL, ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p))
        self.Unlock = wf(b'wglDXUnlockObjectsNV', W.BOOL, ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p))
        self.hdev = self.Open(self.dev)
        if not self.hdev:
            raise OSError('wglDXOpenDeviceNV failed')
        self.tex = self.obj = None; self.gltex = ctypes.c_uint(); self.fbo = ctypes.c_uint(); self.locked = False
        r.gl.glGenTextures(1, ctypes.byref(self.gltex)); r.GenFB(1, ctypes.byref(self.fbo))
        self.w = self.h = 0
        self.size(w, h)

    def size(self, w, h):
        """the window's size: the swap chain and the shared texture made for it"""
        if (w, h) == (self.w, self.h) or w <= 0 or h <= 0:
            return
        self._unlock()
        if self.obj:
            self.Unreg(self.hdev, self.obj); self.obj = None
        release(self.tex); self.tex = None
        call(self.sc, 13, HR, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint)(self.sc, 0, w, h, F16, 0)   # ResizeBuffers
        call(self.sc, 38, HR, ctypes.c_uint)(self.sc, CS_SCRGB)
        td = TEX2D(w, h, 1, 1, F16, SAMPLE(1, 0), 0, 0x28, 0, 0)                     # (DEFAULT; RENDER_TARGET | SHADER_RESOURCE)
        self.tex = ctypes.c_void_p()
        if call(self.dev, 5, HR, ctypes.POINTER(TEX2D), ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p))(self.dev, ctypes.byref(td), None, ctypes.byref(self.tex)) < 0:
            raise OSError('CreateTexture2D')
        self.obj = self.Reg(self.hdev, self.tex, self.gltex.value, 0x0DE1, 1)      # (GL_TEXTURE_2D, WGL_ACCESS_READ_WRITE_NV)
        if not self.obj:
            raise OSError('wglDXRegisterObjectNV failed')
        self._lock()
        self.r.BindFB(0x8D40, self.fbo.value)
        self.r.FBTex(0x8D40, 0x8CE0, 0x0DE1, self.gltex.value, 0)
        self.w, self.h = w, h

    def _lock(self):
        if not self.locked and self.obj:
            o = (ctypes.c_void_p * 1)(self.obj); self.locked = bool(self.Lock(self.hdev, 1, o))

    def _unlock(self):
        if self.locked:
            o = (ctypes.c_void_p * 1)(self.obj); self.Unlock(self.hdev, 1, o); self.locked = False

    def show(self, sync):
        """the picture drawn into fbo onto the screen: OpenGL lets go of it, DirectX copies it to the back buffer"""
        self.r.gl.glFlush()
        self._unlock()
        bb = ctypes.c_void_p()
        if call(self.sc, 9, HR, ctypes.c_uint, ctypes.POINTER(GUID), ctypes.POINTER(ctypes.c_void_p))(self.sc, 0, ctypes.byref(IID_ID3D11Texture2D), ctypes.byref(bb)) >= 0:
            call(self.ctx, 47, None, ctypes.c_void_p, ctypes.c_void_p)(self.ctx, bb, self.tex)   # CopyResource
            release(bb)
        call(self.sc, 8, HR, ctypes.c_uint, ctypes.c_uint)(self.sc, 1 if sync else 0, 0)       # Present
        self._lock()

    def close(self):
        try:
            self._unlock()
            if self.obj:
                self.Unreg(self.hdev, self.obj)
            if self.hdev:
                self.Close(self.hdev)
        except OSError:
            pass
        for o in (self.tex, self.sc, self.sc1, self.fa, self.ad, self.ctx, self.dev):
            try:
                release(o)
            except OSError:
                pass


if __name__ == '__main__':
    for m in probe():
        print(m)
