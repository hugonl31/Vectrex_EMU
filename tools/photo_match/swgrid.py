"""the core over a grid of settings on the Switch Test: pages 2 and 3 (the path against the photo's ridge), page 1 (the
joints' places and the lit lengths). usage: swgrid.py name  (settings lists below)"""
import os, sys, subprocess, json, itertools, numpy as np
from concurrent.futures import ThreadPoolExecutor
S = os.path.dirname(os.path.abspath(__file__))
def tag(c): return 'g_' + '_'.join('%s%s' % (k, str(v).replace('.', 'p').replace(',', 'c')) for k, v in sorted(c.items()))
def env(c):
    e = dict(os.environ)
    if 'lag' in c: e['VT_DEFLAG'] = str(c['lag'])
    if 'n' in c: e['VT_DEFLAGN'] = str(c['n'])
    if 'xd' in c: e['VT_XYZDLY'] = '%d,%d,%d' % (c['xd'], c.get('yd', c['xd']), c.get('zd', 8))
    if 'on' in c: e['VT_RAMPDLY'] = '%d,%d' % (c['on'], c['off'])
    if 'xs' in c: e['VT_XSLEW'] = str(c['xs'])
    if 'vlag' in c: e['VT_VLAG'] = str(c['vlag']); e['VT_VLAGN'] = str(c.get('vn', 1))
    return e
def run(c):
    t = tag(c)
    if not os.path.exists(os.path.join(S, 'sw', 'emu_%s_p3.npy' % t)):
        subprocess.run([sys.executable, os.path.join(S, 'swemu.py'), t], env=env(c), capture_output=True)
    return t
def evaluate(cfgs):
    with ThreadPoolExecutor(8) as ex: tags = list(ex.map(run, cfgs))
    import swfig2, swp1
    from scipy import ndimage as nd
    res = []
    for c, t in zip(cfgs, tags):
        try: s = swfig2.score(t)
        except Exception as e: print('fail', t, e); continue
        p2 = np.mean([s[k][0] for k in s if k[0] == 2]); p3 = np.mean([s[k][0] for k in s if k[0] == 3])
        res.append(dict(cfg=c, p2=round(float(p2), 4), p3=round(float(p3), 4), figs={'%d %s' % k: round(float(s[k][0]), 3) for k in s}))
    return res
if __name__ == '__main__':
    name = sys.argv[1]
    if name == 'lag':
        cfgs = [dict(lag=l, n=n) for n in (1, 2) for l in (5, 6, 7, 8, 9, 10, 12)]
    elif name == 'xd':
        cfgs = [dict(lag=l, n=n, xd=x, yd=9) for n in (1, 2) for l in (6, 8) for x in (4, 5, 6, 7, 8, 9, 10)]
    elif name == 'vlag':
        cfgs = [dict(lag=6, n=1, xd=x, yd=9, vlag=v, vn=vn) for vn in (1, 2) for v in (2, 3, 4, 5, 6) for x in (9 - v - 1, 9 - v, 9 - v + 1, 9 - v + 2) if x >= 0]
    elif name == 'vlag2':
        cfgs = [dict(lag=l, n=1, xd=x, yd=9, vlag=v, vn=1) for l in (4, 5, 7) for v in (3, 4, 5, 6, 7) for x in (9 - v, 9 - v + 1, 9 - v + 2) if x >= 0]
    elif name == 'slew2':
        cfgs = [dict(lag=6, n=1, xd=x, yd=9, xs=xs) for x in (5, 6, 7) for xs in (16, 24, 32, 48)]
    elif name == 'slew3':
        cfgs = [dict(lag=l, n=n, xd=x, yd=9, xs=xs) for l, n in ((7, 1), (8, 1), (8, 2), (10, 2)) for x in (4, 5) for xs in (14, 16, 20)]
    elif name == 'slew':
        cfgs = [dict(lag=l, n=1, xd=x, yd=9, xs=xs) for l in (6, 7) for x, xs in ((2, 16), (3, 18), (4, 22), (1, 14), (2, 20), (3, 14))]
    res = evaluate(cfgs)
    json.dump(res, open(os.path.join(S, 'sw', 'grid_%s.json' % name), 'w'))
    keys = list(res[0]['figs'])
    print('%-14s' % '', ' '.join('%7s' % '/'.join(str(v) for k, v in sorted(r['cfg'].items())) for r in res))
    for k in keys: print('%-14s' % k, ' '.join('%7.2f' % r['figs'][k] for r in res))
    for r in sorted(res, key=lambda r: r['p2'] + r['p3'])[:12]: print(r['cfg'], 'page 2 %.3f  page 3 %.3f' % (r['p2'], r['p3']))
    print('%-14s' % 'page 2', ' '.join('%7.3f' % r['p2'] for r in res)); print('%-14s' % 'page 3', ' '.join('%7.3f' % r['p3'] for r in res))
