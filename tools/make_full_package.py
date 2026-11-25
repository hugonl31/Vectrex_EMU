"""The owner's private complete packages (to share with a tester, on his Drive - never published): the release's program
zip for one system (entries copied as they are - the Mac app keeps its signature, links and permissions), the full
artwork set (release artwork-1), and everything installed in build/Vectrex_EMU: all ROMs and test carts, the overlays,
the speech ROM, the owner's real cartridge / box / overlay pictures (they win over the remade ones: .png before .jpg),
the UVMC2 and PiTrex folders. Left out: the owner's settings and their backups, saves, config backups, caches, logs.
argv: windows|mac|linux release_zip artwork_zip out_zip"""
import sys, os, re, zipfile, time
osname, rel_zip, art_zip, out = sys.argv[1:5]
B = r'G:\AI\Vectrex_EMU\build\Vectrex_EMU'
SKIP = re.compile(r'(^|/)(settings[^/]*\.json[^/]*|settings\.json\.before[^/]*|adhoc\.json|vectrex_emu_settings_epoch\.txt|vectrex_emu_empty\.txt'
                  r'|uvm2_probe\.um2|core_log\.txt|lastrun\.txt|[^/]*\.sav|[^/]*\.ds2431|[^/]*\.bak|[^/]*before_[^/]*|[^/]* - Copy[^/]*'
                  r'|[^/]*\.log|Thumbs\.db|\.DS_Store)$', re.I)
entries = {}                                                    # name -> ('zip', zipfile, info) | ('file', path)
def add_zip(z, rename=None, keep=lambda n: True):
    for i in z.infolist():
        n = (rename or {}).get(i.filename, i.filename)
        if keep(n): entries[n] = ('zip', z, i)
def add_dir(rel, only_ext=None):
    root = os.path.join(B, rel)
    for dp, dn, fn in os.walk(root):
        for f in fn:
            p = os.path.join(dp, f); n = os.path.relpath(p, B).replace(os.sep, '/')
            if SKIP.search(n) or (only_ext and not n.lower().endswith(only_ext)): continue
            entries[n] = ('file', p)
rz = zipfile.ZipFile(rel_zip); az = zipfile.ZipFile(art_zip)
add_zip(rz)                                                     # (the program, as released)
add_zip(az, rename={'README.txt': 'README-artwork.txt'})        # (the full remade artwork set)
for d in ('roms', 'overlays', 'uvmc2', 'pitrex', 'system/vectrex_emu/thumbs'):
    if os.path.isdir(os.path.join(B, d)): add_dir(d)
for f in ('system/bios.bin', 'system/sp0256-al2.bin', 'system/README-speech.txt'):
    if os.path.isfile(os.path.join(B, f)): entries[f] = ('file', os.path.join(B, f))
if osname == 'windows' and os.path.isfile(os.path.join(B, 'system', 'ffmpeg.exe')) and 'system/ffmpeg.exe' not in entries:
    entries['system/ffmpeg.exe'] = ('file', os.path.join(B, 'system', 'ffmpeg.exe'))
tmp = out + '.part'
with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as o:
    for n in sorted(entries):
        src = entries[n]
        if src[0] == 'zip':
            z, i = src[1], src[2]; zi = zipfile.ZipInfo(n, i.date_time); zi.external_attr = i.external_attr
            zi.create_system = i.create_system; zi.compress_type = zipfile.ZIP_STORED if i.is_dir() else zipfile.ZIP_DEFLATED
            o.writestr(zi, b'' if i.is_dir() else z.read(i))
        else:
            o.write(src[1], n)
os.replace(tmp, out)
cnt = lambda pre: sum(1 for n in entries if n.startswith(pre) and not n.endswith('/'))
print('%s: %d files, %.0f MB - roms %d, overlays %d, thumbs %d, uvmc2 %d, pitrex %d' % (os.path.basename(out), sum(1 for n in entries if not n.endswith('/')),
      os.path.getsize(out) / 1e6, cnt('roms/'), cnt('overlays/'), cnt('system/vectrex_emu/thumbs/'), cnt('uvmc2/'), cnt('pitrex/')))
