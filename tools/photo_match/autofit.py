"""the switch / delay fit: the core run with candidate settings, scored against the owner's photos -
the logo's shape (logofit), the corners' cut (Dot Test page 4), the travel during RAMP-off pulses (page 3)"""
import os, sys, subprocess, re, json, itertools, numpy as np
import logofit
PH_CUT = {'90': [1.71, 1.82, 0.84], '45': [0.70, 0.88, 0.40]}            # rests 0, 8, 16
PH_TRAV = {16: 5.9, 24: 2.1, 32: 0.1}
def tab(w0, w1):
    if w0 is None: return None
    t = []
    for R in range(24):
        if R < w0 - 3: d = 5
        elif R < w0: d = round(5 * (w0 - R) / 3.0)
        elif R <= w1: d = 0
        else: d = min(5, round((R - w1) * 5 / 8.0))
        t.append(int(d))
    return ','.join(map(str, t))
def run(cfg):
    env = {'VT_XYZDLY': '%d,%d,8' % (cfg['xd'], cfg['yd'])}
    if cfg.get('vlag'): env['VT_VLAG'] = str(cfg['vlag']); env['VT_VLAGN'] = str(cfg.get('vn', 2))
    t = tab(cfg.get('w0'), cfg.get('w1'))
    if t: env['VT_RAMPSW'] = '1'; env['VT_RAMPSW_TAB'] = t
    else: env['VT_RAMPSW'] = '0'
    e = dict(os.environ); e.update(env)
    logo, sc, ay = logofit.score(logofit.emu_pts(env))
    out = subprocess.run([sys.executable, 'cutemu.py'], env=e, capture_output=True, text=True).stdout
    rows = [[float(v) for v in re.findall(r'-?\d+\.\d+', l.split(':', 1)[1])] for l in out.splitlines() if 'row y' in l]
    cut = {'45': rows[1][:3] if len(rows) > 1 else [], '90': rows[2][:3] if len(rows) > 2 else []}
    e2 = dict(e); e2['VT_BEAMMODE'] = '1'
    out = subprocess.run([sys.executable, 'jemu.py'], env=e2, capture_output=True, text=True).stdout
    m = re.search(r'speed \$50 35 cycles.*?piece: (.*)', out); tr = [float(v) for v in m.group(1).split()] if m else []
    cerr = 0.0; n = 0
    for k in ('45', '90'):
        if len(cut[k]) == 3: cerr += sum(abs(a - b) for a, b in zip(cut[k], PH_CUT[k])); n += 3
        else: cerr += 3.0; n += 3
    terr = abs(tr[-3] - 5.9) + abs(tr[-2] - 2.1) + abs(tr[-1] - 0.1) if len(tr) >= 3 else 9.0
    loss = (logo - 1.20) * 4.0 + cerr / n * 3.0 + terr / 3 * 0.3
    return dict(cfg=cfg, logo=round(logo, 3), cut=cut, trav=tr[-3:], loss=round(loss, 3))
if __name__ == '__main__':
    res = []
    grid = [dict(xd=x, yd=x, w0=w[0], w1=w[1]) for x in (5, 7, 9, 11) for w in ((None, None), (8, 13), (5, 13), (5, 16))]
    grid += [dict(xd=9, yd=9, vlag=v, vn=2) for v in (4, 8)]
    for cfg in grid:
        try: r = run(cfg)
        except Exception as ex: r = dict(cfg=cfg, err=str(ex), loss=99)
        res.append(r); print(json.dumps(r), flush=True)
    res.sort(key=lambda r: r['loss']); print('BEST', json.dumps(res[:3]))
