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
MAX_MM = 2.0                # strength x1: the largest shift at the full reach from the usual distance (the owner: 2 mm -
                            #  what x0.4 of the first 5 mm gave, 2026-10-29)
VIEW_CM = 60.0              # the usual distance from the eyes to the screen - where Centre was pressed counts as this
NEAR_CM, FAR_CM = 25.0, 150.0   # (the distance taken no nearer and no further)
REFL_L_CM = 150.0           # the ceiling light's mirror image behind the glass (as far as the light is in front of it)
                            # The distance as in the real world (the owner, 2026-10-29): the picture just behind the
                            #  sheet moves by the head's move x depth / distance - half the distance, twice the shift; a
                            #  mirror image far behind the glass by x L / (distance + L) - hardly changing. Always so:
                            #  no slider (the owner)
DIST_SIGN = -1.0            # (SmoothTrack's y towards the screen: closer = smaller y; +1 if the owner's test says otherwise)
STALE_S = 0.6               # no packet for this long: the shift eases back to the middle
EASE_S = 0.0                # (the shift itself: not smoothed again - the position is, SMOOTH_S)
SMOOTH_AXES = (0.03, 0.08, 0.08)   # the time constants per axis (x across, y near / far, z up and down): the owner,
                            #  2026-10-29 - heads move mostly across, hardly up and down or nearer and further
SMOOTH_S = 0.04             # (one for all: the earlier slider) the head's position smoothed (Config > Head > Smoothing, which sets
                            #  smooth_s): the reflections moved more than the app's own smoothing hides - small judder
                            #  showed (the owner, 2026-10-29: 80 ms, then half of it); the picture too


class HeadTrack:
    def __init__(self, port=PORT, root=None):
        self.port = port
        self.pos = None                 # (x, y, z) cm, the newest
        self.t = 0.0                    # when it came
        self.n = 0                      # packets so far
        self.src = None                 # who sent them (address)
        self.err = None
        self.cur = (0.0, 0.0)           # the shift now (smoothed), mm
        self.t_cur = time.perf_counter()
        self.sp = None; self.t_sp = 0.0  # (the smoothed position, and when it was last brought up to date)
        self.smooth_s = SMOOTH_AXES      # (its time constants, s: across, near / far, up / down - the Config's sliders)
        self.centre = None              # (x, y, z) taken as the middle and the usual distance: the app's zero, or Centre
        self.stop_ev = threading.Event()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.sock.bind(('0.0.0.0', port))
        except OSError as e:
            self.err = str(e); self.sock.close(); self.sock = None
            return
        self.sock.settimeout(0.5)
        threading.Thread(target=self._run, daemon=True, name='headtrack').start()
        self.usb = None                 # (the iPhone over USB: its serial number while connected)
        threading.Thread(target=self._usb_run, daemon=True, name='headtrack-usb').start()
        ips = local_ips()               # (Android over USB: Gnirehtet's route, run here - see AndroidUSB)
        self.android = AndroidUSB(root, self._feed, self.stop_ev, ips[0] if ips else '10.0.0.1') if root else None

    def _feed(self, p, serial):
        """a position over USB (iPhone or Android): it always counts - USB comes before the network"""
        now = time.perf_counter()
        self.pos, self.t, self.src = p, now, ('USB', serial)
        self.t_usb = now
        self.n += 1

    def via_usb(self):
        """positions coming over USB now (the network's are then left aside)"""
        return time.perf_counter() - getattr(self, 't_usb', 0.0) < STALE_S

    def _usb_run(self):
        """SmoothTrack over USB, without its tunnel tool: ask Apple's usbmuxd for an iPhone on the cable and read the
        app's port (USB_PORT) straight - the same 6 doubles, a stream. Looked for all the time, also while positions
        come over the network: USB comes first (steadier); unplugged, the network's take over at once (the owner)"""
        while not self.stop_ev.is_set():
            try:
                for dev_id, serial in usb_devices():
                    s = usb_connect(dev_id, USB_PORT)
                    if s is None:
                        continue
                    self.usb = serial
                    try:
                        self._usb_read(s, serial)
                    finally:
                        self.usb = None
                        try: s.close()
                        except OSError: pass
            except OSError:
                pass
            self.stop_ev.wait(1.0)                              # (no iPhone, Apple's service, or app: again soon)

    def _usb_read(self, s, serial):
        s.settimeout(0.5); buf = b''
        try: s.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)   # (a phone unplugged: noticed)
        except OSError: pass
        while not self.stop_ev.is_set():
            try:
                chunk = s.recv(4096)
            except socket.timeout:                              # (quiet - SmoothTrack's calibration pauses it for a few
                continue                                        #  seconds: the connection is kept, never closed from
                                                                #  here - closed, the app took no new one until it was
                                                                #  restarted, the owner 2026-10-29)
            if not chunk:
                return
            buf += chunk
            n = len(buf) // 48
            if n:
                p = parse(buf[(n - 1) * 48:n * 48]); buf = buf[n * 48:]   # (only the newest of what came)
                if p is not None:
                    self._feed(p, serial)

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
            self.t_net = time.perf_counter()
            if self.via_usb():                                  # (USB brings them: the network's left aside)
                continue
            self.pos, self.t, self.src = p, self.t_net, addr
            self.n += 1

    def receiving(self):
        return self.pos is not None and time.perf_counter() - self.t < STALE_S

    def smooth(self):
        """the head's position, smoothed (SMOOTH_S, by time: asked any number of times a frame, the same) - None
        without one"""
        if not self.receiving():
            self.sp = None
            return None
        now = time.perf_counter()
        ts = getattr(self, 'smooth_s', SMOOTH_AXES)
        if not isinstance(ts, (tuple, list)):
            ts = (ts, ts, ts)
        if self.sp is None:
            self.sp = tuple(self.pos)
        else:
            dt = max(0.0, now - self.t_sp)
            self.sp = tuple(n if t <= 0 else o + (n - o) * (1.0 - math.exp(-dt / t))
                            for o, n, t in zip(self.sp, self.pos, ts))
        self.t_sp = now
        t = math.tan(math.radians(getattr(self, 'tilt_deg', 0.0)))
        if not t:
            return self.sp
        if self.centre is None:
            self.centre = (0.0, self.pos[1], 0.0)
        x, y, z = self.sp                               # (a phone tilted up at the face - below the screen, looking up:
        return (x, y, z + t * (y - self.centre[1]))     #  coming nearer it sees the head rise. Taken out: up / down by
                                                        #  the move nearer x tan(tilt) - Config > Head > Phone tilt; the
                                                        #  owner's phone: about 13 degrees, 2026-10-29)

    def recentre(self):
        """the head's place now is the middle (no shift)"""
        if self.pos is not None:
            self.centre = tuple(self.pos)

    def distance_cm(self):
        """the head's distance to the screen now (the usual VIEW_CM where Centre was pressed), cm"""
        if not self.receiving():
            return VIEW_CM
        p = self.smooth() or self.pos
        if self.centre is None:
            self.centre = (0.0, p[1], 0.0)
        return min(max(VIEW_CM - DIST_SIGN * (p[1] - self.centre[1]), NEAR_CM), FAR_CM)

    def refl_k(self):
        """the reflections' move against the usual distance: L / (D + L), relative (the real world)"""
        d = self.distance_cm()
        return (VIEW_CM + REFL_L_CM) / (d + REFL_L_CM)

    def distance_k(self):
        """how much bigger the picture's shift is at the head's distance now than at the usual one: as on a real Vectrex
        - a near layer slides against a far one by the head's move x the depth / the distance, so closer is more"""
        if not self.receiving():
            return 1.0
        p = self.smooth() or self.pos
        if self.centre is None:
            self.centre = (0.0, p[1], 0.0)
        y = p[1] - self.centre[1]
        d = min(max(VIEW_CM - DIST_SIGN * y, NEAR_CM), FAR_CM)
        return VIEW_CM / d

    def head_view(self):
        """the head now against Centre: (mm right, mm up, the reflections' distance factor - refl_k) - None without a
        position. The room light's reflections follow this (vecrender: refl_shift)"""
        if not self.receiving():
            return None
        x, _, z = self.smooth()
        if self.centre is None:
            self.centre = (0.0, self.pos[1], 0.0)
        lim = REACH_CM * 15.0                         # (half again the full reach: no further)
        hx = max(-lim, min(lim, -(x - self.centre[0]) * 10.0))
        hy = max(-lim, min(lim, (z - self.centre[2]) * 10.0))
        return hx, hy, self.refl_k()

    def offset(self, strength=1.0):
        """the picture's shift against the overlay now, mm on the tube (x right, y down): MAX_MM x strength at the
        head's full reach (REACH_CM) from the usual distance, the same mm a cm up and down, softly limited there -
        then larger closer to the screen and smaller further away (distance_k)"""
        want = (0.0, 0.0)
        max_mm = MAX_MM * strength
        if self.receiving() and max_mm > 0:
            x, _, z = self.smooth()
            if self.centre is None:                 # (across and up: the app's own centre - SmoothTrack centres on
                self.centre = (0.0, self.pos[1], 0.0)   #  calibration; the distance: where the head is at the start)
            hx = -(x - self.centre[0]) * 10.0         # (mm; head to the right: x negative -> + - the owner's test)
            hy = (z - self.centre[2]) * 10.0          # (mm; head up: z positive)
            s = max_mm / (REACH_CM * 10.0)            # (mm of shift a mm of head)
            def lim(d):                               # (straight to 80 % of max_mm, then bending softly towards it)
                r = abs(d * s); a = 0.8 * max_mm
                r = r if r <= a else a + (max_mm - a) * math.tanh((r - a) / max(max_mm - a, 1e-6))
                return math.copysign(r, d)
            k = self.distance_k()
            want = (lim(hx) * k, -lim(hy) * k)        # (y down on the tube: head up -> the picture up)
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
        if len(data) >= 48:                           # (several at once - SmoothTrack's USB tool sends what it read:
            v = struct.unpack_from('<6d', data, (len(data) // 48 - 1) * 48 if len(data) % 48 == 0 else 0)   #  the newest)
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


# ---- SmoothTrack over USB (iPhone): what its tunnel tool (STUSB: iproxy + a small forwarder) does, built in. The app
# serves its 6 doubles on TCP port 47047 on the phone; Apple's usbmuxd (Windows: the Apple Mobile Device Service, from
# Apple Devices or iTunes, on 127.0.0.1:27015; macOS / Linux: /var/run/usbmuxd) opens that port over the cable.
USB_PORT = 47047
MUX_TCP = ('127.0.0.1', 27015)
MUX_UNIX = '/var/run/usbmuxd'


def _mux_socket():
    if hasattr(socket, 'AF_UNIX') and __import__('os').path.exists(MUX_UNIX):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(2.0); s.connect(MUX_UNIX)
    else:
        s = socket.create_connection(MUX_TCP, timeout=2.0)
    return s


def _mux_ask(s, msg, tag=1):
    """one usbmuxd request (a plist) and its answer"""
    import plistlib
    msg = dict(msg, ClientVersionString='Vectrex_EMU', ProgName='Vectrex_EMU', kLibUSBMuxVersion=3)
    body = plistlib.dumps(msg)
    s.sendall(struct.pack('<IIII', 16 + len(body), 1, 8, tag) + body)   # (length, version 1, plist message, tag)
    hdr = _recv_n(s, 16)
    ln, = struct.unpack('<I', hdr[:4])
    return plistlib.loads(_recv_n(s, ln - 16))


def _recv_n(s, n):
    b = b''
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            raise OSError('usbmuxd closed')
        b += c
    return b


def usb_devices():
    """the iPhones / iPads on a USB cable: [(device id, serial)] - [] without Apple's service"""
    try:
        s = _mux_socket()
    except OSError:
        return []
    try:
        r = _mux_ask(s, {'MessageType': 'ListDevices'})
    except (OSError, ValueError, Exception):   # noqa: (a broken answer: none)
        return []
    finally:
        s.close()
    out = []
    for d in r.get('DeviceList', []) if isinstance(r, dict) else []:
        p = d.get('Properties', {}) if isinstance(d, dict) else {}
        if p.get('ConnectionType', 'USB') == 'USB' and 'DeviceID' in d:
            out.append((d['DeviceID'], p.get('SerialNumber', '?')))
    return out


def usb_connect(dev_id, port):
    """a stream to the device's TCP port over the cable, or None (the app is not serving: USB Connection off)"""
    try:
        s = _mux_socket()
    except OSError:
        return None
    try:
        r = _mux_ask(s, {'MessageType': 'Connect', 'DeviceID': dev_id,
                         'PortNumber': ((port & 0xff) << 8) | (port >> 8)})   # (the port in network order)
        if isinstance(r, dict) and r.get('Number') == 0:
            return s                                            # (from now on the socket is the phone's port)
    except Exception:   # noqa
        pass
    s.close()
    return None


# ---- SmoothTrack over USB (Android): its guide uses Gnirehtet - a VPN app on the phone that carries the phone's
# network packets over ADB to a relay on the PC. Built in here as the smallest relay that does the job: only the
# PC's own address is routed through the VPN (the phone's internet stays as it is), and of what comes only the UDP
# packets for PORT are read - the same 6 doubles. Needs adb and gnirehtet.apk: a Gnirehtet folder (as SmoothTrack's
# guide has the player unpack, with adb.exe, AdbWinApi.dll, AdbWinUsbApi.dll in it) next to the emulator; and ADB
# debugging on the phone. The protocol (Gnirehtet's relay): the phone's app opens the abstract socket 'gnirehtet',
# adb reverse brings it to TCP RELAY_PORT here; the relay first sends a 4-byte client id, then both ways raw IPv4
# packets back to back.
RELAY_PORT = 31416
GN_PKG = 'com.genymobile.gnirehtet'


def find_gnirehtet(root):
    """(adb, gnirehtet.apk) - from a Gnirehtet folder next to the emulator (any name starting 'gnirehtet', or
    'smoothtrack'), adb also from the PATH; None for what is missing"""
    import os, shutil
    exe = 'adb.exe' if os.name == 'nt' else 'adb'
    adb = apk = None
    for d in sorted(os.listdir(root)) if os.path.isdir(root) else []:
        p = os.path.join(root, d)
        if os.path.isdir(p) and d.lower().startswith(('gnirehtet', 'smoothtrack', 'platform-tools')):
            for base, _, files in os.walk(p):
                if exe in files and adb is None: adb = os.path.join(base, exe)
                if 'gnirehtet.apk' in files and apk is None: apk = os.path.join(base, 'gnirehtet.apk')
    return adb or shutil.which('adb'), apk


class AndroidUSB:
    """the Gnirehtet route for SmoothTrack on Android, run by the emulator: feed(x, y, z) for each position"""
    def __init__(self, root, feed, stop_ev, target_ip):
        self.root, self.feed, self.stop_ev, self.ip = root, feed, stop_ev, target_ip
        self.state = 'idle'                 # (for Config's line: idle / no adb / no phone / connected ...)
        self.serial = None
        threading.Thread(target=self._run, daemon=True, name='headtrack-android').start()

    def _adb(self, adb, *args, timeout=8):
        import subprocess
        kw = {'creationflags': 0x08000000} if __import__('os').name == 'nt' else {}   # (no console window)
        r = subprocess.run([adb] + list(args), capture_output=True, text=True, timeout=timeout, **kw)
        return r.returncode, (r.stdout or '') + (r.stderr or '')

    def _run(self):
        srv = None
        while not self.stop_ev.is_set():
            try:
                adb, apk = find_gnirehtet(self.root)
                if not adb:
                    self.state = 'no adb'; self.stop_ev.wait(5.0); continue
                rc, out = self._adb(adb, 'devices')
                devs = [l.split()[0] for l in out.splitlines()[1:] if l.strip().endswith('device')]
                if not devs:
                    self.state = 'no phone'; self.stop_ev.wait(3.0); continue
                self.serial = devs[0]
                if srv is None:
                    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    srv.bind(('127.0.0.1', RELAY_PORT)); srv.listen(1); srv.settimeout(1.0)
                rc, out = self._adb(adb, '-s', self.serial, 'shell', 'pm', 'path', GN_PKG)
                if 'package:' not in out:                      # (the VPN app on the phone, once)
                    if not apk:
                        self.state = 'no gnirehtet.apk'; self.stop_ev.wait(5.0); continue
                    self.state = 'installing'; self._adb(adb, '-s', self.serial, 'install', '-r', apk, timeout=60)
                self._adb(adb, '-s', self.serial, 'reverse', 'localabstract:gnirehtet', 'tcp:%d' % RELAY_PORT)
                self._adb(adb, '-s', self.serial, 'shell', 'am', 'start', '-a', GN_PKG + '.START',
                          '-n', GN_PKG + '/.GnirehtetActivity', '--esa', 'routes', '%s/32' % self.ip,
                          '--esa', 'dnsServers', '8.8.8.8')          # (only this PC's address through the VPN)
                self.state = 'waiting for the phone'
                try:
                    c, _ = srv.accept()
                except socket.timeout:
                    self.stop_ev.wait(2.0); continue
                self.state = 'connected'
                try:
                    self._relay(c)
                finally:
                    c.close(); self.state = 'idle'
            except (OSError, ValueError) as e:                  # (adb gone, a timeout...: again later)
                self.state = 'error: %s' % str(e)[:60]
                self.stop_ev.wait(3.0)
            except Exception as e:   # noqa
                self.state = 'error: %s' % str(e)[:60]
                self.stop_ev.wait(3.0)
        if srv is not None:
            srv.close()

    def _relay(self, c):
        c.sendall(struct.pack('!I', 1))                         # (the client id)
        c.settimeout(1.0); buf = b''
        while not self.stop_ev.is_set():
            try:
                chunk = c.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                return
            buf += chunk
            while len(buf) >= 20:                               # (IPv4 packets back to back: the length in the header)
                if buf[0] >> 4 != 4:
                    return                                      # (lost step: start again)
                ln, = struct.unpack('!H', buf[2:4])
                if ln < 20 or len(buf) < ln:
                    break
                pkt, buf = buf[:ln], buf[ln:]
                ihl = (pkt[0] & 15) * 4
                if pkt[9] == 17 and len(pkt) >= ihl + 8:        # (UDP)
                    dport, = struct.unpack('!H', pkt[ihl + 2:ihl + 4])
                    if dport == PORT:
                        p = parse(pkt[ihl + 8:])
                        if p is not None:
                            self.feed(p, self.serial)


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
