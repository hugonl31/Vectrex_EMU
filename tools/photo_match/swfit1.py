"""page 1 as a small model (RAMP delays, the spot's lag, the beam's delays, the light law, the spot) fitted to the photo's
light along the four rows (units from the line's start, light / the first piece's)"""
import numpy as np, sys
from scipy import ndimage as nd, optimize as op
from swp1b import photo
R = [5, 7, 8, 9, 10, 11, 12, 13, 14, 16, 20, 24, 32, 48]
ROWS = [(64 / 133.1, 24, False), (64 / 133.1, 24, True), (127 / 133.1, 12, False), (127 / 133.1, 12, True)]
DT = 0.05; DU = 0.05
def row(V, L, blank, p):
    """-> (u grid from the first lit place, light per unit)"""
    don, doff, tau, n, zon, zoff, sig, ba, d0 = p
    T = 10 + 15 * L + sum(R) + 40; t = np.arange(0, T, DT); v = np.zeros(len(t)); z = np.zeros(len(t), bool)
    tt = 10.0; ev = []
    for k in range(15):
        ton = tt; toff = tt + L; v[(t >= ton + don) & (t < toff + doff)] = V
        if k < 14:
            if blank and R[k] >= 20: ev.append((toff + 5, R[k] - 11))
            tt = toff + R[k]
    z[(t >= 15 + zon) & (t < toff + 5 + zoff)] = True
    for t0, w in ev: z[(t >= t0 + zoff) & (t < t0 + w + zon)] = False
    x = np.cumsum(v) * DT; n = int(n); k = 1 - np.exp(-DT / (tau / n))
    for s in range(n):
        y = np.empty_like(x); a = 0.0
        # first-order lag (exact recursion)
        from scipy.signal import lfilter
        y = lfilter([k], [1, -(1 - k)], x); x = y
    xl = x[z]; nb = int((x.max() + 12) / DU); h = np.bincount(((xl + 6) / DU).astype(int), minlength=nb)[:nb] * DT / DU      # cycles per unit
    lit = nd.gaussian_filter1d((h > 0).astype(float), sig / DU); hs = nd.gaussian_filter1d(h, sig / DU)
    l = lit + ba * d0 * np.log1p(hs / d0)
    ug = np.arange(nb) * DU - 6
    e0 = ug[np.argmax(lit > 0.5)]                                    # the start's half-level edge
    return ug - e0, l
def model(p):
    out = []
    for V, L, bl in ROWS:
        u, l = row(V, L, bl, p); out.append((u, l / np.median(l[(u > 3) & (u < 8)])))
    return out
PH = [photo('35602', i) for i in range(4)]
UG = np.arange(-2, 200, 0.1)
PHG = [np.interp(UG, u, e) for u, e in PH]
def resid(q, n, full=False):
    p = [q[0], q[0] + q[1], q[2], n, q[3], q[4], q[5], q[6], 150.0]
    r = []
    for (u, l), e in zip(model(p), PHG):
        m = np.interp(UG, u, l, left=0, right=0)
        r.append((np.minimum(m, 6) - np.minimum(e, 6)) / (1 + 0.5 * np.minimum(e, 6)))          # (the peaks count less than their height)
    return np.concatenate(r)
if __name__ == '__main__':
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    best = {}
    for n in (1, 2):
        b = None
        for tau0 in (5.0, 9.0):
            q0 = [5.0, 2.0, tau0, 0.0, 0.0, 0.45, 0.4]
            s = op.least_squares(resid, q0, args=(n,), bounds=([0, 0, 1, -3, -3, 0.2, 0.05], [12, 6, 20, 8, 8, 1.5, 3]), diff_step=0.02)
            if b is None or s.cost < b.cost: b = s
        best[n] = b
        print('n=%d: rms %.4f  RAMP on %.2f off %.2f (piece +%.2f)  lag %.2f  beam on +%.2f off +%.2f  spot %.2f  slow/fast %.3f' % (n, np.sqrt(2 * b.cost / len(b.fun)), b.x[0], b.x[0] + b.x[1], b.x[1], b.x[2], b.x[3], b.x[4], b.x[5], b.x[6]))
    cur = [5.0, 2.0, 6.0, 0.0, 0.0, 0.45, 0.29 / 0.70]
    print('the emulator\'s settings: rms %.4f' % np.sqrt((resid(cur, 1) ** 2).mean()))
    fig, ax = plt.subplots(4, 1, figsize=(24, 14))
    for i in range(4):
        ax[i].plot(UG, PHG[i], 'k', lw=0.8, label='photo')
        for n, c in ((1, 'tab:blue'), (2, 'tab:red')):
            q = best[n].x; u, l = model([q[0], q[0] + q[1], q[2], n, q[3], q[4], q[5], q[6], 150.0])[i]; ax[i].plot(u, l, c, lw=0.8, label='fit n=%d' % n)
        ax[i].set_xlim(-3, 200); ax[i].set_ylim(0, 4); ax[i].grid(alpha=0.3); ax[i].set_xticks(range(0, 200, 5)); ax[i].tick_params(labelsize=6); ax[i].legend(fontsize=7)
    plt.tight_layout(); plt.savefig('sw/p1fit.png', dpi=65)
    np.save('sw/p1fit.npy', np.array([best[1].x, best[2].x]))
