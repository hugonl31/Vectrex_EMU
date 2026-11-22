import sys, os, numpy as np
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
c = V.Core(r'G:\AI\Vectrex_EMU\testcart\corner_test.bin', False, 'auto', 'off', None, None, None, False)
for i in range(960): L = c.frame(0, 0, 0, 0)[0]
L = sorted(list(L), key=lambda l: l[6])
print(len(L[0]), 'fields')
R = [l for l in L if 145 * 134 < l[0] < 158 * 134 and 175 * 134 < l[1] < 195 * 134]
for l in R[:40]: print(' '.join('%9.1f' % v if isinstance(v, float) else '%7d' % v for v in l))
