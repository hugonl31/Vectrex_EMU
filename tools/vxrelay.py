#!/usr/bin/env python3
"""vxrelay.py - the meeting point for Vectrex_EMU online play: a tiny UDP server (one file, no dependencies).

A host registers its game and gets a short code (five letters, e.g. KRAVE); a player joins with the code. The server
tells each side the other's address (public and LAN), both try to reach each other directly (UDP hole punching: works
through most home routers without any port forwarding), and if that fails within a few seconds the server relays
their packets (4 bytes of controller input a frame: ~10 KB/s per game).

    python3 vxrelay.py [PORT]            (default 7745)

Packets (all start with b'VXR1'):
    client -> server   R <json {name, md5, size, ver, lan: [ip, port], pub, gamer}>  register a game  -> C <code> or E <text>
                       J <code> <json {lan: [ip, port]}>               join a game          -> P <json peer> (to both) or E <text>
                       K <code>                                        keep-alive (the host, every 2 s; the code lives 15 s without)
                       X <code> <payload>                              relay this payload to the other side (-> F <payload>)
                       L <page>                                        the game room: the public games waiting -> l <json>
                       N <json {name, key, old}>                       a player name: reserved for this key -> n <json {ok, name|why}>
                       U <code> <json {name, md5, size}>               a waiting host offers another game
                       D <code>                                        a host gone: its code (and pair) gone now
    server -> client   C code | P peer | E error | F payload | k (keep-alive answer) | l {p, n, t, g: [[code, gamer,
                       name, md5, size, waiting s], ...]} (page p of n, t games in all)

The game room: a host that registers with pub = 1 and a gamer name (unique among the waiting public games, 1-12 of
A-Z 0-9 _ -) is listed until someone joins it. Caps keep the little server light: MAX_PUBLIC games listed, MAX_GAMES
codes in all, MAX_PAIRS games joined at once (a full room answers E 'the game room is full').

Player names: a name is reserved for the player whose program holds its key (a random secret kept in their
settings; the server stores only its SHA-256 and when the name was last used - no addresses). Nobody else can take a
reserved name; the game room and joining check it; a name unused for NAME_DAYS days is free again. The names live in
NAMES_FILE (systemd: StateDirectory=vxrelay).

Security: no accounts; codes come from the server (no guessing a game you were not told about: 26^5 = 12 million,
answers rate-limited); the relay forwards only inside a pair, at most 300 packets/s of at most 600 bytes each; anything
else is dropped silently. Nothing else is stored. Run as an unprivileged user (see the systemd unit in the README section
below).

Install on a Linux box (as root):
    useradd -r -s /usr/sbin/nologin vxrelay
    install -m 755 vxrelay.py /usr/local/bin/vxrelay.py
    cat > /etc/systemd/system/vxrelay.service <<'EOF'
    [Unit]
    Description=Vectrex_EMU online play relay
    After=network.target
    [Service]
    User=vxrelay
    StateDirectory=vxrelay
    ExecStart=/usr/bin/python3 /usr/local/bin/vxrelay.py 7745
    Restart=always
    RestartSec=3
    [Install]
    WantedBy=multi-user.target
    EOF
    systemctl enable --now vxrelay
    ufw allow 7745/udp

Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import hashlib
import json
import os
import random
import socket
import sys
import time

MAGIC = b'VXR1'
PORT = 7745
CODE_TTL = 15.0             # s without a keep-alive: the code is gone
PAIR_TTL = 30.0             # s without a relayed packet: the pair is gone
RELAY_MAX = 1200            # bytes of payload the relay forwards (v1.0.6 sends its SD card list in 900-byte pieces)
RELAY_RATE = 300            # packets/s per side
ALPHABET = 'ABCDEFGHJKLMNPRSTUVWXYZ'   # (no I, O, Q: nothing to misread)
MAX_PUBLIC = 100            # public games waiting in the game room at most
MAX_GAMES = 400             # codes (public and private) at most
MAX_PAIRS = 150             # games joined at once at most (each may be relayed: 150 relayed games are ~1/3 of one
                            # 2 GHz core and ~30 Mbit/s each way at worst - measured on the meeting point, 2026-10-28)
LIST_PAGE = 8               # games per answer of the list (one packet, under ~1200 bytes)
GAMER_CHARS = set('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-')
NAMES_FILE = os.environ.get('VXRELAY_NAMES', '/var/lib/vxrelay/names.json')
NAME_DAYS = 365             # (a name unused this long: free again)
MAX_NAMES = 200000          # (names kept at most)


def gamer_name(s):
    """a gamer name as the room keeps it (upper case, 1-12 of A-Z 0-9 _ -), or None"""
    s = str(s or '').strip().upper().replace(' ', '_')
    return s if 1 <= len(s) <= 12 and set(s) <= GAMER_CHARS else None


class Relay:
    def __init__(self, port, names_file=None):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('', port))
        self.names_file = names_file                # (None: the names only in memory - a test)
        self.names, self.names_dirty, self.names_saved = {}, False, 0.0   # (name -> {k: SHA-256 of the key, t: last used})
        if names_file:
            try:
                with open(names_file, encoding='utf-8') as f:
                    self.names = json.load(f)
            except (OSError, ValueError):
                self.names = {}
        self.games = {}                     # code -> dict(addr, lan, info, t, peer)
        self.pairs = {}                     # addr -> (other addr, code)
        self.rate = {}                      # addr -> [second, count]
        self.asked = {}                     # ip -> [minute, count]  (codes asked / wrong codes: rate-limited)
        self.listed = {}                    # ip -> [second, count]  (the game room's list: rate-limited)
        random.seed()

    # ---- the player names
    def key_hash(self, key):
        key = str(key or '')
        if not (16 <= len(key) <= 64) or any(c not in '0123456789abcdef' for c in key):
            return None
        return hashlib.sha256(key.encode()).hexdigest()

    def name_free(self, name, h, now):
        """may the player with this key hash use the name? (it is theirs, or nobody's)"""
        e = self.names.get(name)
        return not e or e['k'] == h or now - e['t'] > NAME_DAYS * 86400

    def name_used(self, name, h, now):
        """a name used now by its player (theirs, or taken for them if free): True, or False when it is another's"""
        if not h:
            return self.name_free(name, None, now)
        if not self.name_free(name, h, now):
            return False
        e = self.names.get(name)
        if e is None and len(self.names) >= MAX_NAMES:
            return True                                 # (the book full: the name used, not kept)
        if e is None or e['k'] != h or now - e['t'] > 86400:
            self.names[name] = dict(k=h, t=int(now)); self.names_dirty = True
        return True

    def save_names(self, now, force=False):
        if not (self.names_file and self.names_dirty) or (not force and now - self.names_saved < 30):
            return
        tmp = self.names_file + '.tmp'
        try:
            with open(tmp, 'w', encoding='utf-8') as f:
                json.dump(self.names, f, separators=(',', ':'))
            os.replace(tmp, self.names_file)
            self.names_dirty, self.names_saved = False, now
        except OSError as e:
            print('names not saved: %s' % e, flush=True); self.names_saved = now

    def code(self):
        for _ in range(100):
            c = ''.join(random.choice(ALPHABET) for _ in range(5))
            if c not in self.games:
                return c
        return None

    def send(self, addr, kind, payload=b''):
        try:
            self.sock.sendto(MAGIC + kind + payload, addr)
        except OSError:
            pass

    def limited(self, table, key, per, limit):
        now = int(time.time() // per)
        e = table.setdefault(key, [now, 0])
        if e[0] != now:
            e[0] = now; e[1] = 0
        e[1] += 1
        return e[1] > limit

    def run(self):
        last_sweep = time.time()
        while True:
            try:
                data, addr = self.sock.recvfrom(2048)   # (larger than any packet: a shorter buffer cuts them off on Linux)
            except (ConnectionResetError, OSError):
                continue
            now = time.time()
            if now - last_sweep > 5.0:
                last_sweep = now; self.sweep(now)
            try:
                self.packet(data, addr, now)
            except Exception as e:                          # (a packet nobody planned for: logged, and on with the next)
                self.bad = getattr(self, 'bad', 0) + 1
                if self.bad < 20 or self.bad % 1000 == 0:
                    print('bad packet %d from %s: %s: %s' % (self.bad, addr[0], type(e).__name__, str(e)[:120]), flush=True)

    def packet(self, data, addr, now):
        """one packet from addr (run: any exception here is caught there - one bad packet never stops the server)"""
        if len(data) < 5 or not data.startswith(MAGIC):
            return
        kind, body = data[4:5], data[5:]
        if kind == b'X':                                # relay: the hot path first
            p = self.pairs.get(addr)
            if not p and len(body) >= 6:                # (a side whose router gave it another port: the same address,
                p = self.rebind(addr, body[:5].decode('ascii', 'replace'))   #  the same code - moved)
            if p and len(body) >= 6 and body[:5].decode('ascii', 'replace') == p[1] and len(body) - 5 <= RELAY_MAX:
                if not self.limited(self.rate, addr, 1.0, RELAY_RATE):
                    self.send(p[0], b'F', body[5:]); p[2][0] = now
            return
        if kind == b'K':
            g = self.games.get(body[:5].decode('ascii', 'replace'))
            if g and g['addr'] == addr:
                g['t'] = now; self.send(addr, b'k')
            return
        if kind == b'L':                                # the game room's list: a page of the public games waiting
            if not self.limited(self.listed, addr[0], 1.0, 6):
                self.room(addr, body[0] if body else 0, now)
            return
        if kind == b'D':                                # (a host gone: its code, and its pair, gone now)
            c = body[:5].decode('ascii', 'replace')
            g = self.games.get(c)
            if g and g['addr'] == addr:
                del self.games[c]
                for a in (addr, g['peer']):
                    if a in self.pairs and self.pairs[a][1] == c:
                        del self.pairs[a]
            return
        if kind == b'U':                                # (a host waiting: another game - the room lists it at once)
            g = self.games.get(body[:5].decode('ascii', 'replace'))
            if g and g['addr'] == addr and not g['peer'] and not self.limited(self.listed, addr, 1.0, 4):
                try:
                    info = json.loads(body[5:].decode())
                    g['info'].update({k: info.get(k) for k in ('name', 'md5', 'size') if k in info})
                except (ValueError, UnicodeDecodeError, AttributeError):
                    pass
            return
        if self.limited(self.asked, addr[0], 60.0, 30):   # (registering and joining: 30 a minute per address)
            return
        if kind == b'N':                                # (a player name: reserved for this key)
            try:
                q = json.loads(body.decode())
                if not isinstance(q, dict): return
            except (ValueError, UnicodeDecodeError):
                return
            name, h = gamer_name(q.get('name')), self.key_hash(q.get('key'))
            if not name or not h:
                self.send(addr, b'n', json.dumps(dict(ok=0, why='not allowed')).encode()); return
            if not self.name_used(name, h, now):
                self.send(addr, b'n', json.dumps(dict(ok=0, why='taken')).encode()); return
            old = gamer_name(q.get('old'))
            if old and old != name and self.names.get(old, {}).get('k') == h:
                del self.names[old]; self.names_dirty = True      # (a name changed: the old one free)
            self.save_names(now, force=True)
            self.send(addr, b'n', json.dumps(dict(ok=1, name=name)).encode())
            print('name %s' % name, flush=True)
            return
        if kind == b'R':
            try:
                info = json.loads(body.decode())
                if not isinstance(info, dict): return
                lan = info.get('lan')
            except (ValueError, UnicodeDecodeError):
                return
            pub = bool(info.get('pub'))
            gamer = gamer_name(info.get('gamer')) if pub else None
            if pub and not gamer:
                self.send(addr, b'E', b'that gamer name is not allowed'); return
            if pub and not self.name_used(gamer, self.key_hash(info.get('key')), now):
                self.send(addr, b'E', b'that name belongs to another player'); return
            mine = [c for c, g in self.games.items() if g['addr'] == addr and not g['peer']]
            if mine:                                    # (asked again - the answer lost: the same code)
                self.send(addr, b'C', mine[0].encode()); return
            if pub and any(g.get('gamer') == gamer for g in self.games.values() if g['pub'] and not g['peer']):
                self.send(addr, b'E', b'name taken'); return
            if len(self.games) >= MAX_GAMES or (pub and sum(1 for g in self.games.values() if g['pub'] and not g['peer'])
                                                >= MAX_PUBLIC):
                self.send(addr, b'E', b'the game room is full'); return
            c = self.code()
            if c:
                self.games[c] = dict(addr=addr, lan=lan, info={k: info.get(k) for k in ('name', 'md5', 'size', 'ver')}, t=now,
                                     since=now, peer=None, pub=pub, gamer=gamer)
                self.send(addr, b'C', c.encode())
                print('%s host %s:%d %s%s' % (c, addr[0], addr[1], info.get('name', ''), ' (public: %s)' % gamer if pub else ''),
                      flush=True)
        elif kind == b'J':
            c = body[:5].decode('ascii', 'replace').upper()
            g = self.games.get(c)
            if not g:
                self.send(addr, b'E', b'no such game'); return
            if g['peer'] and g['peer'] != addr:
                self.send(addr, b'E', b'someone already joined'); return
            if not g['peer'] and len(self.pairs) >= 2 * MAX_PAIRS:
                self.send(addr, b'E', b'the game room is full'); return
            try:
                jq = json.loads(body[5:].decode() or '{}')
                if not isinstance(jq, dict): jq = {}
                lan = jq.get('lan')
            except (ValueError, UnicodeDecodeError, AttributeError):
                jq, lan = {}, None
            jname = gamer_name(jq.get('name'))           # (the joiner's name: told to the host when it is theirs)
            if jname and not self.name_used(jname, self.key_hash(jq.get('key')), now):
                jname = None
            g['peer'] = addr
            self.pairs[addr] = [g['addr'], c, [now]]
            self.pairs[g['addr']] = [addr, c, [now]]
            host = dict(role='host', pub=list(g['addr']), lan=g['lan'], game=g['info'], gamer=g.get('gamer'))
            join = dict(role='join', pub=list(addr), lan=lan, name=jname)
            self.send(addr, b'P', json.dumps(host).encode())
            self.send(g['addr'], b'P', json.dumps(join).encode())
            print('%s join %s:%d' % (c, addr[0], addr[1]), flush=True)

    def rebind(self, addr, code):
        """a pair's side back with another port (its router, after a drop): its entries moved to the new address"""
        old = next((a for a, p in self.pairs.items() if p[1] == code and a[0] == addr[0] and a != addr), None)
        if old is None:
            return None
        p = self.pairs.pop(old)
        self.pairs[addr] = p
        o = self.pairs.get(p[0])
        if o is not None and o[0] == old:
            o[0] = addr
        print('%s moved %s:%d -> %s:%d' % (code, old[0], old[1], addr[0], addr[1]), flush=True)
        return p

    def room(self, addr, page, now):
        """the public games waiting (the oldest first): page `page` of them"""
        games = sorted(((g['since'], c, g) for c, g in self.games.items() if g['pub'] and not g['peer']))
        n = max(1, (len(games) + LIST_PAGE - 1) // LIST_PAGE)
        page = min(page, n - 1)
        rows = [[c, g['gamer'], str(g['info'].get('name') or '')[:40], g['info'].get('md5'), g['info'].get('size'),
                 int(now - g['since']), g['info'].get('ver') or 1] for _, c, g in games[page * LIST_PAGE:(page + 1) * LIST_PAGE]]
        self.send(addr, b'l', json.dumps(dict(p=page, n=n, t=len(games), g=rows), separators=(',', ':')).encode())

    def sweep(self, now):
        self.save_names(now)
        for c in [c for c, g in self.games.items() if now - g['t'] > CODE_TTL]:
            del self.games[c]
        for a in [a for a, p in self.pairs.items() if now - p[2][0] > PAIR_TTL]:
            del self.pairs[a]
        for t in (self.rate, self.asked, self.listed):
            for k in [k for k, e in t.items() if abs(now - e[0] * (60.0 if t is self.asked else 1.0)) > 120]:
                del t[k]


if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else PORT
    print('vxrelay on UDP %d' % port, flush=True)
    Relay(port, NAMES_FILE).run()
