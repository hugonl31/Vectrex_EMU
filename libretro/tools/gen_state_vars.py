"""gen_state_vars.py - the portable save state's variable lists (RetroArch core: netplay between systems).

A save state by memory layout differs between compilers (Visual C++ on Windows, clang on Android / Linux: the order of
the variables, clang even splitting or shrinking some), so the RetroArch core saves each state variable by name, in
one fixed order, 'long' (4 bytes on Windows, 8 elsewhere) as 8 bytes. This writes those lists, one header per file
that holds them (it is included at that file's end, where its statics are seen):
  src/core/state/vars_vecx.h   (vecx.c and what it includes)    src/core/state/vars_e6809.h, vars_e8910.h
  libretro/vars_vx.h           (libretro_vx.c: vtest.c, live.c, bot.c and its own saved variables)
From the symbols in the save-state sections of both builds: the Visual C++ link map (msvc_map.bat's ra.map) and the
clang objects' symbol tables (llvm-objdump -t obj/*.o, as "object size name" lines). Run again when a file's variables
change; VXPOC_STATECHECK=1 (the core) reports section bytes no list covers.
  python gen_state_vars.py <ra.map> <clang_syms.txt>
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import re, sys, os

R = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = {'state_vecx': ('src/core/state/vars_vecx.h', 'vecx'), 'state_e6809': ('src/core/state/vars_e6809.h', 'e6809'),
       'state_e8910': ('src/core/state/vars_e8910.h', 'e8910'), 'libretro_vx': ('libretro/vars_vx.h', 'vx')}

# not saved: this process's pointers and handles; the big buffers saved apart or not state (see vxs_exclude); the
# settings' caches and logs; what one system has only
SKIP = set('''vx_ram vt_beam_hook vectors_draw vectors_erse vl vl_dbg vl_dbg_n uvm2 rptr_xyus e6809_read8 e6809_write8
bcur blast wavf recf stdout_bin abuf abuf_n abuf_cap vt_flash_file syms ra_buf ra_raw ra_state
vectors_set vector_hash cart uvm2_image px_image sp_rom prof acc cur last eyeb ncur nlast neye
vxd_a vxd_z vxb_a vxb_z'''.split())
# long (4 bytes on Windows, 8 elsewhere): saved as 8 each; structs with long in them: member by member ('+': long)
LONG = set()                     # (filled in below from LONGS)
LONGS = '''vt_bank_switches vt_via_mirror alg_xofs alg_yofs dark_x0 dark_y0 dark_x1 dark_y1 dark_dx dark_dy fade_x0 fade_y0
alg_dx alg_dy alg_curr_x alg_curr_y alg_vector_x0 alg_vector_y0 alg_vector_x1 alg_vector_y1 alg_vector_dx alg_vector_dy
vector_draw_cnt vector_erse_cnt fcycles vt_text_shift sj_sil psg_frac sp_phase vl_slice uvm2_n_halt uvm2_n_rd uvm2_n_wr
uvm2_n_via vt_nlines frames frames_over bankchg_log bad_switches wav_bytes frame_budget dev_x0 dev_y0 dev_dx dev_dy
prot_dim prot_lines aq_frames'''
LONG.update(LONGS.split())
STRUCT = {'ss_last': ['d', '+l', 'u'], 'alg_lin': ['u', '+l'], 'PSG': ['index', 'ready', 'lastEnable', '@PeriodA..VolTable']}

def msvc(path):
    rows, mark = [], {}
    for line in open(path, errors='ignore'):
        m = re.match(r'\s+([0-9a-f]{4}):([0-9a-f]{8})\s+(\S+)\s+[0-9a-f]{16}\s+(?:f\s+)?(?:i\s+)?(\S+)\.obj', line)
        if not m: continue
        sec, off, name, obj = int(m.group(1), 16), int(m.group(2), 16), m.group(3), m.group(4)
        rows.append((sec, off, name, obj))
        if name in ('vxd_a', 'vxd_z', 'vxb_a', 'vxb_z'): mark[name] = (sec, off)
    out = {}
    for sec, off, name, obj in rows:          # (only what lies between the sections' marks)
        inside = any(mark[a][0] == sec and mark[a][1] <= off <= mark[z][1] for a, z in (('vxd_a', 'vxd_z'), ('vxb_a', 'vxb_z')))
        if inside and obj in OUT and not name.startswith('?') and not name.startswith('$'): out.setdefault(obj, set()).add(name)
    return out

def clang(path):
    out, local = {}, {}
    for line in open(path):
        p = line.split()
        if len(p) < 3: continue
        obj, name = p[0], p[2]
        if name in ('vxd', 'vxb'): continue
        base = re.sub(r'\.\d+$', '', name)
        if '.' in base: local.setdefault(obj, set()).add(name); continue    # (a function's own static)
        out.setdefault(obj, set()).add(base)
    return out, local

SKIP.update('wd_progress wd_stage wd_ring wd_ri ow_file px_sd'.split())   # (Windows' watchdog; the ds2431's file; a handle)
# the UVMC2 and PiTrex boards: their games have no save states (vx_ok) - their variables not in a state either
NO_STATE_FILES = ['src/core/uvm2_cart.c', 'src/core/pitrex_cart.c', 'src/core/pitrex_usb.c']
def declared_in(files):
    names = set()
    for f in files:
        for line in open(os.path.join(R, f), errors='ignore'):
            if not re.match(r'(static |const |volatile |unsigned |signed |struct |uint|int|double|char|float|long|short|\})', line): continue
            decl = line.split('/*')[0].split('//')[0]
            if '(' in decl.split('=')[0] and not decl.lstrip().startswith('}'): continue   # (a function)
            for part in re.split(r'[;,]', decl):
                m = re.search(r'([A-Za-z_]\w*)\s*(\[[^\]]*\]\s*)*(=.*)?$', part.strip())
                if m: names.add(m.group(1))
    return names

def main():
    SKIP.update(declared_in(NO_STATE_FILES))
    ms = msvc(sys.argv[1]); cl, local = clang(sys.argv[2])
    for obj, (rel, tag) in OUT.items():
        a, b = ms.get(obj, set()), cl.get(obj, set())
        only_ms, only_cl = sorted(a - b - SKIP), sorted(b - a - SKIP)
        if only_ms: print(obj, 'only Visual C++ (kept if the source has it everywhere):', ' '.join(only_ms))
        if only_cl: print(obj, 'only clang:', ' '.join(only_cl))
        names = sorted((a | b) - SKIP)
        lines = ['/* %s - generated by libretro/tools/gen_state_vars.py: the save state\'s variables of this file, by name. */' % os.path.basename(rel),
                 'void vxs_vars_%s (void (*add) (void *p, size_t n, int is_long))' % tag, '{']
        for n in names:
            if n in STRUCT:
                for f in STRUCT[n]:
                    if f.startswith('@'):
                        f0, f1 = f[1:].split('..')
                        lines.append('\tadd (&%s.%s, (size_t) ((char *) (&%s.%s + 1) - (char *) &%s.%s), 0);' % (n, f0, n, f1, n, f0))
                    elif f.startswith('+'): lines.append('\tadd (&%s.%s, sizeof %s.%s, 1);' % (n, f[1:], n, f[1:]))
                    else: lines.append('\tadd (&%s.%s, sizeof %s.%s, 0);' % (n, f, n, f))
            else:
                lines.append('\tadd (&%s, sizeof %s, %d);' % (n, n, 1 if n in LONG else 0))
        lines.append('}')
        open(os.path.join(R, rel), 'w', newline='\n').write('\n'.join(lines) + '\n')
        print(rel, len(names), 'variables')
    for obj, s in local.items():
        print(obj, 'statics inside functions (not saved; move one to file scope if it is the game\'s state):', ' '.join(sorted(s)))

main()
