"""v4e.py - VecFever's .v4e cartridge files (V4EB): unpacked into the plain ROM image the core runs.

The format, from VecFever's send2vf.c (the packer):
  0      'V4EB'
  4/5    the cartridge type (big endian)
  6/7    the number of banks (one based)
  8 + 6 i  each bank: the CRC-32 of its packed data (seeded with 'V4EB'), its packed size (16 bit)
  then the banks' packed data, one after the other; each bank unpacks to 32 KB (the last may be shorter)

The packing (send2vf's 'dzip'), one code at a time:
  0x00-0x7F  n            a run: 128 - n literal bytes follow
  0x80-0xFF  then < 0x80  a short match: offset (the first byte, signed, -1 .. -128), length 128 - the second
  0x80-0xFF  0x80-0xFF  l a long match: offset the 14 bits of both (two's complement), length from l
  (matches copy byte by byte from earlier output, so they may overlap themselves)

The 48 KB bank types are stored reshuffled (send2vf puts the 16 KB upper halves together); they are put
back here into the layout the emulator's 4 x 48 KB board reads (64 KB per bank, PB6 + IRQ)."""
import struct, zlib

TYPES = {0: 'ROM', 1: 'ROM plus RAM', 2: 'VecFever cartridge', 3: 'ROM with a DS2430 save chip',
         4: 'ROM with a DS2431 save chip', 5: 'ROM with a Vector Pilot chip', 6: '50 KB flat ROM',
         7: '16 KB ROM banks', 8: '2 x 48 KB ROM banks', 9: 'hybrid binary', 10: '4 x 48 KB ROM banks',
         254: 'VecFever firmware update'}
SUPPORTED = (0, 3, 4, 5, 8, 10)
SEED = 0x56344542                               # 'V4EB'


def unpack_bank(src):
    out = bytearray()
    i, n = 0, len(src)
    while i < n:
        b = src[i]
        if b < 0x80:                            # a run of literal bytes
            k = 128 - b
            if i + 1 + k > n:
                raise ValueError('a run past the end of the bank')
            out += src[i + 1:i + 1 + k]; i += 1 + k
            continue
        if i + 1 >= n:
            raise ValueError('a match cut off at the end of the bank')
        c = src[i + 1]
        if c < 0x80:                            # a short match
            off, length = b - 256, 128 - c; i += 2
        else:                                   # a long match
            if i + 2 >= n:
                raise ValueError('a match cut off at the end of the bank')
            d = src[i + 2]
            off = (((b & 0x7f) << 7) | (c & 0x7f)) - 16384
            length = 384 - d if d >= 128 else 128 - d
            i += 3
        p = len(out) + off
        if p < 0:
            raise ValueError('a match before the start of the bank')
        for k in range(length):                 # (byte by byte: a match may overlap itself)
            out.append(out[p + k])
    return bytes(out)


def unpack(data):
    """the file's bytes -> (cartridge type, the ROM image for the emulator); ValueError when it is not
    a .v4e file, is damaged, or needs hardware the emulator does not have"""
    if len(data) < 8 or data[:4] != b'V4EB':
        raise ValueError('not a VecFever .v4e file')
    ctype, nb = struct.unpack('>HH', data[4:8])
    if not 1 <= nb <= 64 or len(data) < 8 + 6 * nb:
        raise ValueError('a damaged .v4e header')
    if ctype not in SUPPORTED:
        raise ValueError('VecFever cartridge type %d (%s): this emulator does not have that hardware'
                         % (ctype, TYPES.get(ctype, 'unknown')))
    pos = 8 + 6 * nb
    banks = []
    for k in range(nb):
        crc, size = struct.unpack('>IH', data[8 + 6 * k:14 + 6 * k])
        packed = data[pos:pos + size]
        if len(packed) != size:
            raise ValueError('bank %d is cut short' % (k + 1))
        if zlib.crc32(packed, SEED) != crc:
            raise ValueError('bank %d: wrong checksum (a damaged file)' % (k + 1))
        # (a bank of exactly 32 KB packed is stored as is: send2vf's fallback when packing does not help)
        bank = packed if size == 0x8000 else unpack_bank(packed)
        if len(bank) > 0x8000:
            raise ValueError('bank %d unpacks to more than 32 KB' % (k + 1))
        banks.append(bank)
        pos += size
    rom = b''.join(banks)
    if ctype == 8:                              # 2 x 48 KB, banks chosen by PB6
        if len(rom) != 3 * 0x8000:
            raise ValueError('a 2 x 48 KB cartridge of the wrong size')
        lo0, lo1, hi = rom[:0x8000], rom[0x8000:0x10000], rom[0x10000:]
        pad = b'\xff' * 0x4000
        b0, b1 = lo0 + hi[:0x4000] + pad, lo1 + hi[0x4000:] + pad
        rom = b0 + b1 + b0 + b1                 # (the IRQ line does not switch: both halves the same)
    elif ctype == 10:                           # 4 x 48 KB (Vectorblade): PB6 and IRQ
        if len(rom) != 6 * 0x8000:
            raise ValueError('a 4 x 48 KB cartridge of the wrong size')
        lo = [rom[k * 0x8000:(k + 1) * 0x8000] for k in range(4)]   # (A15 0: IRQ 0 VIA 0, IRQ 0 VIA 1, ...)
        hi = rom[4 * 0x8000:]                   # (A15 1, 16 KB each: IRQ 0 VIA 0, IRQ 1 VIA 0, IRQ 0 VIA 1, IRQ 1 VIA 1)
        his = [hi[0x0000:0x4000], hi[0x8000:0xC000], hi[0x4000:0x8000], hi[0xC000:0x10000]]
        pad = b'\xff' * 0x4000
        rom = b''.join(lo[k] + his[k] + pad for k in range(4))
    return ctype, rom
