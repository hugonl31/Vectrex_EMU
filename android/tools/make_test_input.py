"""make_test_input.py - a deterministic --live input stream for core_test.c: power on, the line-gap
setting, then N frames of scripted stick and buttons (player 2 on every 3rd frame), then quit."""
import random, sys
n = int(sys.argv[2]) if len(sys.argv) > 2 else 1500
r = random.Random(1234); out = bytearray()
for f in range(n):
    jx, jy = (0, 0) if f < 700 else (r.randint(-127, 127), r.randint(-127, 127))
    btn = 8 if 680 <= f < 684 else (r.randint(0, 15) if f >= 700 and f % 7 == 0 else 0)
    flags = 8 if f == 0 else 4 if f == 1200 else 0
    if f == 1: flags |= 0x20
    if f % 3 == 0: flags |= 0x40
    out += bytes([jx + 128, jy + 128, btn, flags])
    if flags & 0x40: out += bytes([128 + r.randint(-50, 50), 128, r.randint(0, 15)])
    if flags & 0x20: out += bytes([1230 & 255, 1230 >> 8])     # (12.30 cycles)
out += bytes([128, 128, 0, 0x80])
open(sys.argv[1], 'wb').write(out)
