"""linux_spec.py SPEC - keeps the system's own C++ runtime, X and OpenGL libraries out of the Linux build.

A one-file PyInstaller build unpacks the libraries it bundles and puts them first on the library path. An
older bundled libstdc++ or libX11 is then found before the system's, and the system's OpenGL driver (Mesa)
can not load with it: no OpenGL, no picture (GitHub issue 3). Every Linux desktop has these libraries, so
the build uses the system's."""
import sys

SYSTEM_LIBS = ('libstdc++.so', 'libgcc_s.so', 'libX11.so', 'libX11-xcb.so', 'libxcb', 'libXext.so', 'libXau.so',
               'libXdmcp.so', 'libGL.so', 'libGLX', 'libGLdispatch', 'libEGL', 'libdrm')

p = sys.argv[1]
t = open(p).read()
assert 'pyz = PYZ(' in t, 'unexpected spec file'
filt = ('import os\n'
        'SYSTEM_LIBS = %r\n'
        'a.binaries = [b for b in a.binaries if not os.path.basename(b[0]).startswith(SYSTEM_LIBS)]\n' % (SYSTEM_LIBS,))
t = t.replace('pyz = PYZ(', filt + 'pyz = PYZ(', 1)
open(p, 'w').write(t)
print('linux_spec: system libraries left out of the bundle')
