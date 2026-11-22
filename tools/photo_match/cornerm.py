"""a corner's roundness measured on a picture - the owner's photo and the emulator's rendered frame alike:
the figure's ridge, a straight line through each leg's outer part, their meeting point I; cut = the ridge's nearest
distance to I, as a share of the leg's length (0 = sharp). The render is blurred to the photo's line width first."""
import numpy as np
from scipy import ndimage as nd
def figures(img, roi, thr=0.25, minpix=150):
    """connected figures inside roi (y0, y1, x0, x1): list of (cy, cx, sub-image, origin)"""
    y0, y1, x0, x1 = roi; g = img[y0:y1, x0:x1]; s = nd.gaussian_filter(g, 1.5); m = s > thr * np.percentile(s, 99.5)
    m = nd.binary_dilation(m, iterations=3); lab, n = nd.label(m); out = []
    for i, sl in enumerate(nd.find_objects(lab)):
        if (lab[sl] == i + 1).sum() < minpix: continue
        a, b = sl; pad = 12; ya, yb, xa, xb = max(0, a.start - pad), min(g.shape[0], a.stop + pad), max(0, b.start - pad), min(g.shape[1], b.stop + pad)
        sub = g[ya:yb, xa:xb] * (nd.binary_dilation(lab[ya:yb, xa:xb] == i + 1, iterations=8))
        out.append(((ya + yb) / 2 + y0, (xa + xb) / 2 + x0, sub, (ya + y0, xa + x0)))
    return out
def ridge(sub, thr=0.15):
    s = nd.gaussian_filter(sub, 1.2); m = s > thr * s.max()
    gy, gx = np.gradient(nd.gaussian_filter(sub, 2.5))
    Jxx = nd.gaussian_filter(gx * gx, 3); Jyy = nd.gaussian_filter(gy * gy, 3); Jxy = nd.gaussian_filter(gx * gy, 3)
    ang = 0.5 * np.arctan2(2 * Jxy, Jxx - Jyy); horiz = abs(np.cos(ang)) > abs(np.sin(ang))
    hx = (s >= np.roll(s, 1, 1)) & (s >= np.roll(s, -1, 1)); hy = (s >= np.roll(s, 1, 0)) & (s >= np.roll(s, -1, 0))
    yy, xx = np.nonzero(m & np.where(horiz, hx, hy))
    # sub-pixel: the parabola's top across the line
    X = xx.astype(float); Y = yy.astype(float)
    for k in range(len(xx)):
        y, x = yy[k], xx[k]
        if 0 < x < s.shape[1] - 1 and 0 < y < s.shape[0] - 1:
            if horiz[y, x]: a, b, c = s[y, x - 1], s[y, x], s[y, x + 1]; d = a - 2 * b + c; X[k] += 0.5 * (a - c) / d if d < 0 else 0
            else: a, b, c = s[y - 1, x], s[y, x], s[y + 1, x]; d = a - 2 * b + c; Y[k] += 0.5 * (a - c) / d if d < 0 else 0
    return np.stack([X, Y], 1), s[yy, xx]
def tls(P):
    c = P.mean(0); u, s, vt = np.linalg.svd(P - c); return c, vt[0]
def measure(sub, far=0.5):
    P, w = ridge(sub)
    if len(P) < 30: return None
    # the ends: the two ridge points farthest apart (via the hull's extremes along the main axes)
    c, d = tls(P); t = (P - c) @ d; A = P[np.argmin(t)]; B = P[np.argmax(t)]
    n = np.array([-(B - A)[1], (B - A)[0]]); n /= np.linalg.norm(n); h = (P - A) @ n
    C = P[np.argmax(abs(h))]
    if abs(h).max() < 0.04 * np.linalg.norm(B - A): return dict(cut=0.0, leg=np.linalg.norm(B - A) / 2, angle=180.0, width=width(sub, P))
    out = []
    for E in (A, B):
        ax = (E - C); L = np.linalg.norm(ax); ax /= L; s_ = (P - C) @ ax; perp = abs((P - C) @ np.array([-ax[1], ax[0]]))
        m = (s_ > far * L) & (perp < 0.35 * L)
        if m.sum() < 6: return None
        out.append(tls(P[m]))
    (c1, d1), (c2, d2) = out
    M = np.array([d1, -d2]).T
    if abs(np.linalg.det(M)) < 1e-6: return None
    k = np.linalg.solve(M, c2 - c1); I = c1 + k[0] * d1
    cut = np.hypot(*(P - I).T).min(); leg = 0.5 * (np.linalg.norm(A - I) + np.linalg.norm(B - I))
    ang = np.degrees(np.arccos(np.clip(abs(d1 @ d2), 0, 1)))
    return dict(cut=cut, leg=leg, angle=ang, width=width(sub, P), I=I, A=A, B=B, la=np.linalg.norm(A - I), lb=np.linalg.norm(B - I))
def width(sub, P):
    """the line's full width at half height (px): the picture's light per ridge length / its peak"""
    s = nd.gaussian_filter(sub, 0.7); pk = np.median([s[int(round(y)), int(round(x))] for x, y in P[::3]])
    # area of the pixels above half the ridge's level / the ridge's length
    ln = max(len(P), 1) * 1.0; return (s > 0.5 * pk).sum() / ln * 1.0
