"""genart.py - the library's pictures for a game with none of its own (a cartridge with its title in the blue pill, a box):
drawn by libretro/genart.c - the same code as RetroArch's, the iPhone's and Android's - inside the native library
(native/vxbuild.dll on Windows, native/libvxgenart.so / .dylib on Linux / the Mac). Without it: None (the name's tile).

    make(look, name) -> (w, h, RGBA bytes) or None     look 'carts' or 'boxes'; name: the file's name without extension
    title(name) -> the title the pill shows"""
import ctypes, os, sys

_lib = None


def lib():
    global _lib
    if _lib is None:
        _lib = False
        here = getattr(sys, '_MEIPASS', os.path.dirname(os.path.abspath(__file__)))
        names = ('vxbuild.dll',) if sys.platform == 'win32' else ('libvxgenart.dylib',) if sys.platform == 'darwin' else ('libvxgenart.so',)
        for n in names:
            p = os.path.join(here, 'native', n)
            if not os.path.exists(p):
                continue
            try:
                L = ctypes.CDLL(p)
                L.vx_genart_make.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
                L.vx_genart_make.restype = ctypes.c_void_p
                L.vx_genart_free.argtypes = [ctypes.c_void_p]; L.vx_genart_free.restype = None
                L.vx_genart_title.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]; L.vx_genart_title.restype = ctypes.c_int
                _lib = L; break
            except (OSError, AttributeError):        # (an older library without it)
                pass
    return _lib or None


def make(look, name):
    L = lib()
    if not L or look not in ('carts', 'boxes'):
        return None
    w, h = ctypes.c_int(0), ctypes.c_int(0)
    p = L.vx_genart_make(1 if look == 'boxes' else 0, name.encode('utf-8'), ctypes.byref(w), ctypes.byref(h))
    if not p:
        return None
    try:
        return w.value, h.value, ctypes.string_at(p, w.value * h.value * 4)
    finally:
        L.vx_genart_free(p)


def title(name):
    L = lib()
    if not L:
        return None
    b = ctypes.create_string_buffer(1024); L.vx_genart_title(name.encode('utf-8'), b, 1024)
    return b.value.decode('utf-8', 'replace')
