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
import sys
import struct
import threading
import time

PORT = 7744
VERSION_NO = b'\x00version'  # (player 2's no: another online version)

# ---- the host's saves for both (2026-11-20, as the apps' netvx.c): a flash cartridge's flash (Vectorblade's calibration,
#      high scores) and Last Human's save chip go to player 2 at the start (and with a game switch); both play with them -
#      each player's own parted the games, none (as they came) lost the calibration. Only for this game, only in files of
#      their own (vectrex_emu_online.*, made anew each time): player 2's own saves are never written. "VXS1", then records:
#      type (1 flash, 2 save chip), length, the runs where it differs from the ROM / an empty chip ([offset 4][length 2][bytes])
SAVE_TAG = b'\x00save'       # (player 2's yes: it takes the host's saves)
SAVE_MAX = 65536


def _save_runs(typ, img, base):
    out = bytearray(); runs = 0; i = 0; n = len(img)
    while i < n:
        if img[i] == base[i]:
            i += 1; continue
        j = last = i
        while j < n and j - i < 60000:
            if img[j] != base[j]:
                last = j
            elif j - last > 8:
                break
            j += 1
        j = last + 1
        out += struct.pack('<IH', i, j - i) + img[i:j]; runs += 1; i = j
    return struct.pack('<BII', typ, n, runs) + bytes(out)


def save_flash_len(n, hw):
    """a flash cartridge's flash size (the core's vtest.c rule), 0: none"""
    if hw == 'vb':
        return n if n > 65536 else 262144
    if hw == 'hb512':
        return 524288
    if hw not in ('auto', '1', '', None):
        return 0
    return 524288 if n == 524288 else (n if 65536 < n <= 262144 else 0)


def save_blob(rom, hw='auto', chip=None):
    """the host's package for rom: its flash save (rom.sav) and its save chip's file (chip)"""
    out = bytearray(b'VXS1')
    try:
        with open(rom, 'rb') as f:
            r = f.read()
    except OSError:
        return bytes(out)
    L = save_flash_len(len(r), hw)
    if L:
        try:
            with open(rom + '.sav', 'rb') as f:
                s = f.read()
        except OSError:
            s = None
        if s is not None and len(s) == L:
            rec = _save_runs(1, s, (r + b'\xff' * L)[:L])
            if len(out) + len(rec) <= SAVE_MAX:
                out += rec
    if chip:
        try:
            with open(chip, 'rb') as f:
                d = f.read()
        except OSError:
            d = None
        if d is not None and len(d) == 128:
            out += _save_runs(2, d, b'\xff' * 128)
    return bytes(out)


def save_env(blob, rom, folder):
    """the package's files for the core (VT_FLASH_IMG, VT_DS2431), written to folder"""
    env = {}
    if not blob or blob[:4] != b'VXS1':
        return env
    try:
        with open(rom, 'rb') as f:
            r = f.read()
    except OSError:
        r = b''
    k, n = 4, len(blob)
    while k + 9 <= n:
        typ, L, runs = struct.unpack_from('<BII', blob, k); k += 9
        if typ not in (1, 2):                                   # (another kind: passed over)
            k += L; continue
        img = bytearray((r + b'\xff' * L)[:L]) if typ == 1 and L <= 1 << 20 else bytearray(b'\xff' * 128) if typ == 2 and L == 128 else None
        for _ in range(runs):
            if k + 6 > n:
                break
            o, l = struct.unpack_from('<IH', blob, k); k += 6
            if img is not None and o + l <= L:
                img[o:o + l] = blob[k:k + l]
            k += l
        if img is not None:
            p = os.path.join(folder, 'vectrex_emu_online' + ('.sav' if typ == 1 else '.ds2431'))
            with open(p, 'wb') as f:
                f.write(img)
            env['VT_FLASH_IMG' if typ == 1 else 'VT_DS2431'] = p
    return env

BIOS_NO = b'\x00bios'        # (player 2's no: another BIOS (bios.bin) - the games would part from the first frame)
CHAT_MAX = 200               # the chat: bytes a line (UTF-8)
CHAT_GIVEUP = 15.0           # (seconds: a line the other side never confirms is reported as not delivered)
MAGIC = b'VXN1'
DELAY = 3                   # frames of input delay (each side's input runs this many frames after it was read): at
                            # least this; the host measures the round trip and adds what it needs (the game's 'delay')
KEEP = 16                   # inputs repeated in every packet
TIMEOUT = 6.0               # seconds without the other side (no packet, or no new input while the game waits for it):
                            # "no connection" said - both wait for it as long as it takes, only Leave ends a game (the
                            # owner, 2026-11-19 - as the iOS app; it used to end after 30 s)
JOIN_SILENT = 20.0          # (joining: seconds without a packet from the host before giving up)
OUT_GRACE = 1.5             # (seconds the game waits before the countdown shows: a connection drop)
SERVER = (os.environ.get('VECTREX_NET_SERVER', '172.233.60.40'), int(os.environ.get('VECTREX_NET_SERVER_PORT', '7745')))
RMAGIC = b'VXR1'            # (the meeting point's packets)
PUNCH_S = 3.0               # seconds of trying the direct way before the relay
CODE_LEN = 5
SYNC_EVERY = 25             # (frames between two sync checks: twice a second)
RB_TAG = b'\x00rb'           # (player 2's yes: it runs on guesses - rollback, as the apps' netvx.c)
RB_W = 20                   # (rollback: frames on guesses at most, then it waits as in lockstep)
RB_SLOTS = 24               # (the core's saved frames, live.c RB_SLOTS)

# ---- line streaming (as the apps' netvx.c): a card game played on the host only - each frame's lines and sound (the
#      core packs them, live.c) to player 2 in pieces of 1180 bytes (560 through the meeting point), one repair piece (the
#      four XORed) after each four; player 2's core shows them (live.c message 0x90) and its controller goes back ('u')
STREAM_TAG = b'\x00stream'   # (player 2's yes: the host's lines shown here)
ST_GROUP = 4


def unlz(data, cap):
    """the apps' small packer (netvx.c nv_lz) undone - None when it does not come out at exactly cap bytes"""
    out = bytearray(); i, n = 0, len(data)
    while i < n:
        t = data[i]; i += 1
        if t & 0x80:
            if i + 4 > n:
                return None
            off = data[i] | data[i + 1] << 8; ln = data[i + 2] | data[i + 3] << 8; i += 4
            if not off or off > len(out) or len(out) + ln > cap:
                return None
            for _ in range(ln):
                out.append(out[-off])
        else:
            if i + t > n or len(out) + t > cap:
                return None
            out += data[i:i + t]; i += t
    return bytes(out) if len(out) == cap else None

NET_VER = 11                # the online version: both sides must have the same (libretro/netvx.c NV_VER too: the iOS app
                            # plays the PC). Raise it with every change to the
                            # core (anything that could make the same input give another game) or to these packets;
                            # 1 = v1.0.5-v1.0.8 (they send none); 3: the pause both share ('Z'); 4: taking a
                            # controller port (the input's flags: 0x10 port 1, 0x20 port 2); 5: the
                            # players' names (the hello carries the joiner's); 6: v1.1.0 - inputs kept after a drop, the PiTrex
                            # JIT fast between the same processor type; 7: v1.1.1 - the UVMC2's TRNG from the power-on seed
                            # 8: the 6809 DAA keeps the carry of the addition before (BCD scores)
                            # 9: v1.4.7 - a UVMC2's / PiTrex's frames end where the program's own frame does
                            # 10: v1.6.0 - a UVMC2 game powers on through the Vectrex's intro
                            # 11: v1.6.3 - the VIA's timer 1 started at $FFFF runs its 65537 cycles (it timed out at once)
PAUSE_ALIVE = 0.25          # (seconds between two 'still here' packets while the game is paused)
PACE_DEAD = 0.5             # (frames: how far one side may be ahead of the other before it slows down)
PACE_MAX = 0.0002           # (seconds a frame it slows down at most: 1 %)


JIT_FAST = b'\x00jit:fast'   # (player 2's yes, with its accept: the same processor type - the PiTrex JIT at full speed)
RGMODE_TAG = b'\x00rgmode'   # (player 2's yes: it follows a switch to or from a card game too - streamed / in step; netvx.c alike)
REGAME_TAG = b'\x00regame'   # (player 2's yes: it follows the host to another game in a game - the iOS app's netvx.c too)
UVM2_TAG = b'\x00uvm2:'      # (player 2's yes: + the UVMC2 mode both play in - the lower of the two players')
UVM2_RANK = ('exact', 'fast', 'dev')


def uvm2_agree(a, b):
    """the UVMC2 mode two players play in: the lower of the two (Dev < Fast < Exact - the slower PC decides)"""
    r = lambda m: UVM2_RANK.index(m) if m in UVM2_RANK else 0
    return UVM2_RANK[max(r(a), r(b))]


def uvm2_from_accept(why):
    """the agreed mode in player 2's accept, 'exact' if it names none"""
    i = why.find(UVM2_TAG)
    m = why[i + len(UVM2_TAG):].split(b'\x00')[0].decode(errors='replace') if i >= 0 else 'exact'
    return m if m in UVM2_RANK else 'exact'


def arch():
    """this PC's processor type as the PiTrex JIT sees it: 'x64', 'arm64' (or what the system says)"""
    import platform
    m = platform.machine().lower()
    return 'x64' if m in ('amd64', 'x86_64', 'x64') else 'arm64' if m in ('arm64', 'aarch64') else m


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
        self.wait_t0 = None                             # (since when the game waits for the other side's input)
        self.left = False                               # (the other side said it leaves)
        self.pause_on, self.pause_seq, self.pause_peer = False, 0, False   # (the pause: on for either side, this side's
        self.pause_t = 0.0                                                 #  number, a change from the other side)
        self.rb = False; self.rb_ok = False; self.rb_peer = False             # (rollback: on for this game; the core can; the other side too)
        self.stream = False; self.st_fno = 0; self.st_asm = {}; self.st_q = []; self.st_last = -1; self.st_done = set()   # (line streaming, below)
        self.st_in = (0, 0, 0, 0); self.st_in_seq = None; self.st_resets = self.st_resets_done = 0
        self.st_take_seq = self.st_take_done = 0; self.st_take_val = 0; self.st_my_resets = self.st_my_take = 0; self.st_out_seq = 0
        self.st_got = self.st_lost = self.st_drop = self.st_fix = 0
        self.my_pause, self.their_pause, self.their_seq = 0, 0, None       # (each side's own: 0 playing, 1 paused / a menu,
                                                                           #  2 another app - the game stands while either is)
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
        self.gap = False
        self.rgm_peer = False; self.x_seq = 0; self.x_until = 0.0; self.x_t = 0.0; self.x_got = None   # (a switch out of a streamed
                                                        #  game: the host's word 'X' - no input frames carry it)
        self.peer_regame = False                                # (another game in a game, both: the host's pick - R / r, a
        self.rg_seq = 0; self.rg_out = None; self.rg_t = 0.0    #  flag 8 in the host's input switches both at one frame)
        self.rg_offer = None; self.rg_answer = None; self.rg_ans_out = None
        self.save_peer = False; self.sv_done = None                         # (the host's saves: player 2 takes them; the one here)
        self.rg_sv = None; self.rg_sv_need = None; self.rg_sv_got = {}; self.rg_sv_blob = None; self.rg_ans_wait = None                                        # (no connection for a while: waited for, not the end)
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
        self.dly = self.delay; self.lhave = self.delay; self.pend_fl = 0    # (rollback: this side's delay now, its inputs made up to)
        self.conf = 0; self.used = {}; self.rb_from = None; self.psum = {}  #  (the frames both are sure of; what each frame ran with)
        self.rlast = (0, 0, 0, 0); self.rlast_f = self.delay - 1

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
                elif self.locked and self.peer and src[0] == self.peer[0] and src[1] != self.peer[1] and \
                        data[4] in (ord('I'), ord('Z'), ord('S')):
                    self.peer = src                             # (its router gave it another port: followed)
                    self.log('the other side moved to %s:%d' % src)
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
                    self.delay = max(DELAY, min(12, int(rtt * 50.0 + 0.999) + 1 + (2 if self.relay else 0)))   # (relayed: 2 frames more room)
                    game = dict(self.offer or game, delay=self.delay, rtt_ms=round(rtt * 1000), relay=self.relay)
                    self.reset()                        # (the first frames for this delay: the same on both sides)
                    self.log('round trip %.0f ms (%s%s): %d frames of input delay' % (rtt * 1000, getattr(self, 'ping_note', ''), ', relayed' if self.relay else '', self.delay))
                    msg = json.dumps(game).encode()
                    for _ in range(int(5.0 / 0.05)):    # (the game, until it is acknowledged)
                        self._send(ord('G'), msg)
                        time.sleep(0.05)
                        q = self._recv()
                        if q and q[0] == ord('A'):
                            game['jit_fast'] = JIT_FAST in q[1]   # (player 2 has the same processor type)
                            self.peer_regame = REGAME_TAG in q[1]    # (it can follow the host to another game)
                            self.rgm_peer = RGMODE_TAG in q[1]       # (to and from a card game too)
                            game['uvm2_net'] = uvm2_from_accept(q[1])   # (the UVMC2 mode both play in)
                            self.save_peer = SAVE_TAG in q[1]        # (it takes the host's saves)
                            self.rb_peer = RB_TAG in q[1]            # (it runs on guesses too: both a short delay)
                            self.stream = bool(game.get('card')) and STREAM_TAG in q[1]   # (a card game: streamed)
                            return game
                        if q and q[0] == ord('N') and q[1] == VERSION_NO:
                            return 'version'
                        if q and q[0] == ord('N') and q[1] == BIOS_NO:
                            return 'bios'
                        if q and q[0] == ord('N'):      # (no: it does not have the ROM)
                            self.log('player 2 does not have the game: ' + q[1].decode(errors='replace'))
                            return 'missing'
                    return None
                self._keepalive()
                time.sleep(0.005 if self.candidates else 0.02)
            return None
        last_h = 0.0
        t_join = time.perf_counter(); self.last_heard = t_join
        while time.perf_counter() - t0 < timeout and not stop():
            if time.perf_counter() - max(t_join, self.last_heard) > JOIN_SILENT:   # (the host gone before the game
                self.log('the host does not answer'); return None                  #  started: no waiting for minutes)
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
                    if not self.peer_name and g.get('host_name'):   # (a direct game - a LAN: the host's name comes with
                        self.peer_name = gamer_name(str(g['host_name'])) or ''   #  its game, as the joiner's with its hello)
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
        if since < PUNCH_S + 1.0 or not (self.last_direct or self.last_relayed):   # (nothing back yet: go on trying
            #  both ways - the relayed hellos only in the window lost ~1 in 5 relay-only connections, 2026-11-02)
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
        """the round trip to the other side (the slowest of n pings, as a connection's jitter needs that much). An answer
        counts also after the next ping went (it carries its send time): through a hotel's relay every answer took over
        0.5 s, none counted and 100 ms was guessed - the game then ran at a third of its speed. Fewer than 4 answers: on,
        up to 24 pings; none at all: a slow path (0.3 s) assumed"""
        got, seen, i = [], set(), 0
        while i < (n if len(got) >= 4 else 24) and len(got) < n:
            t = time.perf_counter()
            self._send(ord('P'), struct.pack('<Id', i, t))
            while time.perf_counter() - t < 0.5:
                q = self._recv()
                if q and q[0] == ord('p') and len(q[1]) >= 12:
                    k, t0 = struct.unpack_from('<Id', q[1])
                    if k <= i and k not in seen:
                        seen.add(k); got.append(time.perf_counter() - t0)
                        if k == i:
                            break
                elif q and q[0] in (ord('H'), ord('h')):
                    pass
                else:
                    time.sleep(0.001)
            i += 1
        self.ping_note = '%d of %d answered' % (len(got), i)
        return max(got) if got else 0.3

    def regame_offer(self, game, save=None):
        """host: another game for both (player 2 answers: rg_answer True / False); save: (package, its number) sent along"""
        self.rg_seq = (self.rg_seq + 1) & 0xFFFF; self.rg_out = json.dumps(game).encode(); self.rg_t = 0.0; self.rg_answer = None
        self.rg_sv = save if (save and self.save_peer) else None
    def regame_reply(self, ok):
        """player 2: the host's next game here (or not) - said until the switch"""
        if self.rg_offer is None:
            return
        seq = self.rg_offer[0]; self.rg_offer = None
        pk = struct.pack('<HB', seq, 1 if ok else 0)
        if ok and self.rg_sv_need and self.rg_sv_blob is None:   # (the host's saves for it: the yes once they are here)
            self.rg_ans_wait = pk; return
        for _ in range(5): self._send(ord('r'), pk)
        self.rg_ans_out = pk if ok else None; self.rg_t = 0.0
    def regame_tick(self):
        """(each frame / idle: the offer or the answer sent again until it is taken)"""
        now = time.perf_counter()
        if now - self.rg_t < 0.1:
            return
        self.rg_t = now
        if self.rg_out is not None:
            self._send(ord('R'), struct.pack('<H', self.rg_seq) + self.rg_out[:590])
            if self.rg_sv is not None:                      # (the host's saves for it: all pieces, until the yes)
                blob, sid = self.rg_sv; parts = [blob[k:k + CARD_CHUNK] for k in range(0, len(blob), CARD_CHUNK)] or [b'']
                for i, part in enumerate(parts):
                    self._send(ord('W'), struct.pack('<HHHI', sid, i, len(parts), len(blob)) + part)
        if self.rg_ans_out is not None:
            self._send(ord('r'), self.rg_ans_out)
        if self.role == 'host' and now < self.x_until:
            self._send(ord('X'), struct.pack('<H', self.x_seq))

    def reset_mode(self, stream):
        """a game switch to or from a streamed game: frame 0 again on both sides, streamed or in step"""
        self.stream = bool(stream); self.reset(); self.sums_mine, self.sums_theirs = {}, {}; self.desync = None
        self.st_fno = 0; self.st_asm = {}; self.st_q = []; self.st_last = -1; self.st_in_seq = None
        if hasattr(self, 'st_done'): self.st_done = set()

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
        self._mark_send(f, state)

    def _mark_send(self, f, state):
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

    # ---- the host's saves (after the game was accepted)
    def send_save(self, blob, sid, timeout=30.0, stop=lambda: False):
        """host: its package to player 2, in pieces until player 2 says it has it (True)"""
        parts = [blob[k:k + CARD_CHUNK] for k in range(0, len(blob), CARD_CHUNK)] or [b'']
        t0 = time.perf_counter(); i = 0
        while time.perf_counter() - t0 < timeout and not stop():
            self._send(ord('W'), struct.pack('<HHHI', sid, i, len(parts), len(blob)) + parts[i])
            i = (i + 1) % len(parts)
            time.sleep(0.004)
            p = self._recv()
            while p:
                if p[0] == ord('w') and len(p[1]) >= 2 and struct.unpack_from('<H', p[1])[0] == sid:
                    return True
                p = self._recv()
        return False

    def _save_piece(self, body, size, sid, got):
        """a piece of the host's package: the whole once all are here, else None"""
        if len(body) < 10:
            return None
        i, k, t, n = struct.unpack_from('<HHHI', body)      # (its number, the piece, of how many, the whole's size)
        if i != sid or n != size or n > SAVE_MAX or t != (n + CARD_CHUNK - 1) // CARD_CHUNK or k >= t:
            return None
        got[k] = body[10:]
        if len(got) < t:
            return None
        blob = b''.join(got[q] for q in range(t)); got.clear()
        if len(blob) != n or blob[:4] != b'VXS1':
            self.log('the host\'s saves arrived damaged: collecting them again'); return None
        for _ in range(3):
            self._send(ord('w'), struct.pack('<H', sid))
        self.sv_done = sid
        return blob

    def recv_save(self, size, sid, timeout=30.0, stop=lambda: False):
        """player 2: the host's package (None: it did not come)"""
        got = {}; t0 = time.perf_counter()
        while time.perf_counter() - t0 < timeout and not stop():
            p = self._recv()
            while p:
                if p[0] == ord('W'):
                    blob = self._save_piece(p[1], size, sid, got)
                    if blob is not None:
                        return blob
                p = self._recv()
            time.sleep(0.002)
        return None

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
        t_wait = t_out = time.perf_counter()
        while f not in self.remote:
            self.wait_t0 = t_out
            if self.lost:                                # (closed meanwhile: the window shut, or the other side left)
                self.wait_t0 = None
                return None
            now = time.perf_counter()
            if self.pause_on:                            # (a pause both share: its time is no wait - else the moment it
                t_out = now                              #  ends, before the next input comes, it read as 30 s of nothing)
            if now - self.last_send > 0.010:
                self._send_inputs()
            if now - self.pause_t > 1.0:                 # (this side's pause state now and then: a lost one is sent again)
                self._send_pause()
            self._drain(); self.regame_tick()
            if f in self.remote:
                break
            if (now - self.last_heard > TIMEOUT or (now - t_out > TIMEOUT and not self.pause_on)) and not self.gap:
                self.gap = True                          # (nothing from the other side - or no new input: a gap,
                self.log('no connection (no input for frame %d) - waiting' % f)   #  waited out; only Leave ends)
            time.sleep(0.002 if self.gap else 0.0005)
        self.wait_t0 = None
        if self.gap:
            self.gap = False; self.log('connected again')
        self.stall_s += time.perf_counter() - t_wait
        self._send_inputs()
        a, b = self.local[f], self.remote.pop(f)     # (this side's own input kept: every packet repeats the last KEEP
        self.f += 1                                  #  frames, and the other side - a few frames behind - may have
        for old in [k for k in self.local if k < f - KEEP - 4]:   #  missed them in a drop: sent again as they were,
            del self.local[old]                      #  never as 'no input' - a desync after a drop, 2026-10-29)
        for old in [k for k in self.remote if k < f - 64]:
            del self.remote[old]
        return (a, b) if self.role == 'host' else (b, a)

    # ---- rollback (as the apps' netvx.c): a frame whose other-side input is late runs at once on a guess (that side's last
    #      input, no moment's button), its state kept in the core (live.c 0x7C); the real input different: the Runner loads
    #      that frame's state (0x7D) and runs the frames since again, unseen (0x7B). The sync check: frames both are sure of
    def _rb_advance(self):
        while self.conf < self.f:
            r = self.remote.get(self.conf)
            if r is None:
                break
            u = self.used.get(self.conf)
            if u and u[1] and u[0] != r:                         # (a wrong guess: the rollback first)
                if self.rb_from is None or self.conf < self.rb_from:
                    self.rb_from = self.conf
                break
            ps = self.psum.pop(self.conf, None)
            if ps is not None:
                self._mark_send(self.conf, ps)
            self.conf += 1

    def _rb_take(self, f):
        """frame f's two inputs (the other's real or guessed); (pair, guessed, sure) - None: it must wait"""
        got = f in self.remote
        theirs = self.remote[f] if got else (self.rlast[0], self.rlast[1], self.rlast[2], 0)
        mine = self.local[f]
        if ((mine[3] | theirs[3]) & 8) and not (got and self.conf == f):   # (a game switch: only on a frame both are sure of)
            return None
        self.used[f] = (theirs, not got)
        sure = got and self.conf == f
        if sure:
            self.conf += 1
        return ((mine, theirs) if self.role == 'host' else (theirs, mine)), not got, sure

    def exchange_rb(self, mine):
        """rollback's exchange: (pair, guessed, sure) for the frame now - 'wait' when it may not run yet (too far on
        guesses, paused, a rollback due first), None when the connection is lost"""
        if self.lost:
            return None
        f = self.f
        if self.rb_peer and self.dly > 2:
            self.dly = 2                                         # (both with rollback: 2 frames of input delay)
        self.pend_fl |= int(mine[3])
        while self.lhave <= f + self.dly:                        # (this side's input `dly` frames on, its buttons once)
            self.local[self.lhave] = (int(mine[0]), int(mine[1]), int(mine[2]), self.pend_fl); self.pend_fl = 0; self.lhave += 1
        now = time.perf_counter()
        if now - self.last_send > 0.008:
            self._send_inputs()
        if now - self.pause_t > 1.0:
            self._send_pause()
        self._drain(); self.regame_tick(); self._rb_advance()
        if self.rb_from is not None:
            return 'wait'
        if f not in self.remote and (self.pause_on or f - self.conf >= RB_W or self.rlast_f < 0 or not self.rb_ok):
            if now - self.last_heard > TIMEOUT and not self.gap:
                self.gap = True; self.log('no connection (no input for frame %d) - waiting' % f)
            return 'wait'
        if self.gap:
            self.gap = False; self.log('connected again')
        r = self._rb_take(f)
        if r is None:
            return 'wait'
        self.f += 1
        if f not in self.remote:
            self.n_guess = getattr(self, 'n_guess', 0) + 1
        for old in [k for k in self.local if k < self.conf - KEEP - 8]:
            del self.local[old]
        for old in [k for k in self.remote if k < self.conf - 64]:
            del self.remote[old]
        for old in [k for k in self.used if k < self.conf - 64]:
            del self.used[old]
        return r

    def rb_begin(self):
        """a rollback: (the frame to go back to, the frame it was at) - the Runner loads its state and runs them again"""
        g, end = self.rb_from, self.f
        self.rb_from = None; self.f = g; self.n_rb = getattr(self, 'n_rb', 0) + 1
        return g, end

    def rb_next(self):
        """a frame run again: its inputs now (as rb_take)"""
        r = self._rb_take(self.f)
        if r is not None:
            self.f += 1
        return r

    def mark_rb(self, f, state, sure):
        if f % SYNC_EVERY:
            return
        if sure:
            self._mark_send(f, state)
        else:
            self.psum[f] = state & 0xffffffff                    # (run on a guess: sent once sure)

    # ---- line streaming: the host's side
    def stream_send(self, payload):
        """the host: a frame (the core's packed lines and sound) to player 2 - in pieces, a repair piece after each four"""
        if self.lost:
            return
        z = b'\x00' + payload                                  # (0: not packed further)
        C = 560 if (self.relay and self.code) else 1180
        nd = (len(z) + C - 1) // C; ng = (nd + ST_GROUP - 1) // ST_GROUP
        if nd + ng > 240:
            return
        fno = struct.unpack_from('<I', payload)[0]
        head = lambda i: struct.pack('<IHHIIH', fno, i, nd, len(z), len(payload), C)
        for g in range(ng):
            par = 0
            for i in range(g * ST_GROUP, min(nd, (g + 1) * ST_GROUP)):
                piece = z[i * C:(i + 1) * C]
                par ^= int.from_bytes(piece.ljust(C, b'\x00'), 'little')   # (the four XORed: whole numbers, fast)
                self._send(ord('L'), head(i) + piece)
            self._send(ord('L'), head(nd + g) + par.to_bytes(C, 'little'))
        now = time.perf_counter()
        if now - self.last_send > 0.25:                         # (alive, the pause - no inputs are sent while streamed)
            self._send_pause(); self.last_send = now
        if self.chat_out and now - self.chat_t > 0.15:          # (the chat: until confirmed - no inputs carry it here)
            self._send_chat(now)
        self._drain(); self.regame_tick()

    def st_input(self):
        """the host: player 2's controller now (its RESET and a port taken once each)"""
        jx, jy, b, _ = self.st_in; fl = 0
        if self.st_resets != self.st_resets_done:
            self.st_resets_done = self.st_resets; fl |= 4
        if self.st_take_seq != self.st_take_done:
            self.st_take_done = self.st_take_seq; fl |= 0x10 if self.st_take_val == 1 else 0x20
        return (jx, jy, b, fl)

    # ---- line streaming: player 2's side
    def _st_piece(self, body):
        if len(body) < 19:
            return
        fno, i, nd, zn, rawn, C = struct.unpack_from('<IHHIIH', body)
        ng = (nd + ST_GROUP - 1) // ST_GROUP
        if fno <= self.st_last or fno in self.st_done or nd <= 0 or i >= nd + ng or C < 64 or C > 1180 or zn > nd * C:
            return                                              # (too late, or whole already: a repair piece after it)
        a = self.st_asm.get(fno)
        if a is None:
            if len(self.st_asm) >= 6:                           # (the oldest unfinished one given up)
                old = min(self.st_asm); del self.st_asm[old]; self.st_lost += 1
            a = self.st_asm[fno] = dict(nd=nd, C=C, zn=zn, rawn=rawn, d={}, p={})
        if a['nd'] != nd or a['C'] != C:
            return
        data = body[18:]
        if i < nd:
            a['d'][i] = data.ljust(C, b'\x00')
        else:
            a['p'][i - nd] = data.ljust(C, b'\x00')
        for g in range(ng):                                     # (a group with one missing and its repair piece: made again)
            if g not in a['p']:
                continue
            ks = [k for k in range(g * ST_GROUP, min(nd, (g + 1) * ST_GROUP)) if k not in a['d']]
            if len(ks) == 1:
                r = int.from_bytes(a['p'][g], 'little')
                for k in range(g * ST_GROUP, min(nd, (g + 1) * ST_GROUP)):
                    if k != ks[0]:
                        r ^= int.from_bytes(a['d'][k], 'little')
                a['d'][ks[0]] = r.to_bytes(C, 'little'); self.st_fix += 1
        if len(a['d']) < nd:
            return
        del self.st_asm[fno]; self.st_done.add(fno)
        if len(self.st_done) > 64:
            self.st_done = {k for k in self.st_done if k > fno - 32}
        z = b''.join(a['d'][k] for k in range(nd))[:zn]
        payload = (unlz(z[1:], rawn) if z[0] == 1 else z[1:1 + rawn]) if z else None
        if not payload or len(payload) != rawn:
            self.st_lost += 1; return
        self.st_q.append((fno, payload)); self.st_q.sort(); self.st_got += 1
        for old in [k for k in self.st_asm if k < fno - 8]:
            del self.st_asm[old]; self.st_lost += 1

    def st_take(self, behind):
        """player 2: the next frame's packed data (None: none here) - more than 3 waiting: the oldest left out; behind:
        the time to show has passed, a second one may go too"""
        self._drain()
        while len(self.st_q) > 3:
            self.st_q.pop(0); self.st_drop += 1
        if not self.st_q:
            return None
        fno, payload = self.st_q.pop(0); self.st_last = fno
        return payload

    def st_send_input(self, jx, jy, b, fl):
        """player 2: its controller to the host - RESET and a port taken as counters (never lost)"""
        if fl & 4:
            self.st_my_resets = (self.st_my_resets + 1) & 255
        if fl & 0x30:
            self.st_my_take = (self.st_my_take + 1) & 255; self.st_take_val = 1 if fl & 0x10 else 2
        self.st_out_seq = (self.st_out_seq + 1) & 0xFFFFFFFF
        self._send(ord('u'), struct.pack('<IbbBBBBB', self.st_out_seq, max(-127, min(127, jx)), max(-127, min(127, jy)), b & 255,
                                         self.st_my_resets, self.st_my_take, self.st_take_val, 0))
        now = time.perf_counter()
        if now - self.pause_t > 1.0:
            self._send_pause()
        if self.chat_out and now - self.chat_t > 0.15:          # (the chat: until confirmed)
            self._send_chat(now)
        self.regame_tick()

    # ---- the pause: each side's own (P, a menu, another app on a phone); the game stands while either side is paused -
    #      one side going on no longer ends the other's (the apps' netvx.c alike)
    def set_pause(self, on, why=1):
        w = (why or 1) if on else 0
        if w == self.my_pause:
            return
        self.my_pause = w
        self.pause_seq = (self.pause_seq + 1) & 0xFFFF
        self.pause_on = bool(self.my_pause or self.their_pause)
        self._send_pause()

    def _send_pause(self):
        self._send(ord('Z'), struct.pack('<BHB', 1 if self.my_pause else 0, self.pause_seq, self.my_pause))
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
        self._drain(); self.regame_tick()
        self.gap = now - self.last_heard > TIMEOUT             # (nothing heard while paused: a gap, waited out)

    def out_s(self):
        """how long the connection has been gone (0: it is not) - the game waits for the other side longer than
        OUT_GRACE, or has heard nothing from it (not at the very start: the two power on)"""
        now = time.perf_counter()
        if self.lost or self.f < 100:
            return 0.0
        w = self.wait_t0
        s = (now - w) if (w is not None and not self.pause_on) else 0.0
        s = max(s, now - self.last_heard)
        return s if s > OUT_GRACE else 0.0

    def pace_s(self):
        """the clock sync: seconds to add to this frame. The side ahead of the other slows down a little, so neither
        PC's clock runs into the other's and waits (a stutter). Each side sees itself ahead by the one-way delay too:
        half the difference of the two is how far ahead it really is"""
        ahead = (self.adv - self.adv_them) / 2.0
        if self.f < 100:
            return 0.0
        if ahead < -1.5:                                 # (behind - a moment's halt over there or here: up to 10 % faster, back in a
            return max(-0.002, (ahead + 1.5) * 0.0004)  #  few seconds - the apps' netvx_pace alike)
        if ahead <= PACE_DEAD:
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
        hi = self.lhave - 1 if getattr(self, 'rb', False) else self.f + self.delay
        lo = max(0, hi - KEEP + 1)
        body = struct.pack('<IB', lo, hi - lo + 1)
        for k in range(lo, hi + 1):
            jx, jy, b, fl = self.local.get(k, (0, 0, 0, 0))
            body += struct.pack('<bbBB', max(-127, min(127, jx)), max(-127, min(127, jy)), b & 255, fl & 255)
        body += struct.pack('<h', int(max(-2000, min(2000, self.adv * 16))))   # (the clock sync: how far ahead I see me)
        body += struct.pack('<B', max(1, min(255, getattr(self, 'dly', self.delay) or self.delay)))   # (this side's delay now)
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
                    rbm = getattr(self, 'rb', False)
                    if k >= (self.conf if rbm else self.f) and k not in self.remote:
                        inp = struct.unpack_from('<bbBB', body, 5 + 4 * i); self.remote[k] = inp
                        if rbm:                                  # (rollback: the guess for frames not here yet; a wrong one -
                            if k > self.rlast_f:                 #  back to it before the next frame)
                                self.rlast, self.rlast_f = inp, k
                            u = self.used.get(k)
                            if k < self.f and u and u[1] and u[0] != inp and (self.rb_from is None or k < self.rb_from):
                                self.rb_from = k
                dly = body[5 + 4 * n + 2] if len(body) >= 5 + 4 * n + 3 and body[5 + 4 * n + 2] else self.delay   # (the app's delay now:
                                                 #  it grows when its game stands - libretro/netvx.c)
                tf = lo + n - 1 - dly                           # (the frame the other side is on)
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
            elif kind == ord('Z') and len(body) >= 3:   # (the other side's own pause: its newest)
                on, seq = struct.unpack_from('<BH', body)
                if self.their_seq is None or (seq - self.their_seq) & 0xFFFF < 0x8000:
                    self.their_seq = seq
                    w = (body[3] or 1) if on and len(body) >= 4 else (1 if on else 0)
                    if w != self.their_pause:
                        self.their_pause = w; self.pause_peer = True
                    self.pause_on = bool(self.my_pause or self.their_pause)
            elif kind == ord('R') and len(body) > 2 and self.role != 'host':   # (the host's next game)
                seq = struct.unpack_from('<H', body)[0]
                if seq != getattr(self, 'rg_seen', None):
                    self.rg_seen = seq
                    try: self.rg_offer = (seq, json.loads(body[2:].decode('utf-8', 'replace')))
                    except ValueError: pass
                    else:
                        g = self.rg_offer[1]; self.rg_sv_got = {}; self.rg_sv_blob = None; self.rg_ans_wait = None
                        self.rg_sv_need = (int(g['sv']), int(g.get('svi', 0))) if g.get('sv') else None
            elif kind == ord('X') and self.stream and self.role != 'host' and len(body) >= 2:   # (the host switched out of its streamed game)
                if struct.unpack_from('<H', body)[0] == getattr(self, 'rg_seen', None):
                    self.x_got = struct.unpack_from('<H', body)[0]
            elif kind == ord('L') and self.stream and self.role != 'host':
                self._st_piece(body)
            elif kind == ord('u') and self.stream and self.role == 'host' and len(body) >= 10:   # (player 2's controller: the newest)
                s_, jx, jy, b_, rs, ts, tv = struct.unpack_from('<IbbBBBB', body)
                if self.st_in_seq is None or 0 < (s_ - self.st_in_seq) & 0xFFFFFFFF < 0x80000000:
                    self.st_in_seq = s_; self.st_in = (jx, jy, b_, 0); self.st_resets = rs
                    if ts != self.st_take_seq:
                        self.st_take_seq, self.st_take_val = ts, tv
            elif kind == ord('W') and self.role != 'host' and len(body) >= 2:   # (the host's saves: a piece)
                sid = struct.unpack_from('<H', body)[0]
                if sid == self.sv_done:                      # (the host did not hear the yes: again)
                    self._send(ord('w'), struct.pack('<H', sid))
                elif self.rg_sv_need and sid == self.rg_sv_need[1] and self.rg_sv_blob is None:
                    blob = self._save_piece(body, self.rg_sv_need[0], sid, self.rg_sv_got)
                    if blob is not None:
                        self.rg_sv_blob = blob
                        if self.rg_ans_wait is not None:    # (the yes held for it: now)
                            pk, self.rg_ans_wait = self.rg_ans_wait, None
                            for _ in range(5): self._send(ord('r'), pk)
                            self.rg_ans_out = pk; self.rg_t = 0.0
            elif kind == ord('r') and len(body) >= 3 and self.role == 'host':   # (player 2: it has it, or not)
                seq, ok = struct.unpack_from('<HB', body)
                if self.rg_out is not None and seq == self.rg_seq:
                    self.rg_out = None; self.rg_answer = bool(ok)
            elif kind == ord('Q'):                       # (the other side quit)
                self.lost = True; self.left = True
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


def gamer_name(s, own=False):
    """a gamer name as the game room keeps it (upper case, 1-10 characters: it fits above a port pictogram), or None.
    own: a name this player chooses - A-Z and 0-9 only (the owner, 2026-11-19: easy to read in the Vectrex letters, to
    type on a phone or with a controller, no look-alikes); anything else is left out. Without own: another player's,
    as the game room has it (an earlier version's may still hold _ or -)"""
    s = str(s or '').strip().upper()
    if own:
        s = ''.join(c for c in s if 'A' <= c <= 'Z' or '0' <= c <= '9')
    else:
        s = s.replace(' ', '_')
    return s if 1 <= len(s) <= 10 and set(s) <= GAMER_CHARS else None


def machine_key():
    """this PC's secret for its player name, made from the machine (Windows' MachineGuid, Linux's machine-id) hashed with
    the program's own salt - the meeting point never sees the id; a fresh install on the same PC gets its name back
    (whoami). Random where the machine has no id"""
    mid = ''
    try:
        if sys.platform == 'win32':
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Cryptography', 0,
                                winreg.KEY_READ | getattr(winreg, 'KEY_WOW64_64KEY', 0)) as k:
                mid = str(winreg.QueryValueEx(k, 'MachineGuid')[0])
        else:
            for f in ('/etc/machine-id', '/var/lib/dbus/machine-id'):
                if os.path.isfile(f):
                    mid = open(f).read().strip(); break
    except (OSError, ImportError):
        mid = ''
    if not mid:
        import secrets
        return secrets.token_hex(16)
    import hashlib
    return hashlib.sha256(('vectrex_emu-player-' + mid).encode()).hexdigest()[:32]


def whoami(server, key, timeout=3.0):
    """the name the meeting point keeps for this key: the name, '' none, None no answer"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(0.5)
    t0, asked = time.perf_counter(), -1.0
    try:
        while time.perf_counter() - t0 < timeout:
            if time.perf_counter() - asked > 1.0:
                s.sendto(RMAGIC + b'W' + json.dumps(dict(key=key)).encode(), server)
                asked = time.perf_counter()
            try:
                data, _ = s.recvfrom(2048)
            except (socket.timeout, OSError):
                continue
            if data.startswith(RMAGIC + b'w'):
                try:
                    return json.loads(data[5:].decode()).get('name') or ''
                except ValueError:
                    return None
    finally:
        s.close()
    return None


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


# ---- the local network: a host announces itself, the Online window lists the games found (no meeting point, no code)
LAN_PORT = 7743                                     # (UDP: the announcements; the game itself on PORT, direct)
LAN_MAGIC = b'VXL1'
LAN_EVERY = 1.0                                     # (seconds between two announcements)
LAN_STALE = 3.5                                     # (a game not heard of for this long is gone from the list)


def lan_addresses():
    """this PC's IPv4 addresses on its networks (not loopback)"""
    out = set()
    try:
        for fam, _, _, _, sa in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            if not sa[0].startswith('127.'):
                out.add(sa[0])
    except OSError:
        pass
    try:                                            # (the address the default route goes out of)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('10.255.255.255', 1)); out.add(s.getsockname()[0]); s.close()
    except OSError:
        pass
    return sorted(a for a in out if not a.startswith('127.'))


class LanBeacon(threading.Thread):
    """a host waiting for player 2 on this network: its game announced once a second (broadcast, each network of this
    PC) until stop() - what the Online window's 'Games on this network' list shows"""
    def __init__(self, info):
        super().__init__(daemon=True)
        self.info = dict(info, nver=NET_VER)
        self.halt = threading.Event()

    def stop(self):
        self.halt.set()

    def run(self):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        except OSError:
            return
        data = LAN_MAGIC + json.dumps(self.info).encode('utf-8')
        while not self.halt.is_set():
            targets = {'255.255.255.255'}
            for a in lan_addresses():                   # (each network's own broadcast address - a /24 is assumed: the
                targets.add(a.rsplit('.', 1)[0] + '.255')   #  usual home network; 255.255.255.255 covers the rest)
            for t in targets:
                try:
                    s.sendto(data, (t, LAN_PORT))
                except OSError:
                    pass
            self.halt.wait(LAN_EVERY)
        s.close()


class LanListener(threading.Thread):
    """the games announced on this network (LanBeacon): games() - [dict(addr, port, name, md5, size, gamer, veclink,
    nver)], the newest first; several emulators on one PC can listen at once"""
    def __init__(self):
        super().__init__(daemon=True)
        self.halt = threading.Event()
        self.found = {}
        self.lock = threading.Lock()
        self.ok = True

    def stop(self):
        self.halt.set()

    def run(self):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind(('', LAN_PORT))
            s.settimeout(0.25)
        except OSError:
            self.ok = False
            return
        mine = set(lan_addresses())
        while not self.halt.is_set():
            try:
                data, (ip, _) = s.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                continue
            if not data.startswith(LAN_MAGIC):
                continue
            try:
                g = json.loads(data[len(LAN_MAGIC):].decode('utf-8'))
                port = int(g.get('port', PORT))
            except (ValueError, TypeError):
                continue
            g['addr'] = '127.0.0.1' if ip in mine else ip     # (a host on this same PC: reached on loopback)
            g['port'] = port
            g['seen'] = time.perf_counter()
            with self.lock:
                self.found['%s:%d' % (g['addr'], port)] = g
        s.close()

    def games(self):
        now = time.perf_counter()
        with self.lock:
            for k in [k for k, g in self.found.items() if now - g['seen'] > LAN_STALE]:
                del self.found[k]
            return sorted(self.found.values(), key=lambda g: -g['seen'])


def new_game(rom_path, settings):
    """the host's game: the ROM's identity and what must be the same on both sides"""
    return dict(md5=rom_md5(rom_path), size=os.path.getsize(rom_path), name=os.path.basename(rom_path),
                seed=random.getrandbits(31), nver=NET_VER, arch=arch(), regame=True, **settings)
