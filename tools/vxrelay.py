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
    server -> client   C code | P peer | E error | F payload | k (keep-alive answer) | l {p, n, t, g: [[code, gamer,
                       name, md5, size, waiting s], ...]} (page p of n, t games in all)

The game room: a host that registers with pub = 1 and a gamer name (unique among the waiting public games, 1-12 of
A-Z 0-9 _ -) is listed until someone joins it. Caps keep the little server light: MAX_PUBLIC games listed, MAX_GAMES
codes in all, MAX_PAIRS games joined at once (a full room answers E 'the game room is full').

Security: no accounts; codes come from the server (no guessing a game you were not told about: 26^5 = 12 million,
answers rate-limited); the relay forwards only inside a pair, at most 300 packets/s of at most 600 bytes each; anything
else is dropped silently. Nothing is stored. Run as an unprivileged user (see the systemd unit in the README section
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
    ExecStart=/usr/bin/python3 /usr/local/bin/vxrelay.py 7745
    Restart=always
    RestartSec=3
    [Install]
    WantedBy=multi-user.target
    EOF
    systemctl enable --now vxrelay
    ufw allow 7745/udp

Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import json
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


def gamer_name(s):
    """a gamer name as the room keeps it (upper case, 1-12 of A-Z 0-9 _ -), or None"""
    s = str(s or '').strip().upper().replace(' ', '_')
    return s if 1 <= len(s) <= 12 and set(s) <= GAMER_CHARS else None


class Relay:
    def __init__(self, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('', port))
        self.games = {}                     # code -> dict(addr, lan, info, t, peer)
        self.pairs = {}                     # addr -> (other addr, code)
        self.rate = {}                      # addr -> [second, count]
        self.asked = {}                     # ip -> [minute, count]  (codes asked / wrong codes: rate-limited)
        self.listed = {}                    # ip -> [second, count]  (the game room's list: rate-limited)
        random.seed()

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
            if len(data) < 5 or not data.startswith(MAGIC):
                continue
            kind, body = data[4:5], data[5:]
            if kind == b'X':                                # relay: the hot path first
                p = self.pairs.get(addr)
                if p and len(body) >= 6 and body[:5].decode('ascii', 'replace') == p[1] and len(body) - 5 <= RELAY_MAX:
                    if not self.limited(self.rate, addr, 1.0, RELAY_RATE):
                        self.send(p[0], b'F', body[5:]); p[2][0] = now
                continue
            if kind == b'K':
                g = self.games.get(body[:5].decode('ascii', 'replace'))
                if g and g['addr'] == addr:
                    g['t'] = now; self.send(addr, b'k')
                continue
            if kind == b'L':                                # the game room's list: a page of the public games waiting
                if not self.limited(self.listed, addr[0], 1.0, 6):
                    self.room(addr, body[0] if body else 0, now)
                continue
            if kind == b'D':                                # (a host gone: its code, and its pair, gone now)
                c = body[:5].decode('ascii', 'replace')
                g = self.games.get(c)
                if g and g['addr'] == addr:
                    del self.games[c]
                    for a in (addr, g['peer']):
                        if a in self.pairs and self.pairs[a][1] == c:
                            del self.pairs[a]
                continue
            if kind == b'U':                                # (a host waiting: another game - the room lists it at once)
                g = self.games.get(body[:5].decode('ascii', 'replace'))
                if g and g['addr'] == addr and not g['peer'] and not self.limited(self.listed, addr, 1.0, 4):
                    try:
                        info = json.loads(body[5:].decode())
                        g['info'].update({k: info.get(k) for k in ('name', 'md5', 'size') if k in info})
                    except (ValueError, UnicodeDecodeError, AttributeError):
                        pass
                continue
            if self.limited(self.asked, addr[0], 60.0, 30):   # (registering and joining: 30 a minute per address)
                continue
            if kind == b'R':
                try:
                    info = json.loads(body.decode())
                    lan = info.get('lan')
                except (ValueError, UnicodeDecodeError):
                    continue
                pub = bool(info.get('pub'))
                gamer = gamer_name(info.get('gamer')) if pub else None
                if pub and not gamer:
                    self.send(addr, b'E', b'that gamer name is not allowed'); continue
                mine = [c for c, g in self.games.items() if g['addr'] == addr and not g['peer']]
                if mine:                                    # (asked again - the answer lost: the same code)
                    self.send(addr, b'C', mine[0].encode()); continue
                if pub and any(g.get('gamer') == gamer for g in self.games.values() if g['pub'] and not g['peer']):
                    self.send(addr, b'E', b'name taken'); continue
                if len(self.games) >= MAX_GAMES or (pub and sum(1 for g in self.games.values() if g['pub'] and not g['peer'])
                                                    >= MAX_PUBLIC):
                    self.send(addr, b'E', b'the game room is full'); continue
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
                    self.send(addr, b'E', b'no such game'); continue
                if g['peer'] and g['peer'] != addr:
                    self.send(addr, b'E', b'someone already joined'); continue
                if not g['peer'] and len(self.pairs) >= 2 * MAX_PAIRS:
                    self.send(addr, b'E', b'the game room is full'); continue
                try:
                    lan = json.loads(body[5:].decode() or '{}').get('lan')
                except (ValueError, UnicodeDecodeError):
                    lan = None
                g['peer'] = addr
                self.pairs[addr] = [g['addr'], c, [now]]
                self.pairs[g['addr']] = [addr, c, [now]]
                host = dict(role='host', pub=list(g['addr']), lan=g['lan'], game=g['info'], gamer=g.get('gamer'))
                join = dict(role='join', pub=list(addr), lan=lan)
                self.send(addr, b'P', json.dumps(host).encode())
                self.send(g['addr'], b'P', json.dumps(join).encode())
                print('%s join %s:%d' % (c, addr[0], addr[1]), flush=True)

    def room(self, addr, page, now):
        """the public games waiting (the oldest first): page `page` of them"""
        games = sorted(((g['since'], c, g) for c, g in self.games.items() if g['pub'] and not g['peer']))
        n = max(1, (len(games) + LIST_PAGE - 1) // LIST_PAGE)
        page = min(page, n - 1)
        rows = [[c, g['gamer'], str(g['info'].get('name') or '')[:40], g['info'].get('md5'), g['info'].get('size'),
                 int(now - g['since']), g['info'].get('ver') or 1] for _, c, g in games[page * LIST_PAGE:(page + 1) * LIST_PAGE]]
        self.send(addr, b'l', json.dumps(dict(p=page, n=n, t=len(games), g=rows), separators=(',', ':')).encode())

    def sweep(self, now):
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
    Relay(port).run()
