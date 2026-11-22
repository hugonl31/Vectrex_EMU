"""regression of a candidate timing: the Vectorblade logo's shape score (logofit) and the Dot Test's corner cuts (cutemu)"""
import os, sys, subprocess, re, json
import logofit
def run(name, env):
    logo, sc, ay = logofit.score(logofit.emu_pts(env))
    e = dict(os.environ); e.update(env)
    out = subprocess.run([sys.executable, 'cutemu.py'], env=e, capture_output=True, text=True).stdout
    rows = [[float(v) for v in re.findall(r'-?\d+\.\d+', l.split(':', 1)[1])] for l in out.splitlines() if 'row y' in l]
    print('%-34s logo %.3f (scale %.3f, y/x %.3f)  corners: %s' % (name, logo, sc, ay, ' | '.join(' '.join('%.2f' % v for v in r[:5]) for r in rows)), flush=True)
if __name__ == '__main__':
    C = [('base', {}),
         ('X 5 slew 16 lag 6', {'VT_XYZDLY': '5,9,8', 'VT_XSLEW': '16', 'VT_DEFLAG': '6'}),
         ('X 5 slew 16 lag 7', {'VT_XYZDLY': '5,9,8', 'VT_XSLEW': '16', 'VT_DEFLAG': '7'}),
         ('X 5 slew 16 lag 8', {'VT_XYZDLY': '5,9,8', 'VT_XSLEW': '16', 'VT_DEFLAG': '8'}),
         ('X 5 Y 7 slew 16 lag 8', {'VT_XYZDLY': '5,7,8', 'VT_XSLEW': '16', 'VT_DEFLAG': '8'}),
         ('X 5 Y 11 slew 16 lag 8', {'VT_XYZDLY': '5,11,8', 'VT_XSLEW': '16', 'VT_DEFLAG': '8'}),
         ('lag 8 only', {'VT_DEFLAG': '8'})]
    for n, e in C: run(n, e)
