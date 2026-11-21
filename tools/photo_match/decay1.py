"""ProRes (Apple Log) clips of Switch Test page 4 -> the linear light at every dot position, frame by frame"""
import numpy as np, subprocess, sys, json
from scipy import ndimage as nd
D = 'G:/Google Drive/#Sharing/Vectrex/Testcarts/Switch test/'
CROP = (1600, 1900, 150, 1300)                        # w, h, x, y in the upright 2160 x 3840 frame
def applelog(P):
    """Apple Log code (0..1) -> linear scene light (Apple Log Profile White Paper)"""
    R0, Rt, c, be, ga, de = -0.05641088, 0.01, 47.28711236, 0.00964052, 0.08550479, 0.69336945
    Pt = c * (Rt - R0) ** 2
    return np.where(P >= Pt, 2.0 ** ((P - de) / ga) - be, np.sqrt(np.maximum(P, 0) / c) + R0)
def frames(clip, n=None, skip=0):
    w, h, x, y = CROP
    cmd = ['ffmpeg', '-v', 'error', '-i', D + clip, '-vf', 'crop=%d:%d:%d:%d' % (w, h, x, y), '-pix_fmt', 'gray16le', '-f', 'rawvideo', '-fps_mode', 'passthrough', '-']
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, bufsize=w * h * 2 * 4); i = 0
    while True:
        b = p.stdout.read(w * h * 2)
        if len(b) < w * h * 2: break
        if i >= skip: yield np.frombuffer(b, '<u2').reshape(h, w).astype(np.float32) / 65535.0
        i += 1
        if n and i >= skip + n: break
    p.kill()
if __name__ == '__main__':
    clip = sys.argv[1]; mx = None; sm = None; k = 0
    for fr in frames(clip, 120, 100):
        lin = applelog(fr); mx = lin if mx is None else np.maximum(mx, lin); sm = lin if sm is None else sm + lin; k += 1
    np.save('dec_max_%s.npy' % clip[-8:-4], mx); np.save('dec_mean_%s.npy' % clip[-8:-4], sm / k)
    print(clip, k, 'frames; linear max %.3f, mean bg %.5f' % (mx.max(), np.median(sm / k)))
    from PIL import Image
    v = np.clip(mx / np.percentile(mx, 99.9), 0, 1) ** 0.45; Image.fromarray((v * 255).astype(np.uint8)).resize((800, 950)).save('dec_max_%s.png' % clip[-8:-4])
