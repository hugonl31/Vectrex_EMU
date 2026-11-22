import numpy as np
V = 0.99   # units a cycle on each axis (speed $7F)
def path(rest, L=16, pre=0, dt=0.05):
    # integrator position: leg 1 (-x, -y), a stop of `rest` cycles, leg 2 (+x, -y); 16 cycles each (as Dot Test page 4)
    t = np.arange(0, 2 * L + rest + 80, dt); x = np.zeros_like(t); y = np.zeros_like(t)
    a = t < L; b = (t >= L) & (t < L + rest); c = (t >= L + rest) & (t < 2 * L + rest); d = t >= 2 * L + rest
    x[a] = -V * t[a]; y[a] = -V * t[a]
    x[b] = -V * L; y[b] = -V * L
    x[c] = -V * L + V * (t[c] - L - rest); y[c] = -V * L - V * (t[c] - L - rest)
    x[d] = 0; y[d] = -2 * V * L
    return t, x, y
def lag(t, p, tau, n):
    dt = t[1] - t[0]; k = 1 - np.exp(-dt / (tau / n)); s = [p[0]] * n; out = np.empty_like(p)
    for i, v in enumerate(p):
        u = v
        for j in range(n): s[j] += (u - s[j]) * k; u = s[j]
        out[i] = u
    return out
def cut(rest, tau, n):
    t, x, y = path(rest); sx, sy = lag(t, x, tau, n), lag(t, y, tau, n)
    return sx.min() - (-V * 16)          # how far the tip stays inside the corner (units)
print('photo 90 deg:            0: 1.71   8: 1.82  16: 0.84  32: ~0   64: ~0')
for tau, n in ((6, 1), (8, 1), (10, 1), (8, 2), (12, 2), (16, 2), (12, 3), (16, 3), (20, 4), (16, 4)):
    print('lag tau %2d, %d stage(s):  ' % (tau, n) + '  '.join('%2d: %.2f' % (r, cut(r, tau, n)) for r in (0, 8, 16, 32, 64)))
