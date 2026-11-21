"""Dot Test pages 3 and 4, simulated: instant integrators -> a candidate deflection response -> the spot's path,
measured as the photos were (joint spacing; corner cut by fitted legs)."""
import numpy as np
DT = 0.1
def lags(p, taus):
    """a cascade of first-order lags (cycles) on a path (n, 2)"""
    out = p.astype(float).copy()
    for tau in taus:
        if tau <= 0.01: continue
        k = 1 - np.exp(-DT / tau); s = out[0].copy()
        for i in range(len(out)):
            s += (out[i] - s) * k; out[i] = s
    return out
def second(p, w, z):
    """a second-order response (natural period w cycles, damping z)"""
    if w <= 0.01: return p
    om = 2 * np.pi / w; x = p[0].astype(float).copy(); v = np.zeros(2); out = np.empty_like(p, dtype=float)
    for i in range(len(p)):
        a = om * om * (p[i] - x) - 2 * z * om * v; v += a * DT; x += v * DT; out[i] = x
    return out
def respond(p, q):
    taus, w, z = q['taus'], q.get('w', 0), q.get('z', 1)
    return second(lags(p, taus), w, z)
RESTS3 = [0, 5, 8, 11, 16, 24, 32, 48]
def page3(v, L, q):
    """a line of 9 pieces at speed v (units a cycle), L cycles each, RAMP off for R cycles at the joints -> travel during each pulse, less... as joints.py"""
    vel = [np.full(int(L / DT), v)]
    for R in RESTS3:
        if R: vel.append(np.zeros(int(R / DT)))
        vel.append(np.full(int(L / DT), v))
    vel.append(np.zeros(int(120 / DT))); vel = np.concatenate(vel)
    x = np.cumsum(vel) * DT; p = np.stack([x, np.zeros_like(x)], 1); s = respond(p, q)
    sp = np.gradient(s[:, 0], DT); slow = (sp < 0.5 * v) & (np.arange(len(sp)) > 20 / DT)
    # clusters of slow time: the dwell points (time-weighted position)
    idx = np.nonzero(slow)[0]; cen = []
    if len(idx):
        cuts = np.nonzero(np.diff(idx) > 3 / DT)[0]; groups = np.split(idx, cuts + 1)
        cen = [s[g, 0].mean() for g in groups if len(g) * DT > 1.0]
    cen = np.array(cen[:-1])                           # (the line's end)
    return cen, np.diff(cen) / v - L
def page4(vx, vy, rest, q, L=16):
    n1 = int(L / DT); nr = int(rest / DT)
    vxs = np.concatenate([np.full(n1, -vx), np.zeros(nr), np.full(n1, vx), np.zeros(int(100 / DT))])
    vys = np.concatenate([np.full(n1, -vy), np.zeros(nr), np.full(n1, -vy), np.zeros(int(100 / DT))])
    p = np.stack([np.cumsum(vxs), np.cumsum(vys)], 1) * DT; s = respond(p, q)
    lit = s[: 2 * n1 + nr + int(30 / DT)]              # (the beam on for the shape and a little after: RAMP off, then beam off)
    ti = int(np.argmin(lit[:, 0])); vy_, vx_ = lit[ti, 1], lit[ti, 0]
    up = np.abs(lit[:ti, 1] - vy_) > 3; dn = np.abs(lit[ti:, 1] - vy_) > 3
    if up.sum() < 5 or dn.sum() < 5: return np.nan
    A = lit[:ti][up]; B = lit[ti:][dn]
    pu = np.polyfit(A[:, 1], A[:, 0], 1); pd = np.polyfit(B[:, 1], B[:, 0], 1)
    yi = (pd[1] - pu[1]) / (pu[0] - pd[0]); xi = pu[0] * yi + pu[1]
    return vx_ - xi
PH3 = {0.625: [1.5, 4.9, 7.4, 5.9, 2.1, 0.1], 0.992: [np.nan, 5.0, 5.5, 5.0, 2.0, 0.6]}   # pulses 5 8 11 16 24 32
PH4 = {(0x7F, 0x7F): [1.71, 1.82, 0.84, -0.29, -0.12], (0x35, 0x7F): [0.70, 0.88, 0.40, -0.10, -0.14], (0x22, 0x7F): [0.34, 0.56, 0.27, -0.13, -0.06]}
def show(q, name=''):
    print('==', name, q)
    for v, L in ((0.625, 35), (0.992, 22)):
        cen, d = page3(v, L, q); print('  page 3 speed %.2f: dwell points %d, travel: %s   photo %s' % (v, len(cen), ' '.join('%5.1f' % t for t in d), PH3[v]))
    for (vx, vy), ph in PH4.items():
        print('  page 4 %02X/%02X: cut %s   photo %s' % (vx, vy, ' '.join('%5.2f' % page4(vx / 128.0, vy / 128.0, r, q) for r in (0, 8, 16, 32, 64)), ph))
if __name__ == '__main__':
    show({'taus': [6]}, 'one lag 6 (no ramp delays)')
    show({'taus': [4, 4, 4]}, 'three lags of 4')
    show({'taus': [3], 'w': 24, 'z': 0.5}, 'lag 3 + 2nd order period 24, damping 0.5')
    show({'taus': [], 'w': 30, 'z': 0.4}, '2nd order period 30, damping 0.4')
