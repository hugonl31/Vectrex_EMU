"""the picture's response by place (lens + tube): page 3's fifteen figures are the same light each"""
import numpy as np
T = {88: ([-0x60 + i * 0x1B for i in range(8)], [438.6, 636.1, 693.9, 797.3, 876.7, 897.1, 830.6, 722.3]),
     0: ([-0x60 + i * 0x1F for i in range(7)], [541.4, 740.8, 941.7, 1063.9, 1158.4, 1134.7, 1034.6])}
U = []; Vv = []; Z = []
for v, (xs, t) in T.items():
    for x, l in zip(xs, t): U.append((x - 7) / 100.0); Vv.append((v - 16) / 100.0); Z.append(l)      # (a figure's middle: 7 units left, 16 below its start)
U, Vv, Z = map(np.array, (U, Vv, Z))
A = np.stack([np.ones_like(U), U, U ** 2, U ** 3, Vv, Vv * U], 1); c = np.linalg.lstsq(A, Z, rcond=None)[0]
print('rms %.1f %%' % (100 * ((A @ c - Z) / Z).std()))
def resp(u, v):
    u = np.asarray(u, float) / 100.0; v = (v - 0.0) / 100.0
    return (c[0] + c[1] * u + c[2] * u ** 2 + c[3] * u ** 3 + c[4] * v + c[5] * v * u) / c[0]
if __name__ == '__main__':
    for v in (80, 56, 16, -8): print(v, ' '.join('%.2f' % resp(u, v) for u in (-113, -100, -80, -40, 0, 40, 80)))
