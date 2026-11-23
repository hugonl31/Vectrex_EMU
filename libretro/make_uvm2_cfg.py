"""make_uvm2_cfg.py - the UVMC2 calibration that comes with the emulator (package/uvmc2/.conf/uvm2.cfg and
config/uvm2.cfg, 512 bytes) and its version (vectrex_emu.py UVM2_CFG_VERSION) as C for the core -> uvm2_cfg_tab.h:
RetroArch players get the core alone, without the package. Run when the calibration changes (and bump the version in
vectrex_emu.py: every card gets it once more).
  python make_uvm2_cfg.py           -> uvm2_cfg_tab.h
  python make_uvm2_cfg.py --check   -> exit 1 unless the two files, vectrex_emu.py's UVM2_CFG_DEFAULT and the header agree
                                       (and package/uvmc2 has no marker or backup: never shipped)
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import ast, os, sys

here = os.path.dirname(os.path.abspath(__file__))
R = os.path.dirname(here)
OUT = os.path.join(here, 'uvm2_cfg_tab.h')


def pc_values():
    """vectrex_emu.py's UVM2_CFG_DEFAULT and UVM2_CFG_VERSION, from its source (no import: it needs a display)"""
    src = open(os.path.join(R, 'src', 'frontend', 'vectrex_emu.py'), encoding='utf-8').read()
    got = {}
    for node in ast.parse(src).body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name) \
                and node.targets[0].id in ('UVM2_CFG_DEFAULT', 'UVM2_CFG_VERSION'):
            got[node.targets[0].id] = eval(compile(ast.Expression(node.value), 'vectrex_emu.py', 'eval'), {'__builtins__': {}})
    return got['UVM2_CFG_DEFAULT'], got['UVM2_CFG_VERSION']


def header():
    files = [open(os.path.join(R, 'package', 'uvmc2', sub, 'uvm2.cfg'), 'rb').read() for sub in ('.conf', 'config')]
    cfg, ver = pc_values()
    errs = []
    if len(files[0]) != 512:
        errs.append('package/uvmc2/.conf/uvm2.cfg: %d bytes, not 512' % len(files[0]))
    if files[0] != files[1]:
        errs.append('package/uvmc2/.conf/uvm2.cfg and config/uvm2.cfg differ')
    if cfg != files[0]:
        errs.append('vectrex_emu.py UVM2_CFG_DEFAULT is not package/uvmc2/.conf/uvm2.cfg')
    for dp, dn, fn in os.walk(os.path.join(R, 'package', 'uvmc2')):   # (the marker in a release: the PC updater adds
        for f in fn:                                                     #  missing files - it would skip the update)
            if f == 'vectrex_emu_cal' or f.startswith('uvm2.cfg.'):
                errs.append('package/uvmc2: %s must not be shipped' % os.path.relpath(os.path.join(dp, f), R))
    rows = [', '.join('0x%02x' % b for b in files[0][i:i + 16]) for i in range(0, len(files[0]), 16)]
    h = ('/* uvm2_cfg_tab.h - made by make_uvm2_cfg.py from package/uvmc2/.conf/uvm2.cfg; do not edit. The UVMC2\n'
         ' * calibration that comes with the emulator and its version (vectrex_emu.py UVM2_CFG_VERSION) */\n'
         '#define UVM2_CFG_VERSION "%s"\n'
         'static const unsigned char uvm2_cfg_new[%d] = {\n\t%s };\n' % (ver, len(files[0]), ',\n\t'.join(rows)))
    return h, errs


if __name__ == '__main__':
    h, errs = header()
    for e in errs:
        print('make_uvm2_cfg:', e)
    if '--check' in sys.argv:
        old = open(OUT, encoding='ascii').read() if os.path.exists(OUT) else ''
        if old != h:
            print('make_uvm2_cfg: uvm2_cfg_tab.h is not the calibration - run libretro/make_uvm2_cfg.py')
            errs.append('header')
        print('make_uvm2_cfg: %s' % ('FAILED' if errs else 'the core, the PC and the package have the same calibration'))
        sys.exit(1 if errs else 0)
    if errs:
        sys.exit(1)
    with open(OUT, 'w', encoding='ascii', newline='\n') as f:
        f.write(h)
    print('make_uvm2_cfg: wrote', OUT)
