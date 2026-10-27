"""netplay.py - two Vectrex_EMU instances playing one game over the network: one hosts (player 1), the other joins
(player 2). A prototype, started from the command line:
    --net-host [PORT]          host on this PC's address (the other side needs it, and on the internet the router must
                               forward the UDP port)
    --net-online               host through the meeting point (tools/vxrelay.py on a server): you get a five-letter
                               code to tell the other player - no addresses, no router settings
    --net-join ADDRESS[:PORT]  join a host at an address
    --net-join CODE            join through the meeting point with the host's code

Lockstep: the emulator core is deterministic - the same ROM, the same power-on seed (the RAM's random state) and the
same controller input every frame give the same game, to the bit (tested 2026-10-27: the RAM's checksum after 1500
frames of two players' random input, and it does not depend on the Vectrex unit's analog settings either). So the
two instances never send the picture or the sound, only each frame's controller: 4 bytes. Each frame's input is sent
DELAY frames ahead (60 ms at 50 Hz: what a connection's round trip may take without a stall), every packet carries
the last 16 frames' inputs (a lost packet is covered by the next), and a frame runs only once the other side's input
for it is there - if it is late, the game waits for it.

The host chooses the game and what changes it (the power-on seed, the intro skipped or not, the cartridge
hardware, the infinite-lives switch); the joining side finds the same ROM in its own ROM folder by its MD5 (or is
told it is missing), and both switch on at the same moment.

The meeting point (vxrelay.py): the host registers and gets a code; the joiner asks with the code; the server tells
each the other's address (public and LAN) and both send to each other at once - UDP hole punching, which gets through
most home routers without any port forwarding. If nothing arrives directly within PUNCH_S seconds, the packets go
through the server instead (relayed: a little more delay, but it always works). Each side sends the way the other's
last packet came, so both end up on the same path.

Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import hashlib
import json
import os
import random
import socket
import struct
import time

PORT = 7744
MAGIC = b'VXN1'
DELAY = 3                   # frames of input delay (each side's input runs this many frames after it was read): at
                            # least this; the host measures the round trip and adds what it needs (the game's 'delay')
KEEP = 16                   # inputs repeated in every packet
TIMEOUT = 10.0              # seconds without the other side: the connection is lost
SERVER = (os.environ.get('VECTREX_NET_SERVER', '172.233.60.40'), int(os.environ.get('VECTREX_NET_SERVER_PORT', '7745')))
RMAGIC = b'VXR1'            # (the meeting point's packets)
PUNCH_S = 3.0               # seconds of trying the direct way before the relay
CODE_LEN = 5


def rom_md5(path):
    try:
        with open(path, 'rb') as f:
            return hashlib.md5(f.read()).hexdigest()
    except OSError:
        return None


def find_rom(md5, size, folders):
    """the ROM with this MD5 in these folders (and their subfolders), or None"""
    for top in folders:
        for d, _, files in os.walk(top):
            for fn in files:
                p = os.path.join(d, fn)
                try:
                    if os.path.getsize(p) != size:
                        continue
                except OSError:
                    continue
                if rom_md5(p) == md5:
                    return p
    return None


def is_code(s):
    s = (s or '').strip()
    return len(s) == CODE_LEN and s.isalpha()


def lan_addr(port):
    """this PC's LAN address (the one that would reach the internet) - a second candidate for a peer behind the same router"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('8.8.8.8', 53)); ip = s.getsockname()[0]; s.close()
        return [ip, port]
    except OSError:
        return None


class NetPlay:
    """one side of a game for two over UDP: connect() (the handshake), then exchange() once a frame"""
    def __init__(self, role, addr=None, port=PORT, log=None, server=None, code=None):
        self.role = role                                # 'host' (player 1) or 'join' (player 2)
        self.port = port
        self.server = server                            # (the meeting point: (host, port), or None for a direct game)
        self.code = code                                # (joining through the meeting point: the host's code)
        self.peer = None if (role == 'host' or server) else (addr, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        if role == 'host' and not server:
            self.sock.bind(('', port))
        else:
            self.sock.bind(('', 0))
        self.sock.settimeout(0.0)
        self.log = log or (lambda *a: None)
        self.delay = DELAY
        self.relay = False                              # (sending through the meeting point)
        self.last_direct = 0.0                          # (when a packet last came straight from the other side)
        self.last_relayed = 0.0
        self.candidates = []                            # (the other side's addresses to try)
        self.reset()
        self.lost = False
        self.stall_s = 0.0                              # (time spent waiting for the other side: the report)

    def reset(self):
        """frame 0: the power-on frame of the game both play"""
        self.f = 0
        self.local = {f: (0, 0, 0, 0) for f in range(self.delay)}     # (the first frames: no input, the same on both)
        self.remote = {f: (0, 0, 0, 0) for f in range(self.delay)}
        self.last_send = 0.0

    # ---- the packets
    def _raw(self, data, addr):
        try:
            self.sock.sendto(data, addr)
        except OSError:
            pass

    def _send(self, kind, payload=b'', to=None):
        pkt = MAGIC + bytes([kind]) + payload
        if to is not None:
            self._raw(pkt, to); return
        if self.relay and self.server and self.code:
            self._raw(RMAGIC + b'X' + self.code.encode() + pkt, self.server)
        elif self.peer:
            self._raw(pkt, self.peer)

    def _recv(self):
        """the next game packet (kind, payload, from) or None; the meeting point's packets handled on the way"""
        while True:
            try:
                data, src = self.sock.recvfrom(2048)
            except (BlockingIOError, socket.timeout, ConnectionResetError, OSError):
                return None
            if data.startswith(RMAGIC) and len(data) >= 5:
                kind, body = data[4:5], data[5:]
                if kind == b'F' and body.startswith(MAGIC):     # (relayed from the other side)
                    self.last_relayed = time.perf_counter()
                    data, src = body, ('relay', 0)
                else:
                    self._server_packet(kind, body)
                    continue
            if not data.startswith(MAGIC) or len(data) < 5:
                continue
            if src != ('relay', 0):
                self.last_direct = time.perf_counter()
                if self.candidates and src in self.candidates and self.peer != src:
                    self.peer = src                             # (the direct way works: this address)
                    self.log('direct connection with %s:%d' % src)
            return data[4], data[5:], src

    def _server_packet(self, kind, body):
        if kind == b'C':
            self.code = body.decode('ascii', 'replace'); self.log('code %s' % self.code)
        elif kind == b'P':
            try:
                info = json.loads(body.decode())
            except ValueError:
                return
            self.peer_info = info
            c = []
            if info.get('pub'): c.append(tuple(info['pub']))
            if info.get('lan'): c.append(tuple(info['lan']))
            self.candidates = c
            if c and self.peer is None:
                self.peer = c[0]
        elif kind == b'E':
            self.server_error = body.decode('ascii', 'replace'); self.log('meeting point: ' + self.server_error)

    def _path(self):
        """after the punching window: send the way the other side's last packet came"""
        if not self.server or not self.candidates:
            return
        if self.last_direct and self.last_direct >= self.last_relayed:
            if self.relay: self.log('the direct way works: no relay'); self.relay = False
        elif self.last_relayed:
            if not self.relay: self.log('through the relay'); self.relay = True

    # ---- the handshake
    def connect(self, game=None, timeout=120.0, stop=lambda: False):
        """host: waits for a player (game: what it chooses - ROM MD5, size, name, seed, settings); join: asks.
        Returns the game (a dict) or None, 'missing' (the joiner lacks the ROM) or 'server' (no meeting point)"""
        t0 = time.perf_counter()
        if self.server:
            if not self._meet(timeout, stop):
                return 'server' if not getattr(self, 'server_error', None) else self.server_error
        if self.role == 'host':
            while time.perf_counter() - t0 < timeout and not stop():
                if self.candidates:
                    self._punch()
                p = self._recv()
                if p and p[0] == ord('H'):              # (hello: a player)
                    if p[2] != ('relay', 0):
                        self.peer = p[2]
                    self._path()
                    self.log('player 2 at %s' % ('the relay' if self.relay else '%s:%d' % self.peer))
                    rtt = self._ping()                  # (the round trip: the input delay that hides it, at 50 Hz)
                    self.delay = max(DELAY, min(12, int(rtt * 50.0 + 0.999) + 1))
                    game = dict(game, delay=self.delay, rtt_ms=round(rtt * 1000), relay=self.relay)
                    self.reset()                        # (the first frames for this delay: the same on both sides)
                    self.log('round trip %.0f ms: %d frames of input delay' % (rtt * 1000, self.delay))
                    msg = json.dumps(game).encode()
                    for _ in range(int(5.0 / 0.05)):    # (the game, until it is acknowledged)
                        self._send(ord('G'), msg)
                        time.sleep(0.05)
                        q = self._recv()
                        if q and q[0] == ord('A'):
                            return game
                        if q and q[0] == ord('N'):      # (no: it does not have the ROM)
                            self.log('player 2 does not have the game: ' + q[1].decode(errors='replace'))
                            return 'missing'
                    return None
                self._keepalive()
                time.sleep(0.005 if self.candidates else 0.02)
            return None
        last_h = 0.0
        while time.perf_counter() - t0 < timeout and not stop():
            if time.perf_counter() - last_h > 0.2:      # (hello, five times a second; the host's pings answered at once)
                if self.candidates:
                    self._punch(hello=True)
                else:
                    self._send(ord('H'))
                last_h = time.perf_counter()
            time.sleep(0.001)
            p = self._recv()
            while p:
                if p[0] == ord('P'):                    # (the host measuring the round trip: the same back at once)
                    self._path()
                    self._send(ord('p'), p[1])
                elif p[0] == ord('G'):
                    g = json.loads(p[1].decode())
                    self.delay = int(g.get('delay', DELAY))
                    self.reset()
                    return g
                p = self._recv()
        return None

    def _meet(self, timeout, stop):
        """the meeting point: the host registers (and gets its code), the joiner asks with the code; both learn the
        other's addresses. True once they have them"""
        t0 = time.perf_counter(); last = 0.0
        me = lan_addr(self.sock.getsockname()[1])
        while time.perf_counter() - t0 < timeout and not stop():
            now = time.perf_counter()
            if now - last > (1.0 if self.role == 'join' or not self.code else 2.0):
                last = now
                if self.role == 'host' and not self.code:
                    g = getattr(self, 'game_info', {})
                    self._raw(RMAGIC + b'R' + json.dumps(dict(g, lan=me, ver=1)).encode(), self.server)
                elif self.role == 'host':
                    self._raw(RMAGIC + b'K' + self.code.encode(), self.server)
                else:
                    self._raw(RMAGIC + b'J' + self.code.upper().encode() + json.dumps(dict(lan=me)).encode(), self.server)
            if now - t0 > 6.0 and not self.code and self.role == 'host':
                return False                            # (no answer: no meeting point)
            if now - t0 > 6.0 and self.role == 'join' and not self.candidates and not getattr(self, 'server_error', None):
                self.server_error = 'no answer from the meeting point'; return False
            p = self._recv()                            # (the server's answers are taken on the way)
            if self.candidates:
                self.punch_t0 = time.perf_counter()
                return True
            if getattr(self, 'server_error', None):
                return False
            time.sleep(0.01)
        return False

    def _keepalive(self):
        if self.server and self.code and self.role == 'host':
            now = time.perf_counter()
            if now - getattr(self, 'ka_t', 0.0) > 2.0:
                self.ka_t = now; self._raw(RMAGIC + b'K' + self.code.encode(), self.server)

    def _punch(self, hello=False):
        """the punching window: hellos to every address of the other side, direct and (after a moment) through the
        relay too; afterwards the path the answers came by"""
        now = time.perf_counter()
        if now - getattr(self, 'punch_last', 0.0) < 0.1:
            return
        self.punch_last = now
        since = now - getattr(self, 'punch_t0', now)
        if since < PUNCH_S + 1.0:
            for c in self.candidates:
                self._send(ord('H') if (hello or self.role == 'join') else ord('h'), to=c)
            if since > PUNCH_S * 0.5 and self.code and self.server and (hello or self.role == 'join'):
                self._raw(RMAGIC + b'X' + self.code.encode() + MAGIC + bytes([ord('H')]), self.server)
        if since > PUNCH_S:
            self._path()
        self._keepalive()

    def _ping(self, n=8):
        """the round trip to the other side (the slowest of n pings, as a connection's jitter needs that much)"""
        got = []
        for i in range(n):
            t = time.perf_counter()
            self._send(ord('P'), struct.pack('<Id', i, t))
            while time.perf_counter() - t < 0.5:
                q = self._recv()
                if q and q[0] == ord('p') and len(q[1]) >= 12:
                    k, t0 = struct.unpack_from('<Id', q[1])
                    if k == i:
                        got.append(time.perf_counter() - t0); break
                elif q and q[0] in (ord('H'), ord('h')):
                    pass
                else:
                    time.sleep(0.001)
        return max(got) if got else 0.1

    def accept(self, ok, why=''):
        """join: the game is there (or not)"""
        for _ in range(5):
            self._send(ord('A') if ok else ord('N'), why.encode())
            time.sleep(0.02)

    # ---- the game: one exchange a frame
    def exchange(self, mine):
        """this frame's input of both players: mine (jx, jy, buttons, flags) is the input read now - it runs DELAY
        frames later. Returns (player 1's, player 2's) input for the frame now, waiting for the other's if needed;
        None when the connection is lost"""
        if self.lost:
            return None
        f = self.f
        self.local[f + self.delay] = tuple(int(v) for v in mine)
        t_wait = time.perf_counter()
        while f not in self.remote:
            if self.lost:                                # (closed meanwhile: the window shut, or the other side left)
                return None
            now = time.perf_counter()
            if now - self.last_send > 0.010:
                self._send_inputs()
            self._drain()
            if f in self.remote:
                break
            if now - t_wait > TIMEOUT:
                self.lost = True
                self.log('connection lost (no input for frame %d)' % f)
                return None
            time.sleep(0.0005)
        self.stall_s += time.perf_counter() - t_wait
        self._send_inputs()
        a, b = self.local.pop(f), self.remote.pop(f)
        self.f += 1
        for old in [k for k in self.remote if k < f - 64]:
            del self.remote[old]
        return (a, b) if self.role == 'host' else (b, a)

    def _send_inputs(self):
        hi = self.f + self.delay
        lo = max(0, hi - KEEP + 1)
        body = struct.pack('<IB', lo, hi - lo + 1)
        for k in range(lo, hi + 1):
            jx, jy, b, fl = self.local.get(k, (0, 0, 0, 0))
            body += struct.pack('<bbBB', max(-127, min(127, jx)), max(-127, min(127, jy)), b & 255, fl & 255)
        self._send(ord('I'), body)
        self.last_send = time.perf_counter()
        if self.server and self.candidates and (self.f & 63) == 0:
            self._path()                                 # (the direct way may open or close during the game)

    def _drain(self):
        while True:
            p = self._recv()
            if not p:
                return
            kind, body, src = p
            if kind == ord('I') and len(body) >= 5:
                lo, n = struct.unpack_from('<IB', body)
                for i in range(n):
                    if 5 + 4 * i + 4 > len(body):
                        break
                    k = lo + i
                    if k >= self.f and k not in self.remote:
                        self.remote[k] = struct.unpack_from('<bbBB', body, 5 + 4 * i)
            elif kind == ord('Q'):                       # (the other side quit)
                self.lost = True
                self.log('the other player left')

    def close(self):
        for _ in range(3):
            self._send(ord('Q'))
        try:
            self.sock.close()
        except OSError:
            pass


def new_game(rom_path, settings):
    """the host's game: the ROM's identity and what must be the same on both sides"""
    return dict(md5=rom_md5(rom_path), size=os.path.getsize(rom_path), name=os.path.basename(rom_path),
                seed=random.getrandbits(31), **settings)
