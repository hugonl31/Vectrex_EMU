"""alias_test.py - macOS: makes real Finder aliases (bookmark files, as Finder writes them) of a folder and of a ROM,
then checks that links.target finds where they point (the ROM browser shows them as a subfolder and a ROM)."""
import ctypes, os, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'frontend'))
import links

cf = ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
vp, idx = ctypes.c_void_p, ctypes.c_long
cf.CFURLCreateFromFileSystemRepresentation.argtypes = [vp, ctypes.c_char_p, idx, ctypes.c_ubyte]
cf.CFURLCreateFromFileSystemRepresentation.restype = vp
cf.CFURLCreateBookmarkData.argtypes = [vp, vp, ctypes.c_ulong, vp, vp, ctypes.POINTER(vp)]
cf.CFURLCreateBookmarkData.restype = vp
cf.CFURLWriteBookmarkDataToFile.argtypes = [vp, vp, ctypes.c_ulong, ctypes.POINTER(vp)]
cf.CFURLWriteBookmarkDataToFile.restype = ctypes.c_ubyte


def url(p, is_dir):
    raw = os.fsencode(p)
    return cf.CFURLCreateFromFileSystemRepresentation(None, raw, len(raw), 1 if is_dir else 0)


def make_alias(target, alias):
    err = vp()
    data = cf.CFURLCreateBookmarkData(None, url(target, os.path.isdir(target)), 1 << 10, None, None, ctypes.byref(err))
    assert data, 'no bookmark data'                     # (1 << 10: kCFURLBookmarkCreationSuitableForBookmarkFile)
    assert cf.CFURLWriteBookmarkDataToFile(data, url(alias, False), 0, ctypes.byref(err)), 'alias not written'


d = os.path.realpath(tempfile.mkdtemp())
os.makedirs(os.path.join(d, 'builds'))
open(os.path.join(d, 'builds', 'game.bin'), 'wb').write(b'\0' * 32)
make_alias(os.path.join(d, 'builds'), os.path.join(d, 'My builds'))
make_alias(os.path.join(d, 'builds', 'game.bin'), os.path.join(d, 'Game alias'))
for name, want in (('My builds', os.path.join(d, 'builds')), ('Game alias', os.path.join(d, 'builds', 'game.bin'))):
    got = links.target(os.path.join(d, name))
    print(name, '->', got)
    assert got and os.path.realpath(got) == os.path.realpath(want), 'alias not resolved'
print('Finder aliases: OK')
