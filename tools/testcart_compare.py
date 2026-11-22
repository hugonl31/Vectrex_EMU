"""testcart_compare.py - the Test Cart's page 1 (TRAIL) measured on a snapshot of any platform, and two snapshots compared.

    python tools/testcart_compare.py pc.png [iphone.png ...]

Each picture: the four bars found (1, 2, 4 units a frame at intensity $7F; 2 at $3F), and for each the level of its head and of
its ghosts by age in frames (screen values 0..1, the green channel). With more than one picture: every further one against
the first, as ratios. The pictures may differ in size and may be cropped to the page's lit area; a snapshot at any moment
will do (the speeds are steady). Float pictures (.npy, from tools/lrhost.py) are read as they are."""
import sys
import numpy as np
from PIL import Image
from scipy.ndimage import label

SPEEDS = (1, 2, 4, 2)       # units a frame, top to bottom
PITCH = 26.0                # units between the bars' feet
RUN = 96.0                  # units a bar travels before it starts again at the left
AGES = 14

def load(f):
    a = np.load(f)[:, :, :3].astype(np.float32) if f.endswith('.npy') else np.asarray(Image.open(f).convert('RGB')).astype(np.float32) / 255.0
    return a / 255.0 if a.max() > 4 else a

def analyse(f, chan=1):
    g = load(f)[:, :, chan]; H, W = g.shape
    rows = (g > 0.6 * g.max()).sum(1); lab, n = label(rows > 0)
    bands = [(np.where(lab == k)[0][0], np.where(lab == k)[0][-1]) for k in range(1, n + 1)]
    bands = [b for b in bands if b[1] - b[0] > 0.02 * H][:3]
    if len(bands) < 3:
        raise SystemExit('%s: the three bright bars not found (is it page 1?)' % f)
    p = (bands[2][0] - bands[0][0]) / 2.0; upx = p / PITCH
    bands.append((int(bands[2][0] + p), int(bands[2][1] + p)))           # the faint bar: under the threshold, by the pitch
    lit = np.where((g[bands[0][0]:bands[3][1]] > 0.02).any(0))[0]; left = lit.min()
    out = []
    for (r0, r1), v in zip(bands, SPEEDS):
        m = (r1 - r0) // 4; prof = g[r0 + m: r1 - m].max(0)
        head = int(prof.argmax()); lv = [prof[head]]; x = float(head); sp = v * upx
        for k in range(1, AGES + 1):               # each ghost looked for one step behind the last one found
            c = x - sp
            if c < left - 0.5 * upx: c += RUN * upx
            w = max(1.0, 0.45 * sp); lo, hi = max(0, int(round(c - w))), min(W, int(round(c + w)) + 1)
            if hi <= lo: lv.append(0.0); x = c; continue
            jj = lo + int(prof[lo:hi].argmax())
            if prof[jj] > 0.012: x = float(jj)     # (a ghost found: on from there; too faint: on by the reckoning)
            else: x = c
            lv.append(prof[jj])
        out.append(dict(speed=v, lv=np.array(lv)))
    print('%s  %d x %d, %.2f px a unit' % (f, W, H, upx))
    for o, nm in zip(out, ('1 unit a frame', '2 units', '4 units', '2 units, $3F')):
        print('  %-15s head %.3f | ghosts by age: %s' % (nm, o['lv'][0], ' '.join('%.3f' % x for x in o['lv'][1:])))
    return out

if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    res = [analyse(f) for f in sys.argv[1:]]
    for f, r in zip(sys.argv[2:], res[1:]):
        print('%s against %s (1.00 = the same):' % (f, sys.argv[1]))
        for a, b in zip(res[0], r):
            print('  %d u/f: head %.2f | %s' % (a['speed'], b['lv'][0] / max(a['lv'][0], 1e-4), ' '.join('%.2f' % (b['lv'][k] / max(a['lv'][k], 1e-4)) for k in range(1, AGES + 1))))
