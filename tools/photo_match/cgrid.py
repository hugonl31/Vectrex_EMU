"""the Corner Test on rendered frames (the libretro core) over settings: cut / leg and second / first leg by rest, against the photo"""
import os, sys, subprocess, numpy as np, json
from concurrent.futures import ThreadPoolExecutor
import cornerm_run as R
PH_CUT = [0.122, 0.120, 0.105, 0.073, 0.044, 0.003, 0.0, 0.0]; PH_RAT = [0.66, 0.64, 0.60, 0.57, 0.58, 0.58, 0.61, 0.59]
def tag(c): return 'c_' + '_'.join('%s%s' % (k, str(v).replace('.', 'p')) for k, v in sorted(c.items()))
PH_DOTS = np.array([10] * 8)
PH_DIP = [0.060, 0.051, 0.058, 0.075, 0.099, 0.132, 0.171, 0.067]
def gap_row(t):
    import gapm
    a = np.load('sl_gap_p1_%s.npy' % t).max(2).astype(np.float32) / 255
    try: m = gapm.measure(a, blur=2.0); return [x[1] for x in m], [x[3] for x in m]
    except Exception: return [np.nan] * 8, [0] * 8
def render(c, only='corner,gap_p1'):
    t = tag(c)
    if all(os.path.exists('sl_%s_%s.npy' % (o, t)) for o in only.split(',')): return t
    e = dict(os.environ); e['ONLY'] = only; e['VT_XYZDLY'] = '%d,%d,8' % (c.get('x', 9), c.get('y', 9)); e['VT_DEFLAGN'] = str(c.get('n', 1))
    if 'z' in c: e['VT_ZDELAY'] = str(c['z'])
    if 'sr' in c: e['VT_SRDELAY'] = str(c['sr'])
    if 'srm' in c: e['VT_SRMODEL'] = str(c['srm'])
    if 'srw' in c: e['VT_SRW'] = str(c['srw'])
    if 'sd' in c: e['VT_SRD0'] = str(c['sd'])
    if 'bd' in c: e['VT_LAGBOX'] = '%g,%g,%g' % (c['bd'], c['bw'], c.get('bt', 0))
    if 'xs' in c: e['VT_XSLEW'] = str(c['xs'])
    if 'ramp' in c: e['VT_RAMPDLY'] = c['ramp']
    e['LROPT'] = 'vectrex_emu_unit_lagx=%.1f,vectrex_emu_unit_lagy=%.1f' % (c.get('l', 6), c.get('l', 6)) + (',vectrex_emu_unit_beamoff=%.1f' % c['bo'] if 'bo' in c else '')
    subprocess.run([sys.executable, 'renall2.py', t], env=e, capture_output=True); return t
def corner_row(t):
    a = np.load('sl_corner_%s.npy' % t).max(2).astype(np.float32) / 255
    r = R.row_table(a, (770, 905, 150, 1060), 10, 'x', t)
    cut = [x['cut'] / x['leg'] if x else np.nan for x in r][:8]; rat = [R.ratio(x, False) if x and 'A' in x else np.nan for x in r][:8]
    return cut, rat
def score(cut, rat):
    c = np.array(cut) - PH_CUT; q = np.array(rat) - PH_RAT
    return float(np.sqrt(np.nanmean(c ** 2))), float(np.sqrt(np.nanmean(q[:6] ** 2)))
if __name__ == '__main__':
    cfgs = eval(sys.argv[1])
    with ThreadPoolExecutor(4) as ex: tags = list(ex.map(render, cfgs))
    out = []
    for c, t in zip(cfgs, tags):
        cut, rat = corner_row(t); sc = score(cut, rat); dip, dots = gap_row(t); dg = float(np.sqrt(np.nanmean((np.array(dip) - PH_DIP) ** 2))) if len(dip) == 8 else 9.9
        dd = float(np.mean(np.abs(np.clip(np.array(dots), 0, 10) - PH_DOTS))); out.append((sc[0] / 0.013 + sc[1] / 0.05 + dg / 0.02 + dd / 2.0, c, cut, rat, sc, dip, dg, dots))
    print('%-40s cut: %s | leg ratio: %s' % ('photo', ' '.join('%.3f' % v for v in PH_CUT), ' '.join('%.2f' % v for v in PH_RAT[:6])))
    print('%-40s dips: %s' % ('photo', ' '.join('%.3f' % v for v in PH_DIP)))
    for s, c, cut, rat, sc, dip, dg, dots in sorted(out, key=lambda o: o[0])[:int(os.environ.get('TOP', '12'))]:
        print('%-58s cut: %s | leg ratio: %s | cut rms %.4f ratio rms %.3f | dips %s rms %.3f | dots %s' % (' '.join('%s=%s' % kv for kv in sorted(c.items()))[:58], ' '.join('%.3f' % v for v in cut), ' '.join('%.2f' % v for v in rat[:6]), sc[0], sc[1], ' '.join('%.3f' % v for v in dip), dg, ' '.join('%d' % v for v in dots)))
