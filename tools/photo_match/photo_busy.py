"""the Photo Test's frame length per page (headless core): from the first line's time to the next frame's first line"""
import sys, os, numpy as np
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
c = V.Core(r'G:\AI\Vectrex_EMU\testcart\photo_test.bin', False, 'auto', 'off', None, None, None, False)
def fr(b=0): return list(c.frame(0, 0, b, 0)[0])
for i in range(900): fr()
for p in range(16):
    for i in range(40): L = fr()
    t = [l[6] for l in L]; d = [l[6] + l[5] / 8.0 for l in L]
    print('page %2d: drawing spans %5.0f cycles (%d lines)' % (p + 1, max(d) - min(t), len(L)))
    for i in range(3): fr(8)
