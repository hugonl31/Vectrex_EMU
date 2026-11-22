"""pages 2 and 3 as a small model of the analog path, fitted to the photo's ridges (emulator units, each figure anchored at
its start). X DAC -> pure delay xd -> slew limit S (DAC units a cycle) -> RC tv -> the integrator (RAMP start / stop delays)
-> the spot's lag (tau, n stages); the beam's own delays. Times in cycles (the VIA writes)."""
import numpy as np, sys, json
from scipy.signal import lfilter
from scipy.spatial import cKDTree
from scipy import optimize as op
DT = 0.1; KV = 1 / 133.1
def figure_events(page, name):
    """-> ydac, [(t, xdac)], [(t_on, t_off)] ramp, (t_beam_on, t_beam_off), xdac before"""
    if page == 2:
        flip = name.startswith('-V'); v = int(name.split('$')[1], 16)
        return -0x40, [(11, -v if flip else 0), (42, v)], [(10, 74)], (15, 81), -0x40
    v = 0x50; rel_on = name.startswith('on'); k = int(name[2:] if rel_on else name[3:])
    t_off = 35; t_on = 67; t_flip = (t_on if rel_on else t_off) + k
    xs = [(11, -v)]
    if t_flip == t_off or t_flip == t_on: xs.append((t_flip + 1, v))
    else: xs.append((t_flip, v))
    if t_flip != t_on: xs.append((t_on + 1, v if t_flip <= t_on else -v))
    return -v, sorted(xs), [(10, t_off), (t_on, 91)], (15, 97), -v
def simulate(page, name, p):
    don, doff, xd, S, tv, tau, n, zon, zoff = p
    ydac, xs, ramps, beam, x0 = figure_events(page, name)
    T = beam[1] + 30; t = np.arange(0, T, DT)
    tgt = np.full(len(t), float(x0)); cur = float(x0); tprev = 0.0
    ev = sorted((tt + xd, val) for tt, val in xs)
    for i, (te, val) in enumerate(ev):
        if S > 0:
            if i: cur = cur + np.clip(pv - cur, -S * (te - tprev), S * (te - tprev))     # (where the slew got to before this change)
            m = t >= te; tgt[m] = cur + np.clip(val - cur, -S * (t[m] - te), S * (t[m] - te))
        else:
            tgt[t >= te] = val
        pv = val; tprev = te
    if tv > 0.02:
        k = 1 - np.exp(-DT / tv); tgt = lfilter([k], [1, -(1 - k)], tgt - tgt[0]) + tgt[0]
    on = np.zeros(len(t), bool)
    for a, b in ramps: on |= (t >= a + don) & (t < b + doff)
    vx = np.where(on, tgt * KV, 0.0); vy = np.where(on, -ydac * KV, 0.0)          # (y down: the emulator's)
    x = np.cumsum(vx) * DT; y = np.cumsum(vy) * DT
    k = 1 - np.exp(-DT / (tau / int(n)))
    for s in range(int(n)): x = lfilter([k], [1, -(1 - k)], x); y = lfilter([k], [1, -(1 - k)], y)
    lit = (t >= beam[0] + zon) & (t < beam[1] + zoff)
    return t[lit], x[lit], y[lit]
_PH = None
def photo_ridges():
    """each figure's ridge (emulator units), anchored at its start (the top end)"""
    global _PH
    if _PH is None:
        import swfig2
        from swfig import FIGS, emu_fig
        _PH = {}
        B = {p: np.load('sw/emu_base_p%d.npy' % p) for p in (2, 3)}
        for page, name, u, v in FIGS:
            win = (-34, 34, -6, 46) if name.startswith('-V') else (-24, 34, -6, 46) if page == 2 else (-19, 5, -6, 46)
            eb, _ = emu_fig(B[page], u, v, win); box = (eb[:, [0, 2]].min() - 4, eb[:, [0, 2]].max() + 4, eb[:, [1, 3]].min() - 4, eb[:, [1, 3]].max() + 4)
            (rx, ry, rw), img, _ = swfig2.photo_fig(page, u, v, box)
            # this figure only: points connected to the main ridge (within 1.2 units chains from the brightest)
            P = np.stack([rx, ry], 1); tr = cKDTree(P); lab = -np.ones(len(P), int); nl = 0
            for i0 in range(len(P)):
                if lab[i0] >= 0: continue
                todo = [i0]; lab[i0] = nl
                while todo:
                    i = todo.pop()
                    for j in tr.query_ball_point(P[i], 0.9):
                        if lab[j] < 0: lab[j] = nl; todo.append(j)
                nl += 1
            P = P[lab == np.argmax(np.bincount(lab))]; top = P[np.argsort(P[:, 1])[:5]].mean(0)
            _PH[(page, name)] = P - top
    return _PH
def score(p, off=(0.0, 0.0, 0.0, 0.0), keys=None, detail=False):
    PH = photo_ridges(); out = {}
    for (page, name), R in PH.items():
        if keys and (page, name) not in keys: continue
        t, x, y = simulate(page, name, p)
        Q = np.stack([x - x[0], y - y[0]], 1)[::3] + (off[0], off[1]) if page == 2 else np.stack([x - x[0], y - y[0]], 1)[::3] + (off[2], off[3])
        d1 = cKDTree(Q).query(R)[0]; d2 = cKDTree(R).query(Q)[0]
        out[(page, name)] = 0.5 * (np.sqrt((d1 ** 2).mean()) + np.sqrt((d2 ** 2).mean()))
    return out if detail else np.array(list(out.values()))
if __name__ == '__main__' and len(sys.argv) > 1:
    PH = photo_ridges(); d = score([5, 7, 9, 0, 0, 6, 1, 0, 0], detail=True)
    for k in d: print(k, len(PH[k]), '%.3f' % d[k])
    sys.exit()
if __name__ == '__main__':
    base = [5, 7, 9, 0, 0, 6, 1, 0, 0]
    def f(q, n, use):
        p = [q[0], q[1], q[2], q[3] if use[0] else 0, q[4] if use[1] else 0, q[5], n, q[6], q[7]]
        return score(p, q[8:12])
    s0 = score(base, detail=True)
    print('the emulator\'s settings: page 2 %.3f  page 3 %.3f' % (np.mean([v for k, v in s0.items() if k[0] == 2]), np.mean([v for k, v in s0.items() if k[0] == 3])))
    res = {}
    for label, n, use in (('delay + lag', 1, (0, 0)), ('+ slew', 1, (1, 0)), ('+ RC', 1, (0, 1)), ('+ slew + RC', 1, (1, 1)), ('2-stage lag', 2, (0, 0)), ('2-stage + slew', 2, (1, 0))):
        best = None
        for xd0, S0, tv0 in ((9, 40, 0.5), (5, 20, 3.0)):
            q0 = [5, 7, xd0, S0, tv0, 6, 0, 0, 0, 0, 0, 0]
            lo = [1, 2, 0, 6, 0, 2, -3, -3, -1.5, -1.5, -1.5, -1.5]; hi = [10, 13, 13, 400, 10, 14, 6, 6, 1.5, 1.5, 1.5, 1.5]
            r = op.least_squares(f, q0, args=(n, use), bounds=(lo, hi), diff_step=0.03)
            if best is None or r.cost < best.cost: best = r
        q = best.x; p = [q[0], q[1], q[2], q[3] if use[0] else 0, q[4] if use[1] else 0, q[5], n, q[6], q[7]]
        d = score(p, q[8:12], detail=True); res[label] = (p, list(q[8:12]), d)
        print('%-16s page 2 %.3f  page 3 %.3f   RAMP on %.2f off %.2f  X delay %.2f  slew %s  RC %.2f  lag %.2f  beam on %+.2f off %+.2f  shift %s' % (label, np.mean([v for k, v in d.items() if k[0] == 2]), np.mean([v for k, v in d.items() if k[0] == 3]), p[0], p[1], p[2], '%.0f/cycle' % p[3] if p[3] else '-', p[4], p[5], p[7], p[8], ' '.join('%+.2f' % v for v in q[8:12])))
    json.dump({k: (v[0], v[1], {'%d %s' % kk: vv for kk, vv in v[2].items()}) for k, v in res.items()}, open('sw/model_fit.json', 'w'))
