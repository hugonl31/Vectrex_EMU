"""snapshot_test.py - runs Vectrex_EMU from the sources for a few seconds and saves what OpenGL drew
(read back before a buffer swap) as a PNG: a check that the window, the OpenGL context and the shaders
work on this system (the GitHub builds run it on macOS and Linux).

    python tools/snapshot_test.py OUT.png SECONDS [ROM]
"""
import ctypes, os, struct, sys, time, zlib
here = os.path.dirname(os.path.abspath(__file__))
out, secs = sys.argv[1], float(sys.argv[2])
sys.path.insert(0, os.path.join(here, '..', 'src', 'frontend'))
sys.argv = ['vectrex_emu.py'] + sys.argv[3:] + ['--demo', str(secs + 2)]
os.environ.setdefault('VECTREX_TEST_KNOB', '300')
import vectrex_emu, vecrender      # noqa: E402

def png(fn, w, h, rgb):
    raw = b''.join(b'\0' + rgb[(h - 1 - y) * w * 3:(h - y) * w * 3] for y in range(h))     # (bottom-up)
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(fn, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def float_fbo_test(r):
    """a 16-bit float framebuffer cleared to (0.25, 0.5, 0.75, 1) and read back"""
    gl = r.gl
    t = ctypes.c_uint(); gl.glGenTextures(1, ctypes.byref(t)); gl.glBindTexture(0x0DE1, t.value)
    gl.glTexImage2D(0x0DE1, 0, 0x881A, 4, 4, 0, 0x1908, 0x140B, None)
    gl.glTexParameteri(0x0DE1, 0x2801, 0x2601); gl.glTexParameteri(0x0DE1, 0x2800, 0x2601)
    f = ctypes.c_uint(); r.GenFB(1, ctypes.byref(f)); r.BindFB(0x8D40, f.value)
    r.FBTex(0x8D40, 0x8CE0, 0x0DE1, t.value, 0)
    st = r.FBStatus(0x8D40)
    gl.glClearColor(0.25, 0.5, 0.75, 1.0); gl.glClear(0x4000)
    px = (ctypes.c_float * 4)()
    gl.glReadPixels.argtypes = [ctypes.c_int] * 4 + [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p]
    gl.glReadPixels(1, 1, 1, 1, 0x1908, 0x1406, px)
    r.BindFB(0x8D40, 0); gl.glClearColor(0, 0, 0, 1)
    gl.glGetString.restype = ctypes.c_char_p
    ext = gl.glGetString(0x1F03) or b''
    print('float FBO status 0x%x, read back %s; extensions:' % (st, [round(v, 3) for v in px]),
          [e for e in ext.decode().split() if 'float' in e.lower() or 'half' in e.lower() or 'framebuffer' in e.lower()], flush=True)

orig = vecrender.GLRenderer._swap
t0 = time.perf_counter(); st = dict(done=False, n=0)
def swap(self):
    st['n'] += 1
    if st['n'] == 1 and hasattr(self, 'GenFB'):
        float_fbo_test(self)
    if not st['done'] and time.perf_counter() - t0 > secs:
        st['done'] = True
        w, h = self.size
        buf = (ctypes.c_ubyte * (w * h * 3))()
        self.gl.glReadPixels.argtypes = [ctypes.c_int] * 4 + [ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p]
        self.gl.glPixelStorei(0x0D05, 1)                                    # GL_PACK_ALIGNMENT
        self.gl.glReadPixels(0, 0, w, h, 0x1907, 0x1401, buf)               # RGB, unsigned byte
        png(out, w, h, bytes(buf))
        vp = (ctypes.c_int * 4)(); self.gl.glGetIntegerv(0x0BA2, vp)            # GL_VIEWPORT
        self.gl.glGetString.restype = ctypes.c_char_p
        print('OpenGL', self.gl.glGetString(0x1F01), self.gl.glGetString(0x1F02), 'viewport', list(vp), flush=True)
        if hasattr(getattr(self, 'ctxo', None), 'diag'):
            print(self.ctxo.diag(), flush=True)
        print('snapshot %dx%d after %d swaps, %d lit bytes' % (w, h, st['n'], sum(1 for b in bytes(buf) if b > 8)), flush=True)
    return orig(self)
vecrender.GLRenderer._swap = swap
vectrex_emu.main()
print('swaps', st['n'])
if not st['done']:
    sys.exit('no OpenGL frame was drawn')
