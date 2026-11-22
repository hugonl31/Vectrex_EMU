import numpy as np, json, sys
NAMES = ['7F 64cy s8', '7F 64cy s16', '7F 64cy s24', '7F 16cy s16', '5F 64cy s16', '40 64cy s16', 'line 7F', 'line 5F']
def load(tag):
    d = json.load(open('dec_%s.json' % tag)); return [np.array([fr[r] for fr in d['data']]) for r in range(8)]
def curves(tag):
    R = load(tag); out = {}
    for ri, a in enumerate(R):
        F, P = a.shape; j = np.argmax(a, 1)
        # the two camera phases of a Vectrex frame: frames where the fresh dot has just moved (first) and the next (second)
        moved = np.r_[False, j[1:] != j[:-1]]
        par = int(np.argmax([moved[0::2].sum(), moved[1::2].sum()]))     # (the parity of the frames in which the dot has moved on)
        res = {}
        for ph, off in (('first', 0), ('second', 1)):
            idx = np.arange(par + off, F, 2); acc = []; 
            for f in idx:
                jj = j[f] if off == 0 else j[f - 1]                       # (the second frame: the same fresh position)
                if off == 0 and f + 1 < F and j[f + 1] != j[f]: continue  # (a slip: skip)
                acc.append(a[f][(jj - np.arange(P)) % P])
            acc = np.array(acc); res[ph] = (np.median(acc, 0), len(acc))
        out[ri] = res
    return out
if __name__ == '__main__':
    for tag in sys.argv[1:]:
        C = curves(tag); print('==', tag)
        for ri in range(8):
            for ph in ('first', 'second'):
                v, n = C[ri][ph]; print('  %-12s %-6s (%3d frames) light by age: %s' % (NAMES[ri], ph, n, ' '.join('%.3g' % x for x in v[:12])))
