"""genart.py - the library's pictures for a game with none of its own (a cartridge with its title in the blue pill, a box):
drawn by libretro/genart.c - the same code as RetroArch's, the iPhone's and Android's - inside the native library
(native/vxbuild.dll on Windows, native/libvxgenart.so / .dylib on Linux / the Mac). Without it: None (the name's tile).

    make(look, name, width=840) -> (w, h, RGBA bytes) or None   look 'carts' or 'boxes'; name: the file's name without
                                                                extension; width: the tile's (size())
    label(path, width=840) -> (w, h, RGBA bytes) or None        the cartridge with the player's own label (a PNG) on it
    fit(path, maxw) -> (w, h, RGBA bytes) or None               a picture (a PNG) at most maxw wide
    size(px) -> the width to make a tile shown px wide at: 256, 384, 512, 640 or 840 (as genart.c's vx_genart_size)
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
                try:                                    # (the player's own label on the cartridge: newer libraries)
                    L.vx_genart_label_png.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
                    L.vx_genart_label_png.restype = ctypes.c_void_p
                    L.vx_genart_fit_png.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
                    L.vx_genart_fit_png.restype = ctypes.c_void_p
                    L.vx_genart_make_w.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
                    L.vx_genart_make_w.restype = ctypes.c_void_p
                    L.vx_genart_label_png_w.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
                    L.vx_genart_label_png_w.restype = ctypes.c_void_p
                except AttributeError:
                    pass
                _lib = L; break
            except (OSError, AttributeError):        # (an older library without it)
                pass
    return _lib or None


STEPS = (256, 384, 512, 640, 840)


def size(px):
    return next((s for s in STEPS if px <= s), STEPS[-1])


def make(look, name, width=840):
    L = lib()
    if not L or look not in ('carts', 'boxes'):
        return None
    w, h = ctypes.c_int(0), ctypes.c_int(0)
    lk = 1 if look == 'boxes' else 0
    if hasattr(L, 'vx_genart_make_w'):
        p = L.vx_genart_make_w(lk, name.encode('utf-8'), int(width), ctypes.byref(w), ctypes.byref(h))
    else:
        p = L.vx_genart_make(lk, name.encode('utf-8'), ctypes.byref(w), ctypes.byref(h))
    if not p:
        return None
    try:
        return w.value, h.value, ctypes.string_at(p, w.value * h.value * 4)
    finally:
        L.vx_genart_free(p)


def label(path, width=840):
    """the cartridge with the player's own label on it (thumbs/labels/<game>.png) -> (w, h, RGBA bytes) or None"""
    return _file('vx_genart_label_png_w', path, int(width))


def fit(path, maxw=840):
    """a picture of the player's own (a PNG) at most maxw wide, made smaller once -> (w, h, RGBA bytes) or None"""
    return _file('vx_genart_fit_png', path, maxw)


def _file(fn, path, *extra):
    L = lib()
    if not L or not hasattr(L, fn):
        return None
    try:
        data = open(path, 'rb').read()
    except OSError:
        return None
    w, h = ctypes.c_int(0), ctypes.c_int(0)
    p = getattr(L, fn)(data, len(data), *extra, ctypes.byref(w), ctypes.byref(h))
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
