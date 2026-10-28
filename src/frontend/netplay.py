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
frames of two players' random input; the Vectrex unit's analog settings move only the picture - except R302, the
DAC offset, which also moves the joystick comparator: online both sides use the host's). So the
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
import collections
import hashlib
import json
import os
import random
import socket
import struct
import threading
import time

PORT = 7744
VERSION_NO = b'\x00version'  # (player 2's no: another online version)
CHAT_MAX = 200               # the chat: bytes a line (UTF-8)
CHAT_GIVEUP = 15.0           # (seconds: a line the other side never confirms is reported as not delivered)
MAGIC = b'VXN1'
DELAY = 3                   # frames of input delay (each side's input runs this many frames after it was read): at
                            # least this; the host measures the round trip and adds what it needs (the game's 'delay')
KEEP = 16                   # inputs repeated in every packet
TIMEOUT = 10.0              # seconds without the other side: the connection is lost
SERVER = (os.environ.get('VECTREX_NET_SERVER', '172.233.60.40'), int(os.environ.get('VECTREX_NET_SERVER_PORT', '7745')))
RMAGIC = b'VXR1'            # (the meeting point's packets)
PUNCH_S = 3.0               # seconds of trying the direct way before the relay
CODE_LEN = 5
SYNC_EVERY = 25             # (frames between two sync checks: twice a second)
NET_VER = 5                 # the online version: both sides must have the same. Raise it with every change to the
                            # core (anything that could make the same input give another game) or to these packets;
                            # 1 = v1.0.5-v1.0.8 (they send none); 3: the pause both share ('Z'); 4: taking a
                            # controller port (the input's flags: 0x10 port 1, 0x20 port 2); 5: the
                            # players' names (the hello carries the joiner's)
PAUSE_ALIVE = 0.25          # (seconds between two 'still here' packets while the game is paused)
PACE_DEAD = 0.5             # (frames: how far one side may be ahead of the other before it slows down)
PACE_MAX = 0.0002           # (seconds a frame it slows down at most: 1 %)


HELLO = struct.pack('<H', NET_VER)     # (the joiner's hello carries its online version)


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


# ---- a cartridge with an SD card (UVMC2, PiTrex): both sides must play from the same card, to the byte
CARD_CHUNK = 480                # (bytes of the host's card list a packet: with the relay's 19 bytes of headers it stays under
                                #  the meeting point's 512-byte packets - longer ones arrive cut short through the relay)
CARD_TIME = 1104537600          # (2005-01-01: every file's date on the card both play from - no PC's own dates)


def card_manifest(folder, cache_path=None):
    """[[path, size, md5]] of every file under an SD card folder (path with '/', sorted); the MD5s are kept in
    cache_path by path, size and date (a big card is hashed once)"""
    cache = {}
    if cache_path:
        try:
            with open(cache_path, encoding='utf-8') as f:
                cache = json.load(f)
        except (OSError, ValueError):
            cache = {}
    out, new = [], {}
    if folder and os.path.isdir(folder):
        for d, dirs, files in os.walk(folder):
            dirs.sort(key=str.lower)
            for fn in files:
                full = os.path.join(d, fn)
                rel = os.path.relpath(full, folder).replace(os.sep, '/')
                try:
                    st = os.stat(full)
                except OSError:
                    continue
                key = '%s|%d|%d' % (full, st.st_size, int(st.st_mtime))
                md5 = cache.get(key) or rom_md5(full)
                if not md5:
                    continue
                new[key] = md5
                out.append([rel, st.st_size, md5])
    if cache_path:
        try:
            with open(cache_path, 'w', encoding='utf-8') as f:
                json.dump(new, f)
        except OSError:
            pass
    out.sort(key=lambda e: e[0].lower())
    return out


def card_matches(manifest, folder, cache_path=None):
    """which of the host's card files this side has, the same to the byte: a list of booleans"""
    mine = {(e[0].lower(), e[1], e[2]) for e in card_manifest(folder, cache_path)}
    return [(e[0].lower(), e[1], e[2]) in mine for e in manifest]


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
        self.server_ip = None                           # (the meeting point's address: its packets only from there)
        if server:
            try:
                self.server_ip = socket.gethostbyname(server[0]); server = (self.server_ip, server[1])
            except OSError:
                pass
        self.server = server
        if role == 'join' and not server and addr:
            try:
                addr = socket.gethostbyname(addr)
            except OSError:
                pass
        self.peer = None if (role == 'host' or server) else (addr, port)
        self.locked = not (role == 'host' and not server)  # (game packets only from the other side's addresses - a
                                                           #  direct host learns them from the first hello)
        self.their_f = -1                               # (the clock sync: the other side's latest frame seen, and how
        self.adv = 0.0                                  #  far ahead each side sees itself - in frames, smoothed)
        self.adv_them = 0.0
        self.me_name, self.me_key = '', ''              # (this player's name and its key - the meeting point checks them)
        self.peer_name = ''                             # (the other player's name: from the meeting point, or their hello)
        self.offer = None                               # (host: the game it offers now - it may change while it waits)
        self.offer_rom = None
        self.last_heard = time.perf_counter()           # (the other side's last packet: TIMEOUT without one = lost)
        self.pause_on, self.pause_seq, self.pause_peer = False, 0, False   # (the pause both share: on or not, its
        self.pause_t = 0.0                                                 #  number, set by the other side)
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
        # the chat (the Chat window): lines sent until the other side confirms them ('T' / 'A'), each shown once
        self.chat_lock = threading.Lock()
        self.chat_out = []                              # ([number, utf-8 bytes, first sent]: not yet confirmed)
        self.chat_seq = 0
        self.chat_in = collections.deque()              # (the other player's lines, for the window)
        self.chat_failed = collections.deque()          # (ours that never arrived: an older emulator over there)
        self.chat_got = {}                              # (arrived ahead of an earlier one: held, to keep the order)
        self.chat_next = 0
        self.chat_t = 0.0
        # the sync check: every SYNC_EVERY frames both sides send the core's state checksum for that frame; the first
        # frame where they differ is kept (desync) - the two games have parted (should never happen)
        self.sums_mine, self.sums_theirs = {}, {}
        self.desync = None

    def reset(self):
        """frame 0: the power-on frame of the game both play"""
        self.f = 0
        self.local = {f: (0, 0, 0, 0) for f in range(self.delay)}     # (the first frames: no input, the same on both)
        self.remote = {f: (0, 0, 0, 0) for f in range(self.delay)}
        self.last_send = 0.0
        self.their_f = -1; self.adv = 0.0; self.adv_them = 0.0

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
                if not self.server_ip or src[0] != self.server_ip:
                    continue                                    # (not from the meeting point)
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
                if self.locked and src[0] not in self._known_ips():
                    continue                                    # (not from the other player: dropped)
                self.last_direct = time.perf_counter()
                if self.candidates and src in self.candidates and self.peer != src:
                    self.peer = src                             # (the direct way works: this address)
                    self.log('direct connection with %s:%d' % src)
            self.last_heard = time.perf_counter()
            return data[4], data[5:], src

    def _known_ips(self):
        ips = {c[0] for c in self.candidates}
        if self.peer:
            ips.add(self.peer[0])
        return ips

    def _server_packet(self, kind, body):
        if kind == b'C':
            self.code = body.decode('ascii', 'replace'); self.log('code %s' % self.code)
        elif kind == b'P':
            try:
                info = json.loads(body.decode())
            except ValueError:
                return
            self.peer_info = info
            if info.get('name'):
                self.peer_name = str(info['name'])[:12]
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
                    self.locked = True
                    ver = struct.unpack_from('<H', p[1])[0] if len(p[1]) >= 2 else 1
                    if not self.peer_name and len(p[1]) > 2:       # (a direct game: the name its hello says)
                        self.peer_name = gamer_name(p[1][2:14].decode('ascii', 'replace')) or ''
                    if ver != NET_VER:                  # (another emulator version: the games could part)
                        self.log('player 2 has online version %d, this is %d' % (ver, NET_VER))
                        for _ in range(5):
                            self._send(ord('V'), struct.pack('<H', NET_VER)); time.sleep(0.02)
                        return 'version'
                    self._path()
                    self.log('player 2 at %s' % ('the relay' if self.relay else '%s:%d' % self.peer))
                    rtt = self._ping()                  # (the round trip: the input delay that hides it, at 50 Hz)
                    self.delay = max(DELAY, min(12, int(rtt * 50.0 + 0.999) + 1))
                    game = dict(self.offer or game, delay=self.delay, rtt_ms=round(rtt * 1000), relay=self.relay)
                    self.reset()                        # (the first frames for this delay: the same on both sides)
                    self.log('round trip %.0f ms: %d frames of input delay' % (rtt * 1000, self.delay))
                    msg = json.dumps(game).encode()
                    for _ in range(int(5.0 / 0.05)):    # (the game, until it is acknowledged)
                        self._send(ord('G'), msg)
                        time.sleep(0.05)
                        q = self._recv()
                        if q and q[0] == ord('A'):
                            return game
                        if q and q[0] == ord('N') and q[1] == VERSION_NO:
                            return 'version'
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
                    self._send(ord('H'), self._hello())
                last_h = time.perf_counter()
            time.sleep(0.001)
            p = self._recv()
            while p:
                if p[0] == ord('P'):                    # (the host measuring the round trip: the same back at once)
                    self._path()
                    self._send(ord('p'), p[1])
                elif p[0] == ord('G'):
                    g = json.loads(p[1].decode())
                    if g.get('nver', 1) != NET_VER:     # (the host has another emulator version)
                        self.log('the host has online version %s, this is %d' % (g.get('nver', 1), NET_VER))
                        for _ in range(5):
                            self._send(ord('N'), VERSION_NO); time.sleep(0.02)
                        return 'version'
                    self.delay = int(g.get('delay', DELAY))
                    self.reset()
                    return g
                elif p[0] == ord('V'):                  # (the host refused: another version)
                    return 'version'
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
                    self._raw(RMAGIC + b'R' + json.dumps(dict(g, lan=me, ver=NET_VER, key=self.me_key)).encode(), self.server)
                elif self.role == 'host':
                    self._raw(RMAGIC + b'K' + self.code.encode(), self.server)
                else:
                    self._raw(RMAGIC + b'J' + self.code.upper().encode() +
                              json.dumps(dict(lan=me, name=self.me_name, key=self.me_key)).encode(), self.server)
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

    def update_offer(self, game, rom, info):
        """host, waiting: another game offered (the cartridge changed) - the game room lists it at once"""
        self.offer, self.offer_rom = game, rom
        self.game_info = dict(getattr(self, 'game_info', {}), **info)
        if self.server and self.code and self.role == 'host':
            for _ in range(2):
                self._raw(RMAGIC + b'U' + self.code.encode() + json.dumps(info).encode(), self.server)

    def _hello(self):
        return HELLO + (self.me_name or '').encode('ascii', 'replace')[:12]

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
                if hello or self.role == 'join':
                    self._send(ord('H'), self._hello(), to=c)
                else:
                    self._send(ord('h'), to=c)
            if since > PUNCH_S * 0.5 and self.code and self.server and (hello or self.role == 'join'):
                self._raw(RMAGIC + b'X' + self.code.encode() + MAGIC + bytes([ord('H')]) + self._hello(), self.server)
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

    # ---- the sync check
    def mark(self, f, state):
        """this side's state checksum after frame f (the core's, live.c): every SYNC_EVERY frames it goes to the other side"""
        if f % SYNC_EVERY:
            return
        self.sums_mine[f] = state & 0xffffffff
        self._send(ord('S'), struct.pack('<II', f, state & 0xffffffff))
        self._compare(f)
        for old in [k for k in self.sums_mine if k < f - 40 * SYNC_EVERY]:
            del self.sums_mine[old]

    def _compare(self, f):
        if f in self.sums_mine and f in self.sums_theirs:
            if self.sums_mine[f] != self.sums_theirs.pop(f) and self.desync is None:
                self.desync = f
                self.log('out of sync at frame %d' % f)

    # ---- the SD card both play from (after the game was accepted)
    def send_card(self, manifest, timeout=60.0, stop=lambda: False):
        """host: its card's file list to player 2, in pieces until all have arrived; returns player 2's answer (a list
        of booleans: which files it has the same) or None"""
        import zlib
        blob = zlib.compress(json.dumps(manifest).encode(), 9)
        parts = [blob[k:k + CARD_CHUNK] for k in range(0, len(blob), CARD_CHUNK)] or [b'']
        t0 = time.perf_counter(); i = 0
        while time.perf_counter() - t0 < timeout and not stop():
            self._send(ord('C'), struct.pack('<HH', i, len(parts)) + parts[i])
            i = (i + 1) % len(parts)
            time.sleep(0.004)
            p = self._recv()
            while p:
                if p[0] == ord('c') and len(p[1]) >= 4:
                    n = struct.unpack_from('<I', p[1])[0]
                    bits = p[1][4:]
                    for _ in range(5):
                        self._send(ord('K'))
                    return [bool(bits[k >> 3] >> (k & 7) & 1) for k in range(min(n, len(manifest)))] +                            [False] * max(0, len(manifest) - n)
                p = self._recv()
        return None

    def recv_card(self, answer, timeout=60.0, stop=lambda: False):
        """player 2: the host's card list; answer(manifest) -> the list of booleans sent back. Returns the manifest"""
        import zlib
        got, total, manifest, bits, n = {}, None, None, None, 0
        t0 = time.perf_counter(); last = 0.0
        while time.perf_counter() - t0 < timeout and not stop():
            p = self._recv()
            while p:
                if p[0] == ord('C') and len(p[1]) >= 4 and manifest is None:
                    i, total = struct.unpack_from('<HH', p[1]); got[i] = p[1][4:]
                    if len(got) == total:
                        try:
                            manifest = json.loads(zlib.decompress(b''.join(got[k] for k in range(total))).decode())
                        except (zlib.error, ValueError, KeyError):     # (a piece damaged on the way: collect them again)
                            self.log('the card list arrived damaged: collecting it again'); got = {}; manifest = None
                        if manifest is not None:
                            ok = answer(manifest); n = len(ok)
                            bits = bytearray((n + 7) // 8)
                            for k, v in enumerate(ok):
                                if v:
                                    bits[k >> 3] |= 1 << (k & 7)
                elif p[0] in (ord('K'), ord('I')) and manifest is not None:     # (the host has it: the game starts)
                    return manifest
                p = self._recv()
            if manifest is not None and time.perf_counter() - last > 0.05:
                self._send(ord('c'), struct.pack('<I', n) + bytes(bits)); last = time.perf_counter()
            time.sleep(0.002)
        return manifest

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
            if now - max(t_wait, self.last_heard) > TIMEOUT:     # (nothing from the other side: a paused one still
                                                                 #  sends that it is there)
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

    # ---- the pause both share: P on either side pauses both, P on either goes on
    def set_pause(self, on):
        self.pause_on, self.pause_peer = bool(on), False
        self.pause_seq = (self.pause_seq + 1) & 0xFFFF
        self._send_pause()

    def _send_pause(self):
        self._send(ord('Z'), struct.pack('<BH', 1 if self.pause_on else 0, self.pause_seq))
        self.pause_t = time.perf_counter()

    def idle(self):
        """while this side does not run frames (paused): the other side's packets taken, and it told now and then that
        this side is still there (so neither loses the other)"""
        if self.lost:
            return
        now = time.perf_counter()
        if now - self.pause_t > PAUSE_ALIVE:
            self._send_pause()
            if self.chat_out and now - self.chat_t > 0.15:
                self._send_chat(now)
        self._drain()
        if now - self.last_heard > TIMEOUT:
            self.lost = True
            self.log('connection lost (nothing from the other side while paused)')

    def pace_s(self):
        """the clock sync: seconds to add to this frame. The side ahead of the other slows down a little, so neither
        PC's clock runs into the other's and waits (a stutter). Each side sees itself ahead by the one-way delay too:
        half the difference of the two is how far ahead it really is"""
        ahead = (self.adv - self.adv_them) / 2.0
        if self.f < 100 or ahead <= PACE_DEAD:
            return 0.0
        return min(PACE_MAX, (ahead - PACE_DEAD) * 0.0001)

    def say(self, text):
        """a chat line to the other player (any thread): sent with the next frames until it is confirmed"""
        data = text.strip().encode('utf-8')[:CHAT_MAX]
        if not data:
            return
        with self.chat_lock:
            self.chat_out.append([self.chat_seq & 0xFFFF, data, time.perf_counter()])
            self.chat_seq += 1
        self.chat_t = 0.0

    def _send_chat(self, now):
        with self.chat_lock:
            for m in list(self.chat_out):
                if now - m[2] > CHAT_GIVEUP:
                    self.chat_out.remove(m); self.chat_failed.append(m[1].decode('utf-8', 'replace'))
                else:
                    self._send(ord('T'), struct.pack('<H', m[0]) + m[1])
        self.chat_t = now

    def _send_inputs(self):
        hi = self.f + self.delay
        lo = max(0, hi - KEEP + 1)
        body = struct.pack('<IB', lo, hi - lo + 1)
        for k in range(lo, hi + 1):
            jx, jy, b, fl = self.local.get(k, (0, 0, 0, 0))
            body += struct.pack('<bbBB', max(-127, min(127, jx)), max(-127, min(127, jy)), b & 255, fl & 255)
        body += struct.pack('<h', int(max(-2000, min(2000, self.adv * 16))))   # (the clock sync: how far ahead I see me)
        self._send(ord('I'), body)
        self.last_send = time.perf_counter()
        if self.chat_out and self.last_send - self.chat_t > 0.15:     # (the chat: until confirmed)
            self._send_chat(self.last_send)
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
                tf = lo + n - 1 - self.delay                    # (the frame the other side is on)
                if tf > self.their_f:
                    self.their_f = tf
                    self.adv += 0.05 * ((self.f - tf) - self.adv)
                    if len(body) >= 5 + 4 * n + 2:
                        self.adv_them = struct.unpack_from('<h', body, 5 + 4 * n)[0] / 16.0
            elif kind == ord('T') and len(body) >= 2:   # (a chat line: confirmed, shown once)
                k = struct.unpack_from('<H', body)[0]
                self._send(ord('A'), struct.pack('<H', k))
                if (k - self.chat_next) & 0xFFFF < 0x8000:   # (not one shown already)
                    self.chat_got[k] = body[2:CHAT_MAX + 2].decode('utf-8', 'replace')
                    while self.chat_next in self.chat_got:     # (in the order they were written)
                        self.chat_in.append(self.chat_got.pop(self.chat_next))
                        self.chat_next = (self.chat_next + 1) & 0xFFFF
            elif kind == ord('A') and len(body) >= 2:   # (ours arrived)
                k = struct.unpack_from('<H', body)[0]
                with self.chat_lock:
                    self.chat_out = [m for m in self.chat_out if m[0] != k]
            elif kind == ord('S') and len(body) >= 8:   # (the other side's state checksum)
                f, v = struct.unpack_from('<II', body)
                self.sums_theirs[f] = v
                self._compare(f)
            elif kind == ord('Z') and len(body) >= 3:   # (the pause: the newer of the two sides' wins)
                on, seq = struct.unpack_from('<BH', body)
                if 0 < (seq - self.pause_seq) & 0xFFFF < 0x8000:
                    self.pause_on, self.pause_seq, self.pause_peer = bool(on), seq, True
            elif kind == ord('Q'):                       # (the other side quit)
                self.lost = True
                self.log('the other player left')

    def close(self):
        for _ in range(3):
            self._send(ord('Q'))
        if self.server and self.code and self.role == 'host':      # (the meeting point: the code gone now - the game
            self._raw(RMAGIC + b'D' + self.code.encode(), self.server)   #  room no longer lists it)
        try:
            self.sock.close()
        except OSError:
            pass


GAMER_CHARS = set('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-')


def gamer_name(s):
    """a gamer name as the game room keeps it (upper case, 1-12 of A-Z 0-9 _ -, spaces as _), or None"""
    s = str(s or '').strip().upper().replace(' ', '_')
    return s if 1 <= len(s) <= 12 and set(s) <= GAMER_CHARS else None


def claim_name(server, name, key, old=None, timeout=3.0):
    """a player name from the meeting point: reserved for this key (key: this program's secret, kept in its settings).
    ('ok', name), ('taken', None), ('bad', None) - or None when it does not answer"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(0.5)
    t0, asked = time.perf_counter(), -1.0
    try:
        while time.perf_counter() - t0 < timeout:
            if time.perf_counter() - asked > 1.0:
                s.sendto(RMAGIC + b'N' + json.dumps(dict(name=name, key=key, old=old)).encode(), server)
                asked = time.perf_counter()
            try:
                data, _ = s.recvfrom(2048)
            except (socket.timeout, OSError):
                continue
            if data.startswith(RMAGIC + b'n'):
                try:
                    d = json.loads(data[5:].decode())
                except ValueError:
                    continue
                if d.get('ok'):
                    return 'ok', d.get('name')
                return ('taken' if d.get('why') == 'taken' else 'bad'), None
            if data.startswith(RMAGIC + b'E'):
                return 'bad', None
    finally:
        s.close()
    return None


def list_games(server, timeout=2.0):
    """the game room: the public games waiting at the meeting point, the oldest first - a list of dicts (code, gamer,
    name, md5, size, wait: seconds), or None when it does not answer (no network, or a meeting point without a game
    room)"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(0.25)
    pages, n, t0, asked = {}, None, time.perf_counter(), -1.0
    try:
        while time.perf_counter() - t0 < timeout:
            want = [p for p in range(n if n is not None else 1) if p not in pages]
            if not want:
                break
            if time.perf_counter() - asked > 0.5:               # (asked again now and then: a lost packet)
                for p in want[:6]:
                    s.sendto(RMAGIC + b'L' + bytes([p]), server)
                asked = time.perf_counter()
            try:
                data, _ = s.recvfrom(4096)
            except (socket.timeout, OSError):
                continue
            if not data.startswith(RMAGIC + b'l'):
                continue
            try:
                d = json.loads(data[5:].decode())
            except ValueError:
                continue
            n = max(1, int(d.get('n', 1)))
            pages[int(d.get('p', 0))] = d.get('g', [])
    finally:
        s.close()
    if n is None:
        return None
    out = []
    for p in sorted(pages):
        for row in pages[p]:
            if len(row) >= 6:
                out.append(dict(code=row[0], gamer=row[1], name=row[2], md5=row[3], size=row[4], wait=row[5],
                                ver=row[6] if len(row) >= 7 else 1))
    return out


def new_game(rom_path, settings):
    """the host's game: the ROM's identity and what must be the same on both sides"""
    return dict(md5=rom_md5(rom_path), size=os.path.getsize(rom_path), name=os.path.basename(rom_path),
                seed=random.getrandbits(31), nver=NET_VER, **settings)
