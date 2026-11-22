"""Settle Test page 3: the beam pulsed on a moving line - each dash's (or gap's) length against the pulse's cycles"""
import numpy as np, st1, sys
from scipy import ndimage as nd
ROWS = [(1694, [4, 6, 7, 8, 12, 16], True, 0x40), (1940, [4, 6, 7, 8, 12, 16], False, 0x40), (2260, [4, 8, 16], True, 0x7F), (2510, [4, 8, 16], False, 0x7F)]
def run(n):
    g = st1.load(n)
    for y, NS, lit, v in ROWS:
        band = g[y - 30:y + 31, 600:2300]; yy = np.argmax(nd.gaussian_filter1d(band.sum(1), 2)); p = band[yy - 6:yy + 7].sum(0); p = nd.gaussian_filter1d(p, 0.7)
        lev = np.percentile(p, 97) if lit else np.median(p[p > 0.3 * p.max()])
        on = p > 0.5 * lev; d = np.diff(on.astype(int)); up = np.nonzero(d == 1)[0]; dn = np.nonzero(d == -1)[0]
        def cr(k, rising):   # sub-pixel half-level crossing between k and k + 1
            return k + (0.5 * lev - p[k]) / (p[k + 1] - p[k])
        ups = [cr(k, 1) for k in up]; dns = [cr(k, 0) for k in dn]
        if lit: segs = list(zip(ups, dns))                   # the lit dashes
        else: segs = list(zip(dns[:-1], ups[1:]))            # the dark gaps inside the line
        segs = [s for s in segs if s[1] - s[0] > 2][:len(NS)]
        starts = np.array([s[0] for s in segs]); t0 = np.cumsum([0] + [n_ + 16 for n_ in NS[:-1]])
        ppc = np.polyfit(t0[:len(starts)], starts, 1)[0]
        peak = [p[int(a):int(b) + 1].max() / lev if lit else p[int(a):int(b) + 1].min() / lev for a, b in segs]
        print('speed $%02X %s: %.2f px a cycle; N: %s' % (v, 'lit dashes' if lit else 'dark gaps ', ppc, ' '.join('%5d' % n_ for n_ in NS)))
        print('      length, cycles: %s   -> less N: %s' % (' '.join('%5.2f' % ((b - a) / ppc) for a, b in segs), ' '.join('%+5.2f' % ((b - a) / ppc - n_) for (a, b), n_ in zip(segs, NS))))
        print('      %s level / the line: %s' % ('peak' if lit else 'floor', ' '.join('%5.2f' % q for q in peak)))
if __name__ == '__main__': run(sys.argv[1])
