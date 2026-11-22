import numpy as np, st1
OFF_K = [-12, -8, -5, 5, 8, 12, 16, 20, 24, 32, 48]; ON_K = [-12, -8, -4, 5, 8, 12, 16, 20, 24, 32]
VU = 1 / 133.1
def page(n, K, cols, rois, on):
    g = st1.load(n); out = {}
    for (v, L), roi in zip(cols, rois):
        E, lev = st1.column(g, roi, len(K)); E = [(e[0] + roi[2], e[1] + roi[2]) for e in E]
        full = max(e[1] - e[0] for e in E); ppc = full / (L + 2.0)          # px a cycle of travel: the whole line = L + 2 cycles
        print('speed $%02X L %d: the whole line %.1f px = %.2f px a cycle (%.2f px a unit)' % (v, L, full, ppc, ppc / (v * VU)))
        rows = []
        for k, e in zip(K, E):
            ln = e[1] - e[0]
            val = (full - ln) / ppc                                         # OFF: travel still to come at the switch-off; ON: travel done at the switch-on
            rows.append((k, ln, val)); print('   K %+3d  start %7.1f  end %7.1f  length %6.1f px  -> %s %5.2f cycles' % (k, e[0], e[1], ln, 'travelled before the beam came on' if on else 'still to travel when the beam went off', val))
        out[v] = rows
    return out
if __name__ == '__main__':
    print('page 1 OFF'); p1 = page('06499', OFF_K, ((0x50, 40), (0x7F, 24)), ((1280, 2580, 820, 1250), (1280, 2580, 1500, 1950)), False)
