"""Dot Test page 3: the joints' positions along each line -> the beam's travel during each RAMP-off pulse (cycles)"""
import numpy as np
from scipy import ndimage as nd
exec(open('dots.py').read())
SP = 7.79; R = [5, 8, 11, 16, 24, 32, 48]
ROWS = [(771, 0x28, 70), (1048, 0x50, 35), (1345, 0x7F, 22), (1642, 0x50, 35)]     # y, speed (DAC), cycles a piece
def run(f1, f2):
    g1 = lum(np.load(f1)[..., :3].astype(np.float32)[::-1, ::-1] / 65535)
    for y, v, L in ROWS:
        band = g1[y - 12:y + 13]; e = nd.gaussian_filter1d(band.sum(0), 2.0)
        on = np.nonzero(band.max(0) > 0.3 * np.median(band.max(0)[1300:2500]))[0]; x0, x1 = on[0], on[-1]
        base = np.median(e[x0 + 20:x1 - 20])
        # the joints: local maxima of the light along the line (sub-pixel: the centroid of the bump above the line)
        pk = [x for x in range(x0 + 30, x1 - 10) if e[x] == e[x - 45:x + 46].max() and e[x] > base * 1.035]
        cen = []
        for x in pk:
            w = np.clip(e[x - 14:x + 15] - base, 0, None); cen.append(x - 14 + (w * np.arange(29)).sum() / w.sum())
        cen = np.array(cen[-7:]) if len(cen) >= 7 else np.array(cen)
        upc = v / 128.0                                  # units a cycle
        d = np.diff(cen) / SP / upc                      # cycles of travel between joints
        rs = R[-len(cen):]
        print('speed $%02X %2d cycles a piece: joints found %d (rests %s); travel between them, less the piece: %s' % (v, L, len(cen), rs, ' '.join('%5.1f' % (t - L) for t in d)))
        print('     line start %.0f, first joint (rest %d) at %.1f cycles from the start, line end %.1f cycles after the last joint' % (x0, rs[0], (cen[0] - x0) / SP / upc, (x1 - cen[-1]) / SP / upc))
run('21790_raw.npy', None)
