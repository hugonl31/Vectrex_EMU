"""recorder.py - Record > Start recording (F9) and Snapshot (F12): the emulator's own picture and sound to an MP4,
a big picture to a PNG, both named by the time (2026-10-31_21-45-12) in one folder.

While recording, each picture is read back from OpenGL (vecrender: GLRenderer._swap, before the red dot is drawn -
the dot is never in the video) and piped to ffmpeg as a fast, nearly lossless working file; the sound is taken where
the emulator hands it to the sound card (AudioTap). Stop: the working file and the sound become the final MP4 in the
background (x264, slow preset), then the working files go. ffmpeg comes with the program (system/ffmpeg[.exe]; macOS:
inside the app) - GPL, as the emulator."""
import os, sys, time, threading, subprocess, shutil, datetime, queue

IS_WIN = sys.platform == 'win32'
IS_MAC = sys.platform == 'darwin'
RATE = 44100                    # (the emulator's sound: mono, 16 bit)
FPS = 50                        # (the Vectrex's own frame rate)

QUALITY = {'veryhigh': ('Very high', 14), 'high': ('High', 18), 'small': ('Smaller files', 23)}   # (x264 crf)
VSIZE = {'full': 'The window\'s size', 'half': 'Half'}
SNAP = {'window': 'Window', '2x': '2x', 'max': '4x (about 6000 px)'}
DEFAULTS = {'rec_dir': '', 'rec_quality': 'veryhigh', 'rec_size': 'full', 'rec_snap': 'max', 'rec_sound': True,
            'rec_deck': False}
SNAP_MAX_H = 6000               # (4x: the snapshot's height at most - and never past the graphics card's limit)
SNAP_MAX_PX = 40e6              # (and never more pixels than this: the phosphor's images at that size need the memory)

_NOWIN = 0x08000000 if IS_WIN else 0         # (CREATE_NO_WINDOW: no console flashing up)
_LOW = 0x00004000 if IS_WIN else 0           # (BELOW_NORMAL_PRIORITY_CLASS: the export never slows the game)


def default_dir():
    """Videos/Vectrex_EMU (Windows, macOS: Movies) in the home folder"""
    home = os.path.expanduser('~')
    for sub in (('Movies',) if IS_MAC else ('Videos',)):
        if os.path.isdir(os.path.join(home, sub)):
            return os.path.join(home, sub, 'Vectrex_EMU')
    return os.path.join(home, 'Vectrex_EMU')


def stamp():
    return datetime.datetime.now().strftime('%Y-%m-%d_%H-%M-%S')


def free_name(folder, ext):
    """<time>.<ext> in folder; a second one in the same second gets _2, _3, ..."""
    base = stamp()
    p = os.path.join(folder, base + ext); n = 2
    while os.path.exists(p):
        p = os.path.join(folder, '%s_%d%s' % (base, n, ext)); n += 1
    return p


_FF = None
def ffmpeg_path(root, res):
    """the bundled ffmpeg (system/ffmpeg next to the program; macOS: inside the app), else imageio-ffmpeg's or the one
    on the PATH (running from the sources); None when there is none"""
    global _FF
    if _FF and os.path.exists(_FF):
        return _FF
    exe = 'ffmpeg.exe' if IS_WIN else 'ffmpeg'
    cands = [os.environ.get('VECTREX_FFMPEG'), os.path.join(root, 'system', exe), os.path.join(res, 'ffmpeg', exe)]
    try:
        import imageio_ffmpeg
        cands.append(imageio_ffmpeg.get_ffmpeg_exe())
    except Exception:   # noqa
        pass
    cands.append(shutil.which('ffmpeg'))
    for c in cands:
        if c and os.path.isfile(c):
            if not IS_WIN and not os.access(c, os.X_OK):
                try:
                    os.chmod(c, 0o755)                  # (an update's unzip drops the execute bit)
                except OSError:
                    continue
            _FF = c
            return c
    return None


def _popen(args, **kw):
    kw.setdefault('stdin', subprocess.DEVNULL)
    kw.setdefault('stdout', subprocess.DEVNULL)
    kw.setdefault('stderr', subprocess.DEVNULL)
    low = kw.pop('low', False)
    if IS_WIN:
        kw['creationflags'] = _NOWIN | (_LOW if low else 0)
    elif low:
        kw['preexec_fn'] = lambda: os.nice(10)
    return subprocess.Popen(args, **kw)


class AudioTap:
    """the sound card, with a tap: everything played is kept from Start recording on (and the last 2 s before it, for
    what was still queued), and when each piece was heard - the sound lines up with the pictures by the clock"""
    def __init__(self, audio):
        self.a = audio
        self.lock = threading.Lock()
        self.total = 0                  # (samples handed to the card so far)
        self.ring = bytearray()         # (the last 2 s of them)
        self.rec = None                 # (recording: what came since, not yet written to the file)
        self.log = None                 # (recording: (time, the sample being heard) now and then)
    def __getattr__(self, k):
        return getattr(self.a, k)
    def play(self, data):
        self.a.play(data)
        if not data:
            return
        data = bytes(data)
        with self.lock:
            self.total += len(data) // 2
            self.ring += data
            if len(self.ring) > 4 * RATE:
                del self.ring[:len(self.ring) - 4 * RATE]
            if self.rec is not None:
                self.rec += data
                if len(self.log) < 20000 and getattr(self.a, 'ok', True):
                    try:
                        q = self.a.queued_samples()
                    except Exception:   # noqa
                        q = 0
                    self.log.append((time.perf_counter(), self.total - q))
    def start(self):
        """-> (the sample number the kept part starts at, the kept 2 s before now)"""
        with self.lock:
            pre = bytes(self.ring)
            self.rec = bytearray(); self.log = []
            return self.total - len(pre) // 2, pre
    def take(self):
        """what came since the last take (recording)"""
        with self.lock:
            if self.rec is None:
                return b''
            d = bytes(self.rec); self.rec = bytearray()
            return d
    def stop(self):
        with self.lock:
            d = bytes(self.rec or b''); log = self.log or []
            self.rec = None; self.log = None
            return d, log


class Recording:
    """one recording: frames in (from the renderer), the working file and the sound out; stop() makes the MP4"""
    def __init__(self, ff, folder, w, h, opts, tap, done):
        self.ff, self.folder, self.w, self.h, self.opts, self.tap, self.done = ff, folder, w, h, opts, tap, done
        os.makedirs(folder, exist_ok=True)
        self.out = free_name(folder, '.mp4')
        base = os.path.splitext(self.out)[0]
        self.part, self.pcm = base + '.part.mkv', base + '.part.pcm'
        self.q = queue.Queue(maxsize=8)
        self.t0 = None; self.last_i = self.claimed = -1; self.written = 0; self.dropped = 0; self.size_changed = False
        self.stopping = False; self.failed = None
        self.p = _popen([ff, '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'bgra', '-s', '%dx%d' % (w, h),
                         '-r', str(FPS), '-i', '-', '-vf', 'vflip', '-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '8',
                         '-pix_fmt', 'yuv444p', self.part], stdin=subprocess.PIPE, stderr=subprocess.PIPE)
        self.a0 = None
        if tap is not None and opts.get('rec_sound', True):
            self.a_first, self.a_pre = tap.start()
            self.af = open(self.pcm, 'wb')
        else:
            self.af = None
        self.th = threading.Thread(target=self._writer, daemon=True); self.th.start()

    # (the renderer's side)
    def claim(self, now):
        """a picture now? one for each 1/50 s (the first one starts the clock)"""
        if self.stopping:
            return False
        if self.t0 is None:
            self.t0 = now
        i = int((now - self.t0) * FPS)
        if i <= self.claimed:
            return False
        self.claimed = i
        return True
    def put(self, t, data, box):
        if self.stopping or self.t0 is None:
            return
        if (box[2], box[3]) != (self.w, self.h):        # (the window changed size: the app stops the recording)
            self.size_changed = True
            return
        i = int((t - self.t0) * FPS)
        if i <= self.last_i:
            return
        self.last_i = i
        try:
            self.q.put_nowait((i, data))
        except queue.Full:                              # (ffmpeg behind: this picture skipped, the last one repeated)
            self.dropped += 1

    def seconds(self):
        return 0.0 if self.t0 is None else max(0.0, time.perf_counter() - self.t0)

    def _writer(self):
        prev = None
        try:
            while True:
                it = self.q.get()
                if self.af is not None:
                    self.af.write(self.tap.take())
                if it is None:
                    break
                i, data = it
                while self.written < i and prev is not None:   # (a gap: the last picture held)
                    self.p.stdin.write(prev); self.written += 1
                self.p.stdin.write(data); self.written += 1
                prev = data
        except Exception as e:   # noqa (ffmpeg gone: the disk full, ...)
            self.failed = str(e) or 'ffmpeg stopped'

    def stop(self):
        """stop taking pictures; the MP4 is made in the background, done(path or None, message) at the end"""
        self.stopping = True
        threading.Thread(target=self._finish, daemon=True).start()

    def _finish(self):
        self.q.put(None); self.th.join()
        try:
            self.p.stdin.close()
        except Exception:   # noqa
            pass
        err = self.p.stderr.read().decode(errors='replace')[-400:] if self.p.stderr else ''
        self.p.wait()
        audio = None
        if self.af is not None:
            rest, log = self.tap.stop()
            self.af.write(rest); self.af.close()
            audio = self._cut_audio(log)
        if self.written == 0 or self.p.returncode != 0:
            self._clean()
            why = self.failed or (err.strip().splitlines() or [''])[-1]
            print('recording failed:', why, err)
            self.done(None, 'Recording failed' + (': ' + why if why else ''))
            return
        q = QUALITY.get(self.opts.get('rec_quality'), QUALITY['veryhigh'])[1]
        args = [self.ff, '-y', '-loglevel', 'error', '-progress', 'pipe:1', '-nostats', '-i', self.part]
        if audio:
            args += ['-f', 's16le', '-ar', str(RATE), '-ac', '1', '-i', audio]
        vf = ['scale=trunc(iw/4)*2:trunc(ih/4)*2:flags=lanczos'] if self.opts.get('rec_size') == 'half' else []
        args += ['-map', '0:v'] + (['-map', '1:a', '-af', 'apad', '-c:a', 'aac', '-b:a', '192k', '-ar', '48000'] if audio else [])
        args += (['-vf', ','.join(vf)] if vf else []) + ['-c:v', 'libx264', '-preset', 'slow', '-crf', str(q),
                 '-pix_fmt', 'yuv420p', '-r', str(FPS), '-movflags', '+faststart', '-shortest', self.out]
        total = self.written / float(FPS)
        try:
            p = _popen(args, stdout=subprocess.PIPE, low=True)
            for line in p.stdout:
                line = line.decode(errors='replace').strip()
                if line.startswith('out_time_us=') and total > 0:
                    try:
                        self.done(False, 'Saving video %d %%' % min(99, int(int(line.split('=')[1]) / 1e6 / total * 100)))
                    except ValueError:
                        pass
            p.wait()
            ok = p.returncode == 0 and os.path.exists(self.out)
        except Exception:   # noqa
            ok = False
        if ok:
            self._clean()
            self.done(self.out, 'Video saved: %s' % os.path.basename(self.out))
        else:
            if os.path.exists(self.out):
                try:
                    os.remove(self.out)
                except OSError:
                    pass
            self.done(None, 'Saving the video failed (the working file is kept: %s)' % os.path.basename(self.part))

    def _cut_audio(self, log):
        """the sound from the moment of the first picture on: the sample heard then, from the log (each entry: the
        time and the sample heard), the kept 2 s before the start in front"""
        try:
            pre, first = self.a_pre, self.a_first
            if self.t0 is None:
                return None
            if log:
                est = sorted(h - (t - self.t0) * RATE for t, h in log[:200])
                h0 = est[len(est) // 2]                 # (the middle one: a late piece does not move it)
            else:
                h0 = first + len(pre) // 2              # (no sound card: as it came)
            skip = int(round(h0)) - first               # (samples to skip from the start of the kept 2 s)
            out = self.pcm + '.cut'
            with open(out, 'wb') as fo:
                if skip < 0:
                    fo.write(bytes(-2 * skip)); skip = 0
                if skip < len(pre) // 2:
                    fo.write(pre[2 * skip:]); skip = 0
                else:
                    skip -= len(pre) // 2
                with open(self.pcm, 'rb') as fi:
                    fi.seek(2 * skip)
                    shutil.copyfileobj(fi, fo)
            os.replace(out, self.pcm)
            return self.pcm
        except Exception:   # noqa
            return None

    def _clean(self):
        for f in (self.part, self.pcm, self.pcm + '.cut'):
            try:
                os.remove(f)
            except OSError:
                pass


def save_png(ff, path, w, h, data, turn=None):
    """bgra rows (bottom row first, as OpenGL reads them) -> a PNG, turned `turn` degrees clockwise (a turned screen:
    the picture read before it was turned); True when it worked"""
    vf = 'vflip' + {90: ',transpose=clock', 180: ',hflip,vflip', 270: ',transpose=cclock'}.get(turn or 0, '')
    try:
        p = _popen([ff, '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'bgra', '-s', '%dx%d' % (w, h), '-i', '-',
                    '-vf', vf, '-frames:v', '1', '-pix_fmt', 'rgb24', '-compression_level', '6', path],
                   stdin=subprocess.PIPE)
        p.stdin.write(data); p.stdin.close(); p.wait()
        return p.returncode == 0 and os.path.exists(path)
    except Exception:   # noqa
        return False


def open_folder(path):
    os.makedirs(path, exist_ok=True)
    if IS_WIN:
        os.startfile(path)                              # noqa
    else:
        _popen(['open' if IS_MAC else 'xdg-open', path])
