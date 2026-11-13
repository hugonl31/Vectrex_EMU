"""veclink_net_test.py - online VecLink without the window: two PCs (or two processes), each running both consoles of a
VecLink pair as the app's Runner does online (its player's console and the other, unseen, in 30000-cycle slices of the
cable's clock), only the controllers exchanged; the sync check every 25 frames as in a real online game.

    host:  python tools/veclink_net_test.py host ROM [--port 7746] [--frames 2000] [--script JSON]
    join:  python tools/veclink_net_test.py join ROM --addr HOST[:PORT] [...]

--script: [[from_frame, to_frame, buttons, stick_x], ...] for this side's player. At the end: frames, the sync checks
compared and the first frame they differed (None: never), and the last frame's checksums of console A and B - the
same on both PCs when the two computed the same pair of games. Needs the core (core/vectrex_core) and system/bios.bin.
"""
import sys, os, json, time, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'src', 'frontend'))
import netplay
import vectrex_emu as V


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('role', choices=('host', 'join'))
    ap.add_argument('rom')
    ap.add_argument('--addr', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=7746)
    ap.add_argument('--frames', type=int, default=2000)
    ap.add_argument('--script', default='[]')
    a = ap.parse_args()
    script = json.loads(a.script)
    port = a.port
    addr = a.addr
    if ':' in addr:
        addr, port = addr.rsplit(':', 1); port = int(port)
    net = netplay.NetPlay(a.role, None if a.role == 'host' else addr, port, log=lambda *x: print('net:', *x, flush=True))
    if a.role == 'host':
        game = netplay.new_game(a.rom, dict(skip_boot=True, cart_hw='auto', dacofs=0.0))
        game['veclink'] = True
        net.offer, net.offer_rom = game, os.path.abspath(a.rom)
        g = net.connect(game, timeout=120.0)
    else:
        g = net.connect(None, timeout=120.0)
        if isinstance(g, dict):
            ok = netplay.rom_md5(a.rom) == g.get('md5')
            net.accept(ok, g.get('name', ''))
            if not ok:
                print('the ROM differs from the host\'s'); return 2
    if not isinstance(g, dict):
        print('no game:', g); return 2
    assert g.get('veclink'), 'not a VecLink game'
    V.Core.force_seed = g['seed']
    lnk = os.path.join(os.path.dirname(os.path.abspath(a.rom)), 'veclink_net_test_%d.lnk' % os.getpid())
    if os.path.exists(lnk): os.remove(lnk)
    mine = 'A' if a.role == 'host' else 'B'
    V.Core.veclink_slice = 30000
    V.Core.veclink = (lnk, 'B' if mine == 'A' else 'A'); V.Core.headless = True
    other_core = V.Core(a.rom, False, g.get('cart_hw', 'auto'), 'off', None, None, None, False)
    V.Core.headless = False
    V.Core.veclink = (lnk, mine)
    own_core = V.Core(a.rom, False, g.get('cart_hw', 'auto'), 'off', None, None, None, False)
    net.reset()
    flags0 = 8 | (16 if g.get('skip_boot') else 0)
    checks = 0; t0 = time.perf_counter(); last = None
    for f in range(a.frames):
        btn = jx = 0
        for s0, s1, b, x in script:
            if s0 <= f < s1:
                btn, jx = b, x
        got = net.exchange((jx, 0, btn, 0))
        if got is None:
            print('connection lost at frame', f); break
        ha, jb = got                                    # (the host's input, the joiner's)
        own, other = (ha, jb) if a.role == 'host' else (jb, ha)
        fl = flags0 if f == 0 else 0
        other_core.frame_send(other[0], other[1], other[2], fl | (other[3] & 4))
        own_core.frame(own[0], own[1], own[2], fl | (own[3] & 4))
        other_core.frame_recv()
        s_own, s_oth = own_core.state_sum & 0xffffffff, other_core.state_sum & 0xffffffff
        sa, sb = (s_own, s_oth) if a.role == 'host' else (s_oth, s_own)
        sync = (sa ^ ((sb << 7 | sb >> 25) & 0xffffffff)) & 0xffffffff
        net.mark(net.f - 1, sync)
        if (net.f - 1) % netplay.SYNC_EVERY == 0:
            checks += 1
        last = (sa, sb)
    time.sleep(1.0)                                     # (the last sync checks arrive)
    net._drain() if hasattr(net, '_drain') else None
    dt = time.perf_counter() - t0
    print(json.dumps(dict(role=a.role, frames=net.f, seconds=round(dt, 1), sync_checks=checks, desync=net.desync,
                          last_A='%08x' % last[0] if last else None, last_B='%08x' % last[1] if last else None,
                          stall_s=round(net.stall_s, 1))), flush=True)
    own_core.close(); other_core.close(); net.close()
    try: os.remove(lnk)
    except OSError: pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
