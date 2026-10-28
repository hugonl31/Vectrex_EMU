"""fatimg.py - a folder on the PC as a cartridge's SD card: a FAT32 image built from the folder before a game
starts (long file names, subfolders), and what the game wrote copied back into the folder afterwards (new
and changed files; nothing in the folder is ever deleted).

The image has no partition table (a "superfloppy", which FatFs reads), 512-byte sectors, 2 FATs."""
import os, struct, time

SECTOR = 512
EOC = 0x0FFFFFFF


def _short_ok(name):
    """a name that is a valid upper-case 8.3 name as it is"""
    if name in ('.', '..') or name != name.upper():
        return False
    base, dot, ext = name.partition('.')
    if not base or len(base) > 8 or len(ext) > 3 or '.' in ext:
        return False
    ok = set('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%\'-_@~`!(){}^#&')
    return all(ch in ok for ch in base + ext)


def _short_name(name, used):
    """an 8.3 name for a long one: NAME~N.EXT, unique in its folder (11 bytes, space padded)"""
    if _short_ok(name):
        base, _, ext = name.partition('.')
        s = base.ljust(8).encode('ascii') + ext.ljust(3).encode('ascii')
        if s not in used:
            used.add(s)
            return s, False
    base, _, ext = name.rpartition('.') if '.' in name.lstrip('.') else (name, '', '')
    clean = lambda t: ''.join(ch for ch in t.upper() if ch.isalnum() or ch in '_-$') or 'X'
    b, e = clean(base), clean(ext)[:3] if ext else ''
    for n in range(1, 999999):
        tail = '~%d' % n
        s = (b[:8 - len(tail)] + tail).ljust(8).encode('ascii') + e.ljust(3).encode('ascii')
        if s not in used:
            used.add(s)
            return s, True
    raise ValueError('too many names')


def _lfn_checksum(short11):
    s = 0
    for b in short11:
        s = (((s & 1) << 7) + (s >> 1) + b) & 0xff
    return s


def _lfn_entries(name, short11):
    """the long-name entries for name, in on-disk order (last part first)"""
    u = name.encode('utf-16-le')
    chars = [u[i:i + 2] for i in range(0, len(u), 2)]
    chars.append(b'\x00\x00')
    while len(chars) % 13:
        chars.append(b'\xff\xff')
    parts = [chars[i:i + 13] for i in range(0, len(chars), 13)]
    if len(parts) > 1 and all(c == b'\xff\xff' for c in parts[-1][1:]) and parts[-1][0] == b'\x00\x00' and len(u) // 2 % 13 == 0:
        pass
    ck = _lfn_checksum(short11)
    out = []
    for i, p in enumerate(parts):
        ordn = i + 1 + (0x40 if i == len(parts) - 1 else 0)
        e = bytes([ordn]) + b''.join(p[0:5]) + bytes([0x0F, 0, ck]) + b''.join(p[5:11]) + b'\x00\x00' + b''.join(p[11:13])
        out.append(e)
    return list(reversed(out))


_UTC = [False]                                                    # (online play's card: dates in UTC, not this PC's zone)


def _dos_time(t):
    lt = time.gmtime(t) if _UTC[0] else time.localtime(t)
    y = max(1980, lt.tm_year)
    return ((lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec // 2)), (((y - 1980) << 9) | (lt.tm_mon << 5) | lt.tm_mday)


def _entry(short11, attr, cluster, size, mtime):
    t, d = _dos_time(mtime)
    return short11 + struct.pack('<BBBHHHHHHHI', attr, 0, 0, t, d, d, cluster >> 16, t, d, cluster & 0xffff, size)


def _tree_size(folder, only=None):
    total = 0
    for root, dirs, files in os.walk(folder):
        for f in files:
            p = os.path.join(root, f)
            if only is not None and os.path.relpath(p, folder).replace(os.sep, '/').lower() not in only:
                continue
            try:
                total += os.path.getsize(p)
            except OSError:
                pass
    return total


def build(folder, img_path, min_mb=64, only=None, fixed_time=None):
    """write a FAT32 image of folder (which may be missing: an empty card) to img_path; returns the size.
    only: a set of paths (relative, '/', lower case) - just those files (and the folders that hold them);
    fixed_time: one date for everything on the card (online play: both sides build the same card, to the byte)"""
    content = _tree_size(folder, only) if folder and os.path.isdir(folder) else 0
    _UTC[0] = fixed_time is not None
    mtime = (lambda p: fixed_time) if fixed_time is not None else os.path.getmtime
    clock = (lambda: fixed_time) if fixed_time is not None else time.time
    def wanted(p, isdir):
        if only is None:
            return True
        rel = os.path.relpath(p, folder).replace(os.sep, '/').lower()
        return rel in only if not isdir else any(o.startswith(rel + '/') for o in only)
    def listing(path):
        return [x for x in os.listdir(path) if wanted(os.path.join(path, x), os.path.isdir(os.path.join(path, x)))]
    mb = max(min_mb, (content * 2 + (32 << 20)) >> 20)
    mb = max(mb, 272)                                             # (4 KB clusters need >= 65525 of them for FAT32)
    total = mb * 2048                                             # sectors
    spc = 8                                                       # 4 KB clusters, as Windows formats a card
    while total // spc > 4000000:                                 # (clusters: 65525 .. ~4 million)
        spc *= 2
    reserved, nfats = 32, 2
    clusters = total // spc
    fatsz = (clusters * 4 + 8 + SECTOR - 1) // SECTOR
    data_start = reserved + nfats * fatsz
    nclus = (total - data_start) // spc
    csize = spc * SECTOR
    fat = [0] * (nclus + 2)
    fat[0], fat[1] = 0x0FFFFFF8, EOC
    data = {}                                                     # cluster -> bytes (only those in use)
    nxt = [2]

    def alloc(nbytes):
        n = max(1, (nbytes + csize - 1) // csize)
        first = nxt[0]
        if first + n > nclus + 2:
            raise ValueError('the SD folder does not fit')
        for k in range(n):
            fat[first + k] = first + k + 1 if k < n - 1 else EOC
        nxt[0] += n
        return first, n

    def put(first, blob):
        for k in range(0, len(blob), csize):
            data[first + k // csize] = blob[k:k + csize]

    def make_dir(path, self_cl, parent_cl):
        """the directory's entries; its own cluster chain is allocated by the caller beforehand"""
        ents = []
        if parent_cl is not None:
            now = clock()
            ents.append(_entry(b'.          ', 0x10, self_cl, 0, now))
            ents.append(_entry(b'..         ', 0x10, parent_cl if parent_cl != 2 else 0, 0, now))
        else:
            ents.append(b'VECTREX SD '.ljust(11) + bytes([0x08]) + b'\x00' * 20)   # (the volume label)
        used = set()
        names = sorted(listing(path), key=str.lower) if path and os.path.isdir(path) else []
        for nm in names:
            p = os.path.join(path, nm)
            if nm.startswith('.') and nm in ('.', '..'):
                continue
            short, need_lfn = _short_name(nm, used)
            if os.path.isdir(p):
                # (size the subdirectory first, then allocate and fill it)
                sub_cnt = 2 + sum(1 + (len(x) + 12) // 13 for x in listing(p))
                cl, _ = alloc((sub_cnt + 1) * 32)
                sub = make_dir(p, cl, self_cl)
                put(cl, sub)
                lfn = _lfn_entries(nm, short) if need_lfn or nm != nm.upper() else []
                ents.extend(lfn)
                ents.append(_entry(short, 0x10, cl, 0, mtime(p)))
            elif os.path.isfile(p):
                blob = open(p, 'rb').read()
                cl = 0
                if blob:
                    cl, _ = alloc(len(blob))
                    put(cl, blob)
                lfn = _lfn_entries(nm, short) if need_lfn or nm != nm.upper() else []
                ents.extend(lfn)
                ents.append(_entry(short, 0x20, cl, len(blob), mtime(p)))
        raw = b''.join(ents)
        return raw + b'\x00' * 32                                 # (an end marker)

    # the root: its size first
    root_names = listing(folder) if folder and os.path.isdir(folder) else []
    root_cnt = 2 + sum(1 + (len(x) + 12) // 13 for x in root_names)
    root_cl, _ = alloc((root_cnt + 1) * 32)
    assert root_cl == 2
    put(root_cl, make_dir(folder, 2, None))
    used_clusters = nxt[0] - 2
    # boot sector
    bs = bytearray(SECTOR)
    bs[0:3] = b'\xEB\x58\x90'; bs[3:11] = b'MSWIN4.1'
    struct.pack_into('<HBHBHHBHHHII', bs, 11, SECTOR, spc, reserved, nfats, 0, 0, 0xF8, 0, 63, 255, 0, total)
    struct.pack_into('<IHHIHH', bs, 36, fatsz, 0, 0, 2, 1, 6)
    bs[64] = 0x80; bs[66] = 0x29
    struct.pack_into('<I', bs, 67, int(clock()) & 0xffffffff)
    bs[71:82] = b'VECTREX SD '; bs[82:90] = b'FAT32   '
    bs[510:512] = b'\x55\xAA'
    fsinfo = bytearray(SECTOR)
    struct.pack_into('<I', fsinfo, 0, 0x41615252)
    struct.pack_into('<III', fsinfo, 484, 0x61417272, nclus - used_clusters, nxt[0])
    struct.pack_into('<I', fsinfo, 508, 0xAA550000)
    fat_bytes = struct.pack('<%dI' % len(fat), *fat)
    fat_bytes += b'\x00' * (fatsz * SECTOR - len(fat_bytes))
    with open(img_path, 'wb') as f:
        f.truncate(total * SECTOR)                                # (zero, sparse where the system can)
        f.seek(0); f.write(bs); f.write(fsinfo)
        f.seek(6 * SECTOR); f.write(bs); f.write(fsinfo)
        for k in range(nfats):
            f.seek((reserved + k * fatsz) * SECTOR); f.write(fat_bytes)
        for cl in sorted(data):
            f.seek((data_start + (cl - 2) * spc) * SECTOR); f.write(data[cl])
    return total * SECTOR


def _read_image(img_path):
    """{relative path: bytes} of every file on the FAT32 image"""
    f = open(img_path, 'rb')
    bs = f.read(SECTOR)
    bps, spc, reserved, nfats = struct.unpack_from('<HBHB', bs, 11)
    fatsz, root_cl = struct.unpack_from('<I', bs, 36)[0], struct.unpack_from('<I', bs, 44)[0]
    f.seek(reserved * bps); fat_raw = f.read(fatsz * bps)
    nent = len(fat_raw) // 4
    data_start = reserved + nfats * fatsz
    csize = spc * bps

    def chain(cl):
        out, seen = [], set()
        while 2 <= cl < nent and cl not in seen and len(out) < nent:
            seen.add(cl); out.append(cl)
            cl = struct.unpack_from('<I', fat_raw, cl * 4)[0] & 0x0FFFFFFF
            if cl >= 0x0FFFFFF8:
                break
        return out

    def read_chain(cl, size=None):
        buf = bytearray()
        for c in chain(cl):
            f.seek((data_start + (c - 2) * spc) * bps); buf += f.read(csize)
            if size is not None and len(buf) >= size:
                break
        return bytes(buf[:size]) if size is not None else bytes(buf)

    files = {}

    def walk(cl, rel, depth):
        if depth > 32:
            return
        raw = read_chain(cl)
        lfn = {}
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if len(e) < 32 or e[0] == 0:
                break
            if e[0] == 0xE5:
                lfn = {}; continue
            attr = e[11]
            if attr == 0x0F:
                ordn = e[0] & 0x1f
                chars = e[1:11] + e[14:26] + e[28:32]
                lfn[ordn] = chars
                continue
            if attr & 0x08:
                lfn = {}; continue
            if lfn:
                u = b''.join(lfn[k] for k in sorted(lfn))
                name = u.decode('utf-16-le', 'replace').split('\x00')[0]
            else:
                base, ext = e[0:8].decode('ascii', 'replace').rstrip(), e[8:11].decode('ascii', 'replace').rstrip()
                if e[12] & 0x08: base = base.lower()
                if e[12] & 0x10: ext = ext.lower()
                name = base + ('.' + ext if ext else '')
            lfn = {}
            if name in ('.', '..'):
                continue
            first = (struct.unpack_from('<H', e, 20)[0] << 16) | struct.unpack_from('<H', e, 26)[0]
            size = struct.unpack_from('<I', e, 28)[0]
            path = name if not rel else rel + '/' + name
            if attr & 0x10:
                if first >= 2:
                    walk(first, path, depth + 1)
            else:
                files[path] = read_chain(first, size) if first >= 2 and size else b''
    walk(root_cl, '', 0)
    f.close()
    return files


def sync_back(img_path, folder):
    """copy new and changed files from the image into folder (never deletes); returns the names written"""
    written = []
    try:
        files = _read_image(img_path)
    except (OSError, struct.error, ValueError):
        return written
    for rel, blob in files.items():
        if any(part in ('', '..') for part in rel.split('/')):
            continue
        dst = os.path.join(folder, *rel.split('/'))
        try:
            if os.path.isfile(dst) and open(dst, 'rb').read() == blob:
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, 'wb') as out:
                out.write(blob)
            written.append(rel)
        except OSError:
            pass
    return written
