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

orig = vecrender.GLRenderer._swap
t0 = time.perf_counter(); st = dict(done=False, n=0)
def swap(self):
    st['n'] += 1
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
