import os, subprocess, sys, numpy as np
from PIL import Image
exec(open('cmpd.py').read().split("if __name__")[0])
P = disp_photo('19710_raw.npy')[680:2200]; RP, _ = analyse(P, 0.15)
def meas(png):
    d = np.array(Image.open(png).convert('RGB')).astype(np.float32).max(2) / 255.0
    H = d.shape[0]; sub = d[int(H * 0.26):int(H * 0.50)]   # the dot rows (F12 snapshot)
    rows = rows10(sub, 0.08)
    sp = np.mean([np.mean(np.diff([p[1] for p in r])) for r in rows]) / 23.0
    return radii(sub, rows, sp)
def run(tag, env, look):
    e = dict(os.environ, LOOK=look); e.update(env)
    subprocess.run([sys.executable, 'pcgrab.py', 'new', tag + '.png'], env=e, capture_output=True, timeout=120)
    R = meas(tag + '.png'); d = (R - RP)[:3]
    print('%-34s err .9/.5/.25/.12 %s rms %.2f | 7F@.25 %s' % (tag, ' '.join('%+.2f' % v for v in d.mean((0, 1))), np.sqrt((d[:, :, 1:] ** 2).mean()), ' '.join('%.2f' % v for v in R[0, :, 2])))
if __name__ == '__main__':
    print('photo 7F@.25', ' '.join('%.2f' % v for v in RP[0, :, 2]))
    look = sys.argv[1]
    for spec in sys.argv[2:]:
        K, W, H, HW, PP, HP = (spec.split(',') + ['0.4545', '0.4545'])[:6]
        run('pc_%s_K%s_W%s_H%s_HW%s_P%s_HP%s' % (look, K, W, H, HW, PP, HP), {'PK': K, 'PW': W, 'PH': H, 'PHW': HW, 'PP': PP, 'PHP': HP}, look)
