"""vart.py - reads MiSTer Vectrex overlay files (.art, "VART" v1 containers) into RGBA pixels.

The format is the MiSTer Vectrex core's (Videodr0me, 2026; its Overlay Tools, vart_encoder.py, GPL): a
64-byte header, 48-byte plane descriptors, per plane an RGBA palette (16 / 64 / 256 entries) and rows of
packed indices - packets of literals, runs or copies of the row above. The planes are the same artwork at
1080p / 720p / 480p / 240p; the 1080p plane is 1360 x 1080 with the 846 x 1080 overlay sheet centred in it
(rotated packages, _90CW / _90CCW: a 1360 x 1066 sheet). Alpha: 255 printed (blocks the beam), 1-254
translucent colour filter (51 as made by the tools), 0 clear.

load(path) -> (width, height, rgba bytes) of the largest (1080p) plane, rows top to bottom - the whole plane: the
sheet's window is its centre 846 columns, artwork may reach the sides (a bezel). ValueError if not an overlay.
load_png(path): the same from a PNG; load_overlay(path): either, with the sheet's width."""
import math, struct, zlib

HEADER = 64
DESC = 48


def _row(record, width, previous, bits):
    bit_count = struct.unpack_from('<H', record)[0]
    data = record[2:]
    val = int.from_bytes(data, 'little')
    pos = 0
    out = bytearray()
    mask = (1 << bits) - 1
    while len(out) < width:
        if pos + 8 > bit_count:
            raise ValueError('row ends inside a packet')
        head = (val >> pos) & 0xff; pos += 8
        if not head & 0x80:                                   # literals
            for _ in range((head & 0x7f) + 1):
                out.append((val >> pos) & mask); pos += bits
        elif not head & 0x40:                                 # a run
            out.extend(bytes([(val >> pos) & mask]) * ((head & 0x3f) + 1)); pos += bits
        else:                                                 # a copy of the row above
            if previous is None:
                raise ValueError('copy in the first row')
            s = len(out); out.extend(previous[s:s + (head & 0x3f) + 1])
    if len(out) != width or pos != bit_count:
        raise ValueError('row length mismatch')
    return bytes(out)


def load(path):
    c = open(path, 'rb').read()
    if len(c) < HEADER or c[:4] != b'VART' or c[4] != 1:
        raise ValueError('not a VART v1 overlay')
    _, _, _, layers, planes, total, crc, doff, dsize = struct.unpack_from('<4sBBBBIIIH', c)
    if total != len(c) or doff != HEADER or dsize != DESC or not planes:
        raise ValueError('bad VART header')
    if zlib.crc32(c[doff:]) & 0xffffffff != crc:
        raise ValueError('VART checksum mismatch')
    best = None
    for i in range(planes):
        f = struct.unpack_from('<HHBBBBHHIIIIIIIII', c, doff + i * DESC)
        if best is None or f[0] * f[1] > best[0] * best[1]:
            best = f
    width, height, _, role, bits, flags = best[:6]
    pal_off, pal_size, pay_off, pay_size = best[8:12]
    if bits not in (4, 6, 8) or pal_size != (1 << bits) * 4:
        raise ValueError('unsupported VART plane')
    pal = c[pal_off:pal_off + pal_size]
    entries = [pal[k * 4:k * 4 + 4] for k in range(1 << bits)]
    payload = c[pay_off:pay_off + pay_size]
    rows = []
    prev = None; off = 0
    for y in range(height):
        nbits = struct.unpack_from('<H', payload, off)[0]
        size = (2 + math.ceil(nbits / 8) + 7) // 8 * 8
        prev = _row(payload[off:off + size], width, prev, bits)
        rows.append(prev); off += size
    rgba = b''.join(b''.join(entries[i] for i in r) for r in rows)
    return width, height, rgba


def load_png(path):
    """a PNG overlay -> (width, height, rgba bytes), rows top to bottom: 8-bit RGBA, RGB, grey (+ alpha) or
    palette (+ tRNS), not interlaced. Like the MiSTer Overlay Tools' input: an 846 x 1080 sheet (or the whole
    1360 x 1080 plane), alpha 255 printed, 1-254 colour filter, 0 clear. Without alpha: black is clear, the
    rest a colour filter (51)."""
    c = open(path, 'rb').read()
    if c[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    pos = 8; idat = []; pal = trns = None; hdr = None
    while pos < len(c):
        n, kind = struct.unpack_from('>I4s', c, pos)
        data = c[pos + 8:pos + 8 + n]; pos += 12 + n
        if kind == b'IHDR':
            hdr = struct.unpack('>IIBBBBB', data)
        elif kind == b'PLTE':
            pal = data
        elif kind == b'tRNS':
            trns = data
        elif kind == b'IDAT':
            idat.append(data)
        elif kind == b'IEND':
            break
    if not hdr:
        raise ValueError('PNG without a header')
    w, h, depth, ctype, _, _, inter = hdr
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if depth != 8 or not chans or inter:
        raise ValueError('PNG: only 8 bits per channel, not interlaced')
    raw = zlib.decompress(b''.join(idat))
    bpp = chans; stride = w * bpp
    prev = bytearray(stride); rows = []
    for y in range(h):
        f = raw[y * (stride + 1)]
        cur = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:                                              # Sub
            for i in range(bpp, stride):
                cur[i] = (cur[i] + cur[i - bpp]) & 255
        elif f == 2:                                            # Up
            cur = bytearray((a + b) & 255 for a, b in zip(cur, prev))
        elif f == 3:                                            # Average
            for i in range(stride):
                cur[i] = (cur[i] + ((cur[i - bpp] if i >= bpp else 0) + prev[i]) // 2) & 255
        elif f == 4:                                            # Paeth
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0; b = prev[i]; cc = prev[i - bpp] if i >= bpp else 0
                p = a + b - cc; pa = abs(p - a); pb = abs(p - b); pc = abs(p - cc)
                cur[i] = (cur[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else cc)) & 255
        elif f:
            raise ValueError('PNG: bad row filter')
        rows.append(bytes(cur)); prev = cur
    px = b''.join(rows)
    if ctype == 6:
        return w, h, px
    if ctype == 3:
        if not pal:
            raise ValueError('PNG: palette missing')
        t = trns or b''
        lut = [pal[i * 3:i * 3 + 3] + bytes([t[i] if i < len(t) else 255]) for i in range(len(pal) // 3)]
        lut += [b'\0\0\0\0'] * (256 - len(lut))
        return w, h, b''.join(lut[i] for i in px)
    if ctype == 4:                                              # grey + alpha
        return w, h, b''.join(bytes((g, g, g, a)) for g, a in zip(px[0::2], px[1::2]))
    if ctype == 0:
        px = bytes(v for g in px for v in (g, g, g))
    out = bytearray(w * h * 4)                                  # (RGB: no alpha - black clear, the rest a filter)
    out[0::4] = px[0::3]; out[1::4] = px[1::3]; out[2::4] = px[2::3]
    out[3::4] = bytes(0 if r == g == b == 0 else 51 for r, g, b in zip(px[0::3], px[1::3], px[2::3]))
    return w, h, bytes(out)


SOFT_EDGE = 0.06       # (a soft outer edge up to this share of the overlay's smaller side fades to black)
EDGE_BLUR = 2.0        # (the outline of an overlay with a clear border: blurred this many of its pixels)


def soft_edge(w, h, px, sheet=None):
    """an overlay with a clear (alpha 0) area at its border - a custom overlay with a shaped or soft edge: that
    outside is the Vectrex's black bezel, and the soft edge between it and the printed art (alpha 1-254, which
    would otherwise count as colour filter) fades to black - printed, its colour times its alpha. Overlays
    without a clear border (the classic ones) and filter areas away from the edge stay as they are"""
    try:
        import numpy as np
        from PIL import Image, ImageDraw
    except ImportError:
        return px
    full = np.frombuffer(px, np.uint8).reshape(h, w, 4).copy()
    x0 = max(0, int(round((w - (sheet or w)) / 2.0)))          # (only the part on the picture: a MiSTer plane's
    a = full[:, x0:w - x0]; h, w = a.shape[:2]                  #  sides stay as they are)
    al = a[..., 3]
    clear = al == 0
    if not (clear[0].any() or clear[-1].any() or clear[:, 0].any() or clear[:, -1].any()):
        return px
    m = Image.frombytes('L', (w, h), np.where(clear, 0, 255).astype(np.uint8).tobytes())   # (outside: the clear
    for x, y in ([(x, 0) for x in range(w)] + [(x, h - 1) for x in range(w)] +            #  area joined to the
                 [(0, y) for y in range(h)] + [(w - 1, y) for y in range(h)]):           #  border)
        if m.getpixel((x, y)) == 0:
            ImageDraw.floodfill(m, (x, y), 128)
    out = np.frombuffer(m.tobytes(), np.uint8).reshape(h, w) == 128
    n = max(2, int(SOFT_EDGE * min(w, h)))
    part = (al > 0) & (al < 255)
    al16 = al.astype(np.int16)
    reach = out.copy()                                          # (the soft edge: half-clear pixels reached from the
    for _ in range(n):                                          #  outside, each step to a neighbour with MORE alpha -
        g = reach.copy()                                        #  a soft edge rises towards the art, a colour filter
        for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):            #  is even and stays a filter)
            src = np.roll(reach, d, (0, 1)); sal = np.roll(al16, d, (0, 1))
            if d[0] == 1: src[0] = False
            elif d[0] == -1: src[-1] = False
            elif d[1] == 1: src[:, 0] = False
            else: src[:, -1] = False
            g |= src & part & (al16 > np.where(np.roll(out, d, (0, 1)), 0, sal))
        if (g == reach).all():
            break
        reach = g
    edge = reach & part
    k = (al[edge].astype(np.float32) / 255.0)[:, None]
    a[edge, :3] = (a[edge, :3] * k + 0.5).astype(np.uint8); a[edge, 3] = 255
    a[out, :3] = 0; a[out, 3] = 255
    if EDGE_BLUR > 0:                                           # (the overlay's outline blurred a little: a cut
        from PIL import ImageFilter                             #  edge not razor sharp - it fades into the black)
        inside = Image.frombytes('L', (w, h), np.where(out, 0, 255).astype(np.uint8).tobytes())
        f = np.frombuffer(inside.filter(ImageFilter.GaussianBlur(EDGE_BLUR)).tobytes(), np.uint8).reshape(h, w)
        zone = ~out & (f < 254)
        k = (f[zone].astype(np.float32) / 255.0)[:, None]
        a[zone, :3] = (a[zone, :3] * k + 0.5).astype(np.uint8); a[zone, 3] = 255
    return full.tobytes()


def load_overlay(path):
    """an overlay file, .art (MiSTer) or .png -> (width, height, rgba bytes, sheet width): the sheet width
    is the width (in its pixels) of the part that covers the picture - an .art plane's centre 846 columns
    (at 1080 rows), a PNG sheet's whole width, a whole-plane PNG's (1360 x 1080) centre 846 columns. A clear
    border and a soft edge fade to black (soft_edge)"""
    if path.lower().endswith('.png'):
        w, h, px = load_png(path)
        sheet = w * 846.0 / 1360 if abs(w / float(h) - 1360 / 1080.0) < 0.02 else w
    else:
        w, h, px = load(path)
        sheet = h * 846.0 / 1080
    return w, h, soft_edge(w, h, px, sheet), sheet
