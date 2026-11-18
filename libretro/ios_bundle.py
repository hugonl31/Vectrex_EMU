"""ios_bundle.py - everything the iOS app (a private TestFlight build, the owner's own) unpacks into its RetroArch folder
(Documents/RetroArch) on the first start after each install or update, ready to play as the desktop app:
  system/vectrex_emu/   the BIOS, the overlays, the GPU path's pictures, the UVMC2's SD card folder, the speech ROM (the
                        PiTrex's SD card, pitrex/, is not in the app: put there once, every update keeps it)
  roms/                 Last Human, its UVMC2 edition, the UVMC2 demo and movie player, the GCE games, the free homebrew
  info/ config/         the core's info file; its override (Max Users 2) and remap (ports 1 and 2 the Vectrex controller)
  playlists/Vectrex_EMU.lpl   every game, linked to the core built into the app
The games, the speech ROM and the PiTrex files come as the desktop's Vectrex > Get games & extras gets them (getter.py,
the same sources in the same order). Paths in the playlist: '~' the app's data folder, ':' the app itself - both move
with each update, RetroArch puts them back (playlist_resolve_path).
  python ios_bundle.py <core dylib> <out folder>
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import json, os, shutil, subprocess, sys, types, zipfile

H = os.path.dirname(os.path.abspath(__file__)); R = os.path.dirname(H)
core, out = sys.argv[1], os.path.abspath(sys.argv[2])
shutil.rmtree(out, ignore_errors=True); os.makedirs(out)

# the RetroArch package the other systems get (system, roms, info, config) - without its cores folder: the core is
# in the app (Frameworks)
pz = os.path.join(out, '..', 'vx_ios_pkg.zip')
subprocess.check_call([sys.executable, os.path.join(H, 'package_retroarch.py'), 'ios-arm64', core, pz])
with zipfile.ZipFile(pz) as z:
    for n in z.namelist():
        if not n.startswith('cores/') and n != 'INSTALL.txt':
            z.extract(n, out)
os.remove(pz)

# the downloads: getter.py without its window
sys.modules['tkinter'] = types.ModuleType('tkinter')
sys.path.insert(0, os.path.join(R, 'src', 'frontend'))
import getter                                                   # noqa: E402
getter.V = types.SimpleNamespace(ROOT=out)
sv = os.path.join(out, 'system', 'vectrex_emu')
for key in ('games', 'homebrew', 'speech'):   # (the PiTrex's files, ~690 MB: not in the app - put on the phone once,
                                              #  system/vectrex_emu/pitrex, kept by every update: unpacking adds, never deletes)
    try:
        a, k = getter.get_item(key, lambda d, t: None, lambda s: print('  ' + key + ': ' + s, flush=True),
                               os.path.join(sv, 'pitrex'))
        print('%s: %d files' % (key, a + k), flush=True)
    except Exception as e:   # noqa
        print('%s: NOT GOT (%s)' % (key, e), flush=True)
sp = os.path.join(out, 'system', 'al2.bin')                     # (the speech ROM: in the core's own folder)
if os.path.exists(sp):
    shutil.move(sp, os.path.join(sv, 'al2.bin'))
print('GCE games %d of %d, homebrew %d of %d' % (getter.games_have() + getter.homebrew_have()))

# the playlist: every game in roms (its folders too), and the PiTrex
CORE = ':/Frameworks/vectrex.emu.libretro.framework'
items = []
rd = os.path.join(out, 'roms')
for dp, dn, fn in os.walk(rd):
    for f in fn:
        s, e = os.path.splitext(f)
        if e.lower() in ('.bin', '.vec', '.gam', '.um2', '.v4e'):
            rel = os.path.relpath(os.path.join(dp, f), out).replace(os.sep, '/')
            items.append((s, rel))
items.append(('PiTrex', 'system/vectrex_emu/pitrex/pitrex.img'))   # (there once its folder is)
items.sort(key=lambda i: i[0].lower())
pl = {'version': '1.5', 'default_core_path': CORE, 'default_core_name': 'Vectrex_EMU', 'label_display_mode': 0,
      'right_thumbnail_mode': 0, 'left_thumbnail_mode': 0, 'sort_mode': 0,
      'items': [{'path': '~/Documents/RetroArch/' + rel, 'label': label, 'core_path': CORE, 'core_name': 'Vectrex_EMU',
                 'crc32': 'DETECT', 'db_name': 'Vectrex_EMU.lpl'} for label, rel in items]}
os.makedirs(os.path.join(out, 'playlists'), exist_ok=True)
with open(os.path.join(out, 'playlists', 'Vectrex_EMU.lpl'), 'w', encoding='utf-8') as f:
    json.dump(pl, f, indent=2)
print('playlist: %d games' % len(items))

# the on-screen gamepad (RetroArch's neo-retropad, in overlays/gamepads - the workflow adds it): on with this core,
# hidden while a controller is connected - an iPhone / iPad has no buttons of its own
with open(os.path.join(out, 'config', 'Vectrex_EMU', 'Vectrex_EMU.cfg'), 'a', encoding='utf-8') as f:
    f.write('input_overlay_enable = "true"\n'
            'input_overlay = "~/Documents/RetroArch/overlays/gamepads/neo-retropad/neo-retropad.cfg"\n'
            'input_overlay_hide_when_gamepad_connected = "true"\n')

# a keyboard (the iPad's Magic Keyboard): the core reads it itself, as the desktop (its option Keyboard as on the PC,
# on here) - so RetroArch maps no key to the RetroPad (a controller keeps RetroArch's own mapping, untouched) and its
# own keys stay only where the core has none: ` Game Focus (every key to the core, Space and M too), M RetroArch's
# menu (the iPad's keyboard has no Esc or F keys), Space held fast forward, F12 a snapshot. Its other keys on letters
# the core uses off (P its pause, L / Space fast forward, O movie recording, E slow motion, R rewind, K frame advance)
with open(os.path.join(out, 'config', 'Vectrex_EMU', 'Vectrex_EMU.cfg'), 'a', encoding='utf-8') as f:
    for b in ('up', 'down', 'left', 'right', 'a', 'b', 'x', 'y', 'l', 'r', 'l2', 'r2', 'l3', 'r3', 'select', 'start'):
        f.write('input_player1_%s = "nul"\n' % b)
    for k, v in (('input_game_focus_toggle', 'tilde'), ('input_menu_toggle', 'm'), ('input_hold_fast_forward', 'space'),
                 ('input_toggle_fast_forward', 'nul'), ('input_screenshot', 'f12'), ('input_exit_emulator', 'nul'),
                 ('input_toggle_fullscreen', 'nul'), ('input_pause_toggle', 'nul'), ('input_rewind', 'nul'),
                 ('input_movie_record_toggle', 'nul'), ('input_hold_slowmotion', 'nul'), ('input_frame_advance', 'nul'),
                 ('input_reset', 'nul'), ('input_shader_next', 'nul'), ('input_shader_prev', 'nul'),
                 ('input_netplay_game_watch', 'nul'), ('input_volume_up', 'nul'), ('input_volume_down', 'nul')):
        f.write('%s = "%s"\n' % (k, v))
