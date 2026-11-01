"""get_ffmpeg.py SYSTEM OUT - the ffmpeg that comes with the program (Record: videos and snapshots), for the builds.

    python tools/get_ffmpeg.py win   build/Vectrex_EMU/system/ffmpeg.exe
    python tools/get_ffmpeg.py linux dist-linux/system/ffmpeg
    python tools/get_ffmpeg.py mac   build/ffmpeg/ffmpeg          (arm64 + x86_64 in one file, with lipo)

The static ffmpeg 7 builds (GPL, as the emulator: libx264 for the MP4s, PNG for the snapshots) from the imageio-ffmpeg
wheel on PyPI, the version pinned below: taken out of the wheel, nothing installed."""
import os, subprocess, sys, tempfile, zipfile

VER = '0.7.0'
PLATS = {'win': ['win_amd64'], 'linux': ['manylinux2014_x86_64'],
         'mac': ['macosx_11_0_arm64', 'macosx_10_9_x86_64']}


def fetch(plat, tmp):
    d = os.path.join(tmp, plat)
    subprocess.check_call([sys.executable, '-m', 'pip', 'download', '-q', '--only-binary=:all:', '--no-deps',
                           '--platform', plat, 'imageio-ffmpeg==' + VER, '-d', d])
    whl = [f for f in os.listdir(d) if f.endswith('.whl')][0]
    with zipfile.ZipFile(os.path.join(d, whl)) as z:
        name = [n for n in z.namelist() if '/binaries/ffmpeg' in n][0]
        out = os.path.join(d, 'ffmpeg-' + plat)
        with open(out, 'wb') as f:
            f.write(z.read(name))
    return out


def main():
    system, out = sys.argv[1], sys.argv[2]
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        bins = [fetch(p, tmp) for p in PLATS[system]]
        if system == 'mac':
            subprocess.check_call(['lipo', '-create'] + bins + ['-output', out])
        else:
            with open(bins[0], 'rb') as f, open(out, 'wb') as g:
                g.write(f.read())
    if system != 'win':
        os.chmod(out, 0o755)
    print('ffmpeg:', out, os.path.getsize(out), 'bytes')


if __name__ == '__main__':
    main()
