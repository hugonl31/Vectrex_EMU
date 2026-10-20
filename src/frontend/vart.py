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


def load_overlay(path):
    """an overlay file, .art (MiSTer) or .png -> (width, height, rgba bytes, sheet width): the sheet width
    is the width (in its pixels) of the part that covers the picture - an .art plane's centre 846 columns
    (at 1080 rows), a PNG sheet's whole width, a whole-plane PNG's (1360 x 1080) centre 846 columns"""
    if path.lower().endswith('.png'):
        w, h, px = load_png(path)
        sheet = w * 846.0 / 1360 if abs(w / float(h) - 1360 / 1080.0) < 0.02 else w
    else:
        w, h, px = load(path)
        sheet = h * 846.0 / 1080
    return w, h, px, sheet
