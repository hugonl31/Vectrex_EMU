import numpy as np, sys
from scipy import ndimage as nd
def lum(a): return 0.2126*a[...,0]+0.7152*a[...,1]+0.0722*a[...,2]
def find_grid(g, ncol=10, nrow=4):
    # dots: local maxima well above background, inside the dot area (exclude text by isolated-ness)
    sm = nd.gaussian_filter(g, 2)
    mx = nd.maximum_filter(sm, 25)
    pk = np.argwhere((sm == mx) & (sm > np.percentile(sm, 99.0)))
    return pk, sm
def profile(g, y, x, R=60):
    yy, xx = np.mgrid[-R:R+1, -R:R+1]; r = np.hypot(yy, xx)
    p = g[y-R:y+R+1, x-R:x+R+1]
    # recentre on the centroid of the top part
    w = np.clip(p - np.percentile(p, 50), 0, None) * (r < 6)
    cy = (w * yy).sum() / w.sum(); cx = (w * xx).sum() / w.sum()
    r = np.hypot(yy - cy, xx - cx)
    bg = np.median(p[r > R - 8])
    q = p - bg
    rb = np.arange(0, R - 8)
    prof = np.array([q[(r >= a) & (r < a + 1)].mean() for a in rb])
    E = np.cumsum([q[(r >= a) & (r < a + 1)].sum() for a in rb])
    return prof, E, bg
