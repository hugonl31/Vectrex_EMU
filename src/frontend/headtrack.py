"""Head tracking (Overlay > Head tracking): the player's head position from a phone app such as SmoothTrack (iOS,
Android) or anything that speaks OpenTrack's "UDP over network": each UDP packet 6 little-endian doubles - the
position (cm) and yaw, pitch, roll (degrees). Only the position is used, in SmoothTrack's names: x across (about
+-15 near the edge of the phone's view), then z up and down, then y towards the screen (unused) - the order in the
packet, found in the owner's test (2026-10-29).

The picture lies behind the overlay sheet and the glass: a head that moves sees it shift against the sheet, the way
a real one does - a near layer slides against a far one (the real depth gives some 4 mm at the full reach; the
effect is set by its largest shift, Config). The renderer asks offset() every frame for that shift in mm on the tube."""
import math, socket, struct, threading, time

PORT = 4242                 # (OpenTrack's own default; SmoothTrack's too)
REACH_CM = 15.0             # the head's full reach sideways in the app (SmoothTrack: about +-15 near the edge of its view)
MAX_MM = 5.0                # the default largest shift: at the full reach (the owner, 2026-10-29: 5 mm - Config)
STALE_S = 0.6               # no packet for this long: the shift eases back to the middle
EASE_S = 0.0                # (no smoothing of its own: SmoothTrack smooths in the app - the owner, 2026-10-29; > 0: a time constant)


class HeadTrack:
    def __init__(self, port=PORT):
        self.port = port
        self.pos = None                 # (x, y, z) cm, the newest
        self.t = 0.0                    # when it came
        self.n = 0                      # packets so far
        self.src = None                 # who sent them (address)
        self.err = None
        self.cur = (0.0, 0.0)           # the shift now (smoothed), mm
        self.t_cur = time.perf_counter()
        self.centre = None              # (x, z) taken as the middle: the first position, or Centre
        self.stop_ev = threading.Event()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.sock.bind(('0.0.0.0', port))
        except OSError as e:
            self.err = str(e); self.sock.close(); self.sock = None
            return
        self.sock.settimeout(0.5)
        threading.Thread(target=self._run, daemon=True, name='headtrack').start()

    def _run(self):
        while not self.stop_ev.is_set():
            try:
                data, addr = self.sock.recvfrom(256)
            except socket.timeout:
                continue
            except OSError:
                break
            p = parse(data)
            if p is None:
                continue
            self.pos, self.t, self.src = p, time.perf_counter(), addr
            self.n += 1

    def receiving(self):
        return self.pos is not None and time.perf_counter() - self.t < STALE_S

    def recentre(self):
        """the head's place now is the middle (no shift)"""
        if self.pos is not None:
            self.centre = (self.pos[0], self.pos[2])

    def offset(self, max_mm=MAX_MM):
        """the picture's shift against the overlay now, mm on the tube (x right, y down): max_mm at the head's full
        reach (REACH_CM), the same mm a cm up and down; softly limited to max_mm, never past it"""
        want = (0.0, 0.0)
        if self.receiving():
            x, _, z = self.pos
            if self.centre is None:
                self.centre = (0.0, 0.0)            # (the app's own centre: SmoothTrack centres on calibration)
            hx = -(x - self.centre[0]) * 10.0         # (mm; head to the right: x negative -> + - the owner's test)
            hy = (z - self.centre[1]) * 10.0          # (mm; head up: z positive)
            s = max_mm / (REACH_CM * 10.0)            # (mm of shift a mm of head)
            def lim(d):                               # (straight to 80 % of max_mm, then bending softly towards it)
                r = abs(d * s); a = 0.8 * max_mm
                r = r if r <= a else a + (max_mm - a) * math.tanh((r - a) / max(max_mm - a, 1e-6))
                return math.copysign(r, d)
            want = (lim(hx), -lim(hy))                # (y down on the tube: head up -> the picture up)
        now = time.perf_counter(); a = 1.0 - math.exp(-max(0.0, now - self.t_cur) / EASE_S) if EASE_S > 0 else 1.0; self.t_cur = now
        cx, cy = self.cur                          # (by time: asked any number of times a frame, the same smoothing)
        self.cur = (cx + (want[0] - cx) * a, cy + (want[1] - cy) * a)
        return self.cur

    def close(self):
        self.stop_ev.set()
        if self.sock is not None:
            try: self.sock.close()
            except OSError: pass


def parse(data):
    """(x, y, z) cm from one packet as SmoothTrack labels them (x across, y towards the screen, z up and down), or
    None: OpenTrack's 6 doubles (48 bytes); 3 or more doubles or floats taken too"""
    try:
        if len(data) >= 48:
            v = struct.unpack_from('<6d', data)
        elif len(data) >= 24:
            v = struct.unpack_from('<3d', data)
        elif len(data) >= 12:
            v = struct.unpack_from('<3f', data)
        else:
            return None
    except struct.error:
        return None
    x, z, y = v[0], v[1], v[2]                        # (the packet's 2nd is up / down - SmoothTrack's Z -, the 3rd
                                                      #  towards the screen - its Y: the owner's test, 2026-10-29)
    if not all(abs(a) < 1000 for a in (x, y, z)):    # (not a position: garbage, or another format)
        return None
    return (x, y, z)


def local_ips():
    """this PC's IPv4 addresses on its networks (for the phone app's target), best guess first"""
    ips = []
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('10.255.255.255', 1)); ips.append(s.getsockname()[0]); s.close()
    except OSError:
        pass
    try:
        for a in socket.gethostbyname_ex(socket.gethostname())[2]:
            if a not in ips and not a.startswith('127.'):
                ips.append(a)
    except OSError:
        pass
    return ips
