#!/usr/bin/env python3
"""vxrelay.py - the meeting point for Vectrex_EMU online play: a tiny UDP server (one file, no dependencies).

A host registers its game and gets a short code (five letters, e.g. KRAVE); a player joins with the code. The server
tells each side the other's address (public and LAN), both try to reach each other directly (UDP hole punching: works
through most home routers without any port forwarding), and if that fails within a few seconds the server relays
their packets (4 bytes of controller input a frame: ~10 KB/s per game).

    python3 vxrelay.py [PORT]            (default 7745)

Packets (all start with b'VXR1'):
    client -> server   R <json {name, md5, size, ver, lan: [ip, port]}>  register a game      -> C <code>
                       J <code> <json {lan: [ip, port]}>               join a game          -> P <json peer> (to both) or E <text>
                       K <code>                                        keep-alive (the host, every 2 s; the code lives 15 s without)
                       X <code> <payload>                              relay this payload to the other side (-> F <payload>)
    server -> client   C code | P peer | E error | F payload | k (keep-alive answer)

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


class Relay:
    def __init__(self, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('', port))
        self.games = {}                     # code -> dict(addr, lan, info, t, peer)
        self.pairs = {}                     # addr -> (other addr, code)
        self.rate = {}                      # addr -> [second, count]
        self.asked = {}                     # ip -> [minute, count]  (codes asked / wrong codes: rate-limited)
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
            if self.limited(self.asked, addr[0], 60.0, 30):   # (registering and joining: 30 a minute per address)
                continue
            if kind == b'R':
                try:
                    info = json.loads(body.decode())
                    lan = info.get('lan')
                except (ValueError, UnicodeDecodeError):
                    continue
                c = self.code()
                if c:
                    self.games[c] = dict(addr=addr, lan=lan, info={k: info.get(k) for k in ('name', 'md5', 'size', 'ver')}, t=now, peer=None)
                    self.send(addr, b'C', c.encode())
                    print('%s host %s:%d %s' % (c, addr[0], addr[1], info.get('name', '')), flush=True)
            elif kind == b'J':
                c = body[:5].decode('ascii', 'replace').upper()
                g = self.games.get(c)
                if not g:
                    self.send(addr, b'E', b'no such game'); continue
                if g['peer'] and g['peer'] != addr:
                    self.send(addr, b'E', b'someone already joined'); continue
                try:
                    lan = json.loads(body[5:].decode() or '{}').get('lan')
                except (ValueError, UnicodeDecodeError):
                    lan = None
                g['peer'] = addr
                self.pairs[addr] = [g['addr'], c, [now]]
                self.pairs[g['addr']] = [addr, c, [now]]
                host = dict(role='host', pub=list(g['addr']), lan=g['lan'], game=g['info'])
                join = dict(role='join', pub=list(addr), lan=lan)
                self.send(addr, b'P', json.dumps(host).encode())
                self.send(g['addr'], b'P', json.dumps(join).encode())
                print('%s join %s:%d' % (c, addr[0], addr[1]), flush=True)

    def sweep(self, now):
        for c in [c for c, g in self.games.items() if now - g['t'] > CODE_TTL]:
            del self.games[c]
        for a in [a for a, p in self.pairs.items() if now - p[2][0] > PAIR_TTL]:
            del self.pairs[a]
        for t in (self.rate, self.asked):
            for k in [k for k, e in t.items() if abs(now - e[0] * (1.0 if t is self.rate else 60.0)) > 120]:
                del t[k]


if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else PORT
    print('vxrelay on UDP %d' % port, flush=True)
    Relay(port).run()
