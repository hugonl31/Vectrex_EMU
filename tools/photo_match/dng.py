import rawpy, numpy as np, glob, os, exifread
from PIL import Image
D = r'G:\Google Drive\#Sharing\Vectrex\Testcarts\Dot test'
for f in sorted(glob.glob(D + r'\*.dng')):
    tags = exifread.process_file(open(f, 'rb'), details=False)
    r = rawpy.imread(f)
    lin = r.postprocess(gamma=(1, 1), no_auto_bright=True, output_bps=16, use_camera_wb=True, half_size=True).astype(np.float32) / 65535
    np.save(f[-12:-4] + '.npy', lin)
    g = lin.max(2)
    print(os.path.basename(f), lin.shape, 'exp', tags.get('EXIF ExposureTime'), 'iso', tags.get('EXIF ISOSpeedRatings'), 'max %.3f p99.9 %.4f clip %d' % (g.max(), np.percentile(g, 99.9), (g > 0.98).sum()))
    Image.fromarray((np.clip(lin * 4, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)).resize((lin.shape[1] // 4, lin.shape[0] // 4)).save(f[-12:-4] + '_prev.png')
