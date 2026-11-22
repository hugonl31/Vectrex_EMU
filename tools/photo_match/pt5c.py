"""Photo Test page 5 in the core (headless): the dash's cycles by G - against the tube's"""
import numpy as np, pt7
TUBE = [11.7, 11.7, 13.8, 13.8, 15.8, 15.8, 17.7, 42, 19.7, 20.8, 21.8, 22.7, 23.6, 24.5, 25.6]
C = pt7.core_rows(5, 15); V7 = 127 / 133.1
d = [(r[:, [0, 2]].max() - r[:, [0, 2]].min()) / V7 for r in C]
print('G      ', ' '.join('%5d' % g for g in range(10, 25))); print('tube   ', ' '.join('%5.1f' % v for v in TUBE)); print('core   ', ' '.join('%5.1f' % v for v in d))
