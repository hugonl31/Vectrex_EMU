"""the core's own Join Test lines (headless; settings from the environment), as jt1 for the tube"""
import sys, os, numpy as np
import sh2
rom = r'G:\AI\Vectrex_EMU\testcart\join_test.bin'; v = 127 / 133.1
N = [6, 8, 11, 13]
T = sh2.table(rom, 1, 4, 9); print('page 1: to come at K 2 5 8 11 14 17 20 24 32')
for n_, ln in zip(N, T): full = max(ln); print('   N %2d (whole %5.2f cycles)  %s' % (n_, full / v, ' '.join('%6.2f' % ((full - x) / v) for x in ln)))
T = sh2.table(rom, 2, 4, 8); print('page 2: done at K -12 -8 -4 4 8 12 16 20')
for n_, ln in zip(N, T): full = max(ln); print('   N %2d (whole %5.2f cycles)  %s' % (n_, full / v, ' '.join('%6.2f' % ((full - x) / v) for x in ln)))
