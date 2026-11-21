import sys, os, collections
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
c = V.Core(os.path.abspath(r'build\Vectrex_EMU\roms\Vectorblade.bin'), False, 'auto', 'off', None, None, None, False)
for i in range(int(sys.argv[1]) if len(sys.argv) > 1 else 760): L = c.frame(0, 0, 0, 0)[0]
L = list(L); lit = [l for l in L if int(l[4]) & 127 and not int(l[4]) & 128]
rest = [l for l in lit if l[0] == l[2] and l[1] == l[3]]
mov = [l for l in lit if not (l[0] == l[2] and l[1] == l[3])]
h = collections.Counter(min(64, int(round(l[5] / 8.0 / 2) * 2)) for l in rest)
print('pieces %d, rests %d, moving cycles %.0f, rest cycles %.0f, intensities %s' % (len(lit), len(rest), sum(l[5] for l in mov) / 8, sum(l[5] for l in rest) / 8, sorted(collections.Counter(int(l[4]) & 127 for l in lit).items())))
print('rest lengths (cycles: count):', ' '.join('%d:%d' % (k, h[k]) for k in sorted(h)))
xs = [l[0] for l in lit]; ys = [l[1] for l in lit]; print('extent x %.0f..%.0f y %.0f..%.0f' % (min(xs), max(xs), min(ys), max(ys)))
import numpy as np
sp = np.array([(((l[2]-l[0])**2 + (l[3]-l[1])**2) ** 0.5 / 134.0) / max(l[5] / 8.0, 1e-6) for l in lit]); t = np.array([l[5] / 8.0 for l in lit])
med = np.average(sp, weights=t * (sp > 0.05))
print('typical speed %.2f units/cycle; beam time at <10%% of it: %.0f cycles, 10-30%%: %.0f, 30-70%%: %.0f, above: %.0f' % (med, t[sp < 0.1 * med].sum(), t[(sp >= 0.1 * med) & (sp < 0.3 * med)].sum(), t[(sp >= 0.3 * med) & (sp < 0.7 * med)].sum(), t[sp >= 0.7 * med].sum()))
