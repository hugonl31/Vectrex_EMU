"""links.py - where a Finder alias (macOS) or a shortcut (.lnk, Windows) points: the ROM browser shows an alias of a
folder as a subfolder and an alias of a ROM as that ROM (a user keeps an alias of the folder they build their
games in, in roms - no copying). Symbolic links need nothing: the system follows them by itself."""
import ctypes, os, struct, sys

_cache = {}                                                     # path -> (mtime, size, target)


def target(path):
    """the file or folder the alias / shortcut `path` points to, or None (not one, or it can not be found)"""
    try:
        st = os.stat(path)
    except OSError:
        return None
    key = (st.st_mtime, st.st_size)
    hit = _cache.get(path)
    if hit and hit[0] == key:
        return hit[1]
    t = None
    try:
        if path.lower().endswith('.lnk'):
            t = _lnk(path)
        elif sys.platform == 'darwin' and st.st_size < 1 << 20:
            with open(path, 'rb') as f:
                head = f.read(4)
            if head == b'book':                                 # (a Finder alias: bookmark data)
                t = _alias(path)
    except Exception:                                           # noqa: an odd file is just not shown as a link
        t = None
    if t and not os.path.exists(t):
        t = None
    _cache[path] = (key, t)
    return t


def _lnk(path):
    """a Windows shortcut's target path (the MS-SHLLINK format: its LinkInfo's local base path)"""
    b = open(path, 'rb').read()
    if len(b) < 0x4C or struct.unpack_from('<I', b, 0)[0] != 0x4C:
        return None
    flags = struct.unpack_from('<I', b, 0x14)[0]
    p = 0x4C
    if flags & 1:                                               # HasLinkTargetIDList
        p += 2 + struct.unpack_from('<H', b, p)[0]
    if not flags & 2:                                           # HasLinkInfo
        return None
    li = p
    size, hsize, liflags, vol, base, net, suffix = struct.unpack_from('<7I', b, li)
    if not liflags & 1:                                         # (a network path: not handled)
        return None

    def cstr(o, wide=False):
        if wide:
            e = o
            while b[e:e + 2] != b'\0\0':
                e += 2
            return b[o:e].decode('utf-16-le')
        e = b.index(b'\0', o)
        return b[o:e].decode('mbcs' if sys.platform == 'win32' else 'latin-1')

    if hsize >= 0x24:                                           # (the Unicode paths)
        base_u, suffix_u = struct.unpack_from('<2I', b, li + 0x1C)
        t = cstr(li + base_u, True) + cstr(li + suffix_u, True)
    else:
        t = cstr(li + base) + cstr(li + suffix)
    return t or None


def _alias(path):
    """a Finder alias's target, resolved by macOS itself (CoreFoundation's bookmark functions)"""
    cf = ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
    vp, b8, idx = ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_long
    cf.CFURLCreateFromFileSystemRepresentation.argtypes = [vp, ctypes.c_char_p, idx, b8]
    cf.CFURLCreateFromFileSystemRepresentation.restype = vp
    cf.CFURLCreateBookmarkDataFromFile.argtypes = [vp, vp, ctypes.POINTER(vp)]
    cf.CFURLCreateBookmarkDataFromFile.restype = vp
    cf.CFURLCreateByResolvingBookmarkData.argtypes = [vp, vp, ctypes.c_ulong, vp, vp, ctypes.POINTER(b8), ctypes.POINTER(vp)]
    cf.CFURLCreateByResolvingBookmarkData.restype = vp
    cf.CFURLGetFileSystemRepresentation.argtypes = [vp, b8, ctypes.c_char_p, idx]
    cf.CFURLGetFileSystemRepresentation.restype = b8
    cf.CFRelease.argtypes = [vp]
    raw = os.fsencode(path)
    url = cf.CFURLCreateFromFileSystemRepresentation(None, raw, len(raw), 0)
    if not url:
        return None
    data = res = None
    try:
        err = vp()
        data = cf.CFURLCreateBookmarkDataFromFile(None, url, ctypes.byref(err))
        if not data:
            return None
        stale = b8(0)
        # (without any user interface, without mounting volumes: an alias on an unplugged disk is skipped)
        res = cf.CFURLCreateByResolvingBookmarkData(None, data, (1 << 8) | (1 << 9), None, None, ctypes.byref(stale),
                                                    ctypes.byref(err))
        if not res:
            return None
        buf = ctypes.create_string_buffer(4096)
        if not cf.CFURLGetFileSystemRepresentation(res, 1, buf, len(buf)):
            return None
        return os.fsdecode(buf.value)
    finally:
        for o in (res, data, url):
            if o:
                cf.CFRelease(o)
