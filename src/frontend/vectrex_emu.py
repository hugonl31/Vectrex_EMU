#!/usr/bin/env python3
r"""Vectrex_EMU - a Vectrex emulator that looks and sounds like a real Vectrex.

    Vectrex_EMU.exe [ROM]          (or: pythonw src\frontend\vectrex_emu.py [ROM])

Without a ROM the Vectrex starts the cartridge used last (or, without one, its built-in game Mine Storm).

The emulator core (core\vectrex_core.exe, built from src\core: the vecx core, GPL) runs the BIOS and
the cartridge and sends every frame's vectors with their timing; this window draws the beam as a
soft round spot whose brightness and width follow the time it spent per pixel, with phosphor
afterglow and bloom, the mains hum wobble, the brightness bending of the high voltage, the
Vectrex buzz and the small built-in speaker. The analog model (zero-reference drift, the shift
register timing, the BIOS text offset, the beam look) was fitted to photos, a video and a sound
recording of a real Vectrex (2026-10-08).

Keys (only what full screen needs; everything else is in the menu bar, the keyboard is the players')
  F11 full screen, Esc (ROM browser: close; full screen: back to the window; window: quit),
  L ROM browser, O overlay on / off, P pause, Ctrl+E eject (Mine Storm), Ctrl+R RESET button,
  Page Up / Page Down the volume knob, which is also the power switch, as on the Vectrex: down to
  0 % = off, up from 0 % = on (a dirty potentiometer while turning), + / - / 0 size
Controls (Controls > Keyboard and controllers): each player's controller (the connected ones, by name) and keys - both
  work at once; each controller keeps its own axes, buttons and dead zone; a test field per player.
  Defaults: player 1 arrows + A W S D (buttons 1-4), Shift half stick, and the first controller found
  (its X / Y axes, buttons 1-4; an Xbox pad: the left stick, X Y A B); player 2 keyboard, no keys.
Settings are kept in system/settings.json next to the exe (or in %APPDATA%\Vectrex_EMU if that folder
is read-only). The screen is shown at a fixed 60 Hz whatever the game's frame rate: every line lights
the phosphor at the moment the beam drew it and the spot fades from there to black, so afterglow and
trails move smoothly (vsync on a 60 / 120 / 240 Hz monitor, else paced to 60 Hz - G-Sync shows it as is).
"""
import argparse, ctypes, ctypes.wintypes as W, json, math, os, queue, random, shutil, struct, subprocess
import sys, threading, time, datetime, collections, re, gc, platform
import tkinter as tk
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vecrender
import vart
import deck
import darkmenu
deck.V = sys.modules[__name__]                  # (the control deck reads this module's tables)
import getter
getter.V = sys.modules[__name__]                # (Get games & extras: the folders)
import pctest
pctest.V = sys.modules[__name__]                # (Test this PC: the Quality profile that suits it)
import v4e
import fatimg
import links

IS_WIN, IS_MAC = sys.platform == 'win32', sys.platform == 'darwin'
# Steam Deck Game Mode (gamescope): one window is shown - a second one (the first-run setup) stays out of
# sight, and there is no mouse for the volume knob: there the setup is skipped and the Vectrex starts on
IN_GAMESCOPE = bool(os.environ.get('GAMESCOPE_WAYLAND_DISPLAY')) or os.environ.get('XDG_CURRENT_DESKTOP', '').lower() == 'gamescope'
def _raspberry_pi():
    """a Raspberry Pi (its device tree names the board): its V3D graphics want the Low quality profile"""
    for f in ('/proc/device-tree/model', '/sys/firmware/devicetree/base/model'):
        try:
            with open(f, 'rb') as fh:
                if b'Raspberry Pi' in fh.read():
                    return True
        except OSError:
            pass
    return False
IS_PI = sys.platform.startswith('linux') and _raspberry_pi()
if not IS_WIN:                                          # macOS / Linux: the window, keys, controllers, sound
    import osdep
if getattr(sys, 'frozen', False):                       # Vectrex_EMU.exe (PyInstaller; macOS: next to Vectrex_EMU.app)
    ROOT = os.path.dirname(os.path.abspath(sys.executable)) if IS_WIN else osdep.app_root(sys.executable)
else:                                                   # src\frontend\vectrex_emu.py
    ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
# the core and the power sounds travel inside the program (Vectrex_EMU.exe: unpacked to its temp folder at
# start); from the sources they come from the project folders. Next to the program, the same on every system:
# bios (bios.bin, the speech ROM), roms (the user's ROMs) and overlays (Mine Storm (built in).art and the user's)
RES = getattr(sys, '_MEIPASS', ROOT)
CORE = os.path.join(RES, 'core', 'vectrex_core.exe' if IS_WIN else 'vectrex_core')
if os.environ.get('VECTREX_CORE'):                       # (tests: another build of the core, e.g. one tuned for this CPU)
    CORE = os.environ['VECTREX_CORE']
# the folder 'system' next to the program: the BIOS, the speech ROM, the settings (it was called 'bios' before:
# renamed once, when it can be)
if os.path.isdir(os.path.join(ROOT, 'bios')) and not os.path.exists(os.path.join(ROOT, 'system')):
    try:
        os.replace(os.path.join(ROOT, 'bios'), os.path.join(ROOT, 'system'))
    except OSError:
        pass
SYS_DIRS = [os.path.join(ROOT, 'system'), os.path.join(ROOT, 'bios'), os.path.join(RES, 'system'), os.path.join(RES, 'bios')]
BIOS = next((os.path.join(d, 'bios.bin') for d in SYS_DIRS if os.path.exists(os.path.join(d, 'bios.bin'))),
            os.path.join(ROOT, 'system', 'bios.bin'))
# the VecVoice's speech ROM (SP0256-AL2, (c) Microchip Technology Inc.): not shipped - the user downloads
# it from Joe Zbiciak's page (Microchip let him distribute it) into the system folder next to the program
SP_ROM_URL = 'http://spatula-city.org/~im14u2c/sp0256-al2/'
def sp_rom():
    for d in SYS_DIRS:
        for n in ('sp0256-al2.bin', 'al2.bin'):
            if os.path.exists(os.path.join(d, n)):
                return os.path.join(d, n)
    return None
def dark_title(win):
    """the window's title bar and border dark, as the menus: DWM attributes (Windows only)"""
    if not IS_WIN:
        return
    try:
        hwnd = ctypes.windll.user32.GetParent(win.winfo_id()) or win.winfo_id()
        for a, v in ((20, 1), (35, 0x000e0f0b), (34, 0x00252a1d), (36, 0x00cfd6c8)):   # dark mode, caption, border, text
            val = ctypes.c_int(v)
            ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, a, ctypes.byref(val), 4)
    except Exception:   # noqa
        pass
if IS_WIN:                      # (Windows' own dialogs - Open, Save, folders - dark too: the app's preferred mode,
    try:                        #  uxtheme ordinal 135 SetPreferredAppMode(ForceDark), 136 FlushMenuThemes; Windows 10
        _ux = ctypes.windll.uxtheme  #  1903+ and 11 - older ones ignore it)
        _ux[135](2); _ux[136]()
    except Exception:   # noqa
        pass
DARK_MENU = not IS_MAC          # (the menu bar drawn by the emulator, dark; macOS keeps its own at the top)
def _writable(d):
    try:
        t = os.path.join(d, '.write_test'); open(t, 'w').close(); os.remove(t); return True
    except OSError:
        return False
# The settings (JSON, SETTINGS_F) live next to the exe (portable: copy the folder and the settings come along); a
# folder that cannot be written (e.g. Program Files) falls back to %APPDATA%\Vectrex_EMU
APPDATA_DIR = os.path.join(os.environ.get('APPDATA', ROOT), 'Vectrex_EMU') if IS_WIN else osdep.settings_dir()
SETTINGS_DIR = ROOT if _writable(ROOT) else APPDATA_DIR
# the settings file: system/settings.json next to the program (with the BIOS: the folders next to it are for the
# ROMs, overlays and cards), else settings.json in the fallback folder; the earlier names are moved there once
SETTINGS_F = os.path.join(ROOT, 'system', 'settings.json') if SETTINGS_DIR == ROOT else os.path.join(APPDATA_DIR, 'settings.json')
def _move_old_settings():
    if os.path.exists(SETTINGS_F):
        return
    for old in (os.path.join(ROOT, 'bios', 'settings.json'), os.path.join(SETTINGS_DIR, 'Vectrex_EMU.conf'),
                os.path.join(SETTINGS_DIR, 'settings.json')):
        if os.path.abspath(old) != os.path.abspath(SETTINGS_F) and os.path.exists(old):
            try:
                os.makedirs(os.path.dirname(SETTINGS_F), exist_ok=True)
                os.replace(old, SETTINGS_F)
            except OSError:
                pass
            return
_move_old_settings()
# Next to the program, every folder you put things in is one level down: roms, overlays, system (BIOS, speech ROM,
# settings), uvmc2 (the UVMC2's SD card) and pitrex (the PiTrex's). 'cache' only holds working files: the SD card
# images a UVMC2 or PiTrex game runs from, built from those folders at its start and copied back at its end.
CACHE_DIR = os.path.join(SETTINGS_DIR, 'cache')
CARD_CACHE = os.path.join(CACHE_DIR, 'card_md5.json')     # (online play: the SD cards' file checksums, kept)
SD_DEFAULT = os.path.join(ROOT if SETTINGS_DIR == ROOT else SETTINGS_DIR, 'uvmc2')
PX_DEFAULT = os.path.join(ROOT, 'pitrex')
# (earlier places: sd, disks/sd and disks/pitrex - moved once; a setting that still names one follows the card)
OLD_CARDS = {SD_DEFAULT: [os.path.join(SETTINGS_DIR, 'sd'), os.path.join(SETTINGS_DIR, 'disks', 'sd')],
             PX_DEFAULT: [os.path.join(ROOT, 'disks', 'pitrex')]}
def _move_old_cards():
    for new, olds in OLD_CARDS.items():
        for old in olds:
            if os.path.isdir(old) and not os.path.exists(new):
                try:
                    os.replace(old, new)
                except OSError:
                    pass
    old = os.path.join(SETTINGS_DIR, 'disks')                 # (its images: into the cache; the folder gone if empty)
    if os.path.isdir(old):
        try:
            os.makedirs(CACHE_DIR, exist_ok=True)
            for f in os.listdir(old):
                if f.lower().endswith('.img') and not os.path.exists(os.path.join(CACHE_DIR, f)):
                    os.replace(os.path.join(old, f), os.path.join(CACHE_DIR, f))
            if not os.listdir(old):
                os.rmdir(old)
        except OSError:
            pass
_move_old_cards()
def _card_dir(d, default):
    """a card folder from the settings: the default when unset, or when it names an old place that has moved"""
    if not d:
        return default
    if not os.path.isdir(d) and os.path.isdir(default) and \
            os.path.abspath(d) in [os.path.abspath(o) for o in OLD_CARDS.get(default, [])] + [os.path.abspath(os.path.join(ROOT, 'sd'))]:
        return default
    return d
NUL = 'NUL' if IS_WIN else os.devnull      # the core's input recording and an empty cartridge slot: nothing is written or kept
ALG_MAX_X, ALG_MAX_Y = 35000, 41000      # (the core's canvas, vecx.h)
LIVE_OFS = 8000                          # (the lines' coordinates come + this: the core draws 6000 past the canvas, live.c)
OSD_KMIN = 1.5      # the window OSD's smallest text: 6 x this px high (9 px)
MONO, SANS = ('Consolas', 'Segoe UI') if IS_WIN else ('Menlo', 'Helvetica') if IS_MAC else ('DejaVu Sans Mono', 'DejaVu Sans')
RCLICK = '<Button-2>' if IS_MAC else '<Button-3>'    # (the right mouse button: Tk on macOS numbers it 2)
RATE = 44100                      # 16-bit mono from the emulator's AY model (tools/vtest/psg.c)

# ------------------------------------------------------------------ input
class XINPUT_GAMEPAD(ctypes.Structure):
    _fields_ = [('wButtons', W.WORD), ('bLeftTrigger', ctypes.c_ubyte), ('bRightTrigger', ctypes.c_ubyte),
                ('sThumbLX', ctypes.c_short), ('sThumbLY', ctypes.c_short),
                ('sThumbRX', ctypes.c_short), ('sThumbRY', ctypes.c_short)]
class XINPUT_STATE(ctypes.Structure):
    _fields_ = [('dwPacketNumber', W.DWORD), ('Gamepad', XINPUT_GAMEPAD)]
class JOYINFOEX(ctypes.Structure):
    _fields_ = [(n, W.DWORD) for n in ('dwSize', 'dwFlags', 'dwXpos', 'dwYpos', 'dwZpos', 'dwRpos', 'dwUpos',
                                       'dwVpos', 'dwButtons', 'dwButtonNumber', 'dwPOV', 'dwReserved1',
                                       'dwReserved2')]

XI_UP, XI_DOWN, XI_LEFT, XI_RIGHT, XI_START = 0x1, 0x2, 0x4, 0x8, 0x10
XI_A, XI_B, XI_X, XI_Y = 0x1000, 0x2000, 0x4000, 0x8000
# the XInput pad buttons that can be mapped (name -> bit; LT / RT are the triggers)
XI_BUTTONS = dict(A=0x1000, B=0x2000, X=0x4000, Y=0x8000, START=0x10, BACK=0x20, LB=0x100, RB=0x200,
                  LS=0x40, RS=0x80, LT=-1, RT=-2)
XI_AXES = ['LX', 'LY', 'RX', 'RY']
XI_AXIS_NAMES = dict(LX='left stick X', LY='left stick Y', RX='right stick X', RY='right stick Y')
# a WinMM joystick's axes (X and Y always, the others when its caps say so)
JOY_AXES = ['X', 'Y', 'Z', 'R', 'U', 'V']
JOY_AXIS_CAP = dict(Z=0x1, R=0x2, U=0x4, V=0x8)
JOY_HASPOV = 0x10
# where Windows keeps a USB joystick's own name (WinMM itself calls them all "Microsoft PC-joystick driver")
REG_OEM = r'System\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_%04X&PID_%04X'
# fire direction (0 = right, counter-clockwise in 45 degree steps) -> button mask
DIR_MASK = [8, 8 | 2, 2, 1 | 2, 1, 1 | 4, 4, 8 | 4]
# the Vectrex's inputs per player: the stick (four directions), buttons 1-4, and a key held for half
# the stick (keyboard only: a controller's stick is analog)
CTRL_INPUTS = ['up', 'down', 'left', 'right', 'b1', 'b2', 'b3', 'b4', 'half']
CTRL_NAMES = dict(up='Up', down='Down', left='Left', right='Right', b1='Button 1', b2='Button 2',
                  b3='Button 3', b4='Button 4', half='Half stick')
# defaults (the owner, 2026-10-19): player 1 on the arrows and A W S D (buttons 1-4: left, top, bottom, right
# - the WASD cross under the left hand), Shift for half the stick;
# player 2 has no keys (a controller, or keys of your own)
DEFAULT_KEYS = {'p1': dict(up=[(0x26, 'Up')], down=[(0x28, 'Down')], left=[(0x25, 'Left')], right=[(0x27, 'Right')],
                           b1=[(0x41, 'A')], b2=[(0x53, 'S')], b3=[(0x44, 'D')], b4=[(0x46, 'F')],   # (a row, as on the controller)
                           half=[(0x10, 'Shift')]),
                'p2': {k: [] for k in CTRL_INPUTS}}
DEFAULT_DEADZONE = 0.2
# keys the emulator keeps for itself (a player can not have them): F11, Esc, Page Up / Down, + - 0 9 (the
# window size, the real size), P L O C (pause, the ROM browser, the overlay, the case) and everything with Ctrl
RESERVED_VK = {0x7A: 'F11', 0x1B: 'Esc', 0x21: 'Page Up', 0x22: 'Page Down', 0x11: 'Ctrl', 0xA2: 'Ctrl', 0xA3: 'Ctrl',
               0xBB: '+', 0xBD: '-', 0x30: '0', 0x39: '9', 0x6B: 'Numpad +', 0x6D: 'Numpad -', 0x60: 'Numpad 0',
               0x69: 'Numpad 9', 0x50: 'P', 0x4C: 'L', 0x4F: 'O', 0x43: 'C'}

# the real Vectrex picture's height (the tube's window behind the overlay), for Display > Real size
REAL_PIC_MM = 207.0     # (the overlay sheet: 162 x 207 mm, the owner measured; the picture is as tall as the sheet)

def default_map(dev):
    """a controller's mapping when it is first chosen (2026-10-11): its X / Y axes the stick, its buttons
    1-4 the Vectrex buttons 1-4 (an Xbox pad: the left stick, X Y A B); remapped in the Controls window.
    mode 'analog': the axes, each with a dead zone (dzx / dzy) and an endpoint (epx / epy: the stick
    position that is already full deflection, for sticks with a long travel), link = X and Y set together;
    mode 'buttons': controller buttons for up / down / left / right (full deflection) and half"""
    m = dict(mode='analog', dzx=DEFAULT_DEADZONE, dzy=DEFAULT_DEADZONE, epx=1.0, epy=1.0, link=True,
             up=[], down=[], left=[], right=[], half=[])
    if dev.startswith('xi'):
        m.update(x='LX', xflip=False, y='LY', yflip=False, b1=['X'], b2=['Y'], b3=['A'], b4=['B', 'START'])
    else:
        m.update(x='X', xflip=False, y='Y', yflip=False, b1=['J1'], b2=['J2'], b3=['J3'], b4=['J4'])
    return m

def shape(v, dz, ep):
    """an axis through its dead zone and endpoint: 0 inside the dead zone, then straight up to full
    deflection at the endpoint (and beyond it)"""
    a = abs(v)
    if a <= dz:
        return 0.0
    return math.copysign(min(1.0, (a - dz) / max(0.01, ep - dz)), v)

class JOYCAPSW(ctypes.Structure):
    _fields_ = [('wMid', W.WORD), ('wPid', W.WORD), ('szPname', W.WCHAR * 32),
                ('wXmin', W.UINT), ('wXmax', W.UINT), ('wYmin', W.UINT), ('wYmax', W.UINT),
                ('wZmin', W.UINT), ('wZmax', W.UINT), ('wNumButtons', W.UINT), ('wPeriodMin', W.UINT),
                ('wPeriodMax', W.UINT), ('wRmin', W.UINT), ('wRmax', W.UINT), ('wUmin', W.UINT),
                ('wUmax', W.UINT), ('wVmin', W.UINT), ('wVmax', W.UINT), ('wCaps', W.UINT),
                ('wMaxAxes', W.UINT), ('wNumAxes', W.UINT), ('wMaxButtons', W.UINT),
                ('szRegKey', W.WCHAR * 32), ('szOEMVxD', W.WCHAR * 260)]

class Input:
    """both players' controls: the keyboard (Windows virtual-key codes) and a controller each (an XInput
    pad or a WinMM / DirectInput joystick / gamepad), both at once. Each controller keeps its own mapping
    (axes, buttons, dead zone), so a player can switch controllers without mapping again. Mapped in the
    Controls window and kept in the settings (system/settings.json)."""
    def __init__(self, conf=None):
        self.xi = None
        self.pads = None
        if not IS_WIN:
            self.pads = osdep.Pads()                    # (SDL2: macOS / Linux)
        for dll in (('xinput1_4', 'xinput1_3', 'xinput9_1_0') if IS_WIN else ()):
            try:
                self.xi = getattr(ctypes.windll, dll); break
            except OSError:
                pass
        if IS_WIN:
            self.winmm = ctypes.windll.winmm
            self.user32 = ctypes.windll.user32
            self._u32 = ctypes.WinDLL('user32')          # (its own prototypes: 64-bit window handles)
            self._u32.GetForegroundWindow.restype = W.HWND
            self._u32.GetWindowThreadProcessId.argtypes = [W.HWND, ctypes.POINTER(W.DWORD)]
        self.focused = True
        self.block = False             # (the ROM browser is open: the game runs on without input)
        self.mouse = None              # (jx, jy, buttons) from the mouse / touchpad when it steers player 1 (App)
        self.device = 'keyboard'
        self._names = {}
        self._devs = []
        self.load(conf or {})
        self.refresh_devices()

    # ---- the settings
    def load(self, c):
        self.keys = {p: {k: [tuple(x) for x in c.get('keys', {}).get(p, {}).get(k, DEFAULT_KEYS[p][k])
                             if x[0] not in RESERVED_VK]        # (a key that became a shortcut: dropped)
                         for k in CTRL_INPUTS} for p in ('p1', 'p2')}
        self.maps = {}
        for d, m in c.get('pads', {}).items():
            m = dict(m)
            if 'deadzone' in m:                         # (2026-10-11: one dead zone, before the endpoints)
                m.setdefault('dzx', m['deadzone']); m.setdefault('dzy', m.pop('deadzone'))
            self.maps[d] = dict(default_map(d), **m)
        # the controller each player has: 'keyboard' (keyboard only), a device id ('xi0'..'xi3',
        # 'joy:VVVV:PPPP[:n]'), or 'auto' (player 1 at first: the first controller that shows up). The
        # settings before 2026-10-11 kept a Windows joystick number ('joy5'): such a number changes when
        # controllers are plugged in / out, so those start again from 'auto'.
        dev = c.get('dev', {})
        self.dev = {}
        for p in ('p1', 'p2'):
            d = dev.get(p, 'auto' if p == 'p1' else 'keyboard')
            if d not in ('auto', 'keyboard') and not re.match(r'(xi\d|joy:[0-9a-f]{4}:[0-9a-f]{4}(:\d+)?)$', d):
                d = 'auto' if p == 'p1' else 'keyboard'
            self.dev[p] = d
        if self.dev['p2'] == 'auto':
            self.dev['p2'] = 'keyboard'
    def save(self):
        return dict(keys={p: {k: [list(x) for x in v] for k, v in d.items()} for p, d in self.keys.items()},
                    pads=self.maps, dev=self.dev)
    def reset(self):
        self.load({})
        self.refresh_devices()
    def map(self, dev):
        """a controller's mapping (made on first use: the defaults)"""
        if dev not in self.maps:
            self.maps[dev] = default_map(dev)
        return self.maps[dev]

    # ---- the controllers
    def oem_name(self, vid, pid):
        if (vid, pid) not in self._names:
            import winreg
            name = ''
            for root in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
                try:
                    with winreg.OpenKey(root, REG_OEM % (vid, pid)) as k:
                        name = str(winreg.QueryValueEx(k, 'OEMName')[0]).strip()
                    if name: break
                except OSError:
                    pass
            self._names[(vid, pid)] = name or 'USB controller %04X:%04X' % (vid, pid)
        return self._names[(vid, pid)]
    def devices(self):
        """the connected controllers, [dict(id, name, kind, index, axes, ranges)]: the XInput (Xbox) pads,
        then the other joysticks / gamepads. A device id stays the same when controllers are plugged in /
        out (Windows' joystick numbers do not): 'joy:' + USB vendor:product (+ ':2' for a second one)."""
        if not IS_WIN:
            return self.pads.devices()
        out = []
        st = XINPUT_STATE()
        if self.xi:
            for i in range(4):
                if self.xi.XInputGetState(i, ctypes.byref(st)) == 0:
                    out.append(dict(id='xi%d' % i, name='Xbox controller %d' % (i + 1), kind='xi', index=i,
                                    axes=XI_AXES))
        xi = bool(out)
        seen = collections.Counter()
        ji = JOYINFOEX(); ji.dwSize = ctypes.sizeof(JOYINFOEX); ji.dwFlags = 0xFF
        for i in range(16):
            if self.winmm.joyGetPosEx(i, ctypes.byref(ji)) != 0:
                continue
            caps = JOYCAPSW()
            if self.winmm.joyGetDevCapsW(i, ctypes.byref(caps), ctypes.sizeof(caps)) != 0:
                continue
            name = self.oem_name(caps.wMid, caps.wPid)
            if xi and (caps.wMid == 0x045E or any(s in name.upper() for s in ('XBOX', 'XINPUT'))):
                continue                                # (an Xbox pad shows up here too)
            seen[(caps.wMid, caps.wPid)] += 1
            n = seen[(caps.wMid, caps.wPid)]
            did = 'joy:%04x:%04x' % (caps.wMid, caps.wPid) + ('' if n == 1 else ':%d' % n)
            axes = [a for a in JOY_AXES if a in 'XY' or caps.wCaps & JOY_AXIS_CAP[a]]
            ranges = {a: (getattr(caps, 'w%smin' % a), getattr(caps, 'w%smax' % a)) for a in JOY_AXES}
            out.append(dict(id=did, name=name + ('' if n == 1 else ' (%d)' % n), kind='joy', index=i, axes=axes,
                            ranges=ranges, buttons=caps.wNumButtons, pov=bool(caps.wCaps & JOY_HASPOV)))
        return out
    def refresh_devices(self):
        self._devs = devs = self.devices()
        self._devs_t = time.perf_counter()
        for d in devs:                                  # (the name, for when it is not connected)
            self.map(d['id'])['name'] = d['name']
        for p in ('p1', 'p2'):                          # 'auto': the first free controller, from then on
            if self.dev[p] == 'auto':
                d = self.resolve(p)
                if d: self.dev[p] = d['id']
    def other(self, p):
        return 'p2' if p == 'p1' else 'p1'
    def resolve(self, p):
        """the connected controller a player has now (a dict from devices()), or None: keyboard only"""
        want = self.dev[p]
        if want == 'keyboard':
            return None
        if want == 'auto':
            taken = self.dev[self.other(p)]
            return next((d for d in self._devs if d['id'] != taken), None)
        return next((d for d in self._devs if d['id'] == want), None)
    def set_dev(self, p, dev):
        """a player takes a controller (or 'keyboard'); a controller is one player's: the other player
        who had it is left with the keyboard"""
        self.dev[p] = dev
        o = self.other(p)
        if dev != 'keyboard' and self.dev[o] == dev:
            self.dev[o] = 'keyboard'
        if dev != 'keyboard':
            self.map(dev)
    def pad_state(self, d):
        """a controller's state: dict(axes={name: -1..1}, buttons={pressed names}, pov=(x, y) or None),
        or None (unplugged)"""
        if not d:
            return None
        if not IS_WIN:
            return self.pads.pad_state(d)
        if d['kind'] == 'xi':
            st = XINPUT_STATE()
            if not self.xi or self.xi.XInputGetState(d['index'], ctypes.byref(st)) != 0:
                return None
            g = st.Gamepad
            b = {n for n, bit in XI_BUTTONS.items() if bit > 0 and g.wButtons & bit}
            if g.bLeftTrigger > 128: b.add('LT')
            if g.bRightTrigger > 128: b.add('RT')
            pov = None
            if g.wButtons & (XI_LEFT | XI_RIGHT | XI_UP | XI_DOWN):     # (the d-pad: the stick too)
                pov = ((1.0 if g.wButtons & XI_RIGHT else 0.0) - (1.0 if g.wButtons & XI_LEFT else 0.0),
                       (1.0 if g.wButtons & XI_UP else 0.0) - (1.0 if g.wButtons & XI_DOWN else 0.0))
            return dict(axes=dict(LX=g.sThumbLX / 32767.0, LY=g.sThumbLY / 32767.0,
                                  RX=g.sThumbRX / 32767.0, RY=g.sThumbRY / 32767.0), buttons=b, pov=pov)
        ji = JOYINFOEX(); ji.dwSize = ctypes.sizeof(JOYINFOEX); ji.dwFlags = 0xFF
        if self.winmm.joyGetPosEx(d['index'], ctypes.byref(ji)) != 0:
            return None
        axes = {}
        for a in d['axes']:
            lo, hi = d['ranges'][a]
            v = getattr(ji, 'dw%spos' % a)
            axes[a] = max(-1.0, min(1.0, 2.0 * (v - lo) / max(1, hi - lo) - 1.0))
        pov = None
        if d.get('pov') and ji.dwPOV != 0xFFFF:             # (the hat: the stick too)
            a = math.radians(ji.dwPOV / 100.0)
            pov = (float(round(math.sin(a))), float(round(math.cos(a))))
        return dict(axes=axes, buttons={'J%d' % (i + 1) for i in range(32) if ji.dwButtons & (1 << i)}, pov=pov)
    @staticmethod
    def natural(d, axis_of):
        """-1 where a raw axis runs the other way than the Vectrex stick: a joystick's Y (and Z, R...)
        axis grows downwards (HID), an Xbox pad's grows upwards"""
        return -1.0 if axis_of == 'y' and d['kind'] == 'joy' else 1.0
    def stick(self, d, st):
        """a controller's stick as the Vectrex sees it before the dead zone: (x, y), y positive = up"""
        m = self.map(d['id'])
        x = st['axes'].get(m['x'], 0.0) * (-1.0 if m['xflip'] else 1.0)
        y = st['axes'].get(m['y'], 0.0) * self.natural(d, 'y') * (-1.0 if m['yflip'] else 1.0)
        if st['pov']:
            x, y = st['pov']
        return x, y

    # ---- reading
    def update_focus(self):
        """the keys count only while one of this program's windows is in front (not while typing elsewhere)"""
        if not IS_WIN:                                  # (macOS / Linux: Tk's focus events set it)
            if not self.focused: osdep.KEYS.clear()
            return
        pid = W.DWORD()
        self._u32.GetWindowThreadProcessId(self._u32.GetForegroundWindow(), ctypes.byref(pid))
        self.focused = pid.value == os.getpid()
    kbd_off = False                     # (typing on a UVMC2's USB keyboard: the keyboard is not the controller)
    def keydown(self, vk):
        if self.kbd_off:
            return False
        if not IS_WIN:
            return self.focused and osdep.KEYS.held(vk)
        return self.focused and bool(self.user32.GetAsyncKeyState(vk) & 0x8000)
    def read_player(self, p, kbd_dirs=True):
        """(jx, jy, buttons, the controller's stick before the dead zone or None). The keyboard and the
        controller work at once: the buttons add up, a direction key overrides the stick on its axis
        (kbd_dirs False: not the keyboard's directions - the ROM browser has the arrow keys itself)."""
        if time.perf_counter() - getattr(self, '_devs_t', -9) > 3.0:   # (controllers plugged in / out)
            self.refresh_devices()
        jx = jy = 0.0; btn = 0; raw = None
        d = self.resolve(p)
        st = self.pad_state(d)
        if st:
            m = self.map(d['id'])
            if m['mode'] == 'buttons':                  # (the stick on buttons: full deflection, or half)
                on = lambda k: bool(st['buttons'] & set(m[k]))
                x = (1.0 if on('right') else 0.0) - (1.0 if on('left') else 0.0)
                y = (1.0 if on('up') else 0.0) - (1.0 if on('down') else 0.0)
                if on('half'): x, y = x / 2, y / 2
                if st['pov']: x, y = st['pov']
                raw = jx, jy = x, y
            else:
                raw = self.stick(d, st)
                jx, jy = shape(raw[0], m['dzx'], m['epx']), shape(raw[1], m['dzy'], m['epy'])
            for i, k in enumerate(('b1', 'b2', 'b3', 'b4')):
                if st['buttons'] & set(m[k]): btn |= 1 << i
            if d['kind'] == 'xi' and not {m['x'], m['y']} & {'RX', 'RY'}:     # (the right stick: the fire directions)
                rs = st['axes']['RX'], st['axes']['RY']
                if math.hypot(*rs) > 0.5:
                    btn |= DIR_MASK[int(round(math.atan2(rs[1], rs[0]) / (math.pi / 4))) % 8]
        km = self.keys[p]
        down = lambda k: any(self.keydown(vk) for vk, _ in km[k])
        kx = (1.0 if down('right') else 0.0) - (1.0 if down('left') else 0.0) if kbd_dirs else 0.0
        ky = (1.0 if down('up') else 0.0) - (1.0 if down('down') else 0.0) if kbd_dirs else 0.0
        h = 0.5 if (kx or ky) and down('half') else 1.0
        if kx: jx = kx * h
        if ky: jy = ky * h
        for i, k in enumerate(('b1', 'b2', 'b3', 'b4')):
            if down(k): btn |= 1 << i
        c = lambda v: max(-128, min(127, int(round(v * 127))))
        return c(jx), c(jy), btn, raw
    def read(self):
        """(jx, jy, buttons) of player 1 and of player 2"""
        if self.block:
            return (0, 0, 0), (0, 0, 0)
        self.update_focus()
        a = self.read_player('p1'); b = self.read_player('p2')
        self.device = 'pad' if a[3] else 'keyboard'
        ms = self.mouse                                         # (the mouse / touchpad as player 1's stick)
        if ms:
            jx, jy, btn = a[:3]
            if abs(ms[0]) > abs(jx): jx = ms[0]
            if abs(ms[1]) > abs(jy): jy = ms[1]
            a = (jx, jy, btn | ms[2])
        return a[:3], b[:3]

# ------------------------------------------------------------------ audio (winmm waveOut)
class WAVEFORMATEX(ctypes.Structure):
    _fields_ = [('wFormatTag', W.WORD), ('nChannels', W.WORD), ('nSamplesPerSec', W.DWORD),
                ('nAvgBytesPerSec', W.DWORD), ('nBlockAlign', W.WORD), ('wBitsPerSample', W.WORD),
                ('cbSize', W.WORD)]
class WAVEHDR(ctypes.Structure):
    pass
WAVEHDR._fields_ = [('lpData', ctypes.c_void_p), ('dwBufferLength', W.DWORD), ('dwBytesRecorded', W.DWORD),
                    ('dwUser', ctypes.c_void_p), ('dwFlags', W.DWORD), ('dwLoops', W.DWORD),
                    ('lpNext', ctypes.c_void_p), ('reserved', ctypes.c_void_p)]
WHDR_DONE = 1

class Biquad:
    """RBJ audio-cookbook biquad (lowpass, highpass, peak, low shelf)"""
    def __init__(self, kind, f0, q, gain_db=0.0):
        w = 2 * math.pi * f0 / RATE; cw, sw = math.cos(w), math.sin(w); al = sw / (2 * q)
        if kind == 'lp':
            b = [(1 - cw) / 2, 1 - cw, (1 - cw) / 2]; a = [1 + al, -2 * cw, 1 - al]
        elif kind == 'hp':
            b = [(1 + cw) / 2, -(1 + cw), (1 + cw) / 2]; a = [1 + al, -2 * cw, 1 - al]
        elif kind == 'ls':
            A = 10 ** (gain_db / 40.0); sa = 2 * math.sqrt(A) * al
            b = [A * ((A + 1) - (A - 1) * cw + sa), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sa)]
            a = [(A + 1) + (A - 1) * cw + sa, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sa]
        else:
            A = 10 ** (gain_db / 40.0)
            b = [1 + al * A, -2 * cw, 1 - al * A]; a = [1 + al / A, -2 * cw, 1 - al / A]
        self.b = [x / a[0] for x in b]; self.a = [x / a[0] for x in a]
        self.z1 = self.z2 = 0.0
    @classmethod
    def raw(cls, b, a):
        """a biquad with these coefficients (a[0] = 1)"""
        f = cls('lp', 1000, 0.7); f.b = list(b); f.a = list(a); return f
    def run(self, xs):
        b0, b1, b2 = self.b; _, a1, a2 = self.a; z1, z2 = self.z1, self.z2
        out = []
        for x in xs:
            y = b0 * x + z1
            z1 = b1 * x - a1 * y + z2
            z2 = b2 * x - a2 * y
            out.append(y)
        self.z1, self.z2 = z1, z2
        return out

class BiquadChain:
    """biquads in a row, run a block of samples at a time with numpy: the same output as running them one after the
    other sample by sample (to ~1e-9 of a sample), ~10 times faster - the sound took 8 % of a fast processor in
    pure Python, under the lock the drawing thread needs too. The chain is one linear system - its state s (two
    values a biquad), each sample y = C s + D x, s' = M s + N x: a block's output is its input convolved with the
    chain's impulse response (one FFT pair; exact - the first n outputs use only the first n of it) plus the carried
    state's own response, and the state after the block two small matrix products"""
    NB = 1024                                           # (longer blocks: in pieces)
    def __init__(self, fs):
        np = _np
        fs = list(fs); m = 2 * len(fs)
        def step(s, x):                                 # (one sample through the chain: the new state, the output)
            s2 = [0.0] * m; u = x
            for j, f in enumerate(fs):
                b0, b1, b2 = f.b; _, a1, a2 = f.a
                y = b0 * u + s[2 * j]
                s2[2 * j] = b1 * u - a1 * y + s[2 * j + 1]
                s2[2 * j + 1] = b2 * u - a2 * y
                u = y
            return s2, u
        M = np.zeros((m, m)); C = np.zeros(m)
        for j in range(m):
            e = [0.0] * m; e[j] = 1.0
            M[:, j], C[j] = step(e, 0.0)
        N, D = step([0.0] * m, 1.0); N = np.array(N)
        h = np.empty(self.NB); R = np.empty((m, self.NB)); G = np.empty((m, self.NB))
        cp, gn = C.copy(), N.copy()
        for t in range(self.NB):                        # (R: each state's output response, G: the state from an
            R[:, t] = cp; G[:, t] = gn                  #  input sample t samples back, h: the impulse response)
            h[t] = D if t == 0 else C @ G[:, t - 1]
            cp = cp @ M; gn = M @ gn
        self.M, self.R, self.G, self.L = M, R, G, 2 * self.NB
        self.H = np.fft.rfft(h, self.L)
        self.s = np.array([v for f in fs for v in (f.z1, f.z2)], dtype=float)
        self.phi = {}
    def run(self, x):
        np = _np
        x = np.asarray(x, dtype=float)
        if len(x) > self.NB:
            return np.concatenate([self.run(x[i:i + self.NB]) for i in range(0, len(x), self.NB)])
        n = len(x)
        if not n:
            return x
        y = np.fft.irfft(np.fft.rfft(x, self.L) * self.H, self.L)[:n] + self.s @ self.R[:, :n]
        P = self.phi.get(n)
        if P is None:
            P = self.phi[n] = np.linalg.matrix_power(self.M, n)
        self.s = P @ self.s + self.G[:, n - 1::-1] @ x
        return y

BUZZ_GAIN = 47.5       # default volume 0.0655: empty-screen buzz ~34 dB below the game sound, +40 % (owner)

LOUDEST = 9000.0        # the volume knob's scratch at its loudest (the owner's reference, 2026-10-08)
POWER_SND_GAIN = 1.6    # the power on / off recordings, 60 % louder (owner, 2026-10-08)

def load_wav(name):
    """a 16-bit mono 44.1 kHz WAV from the sounds folder or the folder above (power on / off)"""
    import wave
    from array import array
    for d in (os.path.join(RES, 'sounds'), os.path.join(ROOT, 'sounds'), ROOT):
        fn = os.path.join(d, name)
        if os.path.exists(fn):
            try:
                w = wave.open(fn)
                if w.getsampwidth() == 2 and w.getnchannels() == 1:
                    a = array('h'); a.frombytes(w.readframes(w.getnframes()))
                    pk = max(abs(x) for x in a) or 1                # skip the silence before the sound
                    k = next(i for i, x in enumerate(a) if abs(x) > pk * 0.05)
                    return list(a[max(0, k - 88):])
            except Exception:       # noqa
                pass
    return []

# the speech module's level: the MiSTer core's Normal (x1.5) and 50 % more (the owner, 2026-10-19)
VOICE_BOOST = 1.5

# the sliders: the default in the middle, this much below and above it (the owner, 2026-10-19)
SLIDER_SPAN = 1.0                   # (0 .. twice the default)

class CrtSound:
    """the Vectrex's own noise mixed into the game sound: the BUZZ (the beam and deflection
    currents leak into the audio amplifier, so it follows what is drawn: louder and harsher with
    more and brighter vectors, repeating at the frame rate), then all of it through the small
    built-in speaker."""
    def __init__(self):
        self.buzz_on = True; self.buzz_vol = 0.0912     # (the owner's choice: 0.152 (2026-10-19) - 40 % (2026-10-22); the buzz does not follow the volume knob)
        self.speaker_on = True                           # the Vectrex's small speaker (see below)
        self.volume = 1.0                                # the volume knob (Page Up / Page Down)
        # the speech module (VecVoice / VecVox): its own amplifier and audio out - its own volume (Ctrl+Page
        # Up / Down) and the speakers owners plug into it: full range (as it comes) or a small speaker
        self.voice_vol = 1.0                             # (100 % = the MiSTer core's Normal level)
        self.voice_full = True
        self.voice_spk = [Biquad('hp', 140, 0.7), Biquad('pk', 1800, 1.0, 2.0), Biquad('lp', 7500, 0.7)]
        # full-range speakers: an arcade cabinet's warm bass (+6 dB below ~150 Hz; no rumble under 40 Hz)
        self.voice_bass = [Biquad('hp', 40, 0.7), Biquad('ls', 150, 0.7, 6.0)]
        # an Ultimate Vectrex Multicart 2 game: the second channel is the cartridge's headphone jack (its
        # PT8211 DAC) - clean and full range beside the Vectrex, through the Vectrex's speaker, or off
        self.jack = False                                # (a .um2 game is running: set by the App)
        self.jack_mode = 'clean'                         # 'clean' / 'speaker' / 'off'
        self.jack_vol = 1.0
        # the 3D Imager's motor (the core's second channel while the goggles are in port 2): beside the Vectrex,
        # clean - not through its knob or speaker (the motor is on the player's head)
        self.motor = False                               # (the 3D Imager is in port 2: set by the App)
        self.motor_on = True
        self.motor_vol = 0.5
        self.scruff_until = 0.0                         # turning the knob: a dirty potentiometer crackles
        self.overlay = []                                # power on / off sound, mixed in after the speaker
        self.snd_on = load_wav('on.wav'); self.snd_off = load_wav('off.wav')
        import random; self.rnd = random.Random(); self.drop = 0; self.crk = 0.0   # (fresh every session)
        self.g_env = 0.0; self.g_tgt = 0.0; self.g_cnt = 0; self.dead = False
        self.new_turn(); self.scruff_until = 0.0; self.dead = False   # (the knob starts with contact)
        self.ph = 0.0; self.xp = 0.0; self.yp = 0.0
        # the buzz as recorded on the owner's Vectrex (buzz.wav, 2026-10-08): a comb of frame-rate
        # harmonics, nearly all between 1.2 and 4.8 kHz, with resonances near 1.45, 2.75 and 3.5 kHz,
        # ~34 dB below the game sound
        self.buzz_lp = [Biquad('hp', 1300, 0.7), Biquad('hp', 1300, 0.7),
                        Biquad('pk', 1450, 4.0, 6.0), Biquad('pk', 2750, 5.0, 5.0), Biquad('pk', 3800, 3.0, 7.0),
                        Biquad('lp', 4700, 0.8), Biquad('lp', 4700, 0.8)]
        # the built-in speaker (the same recording, game sound): little below ~250 Hz, fairly even
        # up to ~5 kHz, down above ~6.5 kHz
        self.spk = [Biquad('hp', 220, 0.9), Biquad('hp', 175, 0.7), Biquad('pk', 270, 1.3, 2.0),   # (cabinet: a little port bass)
                    Biquad('pk', 900, 1.0, 4.0),
                    Biquad('lp', 6200, 0.7), Biquad('lp', 8000, 0.7)]
    def _chain(self, fs):
        """the BiquadChain of a filter list (made at its first use; a new list, a new chain)"""
        cs = self.__dict__.setdefault('_chains', {})
        c = cs.get(id(fs))
        if c is None or c[0] is not fs:
            c = cs[id(fs)] = (fs, BiquadChain(fs))
        return c[1]
    def process(self, pcm, lines, speech=None, arr=None):
        """a game frame's sound (pcm: 16-bit; speech: the second channel) through the Vectrex: the knob, the buzz, the
        speaker, then the accessories beside it - numpy (process_py: the same, sample by sample, without it)"""
        if _np is None:
            return self.process_py(pcm, lines, speech, arr)
        np = _np
        if speech is None and not ((self.buzz_on and self.buzz_vol > 0) or self.speaker_on or self.overlay or self.dead
                or self.volume != 1.0 or time.perf_counter() < self.scruff_until):
            return pcm
        a = np.frombuffer(pcm, dtype=np.int16)
        n = len(a)
        if not n:
            return pcm
        out = a.astype(np.float64)
        jack = None
        if speech and self.motor:                               # (the 3D Imager's motor, not a speech module)
            g = 2.0 * self.motor_vol if self.motor_on else 0.0  # (the core sends it at half scale)
            if g > 0:
                jack = np.frombuffer(speech, dtype=np.int16) * g
            speech = None
        if speech and self.jack:                                # (the UVMC2's jack, not a speech module)
            if self.jack_mode != 'off':
                jack = np.frombuffer(speech, dtype=np.int16) * (2.0 * self.jack_vol)
            speech = None
            if jack is not None and self.jack_mode == 'speaker':   # (plugged into the Vectrex: its knob, its speaker)
                m = min(n, len(jack)); out[:m] += jack[:m]
                jack = None
        if time.perf_counter() < self.scruff_until or self.dead:   # (the knob turning, or on a dead spot: its
            lst = out.tolist(); self.knob(lst); out = np.array(lst, dtype=np.float64)   #  crackle, sample by sample)
        elif self.volume != 1.0:
            out *= self.volume
        if self.buzz_on and self.buzz_vol > 0:
            k = RATE / 1.5e6                                    # samples per CPU cycle
            if arr is not None and len(arr) == len(lines):
                ev = arr['e'] / 8.0
                e = ev * ((arr['c'] & 127) / 127.0)
                i0 = (arr['t'] * k).astype(np.int64); i1 = ((arr['t'] + ev) * k).astype(np.int64)
                ok = i0 < n
                i0, i1, e = i0[ok], np.minimum(n - 1, np.maximum(i0[ok], i1[ok])), e[ok]
                w = e / (i1 - i0 + 1)
                d = np.zeros(n + 1); np.add.at(d, i0, w); np.add.at(d, i1 + 1, -w)
                act = np.cumsum(d)[:n]
            else:
                act = np.zeros(n)
                for ln in lines:
                    if len(ln) < 7: continue
                    e = ln[5] / 8.0 * ((ln[4] & 127) / 127.0)
                    i0 = int(ln[6] * k); i1 = int((ln[6] + ln[5] / 8.0) * k)
                    if i0 >= n: continue
                    i1 = min(n - 1, max(i0, i1)); act[i0:i1 + 1] += e / (i1 - i0 + 1)
            m = min(n, 8)                                       # (the frame's start: a sharp pulse, 0.2 ms)
            act[:m] += 300.0 * (1.0 - np.arange(m) / 8.0)
            if not hasattr(self, 'buzz_hp1'):                   # (the high pass ~300 Hz: a buzz, not a thump - one
                self.buzz_hp1 = [Biquad.raw((0.957, -0.957, 0.0), (1.0, -0.957, 0.0))] + self.buzz_lp   #  pole, as before)
            out += self._chain(self.buzz_hp1).run(act * (self.buzz_vol * BUZZ_GAIN))
        if self.speaker_on:
            out = self._chain(self.spk).run(out)
            out = 32767.0 * np.tanh(out / 26000.0)              # (the driver's limited excursion: a soft limit)
        def soft(v):                                            # (a soft limit above 24000: no hard clipping)
            return np.where(v > 24000, 24000 + 8767 * np.tanh((v - 24000) / 8767),
                            np.where(v < -24000, -24000 - 8767 * np.tanh((-v - 24000) / 8767), v))
        if speech:                                              # (the speech module: not through the
            s = np.frombuffer(speech, dtype=np.int16) * (self.voice_vol * 2.0 * VOICE_BOOST)   # Vectrex's knob, buzz
            s = self._chain(self.voice_bass if self.voice_full else self.voice_spk).run(s)       # or speaker)
            m = min(n, len(s)); out[:m] = soft(out[:m] + s[:m])
        if jack is not None:                                    # (the jack, clean: a line out beside the Vectrex)
            m = min(n, len(jack)); out[:m] = soft(out[:m] + jack[:m])
        if self.overlay:                                        # (the power switch's sound: not through the knob)
            m = min(n, len(self.overlay))
            out[:m] += np.array(self.overlay[:m], dtype=np.float64) * POWER_SND_GAIN
            del self.overlay[:m]
        return np.clip(out, -32768, 32767).astype(np.int16).tobytes()
    def process_py(self, pcm, lines, speech=None, arr=None):
        if speech is None and not ((self.buzz_on and self.buzz_vol > 0) or self.speaker_on or self.overlay or self.dead
                or self.volume != 1.0 or time.perf_counter() < self.scruff_until):
            return pcm
        from array import array
        a = array('h'); a.frombytes(pcm)
        n = len(a)
        if not n:
            return pcm
        out = [float(v) for v in a]
        jack = None
        if speech and self.motor:                               # (the 3D Imager's motor, not a speech module)
            g = 2.0 * self.motor_vol if self.motor_on else 0.0  # (the core sends it at half scale)
            if g > 0:
                sp = array('h'); sp.frombytes(speech)
                jack = [v * g for v in sp]
            speech = None
        if speech and self.jack:                                # (the UVMC2's jack, not a speech module)
            if self.jack_mode != 'off':
                sp = array('h'); sp.frombytes(speech)
                jack = [v * 2.0 * self.jack_vol for v in sp]     # (the core sends it at half scale)
            speech = None
            if jack and self.jack_mode == 'speaker':            # (plugged into the Vectrex: its knob, its speaker)
                for i in range(min(n, len(jack))): out[i] += jack[i]
                jack = None
        self.knob(out)                                          # the volume knob: the game sound only (the
        if self.buzz_on and self.buzz_vol > 0:                  # buzz goes on, even on a dead spot)
            k = RATE / 1.5e6                                    # samples per CPU cycle
            if arr is not None and len(arr) == len(lines):      # (numpy: each line's share added at its first sample,
                ev = arr['e'] / 8.0                             #  taken off after its last, summed up - the same)
                e = ev * ((arr['c'] & 127) / 127.0)
                i0 = (arr['t'] * k).astype(_np.int64); i1 = ((arr['t'] + ev) * k).astype(_np.int64)
                ok = i0 < n
                i0, i1, e = i0[ok], _np.minimum(n - 1, _np.maximum(i0[ok], i1[ok])), e[ok]
                w = e / (i1 - i0 + 1)
                d = _np.zeros(n + 1); _np.add.at(d, i0, w); _np.add.at(d, i1 + 1, -w)
                act = _np.cumsum(d)[:n].tolist()
            else:
                act = [0.0] * n
                for ln in lines:
                    if len(ln) < 7: continue
                    e = ln[5] / 8.0 * ((ln[4] & 127) / 127.0)
                    i0 = int(ln[6] * k); i1 = int((ln[6] + ln[5] / 8.0) * k)
                    if i0 >= n: continue
                    i1 = min(n - 1, max(i0, i1)); w = e / (i1 - i0 + 1)
                    for i in range(i0, i1 + 1): act[i] += w
            # the frame's start: the BIOS zeroes the integrators and recalibrates every frame, even
            # with nothing on screen, so the buzz never stops (its pitch is the frame rate)
            for i in range(min(n, 8)):                          # (a sharp pulse: 0.2 ms)
                act[i] += 300.0 * (1.0 - i / 8.0)
            g = self.buzz_vol * BUZZ_GAIN
            xp, yp = self.xp, self.yp
            hp = []
            for i in range(n):                                  # high pass (~300 Hz): a buzz, not a thump
                x = act[i] * g
                yp = 0.957 * (yp + x - xp); xp = x
                hp.append(yp)
            self.xp, self.yp = xp, yp
            for f in self.buzz_lp:
                hp = f.run(hp)
            for i in range(n):
                out[i] += hp[i]
        if self.speaker_on:
            for f in self.spk:
                out = f.run(out)
            # the driver's limited excursion: a soft limit instead of clean peaks
            out = [32767.0 * math.tanh(v / 26000.0) for v in out]
        if speech:                                              # (the speech module: not through the
            sp = array('h'); sp.frombytes(speech)               #  Vectrex's knob, buzz or speaker)
            s = [v * self.voice_vol * 2.0 * VOICE_BOOST for v in sp]   # (the core sends it at half scale
            for f in (self.voice_bass if self.voice_full else self.voice_spk):   #  for the headroom)
                s = f.run(s)
            for i in range(min(n, len(s))):
                v = out[i] + s[i]
                if v > 24000: v = 24000 + 8767 * math.tanh((v - 24000) / 8767)      # (a soft limit above
                elif v < -24000: v = -24000 - 8767 * math.tanh((-v - 24000) / 8767) #  24000: no hard clipping)
                out[i] = v
        if jack:                                                # (the jack, clean: a line out beside the Vectrex)
            for i in range(min(n, len(jack))):
                v = out[i] + jack[i]
                if v > 24000: v = 24000 + 8767 * math.tanh((v - 24000) / 8767)
                elif v < -24000: v = -24000 - 8767 * math.tanh((-v - 24000) / 8767)
                out[i] = v
        self.mix_overlay(out)
        return array('h', [max(-32768, min(32767, int(v))) for v in out]).tobytes()
    def new_turn(self):
        """a press of Page Up / Page Down: every turn of the worn knob sounds different (never louder
        than LOUDEST): its loudness, the pitch and grain of the scratch, crackle, how long it lasts,
        and whether the wiper loses contact - briefly, for a longer stretch, or (now and then) it stops
        on a dead spot where the sound stays cut until the knob is turned again"""
        r = self.rnd
        self.t_loud = 0.3 + 0.7 * r.random() ** 0.7          # 0.3 .. 1.0 of the loudest
        lo = r.uniform(550, 1300); hi = r.uniform(2400, 4600)
        self.scr_f = [Biquad('hp', lo, 0.7), Biquad('lp', hi, 0.7)]
        self.t_grain = (r.randint(40, 120), r.randint(200, 500))   # grain length range (samples)
        self.t_sharp = r.uniform(1.0, 2.5)                    # grain loudness spread
        self.t_crk = r.uniform(0.0003, 0.003)                 # crackle rate
        self.t_drop = r.uniform(0.0002, 0.0015)               # short contact loss rate
        span = r.uniform(0.3, 0.7)
        self.t_span = span; self.scruff_until = time.perf_counter() + span
        self.cut = []                                         # longer cutouts (start, end) in the turn
        for _ in range(r.choice((0, 0, 0, 1, 1, 2))):
            a = r.uniform(0.0, span * 0.8); self.cut.append((a, a + r.uniform(0.03, 0.25)))
        self.dead = r.random() < 0.08                         # stops on a dead spot
    def knob(self, out):
        """the volume knob on the signal (see new_turn)"""
        v = self.volume
        now = time.perf_counter()
        n = len(out)
        if now >= self.scruff_until:
            if self.dead:                                     # resting on a dead spot: silent, but the
                r = self.rnd; crk = self.crk                  # contact now and then nearly closes
                for i in range(n):
                    if r.random() < 0.0004: crk += (r.random() - 0.5) * 2500.0 * self.t_loud
                    crk *= 0.8
                    out[i] = crk
                self.crk = crk
                return
            if v != 1.0:
                for i in range(n): out[i] *= v
            return
        r = self.rnd
        span = self.t_span; t0 = span - (self.scruff_until - now)
        noise = [r.random() * 2.0 - 1.0 for _ in range(n)]
        for f in self.scr_f:
            noise = f.run(noise)
        g_env = self.g_env; g_tgt = self.g_tgt; g_cnt = self.g_cnt; drop = self.drop; crk = self.crk
        glo, ghi = self.t_grain; sharp = self.t_sharp; pc = self.t_crk; pd = self.t_drop
        amp = LOUDEST * self.t_loud * (0.35 + 0.65 * v)
        cuts = self.cut
        for i in range(n):
            t = t0 + i / RATE
            fade = max(0.0, min(1.0, t / 0.06, (span - t) / 0.12))
            if g_cnt <= 0:                                    # a new grain, its own loudness
                g_cnt = r.randint(glo, ghi)
                g_tgt = r.random() ** sharp
            g_cnt -= 1
            g_env += (g_tgt - g_env) * 0.05
            if drop > 0:
                drop -= 1; gain = 0.0
            else:
                gain = v * (1.0 + 0.18 * (r.random() - 0.5))  # the wiper's contact resistance jitters
                if r.random() < pd: drop = r.randint(15, 120)
            if r.random() < pc: crk += (r.random() - 0.5) * 3500.0 * self.t_loud
            crk *= 0.8
            if cuts and any(a <= t < b for a, b in cuts):     # no contact at all: silence
                out[i] = 0.0; continue
            out[i] = out[i] * gain + fade * (noise[i] * g_env * amp + crk)
        self.g_env = g_env; self.g_tgt = g_tgt; self.g_cnt = g_cnt; self.drop = drop; self.crk = crk
    def mix_overlay(self, out):
        if not self.overlay:
            return
        # the power switch's sound does not go through the volume knob: off.wav plays at 0 %, on.wav at 5 %
        n = min(len(out), len(self.overlay)); v = POWER_SND_GAIN
        for i in range(n): out[i] += self.overlay[i] * v
        del self.overlay[:n]
    def idle(self, n):
        """n samples while the Vectrex is switched off: only the power-off sound (and the knob)"""
        from array import array
        out = [0.0] * n
        if time.perf_counter() < self.scruff_until or self.dead: self.knob(out)
        self.mix_overlay(out)
        return array('h', [max(-32768, min(32767, int(v))) for v in out]).tobytes()

class Audio:
    def __init__(self):
        self.w = ctypes.windll.winmm
        self.h = W.HANDLE()
        fmt = WAVEFORMATEX(1, 1, RATE, RATE * 2, 2, 16, 0)
        self.ok = self.w.waveOutOpen(ctypes.byref(self.h), 0xFFFFFFFF, ctypes.byref(fmt), 0, 0, 0) == 0
        self.pending = []
        self.last = bytes(2)
        self.underruns = 0
        if self.ok:
            self._write(bytes(2 * RATE * 40 // 1000))     # 40 ms head start: the queue never runs dry
    def queued_samples(self):
        self.reap()
        return sum(len(b) for _, b in self.pending) // 2
    def reap(self):
        keep = []
        for hdr, buf in self.pending:
            if hdr.dwFlags & WHDR_DONE:
                self.w.waveOutUnprepareHeader(self.h, ctypes.byref(hdr), ctypes.sizeof(WAVEHDR))
            else:
                keep.append((hdr, buf))
        self.pending = keep
    def play(self, data):
        if not self.ok or not data:
            return
        self.reap()
        if not self.pending:                # ran dry: bridge with 8 ms of the last level (no click)
            self.underruns += 1
            self._write(self.last * (RATE // 125))
        self.last = data[-2:]
        self._write(data)
    def _write(self, data):
        buf = ctypes.create_string_buffer(data, len(data))
        hdr = WAVEHDR(); hdr.lpData = ctypes.cast(buf, ctypes.c_void_p); hdr.dwBufferLength = len(data)
        self.w.waveOutPrepareHeader(self.h, ctypes.byref(hdr), ctypes.sizeof(WAVEHDR))
        self.w.waveOutWrite(self.h, ctypes.byref(hdr), ctypes.sizeof(WAVEHDR))
        self.pending.append((hdr, buf))
    def flush(self):
        """drop what is queued (power on: the click must not wait behind buffered silence)"""
        if self.ok:
            self.w.waveOutReset(self.h)
            self.reap(); self.pending = []
            self._write(bytes(2 * RATE * 10 // 1000))       # 10 ms head start
    def close(self):
        if self.ok:
            self.w.waveOutReset(self.h)
            self.reap()
            self.w.waveOutClose(self.h)

# the cartridge boards (Vectrex > Cartridge hardware): conf value -> menu label
CART_HW = {'auto': "Auto (by the ROM's size)", 'off': 'Off - bone stock, 32 KB',
           'pb6': '64 KB, PB6 bank switching', 'vb': 'Vectorblade / MiSTer flash, 4 x 48 KB',
           'hb512': '512 KB flash with bank latch (HB v2 board)', 'multi': '1 MB multi-cart (4 KB pages, latch at $7FFF)',
           'ram': 'ROM + 8 KB RAM at $2000 (AnimAction)'}

# ------------------------------------------------------------------ overlays
# The screen overlays: MiSTer .art files (or PNGs) named like the ROM, in the overlays folder next to the
# program (roms/overlays still works; the ROM browser does not list that folder). Rotated packages (_90CW / _90CCW) are not needed:
# the emulator turns the whole screen, the overlay with it.
def overlay_dirs():
    """the overlays folder next to the program (and roms/overlays, where they were before 2026-10-22)"""
    return [os.path.join(ROOT, 'overlays'), os.path.join(ROOT, 'roms', 'overlays')]

BUILTIN_ART = 'Mine Storm (built in).art'
def builtin_overlay():
    """the one overlay that comes with the emulator: Mine Storm's, for the console's own game - a plain file in
    the overlays folder, so it can be swapped (a 'Mine Storm.art' of your own is chosen before it)"""
    for p in [os.path.join(d, BUILTIN_ART) for d in overlay_dirs()] + [os.path.join(d, s, 'bios.art') for d in (ROOT, RES) for s in ('system', 'bios')]:
        if os.path.exists(p):
            return p
    return None

def moved_overlay(path):
    """an overlay chosen by hand whose file has moved (roms/overlays -> overlays): the same name in an overlay
    folder"""
    for d in overlay_dirs():
        p = os.path.join(d, os.path.basename(path))
        if os.path.exists(p):
            return p
    return None

def _art_base(s):
    """a name without its trailing (...) / [...] parts and version: 'Berzerk (Hack - Speed, 2016)' -> 'Berzerk',
    'Thrust v1.2 (Hack - Spinner, 2019)' -> 'Thrust'; MiSTer's 'Dark Tower Proto', 'VectorbladeV1.13' -> 'Dark Tower',
    'Vectorblade'"""
    while True:
        t = re.sub(r'\s*[\(\[][^\)\]]*[\)\]]\s*$|\s+v\d+(\.\d+)*[a-z]?$|\s+proto$|(?<=[a-z])v\d+(\.\d+)+$', '', s, flags=re.I)
        if t == s:
            return s.strip()
        s = t

def find_overlay(name):
    """the overlay for a ROM name: the same name, else the same base name (without (...) parts), else the
    same letters and digits ('Heads-Up' = 'Heads Up') - .art (MiSTer) or .png files; an underscore in the
    file's name counts as a space (MiSTer's names: Armor_Attack.art)"""
    files = []
    for d in overlay_dirs():
        if os.path.isdir(d):
            files += [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.lower().endswith(('.art', '.png'))
                      and not re.search(r'_90c?cw$', os.path.splitext(f)[0], re.I)]
    # (several with one base name: the plain one first - no hack, no variant suffix - then the shortest)
    files.sort(key=lambda f: (bool(re.search(r'hack|_\(', os.path.basename(f), re.I)), len(os.path.basename(f)),
                              os.path.basename(f).lower()))
    stem = lambda f: re.sub(r'\s+', ' ', os.path.splitext(os.path.basename(f))[0].replace('_', ' ')).strip()
    norm = lambda s: re.sub(r'[^a-z0-9]', '', s.lower())
    name = ART_ALIAS.get(_art_base(name).lower(), name)          # (the European names)
    for test in (lambda f: stem(f).lower() == name.lower(),
                 lambda f: stem(f).lower() == _art_base(name).lower(),
                 lambda f: _art_base(stem(f)).lower() == _art_base(name).lower(),
                 lambda f: norm(_art_base(stem(f))) == norm(_art_base(name))):
        hit = [f for f in files if test(f)]
        if hit:
            return hit[0]
    return None

# the same game under its European (MB) name
ART_ALIAS = {'mr. boston clean sweep': 'Clean Sweep'}

# the overlay's position / size (per overlay, Overlay > Position and size): their defaults
ART_ADJ_DEFAULT = {'art_dx': 0.0, 'art_dy': 0.0, 'art_scale': 1.0}

# controller port 2 (Vectrex > Controller port 2): conf value -> menu label
PORT2 = {'off': 'Joystick', 'vecvoice': 'VecVoice speech (SP0256)', 'vecvox': 'VecVox speech (SpeakJet)',
         'imager': '3D Imager (colour goggles)', 'lightpen': 'Light pen (the mouse)'}


# the games that talk (README: Games that talk), by the title in their header or, for the demos without one, the file's
# name: VecVox (SpeakJet) or VecVoice (SP0256) - port2_needed
SPEECH_TITLES = ((b'BERZERK ULTIMATE', 'vecvox'), (b'SPACE FRENZY', 'vecvox'), (b'VECFAHREN', 'vecvox'),
                 (b'VECTORBLADE', 'vecvox'), (b'VECVOX XMAS', 'vecvox'), (b'VECTALK', 'vecvox'),
                 (b'VERZERK', 'vecvoice'), (b'PYTHAGORIAN THEOREM', 'vecvoice'), (b'PYTHAGOREAN THEOREM', 'vecvoice'),
                 (b'PROTECTOR/YASI', 'vecvoice'))
SPEECH_FILES = (('brecher', 'vecvox'), ('color clash', 'vecvox'), ('debris', 'vecvox'), ('rushhour', 'vecvox'),
                ('rush hour', 'vecvox'), ('shifted', 'vecvox'), ("spike's circus", 'vecvox'), ('spikes circus', 'vecvox'),
                ('vectoblox', 'vecvox'), ('v-hockey', 'vecvox'), ('vhockey', 'vecvox'), ('vecvox demo', 'vecvox'),
                ('vecvox driver demo', 'vecvox'), ('vectalk', 'vecvox'), ('b.e.t.h', 'vecvoice'), ('deathchase', 'vecvoice'),
                ('vecvoice demo', 'vecvoice'), ('yasi', 'vecvoice'))


def gce_titles(h):
    """the title lines of a cartridge header ('g GCE yyyy', 0x80, the music's address, then lines of height, width, y,
    x and text up to 0x80, until a zero byte) - the title alone, not other text in the first bytes (Vectrex C Demo 2
    lists its tunes there: "CRAZY COASTER")"""
    if not h.startswith(b'g GCE'):
        return []
    i = h.find(b'\x80')
    if i < 0:
        return []
    i += 3                                                      # (0x80, the music's two bytes)
    out = []
    while i + 4 < len(h) and h[i] != 0 and len(out) < 4:
        j = h.find(b'\x80', i + 4)
        if j < 0:
            out.append(h[i + 4:]); break
        out.append(h[i + 4:j]); i = j + 1
    return out


def port2_needed(rom):
    """what belongs in controller port 2 for a game, by the title in its header: the 3D Imager's games wait for the
    disc (a dark screen for ever without it - Rocket Sledge too, an unreleased 3D game); the light pen's need the
    pen; the games that talk get their speech module (SPEECH_TITLES / SPEECH_FILES - they run without it, silent,
    so only when the speech ROM is there: sp_rom). None for every other game"""
    try:
        if not rom or not rom.lower().endswith(('.bin', '.vec', '.gam')):
            return None
        with open(rom, 'rb') as f:
            h = f.read(96)
    except OSError:
        return None
    gce = h.startswith(b'g GCE')
    titles = gce_titles(h)
    name = os.path.basename(rom).lower()
    flat = name.replace('_', ' ')
    if any(t in (b'3D MINE STORM', b'NARROW', b'CRAZY COASTER', b'ROCKET SLEDGE') for t in titles) \
            and not re.search(r'\b2d\b', flat):                 # (the 2D hacks keep the 3D game's header)
        return 'imager'
    if (b'ART' in titles and b'MASTER' in titles) or b'MELODY' in titles or b'ANIMACTION' in titles:
        return 'lightpen'
    if b'SPINNER' in h:                                         # (a hack for a spinner in port 2: it stays a controller)
        return None
    talk = next((dev for t, dev in SPEECH_TITLES if any(t in x for x in titles)), None)
    if not talk:
        talk = next((dev for t, dev in SPEECH_FILES if t in name), None)
        if not talk and re.search(r'\bnox\b', name):
            talk = 'vecvoice'
    if talk and sp_rom():
        return talk
    return None


def imager_disc(rom):
    """the 3D Imager's disc for a game (core imager.c, VT_IMAGER): 3D Mine Storm's has its own colours; Narrow
    Escape's and Crazy Coaster's - and any other game's - are about thirds"""
    n = os.path.basename(rom or '').lower()
    return 'mine' if 'mine' in n and '3d' in n else 'coaster' if 'coaster' in n else 'narrow'

# ------------------------------------------------------------------ emulator link
# VecFever .v4e files (v4e.py): unpacked into the folder 'v4e' next to the settings, where the game's save (the
# flash's .sav, a DS2431's memory) is kept too; the core runs the plain image
V4E_ENV = {}                            # the unpacked image -> extra settings for the core (the DS2431)
_v4e_done = {}                          # the .v4e file -> ((size, mtime), the unpacked image)
def v4e_rom(path):
    """a .v4e file -> the unpacked ROM image's path; ValueError (a message) when it cannot run here"""
    st = os.stat(path); key = (st.st_size, st.st_mtime)
    hit = _v4e_done.get(path)
    if hit and hit[0] == key:
        return hit[1]
    with open(path, 'rb') as fh:
        ctype, img = v4e.unpack(fh.read())
    d = os.path.join(SETTINGS_DIR, 'v4e')
    os.makedirs(d, exist_ok=True)
    out = os.path.join(d, os.path.splitext(os.path.basename(path))[0] + '.bin')
    try:
        same = open(out, 'rb').read() == img
    except OSError:
        same = False
    if not same:
        with open(out, 'wb') as fh:
            fh.write(img)
    V4E_ENV[out] = {'VT_DS2431': out + '.ds2431'} if ctype == 4 else {}
    _v4e_done[path] = (key, out)
    return out

RECENT_MARK = '\u2197'                                           # (Recent games: a game from outside the roms folder)
PITREX_UI = True                                                # the PiTrex add-on in the menus (core support stays)


try:                                                            # (numpy, if there: the frame's lines in one go)
    import numpy as _np
    LINE_DT = _np.dtype([('x0', '<u2'), ('y0', '<u2'), ('x1', '<u2'), ('y1', '<u2'), ('c', 'u1'), ('e', '<u2'), ('t', '<u2')])
    LINE_DTS = _np.dtype([('x0', '<i4'), ('y0', '<i4'), ('x1', '<i4'), ('y1', '<i4'), ('c', 'u1'), ('e', '<u2'), ('t', '<u2')])
except Exception:   # noqa
    _np = None


# the Vectrex unit (control deck): how one real Vectrex's analog parts differ from another's, as in VIDE's
# analog settings - live in the core (vt_set_analog id), saved, exported and imported as a .vxunit file.
# (key, core id, label, low, high, format, environment variable at the core's start). The stop delay (line gaps)
# is the renderer's line_gap, id 0.
# the brightness knob at the back: 0..1 as the knob turns (0 = a black screen, 1 = the brightest a real Vectrex goes),
# the tube's bias on the intensity drive KNOB_LO .. KNOB_HI (vecrender knob_bright; the core's VT_BRIGHT / analog 8).
# KNOB_DEFAULT = a well-set Vectrex (no bias). The owner's Vectrex side by side, 2026-10-25
KNOB_LO, KNOB_HI = -0.80, 0.32
KNOB_DEFAULT = round(-KNOB_LO / (KNOB_HI - KNOB_LO), 4)
def knob_bias(v):
    """the knob's position 0..1 -> the bias on the intensity drive"""
    b = KNOB_LO + (KNOB_HI - KNOB_LO) * min(1.0, max(0.0, v))
    return 0.0 if abs(b) < 1e-3 else b                  # (the default: exactly no bias)
# The trim pots (the service manual's adjustments, sections VII-IX; the parts lists' "semi-fixed resistors"): on the
# logic board R302 DAC offset, R333 Y / R335 X rate offset (the integrators' drift: +-5 V through 3.3M into the
# integrator, +-15 mV against the 10K input = +-0.585 DAC steps); on the power board R401 X / R408 Y size, R509 the
# brightness knob, R525 / R526 the high voltage's 555 (charge time: the voltage; discharge time: the flyback's
# ringing). No focus pot, no position pot: the picture is centred with the yoke's ring magnets (and the yoke turned on
# the neck tilts it) - those are here too. The core's ones are in UNIT_PARAMS, the picture's (the deflection after
# the integrators, the tube's voltage) in UNIT_PICTURE.
UNIT_PARAMS = (('zhold', 1, 'Zero hold (how well the zero reference charges)', 0.10, 1.00, '%.2f', 'VT_ZREFK'),
               ('zcal', 2, 'Zero calibration shift', 0.0, 6.0, '%.1f', 'VT_CALSHIFT'),
               ('drift_x', 3, 'R335 X rate offset (the X integrator\'s drift)', -0.585, 0.585, '%+.3f', None),
               ('drift_y', 4, 'R333 Y rate offset (the Y integrator\'s drift)', -0.585, 0.585, '%+.3f', None),
               ('dacofs', 23, 'R302 DAC offset', -74.0, 128.0, '%+.1f steps', 'VT_DACOFS'),
               ('zofs', 7, 'Zero offset (the reference against the integrators)', -2.0, 2.0, '%+.2f', 'VT_ZOFS'),
               ('ysettle', 5, 'Y hold settling (1 = instant)', 0.20, 1.00, '%.2f', 'VT_YSHK'),
               ('beamoff', 6, 'Beam switch-off delay', 0.0, 3.0, '%.1f cycles', 'VT_BEAMOFF'),
               ('knob', 8, 'R509 Brightness (the knob at the back)', 0.0, 1.0, '%.2f', 'VT_BRIGHT'),
               # measured with testcart/beam_test (2026-10-26, see vecx.c OFS_K, ZERO_TX, dacv)
               ('zerotx', 12, 'Zeroing time X (ZERO is an RC)', 0.0, 30.0, '%.1f cycles', None),
               ('zeroty', 13, 'Zeroing time Y (ZERO is an RC)', 0.0, 30.0, '%.1f cycles', None),
               ('ofsk', 14, 'Offset shift (zero reference)', 0.0, 300.0, '%.0f', 'VT_OFSK'),
               ('lagx', 9, 'Deflection lag X', 0.0, 20.0, '%.1f cycles', 'VT_DEFLAG'),
               ('lagy', 15, 'Deflection lag Y', 0.0, 20.0, '%.1f cycles', 'VT_DEFLAGY'),
               ('dac0', 16, 'DAC bit 0 error', -0.03, 0.03, '%+.4f', None),
               ('dac1', 17, 'DAC bit 1 error', -0.03, 0.03, '%+.4f', None),
               ('dac2', 18, 'DAC bit 2 error', -0.03, 0.03, '%+.4f', None),
               ('dac3', 19, 'DAC bit 3 error', -0.03, 0.03, '%+.4f', None))
UNIT_BEAM = dict(zerotx=4.5, zeroty=3.3, ofsk=166.0, lagx=6.0, lagy=6.0)           # (the owner's, measured; lag 6: testcart/gap_test)
UNIT_DAC_OWNER = dict(dac0=0.0170, dac1=0.0069, dac2=-0.0089, dac3=-0.0014)       # (the owner's: half the slant pattern)
# the picture's pots and the yoke (the frontend's: they act after the integrators, see unit_picture): R401 / R408 the
# deflection amplifiers' input dividers (2K pot over 1.5K: 0.43..1 of the signal; 100 % = the pot's middle, 60..140 %),
# R525 the high voltage (5.8 kV at the factory), R526 the flyback's tuning (0 = minimum ringing), the ring magnets
# (centring, % of the picture's height) and the yoke turned on the neck (degrees)
HV_FACTORY = 5.8
def _ring_fmt(v):
    return 'minimum' if abs(v) < 0.005 else '%+.2f' % v
UNIT_PICTURE = (('sizex', None, 'R401 X size (the X deflection amplifier\'s input)', 60.0, 140.0, '%.0f %%', None),
                ('sizey', None, 'R408 Y size (the Y deflection amplifier\'s input)', 60.0, 140.0, '%.0f %%', None),
                ('hv', None, 'R525 High voltage', 4.6, 7.0, '%.2f kV', None),
                ('ring', None, 'R526 HV ringing', -1.0, 1.0, _ring_fmt, None),
                ('cenx', None, 'Centering X (the yoke\'s magnets)', -10.0, 10.0, '%+.1f %%', None),
                ('ceny', None, 'Centering Y (the yoke\'s magnets)', -10.0, 10.0, '%+.1f %%', None),
                ('rot', None, 'Yoke rotation', -5.0, 5.0, '%+.1f deg', None))
UNIT_PICTURE_KEYS = tuple(p[0] for p in UNIT_PICTURE)
UNIT_FACTORY_PICTURE = dict(sizex=100.0, sizey=100.0, hv=HV_FACTORY, ring=0.0, cenx=0.0, ceny=0.0, rot=0.0)
# Config's two tabs: Unit (what an owner turns: the knob, the size, the voltage, the yoke) and Circuit (the rest)
UNIT_TAB_KEYS = ('knob', 'sizex', 'sizey', 'hv', 'cenx', 'ceny', 'rot')
UNIT_PRESETS = {
    'owner':  dict(zhold=0.38, zcal=2.9, drift_x=0.0, drift_y=0.0, dacofs=0.0, zofs=0.0, ysettle=0.70, beamoff=0.5, gap=7.0, knob=KNOB_DEFAULT,
                   **UNIT_BEAM, **UNIT_DAC_OWNER, **UNIT_FACTORY_PICTURE),  # (measured; the pots as the factory set them)
    'ideal':  dict(zhold=1.00, zcal=0.0, drift_x=0.0, drift_y=0.0, dacofs=0.0, zofs=0.0, ysettle=1.00, beamoff=0.0, gap=7.0, knob=KNOB_DEFAULT,
                   zerotx=3.3, zeroty=3.3, ofsk=166.0, lagx=6.0, lagy=6.0, dac0=0.0, dac1=0.0, dac2=0.0, dac3=0.0,   # (ZERO as drawn: 220 ohm x 0.01 uF)
                   **UNIT_FACTORY_PICTURE)}       # (Random and Cranky: made new each time, random_unit / UNIT_SPREAD)
def unit_picture(unit):
    """the picture's pots and the yoke -> (the lines' transform or None, the light's share, the spot's width share).
    The transform (canvas units, about the canvas's middle - the undeflected beam): sx, sy (the size: R401 / R408 times
    the high voltage's stiffness), the yoke's angle (+: clockwise on the screen), tx, ty (the magnets). The high voltage (R525; R526 off the minimum ringing loses some
    of it): the deflection goes as 1 / sqrt(HV) (a magnetic yoke), the light as the beam's power above the phosphor's
    dead voltage (~0.8 kV), the spot as the deflection (a stiffer beam, a smaller spot)"""
    u = dict(UNIT_FACTORY_PICTURE, **{k: v for k, v in (unit or {}).items() if k in UNIT_PICTURE_KEYS})
    hv = float(u['hv']) * (1.0 - 0.08 * float(u['ring']) ** 2)
    stiff = math.sqrt(HV_FACTORY / max(1.0, hv))
    sx, sy = float(u['sizex']) / 100.0 * stiff, float(u['sizey']) / 100.0 * stiff
    a = math.radians(float(u['rot']))
    tx, ty = float(u['cenx']) / 100.0 * ALG_MAX_Y, -float(u['ceny']) / 100.0 * ALG_MAX_Y   # (+: right, up - the canvas's y runs down)
    light = (hv - 0.8) / (HV_FACTORY - 0.8)
    xf = None if (abs(sx - 1.0) < 1e-9 and abs(sy - 1.0) < 1e-9 and a == 0.0 and tx == 0.0 and ty == 0.0) else (sx, sy, a, tx, ty)
    return xf, light, stiff
def picture_apply(xf, x, y):
    """the picture's pots on canvas points (numpy arrays or floats)"""
    sx, sy, a, tx, ty = xf
    cx, cy = ALG_MAX_X / 2.0, ALG_MAX_Y / 2.0
    u, v = (x - cx) * sx, (y - cy) * sy
    c, s = math.cos(a), math.sin(a)
    return cx + c * u - s * v + tx, cy + s * u + c * v + ty
def picture_undo(xf, x, y):
    """picture_apply's inverse (the light pen: where on the canvas the spot it sees came from)"""
    sx, sy, a, tx, ty = xf
    cx, cy = ALG_MAX_X / 2.0, ALG_MAX_Y / 2.0
    u, v = x - cx - tx, y - cy - ty
    c, s = math.cos(a), math.sin(a)
    return cx + (c * u + s * v) / sx, cy + (-s * u + c * v) / sy
UNIT_NAMES = {'owner': "Owner's Vectrex (measured)", 'ideal': 'Ideal', 'cranky': 'Cranky', 'random': 'Random unit', 'custom': 'Custom'}
def old_gap(look):
    """a look saved before 2026-10-25 with the old default line gap (12.3 cycles): the new default (7; the stop delay
    refitted with the spot's lag, see vecx.c LAG_TAU) - a value someone chose stays"""
    if isinstance(look, dict) and abs(float(look.get('line_gap', 0)) - 12.3) < 1e-6:
        look['line_gap'] = 7.0
    if isinstance(look, dict) and any(abs(float(look.get('white_k', 0)) - old) < 1e-6 for old in (6400.0, 9300.0)):
        look['white_k'] = vecrender.WHITE_DEFAULT       # (White balance: the old defaults - 6400 K, kept a day, then 9300 K
                                                        #  until 2026-10-27 - now 12000 K, the real Vectrex's bluish white)


# how far real Vectrexes differ (the owner, 2026-10-28: as real pots are set - nobody turns one so far that the screen
# is unusable; the BIOS text stays readable and the games playable). Per part: ('r', lo, hi) a range, or ('s', a, b) a
# deviation either way from the centre (the owner's measured unit, the pots as the factory set them) of size a..b.
# RANDOM: a unit slightly off, CRANKY: the same parts further off - every press a new one. R302 (dacofs) stays 0:
# it moves the joystick comparator, so it would change the game itself and break online sync
UNIT_SPREAD = dict(
    #          random                   cranky
    zhold=   (('r', 0.30, 0.60),       ('r', 0.20, 0.32)),
    zcal=    (('r', 2.0, 3.6),         ('r', 3.4, 4.6)),
    drift_x= (('s', 0.0, 0.012),       ('s', 0.015, 0.04)),
    drift_y= (('s', 0.0, 0.012),       ('s', 0.015, 0.04)),
    zofs=    (('s', 0.0, 0.04),        ('s', 0.05, 0.10)),   # (the most sensitive: +-0.25 flattens the BIOS's small text)
    ysettle= (('r', 0.60, 0.90),       ('r', 0.45, 0.62)),
    beamoff= (('r', 0.3, 0.9),         ('r', 0.9, 1.5)),
    gap=     (('r', 6.3, 7.7),         ('r', 7.7, 9.3)),
    zerotx=  (('r', 3.8, 5.2),         ('r', 5.2, 6.2)),
    zeroty=  (('r', 2.9, 3.8),         ('r', 3.8, 4.6)),
    ofsk=    (('r', 158.0, 174.0),     ('r', 150.0, 182.0)),
    lagx=    (('r', 5.5, 6.8),         ('r', 6.8, 8.0)),
    lagy=    (('r', 5.5, 6.8),         ('r', 6.8, 8.0)),
    dac0=    (('s', 0.0, 0.012),       ('s', 0.012, 0.025)),
    dac1=    (('s', 0.0, 0.008),       ('s', 0.008, 0.015)),
    dac2=    (('s', 0.0, 0.008),       ('s', 0.008, 0.012)),
    dac3=    (('s', 0.0, 0.004),       ('s', 0.004, 0.006)),
    sizex=   (('s', 0.0, 2.5),         ('s', 2.5, 6.0)),
    sizey=   (('s', 0.0, 2.5),         ('s', 2.5, 6.0)),
    hv=      (('s', 0.0, 0.12),        ('s', 0.12, 0.35)),
    ring=    (('s', 0.0, 0.15),        ('s', 0.15, 0.45)),
    cenx=    (('s', 0.0, 1.0),         ('s', 1.0, 2.5)),
    ceny=    (('s', 0.0, 1.0),         ('s', 1.0, 2.5)),
    rot=     (('s', 0.0, 0.5),         ('s', 0.5, 1.3)))
UNIT_RANGE = dict({p[0]: (p[3], p[4]) for p in UNIT_PARAMS + UNIT_PICTURE}, gap=(4.0, 10.0))   # (the sliders' ends)
def random_unit(cranky=False):
    """a believable other Vectrex (UNIT_SPREAD): slightly off, or cranky - further off, still usable. The brightness
    knob is left where it is set: it is the owner's knob, not a part"""
    own = UNIT_PRESETS['owner']
    u = {}
    for k, spec in UNIT_SPREAD.items():
        kind, a, b = spec[1 if cranky else 0]
        if kind == 'r':
            v = random.uniform(a, b)
        else:
            c = 0.0 if k.startswith('dac') else own[k]                      # (the DAC's bit errors: about none - the
            v = c + random.choice((-1.0, 1.0)) * random.uniform(a, b)       #  owner's are one unit's)
        lo, hi = UNIT_RANGE.get(k, (v, v))
        u[k] = round(min(hi, max(lo, v)), 4)
    u['dacofs'] = 0.0   # (R302 stays at the factory setting: see above)
    return u
ADDON_ROM = ('.um2', '.img', '.pit')


SD_README = """This folder is the SD card of the Ultimate Vectrex Multicart 2 (UVMC2) in Vectrex_EMU.

Put UVMC2 games (.um2 files) in the roms folder, next to your other games: the ROM browser lists them.
Some UVMC2 games read their own data from the SD card - put that here, as on the real card. For example:
  VecDoom:  a folder doom with DOOM.WAD in it        (uvmc2 / doom / DOOM.WAD)
  Quake:    a folder quake with pak0.pak and pak1.pak (uvmc2 / quake / ...)
  Movies:   a folder movies with the vector movies   (uvmc2 / movies / ...)
The game sees this folder as its card while it runs; what it writes (saves, settings) is kept here.
Another folder can be chosen in Config (Tab), the Cartridge tab: SD card folder.
"""
CACHE_README = """Working files only - nothing to put here.
When a UVMC2 game starts, the emulator builds an SD card image of the uvmc2 folder here (uvmc2_sd.img),
runs the game from it and copies what the game wrote back into the folder. It is made again at every
start: this folder can be deleted at any time.
"""
# UVMC2 games that need files of their own on the SD card (the data of the PC game they port: not shipped with them).
# (a word in the .um2's file name, the folder on the card, the files - any one of them will do -, what to tell the user)
UM2_DATA = (
    ('doom', 'doom', ('*.wad',), 'a DOOM WAD file (DOOM.WAD, DOOM2.WAD, or DOOM1.WAD - the free shareware episode)'),
    ('quake', 'quake', ('pak0.pak',), 'pak0.pak from Quake (the free shareware one will do), and pak1.pak if you have the full game'),
    ('movie', 'movies', ('*.bin',), 'vector movies (.bin files). Bad Apple comes with the emulator; more are in Malban\'s '
                                    'free PiTrex SD card download (vide.malban.de): copy its vectrex/movies folder'),
)
def um2_missing(rom, sd):
    """what a UVMC2 game in UM2_DATA lacks on the SD card folder sd: (folder, what) or None"""
    import fnmatch
    name = os.path.basename(rom or '').lower()
    if not name.endswith('.um2'):
        return None
    for word, folder, pats, what in UM2_DATA:
        if word in name:
            d = os.path.join(sd, folder)
            files = [f.lower() for f in os.listdir(d)] if os.path.isdir(d) else []
            if not any(fnmatch.fnmatch(f, pat) for f in files for pat in pats):
                return folder, what
    return None
def open_folder(d):
    """the folder in the system's file browser (made first if it is not there)"""
    try:
        os.makedirs(d, exist_ok=True)
        if IS_WIN:
            os.startfile(d)
        else:
            subprocess.Popen(['open' if IS_MAC else 'xdg-open', d])
    except OSError:
        pass


def netplay_card_time():
    import netplay
    return netplay.CARD_TIME


def ensure_sd_dir(d):
    """the UVMC2's SD card folder: made, with a short README, if it is not there yet (a new install: plug and play)"""
    try:
        if not os.path.isdir(d):
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, 'README.txt'), 'w', encoding='utf8') as f:
                f.write(SD_README)
    except OSError:
        pass


def c1_setup_done(fn):
    try:
        return bool(json.load(open(fn)).get('setup_done'))
    except Exception:   # noqa
        return False


class Lines(list):
    """a frame's lines as tuples, with the same lines as a float array (.np, 7 columns) for the renderer: made from the
    core's records in one go, so the picture is not converted back from the tuples (~1 ms a dense frame).
    Lines.lazy(records): the tuples only made when something reads them as a list - the renderer and the sound use the
    arrays, so most frames never need them (0.3-0.8 ms a heavy frame saved). Every list operation fills it first."""
    np = None
    _rec = None                                         # (the core's records, not yet turned into tuples)
    _fill_lock = threading.Lock()

    @classmethod
    def lazy(cls, rec):
        o = cls(); o._rec = rec
        return o

    def _fill(self):
        if self._rec is not None:
            with Lines._fill_lock:                      # (two threads may read one frame: filled once)
                rec = self._rec
                if rec is not None:
                    list.extend(self, rec.tolist()); self._rec = None

    def __len__(self):
        rec = self._rec
        return len(rec) if rec is not None else list.__len__(self)

    def __bool__(self):
        return self.__len__() > 0

    def __radd__(self, other):                          # ([...] + lines: a subclass's own __radd__ comes first)
        self._fill()
        return list(other) + list(list.__iter__(self))


def _lines_filled(name):
    f = getattr(list, name)
    def g(self, *a, **k):
        self._fill()
        return f(self, *a, **k)
    g.__name__ = name
    return g
for _n in ('__iter__', '__getitem__', '__contains__', '__reversed__', '__eq__', '__ne__', '__lt__', '__le__', '__gt__', '__ge__',
           '__add__', '__mul__', '__rmul__', '__iadd__', '__imul__', '__setitem__', '__delitem__', '__repr__', '__reduce_ex__',
           'append', 'extend', 'insert', 'pop', 'remove', 'index', 'count', 'sort', 'reverse', 'clear', 'copy'):
    setattr(Lines, _n, _lines_filled(_n))
Lines.__hash__ = None


class Core:
    force_seed = None                                   # (online play, netplay.py: the power-on seed both sides use)
    net_card = None                                     # (online play with a UVMC2 / PiTrex: (card folder, the files both
                                                        #  sides have the same) - the card both play from)
    def __init__(self, rom, ideal=False, cart_hw='auto', speech='off', sd_dir=None, px_dir=None, unit=None, usb=False):
        env = dict(os.environ)
        env.update({'VT_ZIDEAL': '1'} if ideal else {'VT_ZREFK': '0.38', 'VT_CALSHIFT': '2.9'})  # perfect / a real Vectrex
        self.picture = unit_picture(unit)[0] if unit and not ideal else None   # (the size / centring pots, the yoke: frame)
        if unit and not ideal:                                     # (the Vectrex unit: the analog parts at the start)
            for key, i, label, lo, hi, fmt, var in UNIT_PARAMS:
                if var and key in unit:
                    env[var] = '%.4f' % (knob_bias(unit[key]) if key == 'knob' else unit[key])
            env['VT_HWOFS'] = '%.4f,%.4f' % (unit.get('drift_x', 0.0), unit.get('drift_y', 0.0))
            env['VT_ZEROTAU'] = '%.3f,%.3f' % (unit.get('zerotx', UNIT_BEAM['zerotx']), unit.get('zeroty', UNIT_BEAM['zeroty']))
            env['VT_DACBITS'] = ','.join('%.5f' % unit.get('dac%d' % b, 0.0) for b in range(4))
        if not ideal and rom.lower().endswith(('.um2', '.img', '.pit')):
            # (a UVMC2 / PiTrex drives the beam with short zero pulses: the slow reference model (0.38) leaves
            # their lines broken and slanted where the real console draws them clean - photo of Quake on a UVMC2)
            env.pop('VT_ZREFK', None)
        # cartridge hardware: auto = the board by the ROM's size (64K PB6, Vectorblade's PB6 + IRQ with its
        # flash, the 512K latch flash, Sean Kelly's Multi-Cart latch), or one board by name; off = a
        # bone-stock Vectrex with a plain 32K cartridge
        env['VT_CARTHW'] = {'auto': '1', 'off': '0'}.get(cart_hw, cart_hw)
        env['VT_POWERSEED'] = str(Core.force_seed if Core.force_seed is not None else random.getrandbits(31))   # (the RAM's random
        #  state at every power-on; online play: the host's, the same on both sides)
        if Core.force_seed is not None:                             # (online: the 512K cartridge's latch powers up the same on
            env['VT_HBLATCH'] = str(Core.force_seed & 7)            #  both sides - otherwise from the clock, vecx.c)
        if speech == 'imager':
            env['VT_IMAGER'] = imager_disc(rom)                     # (the 3D Imager's goggles on port 2, imager.c)
        elif speech == 'lightpen':
            env['VT_LIGHTPEN'] = '1'                                # (the light pen on port 2: the mouse, imager.c)
        elif speech != 'off' and sp_rom():
            env['VT_VECVOICE'] = sp_rom()                           # (a speech accessory on port 2)
            env['VT_SPEECHDEV'] = speech
            env['VT_SPEECHOUT'] = '1'                               # (its sound on a channel of its own)
        # no input recording, no log: the core records to NUL, its messages are discarded
        env.update(V4E_ENV.get(rom, {}))                           # (a .v4e cartridge's save chip)
        # a UVMC2 game: its SD card is the folder sd_dir, as a FAT32 image (fatimg.py), copied back at the end
        # a UVMC2 game: its core 0 on a host thread of its own when that pays (the core decides: both cores busy);
        # only with 4 or more processors (the window and the sound need one too)
        if usb and rom.lower().endswith(('.um2', '.img', '.pit')):
            env['VT_USBHID'] = '1'                                  # (the PC's keyboard and mouse on its USB port: UVMC2, PiTrex)
        if rom.lower().endswith('.um2') and (os.cpu_count() or 1) >= 4:
            env.setdefault('VT_THREADS', '1')
        net = Core.net_card is not None
        if net:                                                     # online: both sides must run the same, to the bit -
            env['VT_THREADS'] = '0'                                 # the UVMC2's two cores in step on one thread,
            env['VT_JIT'] = 'exact'                                 # the PiTrex JIT exactly as the interpreter (any PC)
            env.pop('VT_USBHID', None)                              # (no PC keyboard / mouse: not shared)
        self.sd = None
        if (px_dir and rom.lower().endswith(('.img', '.pit'))) or (sd_dir and rom.lower().endswith('.um2')):
            try:
                os.makedirs(CACHE_DIR, exist_ok=True)
                if not os.path.exists(os.path.join(CACHE_DIR, 'README.txt')):
                    with open(os.path.join(CACHE_DIR, 'README.txt'), 'w', encoding='utf8') as f:
                        f.write(CACHE_README)
            except OSError:
                pass
        if net and rom.lower().endswith(('.um2', '.img', '.pit')):  # online: the card both have, nothing written back
            folder, only = Core.net_card
            img = os.path.join(CACHE_DIR, 'online_sd_%d.img' % os.getpid())   # (two emulators on one PC: a card each)
            for old in [os.path.join(CACHE_DIR, x) for x in (os.listdir(CACHE_DIR) if os.path.isdir(CACHE_DIR) else []) if x.startswith('online_sd_')]:
                try:                                                # (left by a game that did not end cleanly)
                    if time.time() - os.path.getmtime(old) > 12 * 3600:
                        os.remove(old)
                except OSError:
                    pass
            self.net_img = img                                      # (removed when the game ends: close)
            try:
                fatimg.build(folder, img, only=only, fixed_time=netplay_card_time())
                env['VT_UVM2_SD' if rom.lower().endswith('.um2') else 'VT_PITREX_SD'] = img
            except (OSError, ValueError):
                pass
            px_dir = sd_dir = None
        if px_dir and rom.lower().endswith(('.img', '.pit')):       # a PiTrex program: its SD card is the folder px_dir
            sd_dir = None
            img = os.path.join(CACHE_DIR, 'pitrex_sd.img')
            try:
                fatimg.build(px_dir, img)
                env['VT_PITREX_SD'] = img
                self.sd = (img, px_dir)
            except (OSError, ValueError):
                pass
        if sd_dir and rom.lower().endswith('.um2'):
            img = os.path.join(CACHE_DIR, 'uvmc2_sd.img')
            try:
                fatimg.build(sd_dir, img)
                env['VT_UVM2_SD'] = img
                self.sd = (img, sd_dir)
            except (OSError, ValueError):
                pass
        self.rom = rom
        self.p = subprocess.Popen([CORE, BIOS, rom, '--live', NUL], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, env=env, bufsize=0,
                                  creationflags=0x08000000 if IS_WIN else 0)            # CREATE_NO_WINDOW
        # the core's messages: a UVMC2 cartridge's problem ("UVMC2: ...") for the window; all of them in
        # cache/core_log.txt (new at every start, at most 4000 lines: what to send when something is wrong)
        self.msgs = []
        try:
            os.makedirs(CACHE_DIR, exist_ok=True)
            log = open(os.path.join(CACHE_DIR, 'core_log.txt'), 'w', encoding='utf-8', errors='replace')
            log.write('%s  %s  %s\n' % (datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'), os.path.basename(rom), platform.platform()))
        except OSError:
            log = None
        self.threads = 1                                        # (a UVMC2: host threads in use, from the core)
        self.sig = None                                         # (the last frame's picture, as a checksum)
        def drain(f=self.p.stderr, out=self.msgs, log=log):
            n = 0
            try:
                for ln in iter(f.readline, b''):
                    t = ln.decode('utf-8', 'replace').strip()
                    if log and n < 4000:
                        n += 1
                        try: log.write(t + '\n'); log.flush()
                        except (OSError, ValueError): pass
                    if t.startswith('PITREX: no JIT') and len(out) < 20:     # (the Pi's code interpreted: ~3x slower)
                        out.append("the PiTrex runs without its JIT on this PC (Windows did not give it executable "
                                   "memory - a security tool or Exploit protection?): slower")
                    elif t.startswith('UVMC2: no JIT') and len(out) < 20:      # (the UVMC2's code interpreted: slower)
                        out.append("the UVMC2 runs without its JIT on this PC (Windows did not give it executable "
                                   "memory - a security tool or Exploit protection?): slower")
                    elif t.startswith('UVMC2: JIT'):                    # (information only: in the log)
                        pass
                    elif (t.startswith('UVMC2:') or t.startswith('PITREX: the program stopped')) and len(out) < 20:
                        out.append(t.split(':', 1)[1].strip())          # (a PiTrex's own text, its UART, is not shown)
                    elif t.startswith('STAT: threads '):               # (the core: its UVMC2 runs on 1 or 2 host threads)
                        try: self.threads = int(t.split()[2])
                        except (ValueError, IndexError): pass
            except (OSError, ValueError):
                pass
        threading.Thread(target=drain, daemon=True).start()
    ft_hi = 0; ft_last = 0; frame_t0 = 0
    def _read(self, n):
        b = bytearray()
        while len(b) < n:
            c = self.p.stdout.read(n - len(b))
            if not c:
                raise EOFError('the emulator core stopped (is %s a Vectrex ROM?)' % os.path.basename(self.rom))
            b += c
        return bytes(b)
    gap = None
    def set_gap(self, cycles):
        """Tube > Line gaps: the stop delay goes to the core with the next frame"""
        self.set_analog(0, cycles)
    def set_analog(self, i, v):
        """a Vectrex unit setting (vt_set_analog: 0 stop delay, 1 zero hold, 2 zero calibration, 3 / 4 drift
        X / Y, 5 Y hold settling, 6 beam off delay, 7 zero offset): to the core with the next frames, one a frame"""
        q = getattr(self, 'analog_q', None)
        if q is None:
            q = self.analog_q = {}
        q[i] = v
    # a UVMC2's USB keyboard and mouse (usbhost.c): the PC's, as HID reports - set from the window's thread
    usb_keys = None                                     # (modifiers, up to 3 key codes) when changed
    usb_mouse = None                                    # [buttons, dx, dy, wheel] summed since the last frame
    def usb_key_report(self, mods, keys):
        self.usb_keys = bytes([mods & 255] + [k & 255 for k in (list(keys) + [0, 0, 0])[:3]])
    def usb_mouse_move(self, buttons, dx=0, dy=0, wheel=0):
        m = self.usb_mouse or [buttons, 0, 0, 0]
        m[0] = buttons; m[1] += dx; m[2] += dy; m[3] += wheel
        self.usb_mouse = m
    def frame(self, jx, jy, btn, flags, p2=None):
        """one game frame: player 1's stick and buttons, the flags, player 2's (the 2nd port: flags bit 6),
        and settings that changed (flags bit 5: a count, then each an id byte and 4 bytes - a float for the
        Vectrex unit, or a UVMC2 USB keyboard / mouse report)"""
        items = []
        q, self.analog_q = getattr(self, 'analog_q', None), {}
        for i, v in (q or {}).items():
            items.append(bytes([i]) + struct.pack('<f', float(v)))
        k, self.usb_keys = self.usb_keys, None
        if k is not None:
            items.append(bytes([0x80]) + k)
        m, self.usb_mouse = self.usb_mouse, None
        if m is not None:
            c = lambda v: max(-127, min(127, int(v))) & 255
            items.append(bytes([0x81, m[0] & 255, c(m[1]), c(m[2]), c(m[3])]))
            rest = [m[0], m[1] - max(-127, min(127, int(m[1]))), m[2] - max(-127, min(127, int(m[2]))), 0]
            if rest[1] or rest[2]:                      # (a fast flick: the rest next frame)
                self.usb_mouse_move(*rest)
        extra = b''
        if items:
            flags |= 0x20; extra = bytes([len(items)]) + b''.join(items)
        if p2:
            self.p.stdin.write(bytes([jx + 128, jy + 128, btn, flags | 0x40, p2[0] + 128, p2[1] + 128, p2[2]]) + extra)
        else:
            self.p.stdin.write(bytes([jx + 128, jy + 128, btn, flags]) + extra)
        tw = time.perf_counter()
        h = self._read(14)
        self.wait_s = time.perf_counter() - tw                  # (the core emulating the frame: its own process)
        if h[:2] not in (b'VF', b'VC'):
            raise IOError('bad frame header %r' % h)
        col = h[1:2] == b'C'                                    # ('VC': the 3D Imager - each line's colour follows the lines)
        n, ns, ft, self.state_sum = struct.unpack('<HHII', h[2:])     # (+ the state's checksum: online play compares it)
        if ft < self.ft_last: self.ft_hi += 1 << 32                   # (32-bit cycle counter wraps)
        self.ft_last = ft; self.frame_t0 = self.ft_hi + ft
        two = ns & 0x8000; ns &= 0x7fff                # (bit 15: two channels - the console, the speech module)
        body = self._read(n * 13 + (n + 2 if col else 0) + ns * (4 if two else 2))   # 16-bit samples
        if col:                                                 # (the colours and the disc's speed out of the way: the rest as always)
            cols = body[n * 13:n * 14]
            self.imager_speed = struct.unpack_from('<H', body, n * 14)[0] / 100.0
            body = body[:n * 13] + body[n * 14 + 2:]
        if n:
            self.lines_t = time.perf_counter()                  # (the last frame with something drawn: the spin-up note)
        if _np is not None:                                     # (numpy: the records in one go, the same tuples)
            self.arr = _np.frombuffer(body, dtype=LINE_DT, count=n).astype(LINE_DTS)
            for f in ('x0', 'y0', 'x1', 'y1'):                  # (sent + LIVE_OFS: the beam may go past the canvas, live.c)
                self.arr[f] -= LIVE_OFS
            xf = self.picture
            if xf is not None and n:                            # (the picture's pots and the yoke: after the integrators,
                for a, b in (('x0', 'y0'), ('x1', 'y1')):       #  before the tube's own shape - see unit_picture)
                    px, py = picture_apply(xf, self.arr[a].astype(_np.float64), self.arr[b].astype(_np.float64))
                    self.arr[a] = _np.rint(px); self.arr[b] = _np.rint(py)
            lines = Lines.lazy(self.arr)                        # (the tuples only if something asks for them)
            A = _np.empty((n, 8 if col else 7), dtype=_np.float64)
            for i, f in enumerate(('x0', 'y0', 'x1', 'y1', 'c', 'e', 't')):
                A[:, i] = self.arr[f]
            if col:                                             # (an 8th column: the 3D Imager's filter, vecrender IMAGER_RGB)
                A[:, 7] = _np.frombuffer(cols, dtype=_np.uint8)
            lines.np = A
        else:
            self.arr = None
            lines = [(lambda r: (r[0] - LIVE_OFS, r[1] - LIVE_OFS, r[2] - LIVE_OFS, r[3] - LIVE_OFS) + r[4:])(
                     struct.unpack_from('<HHHHBHH', body, i * 13)) for i in range(n)]  # + energy, start cycle
            if self.picture is not None:                        # (the picture's pots, as above)
                def _p(r, xf=self.picture):
                    x0, y0 = picture_apply(xf, r[0], r[1]); x1, y1 = picture_apply(xf, r[2], r[3])
                    return (int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))) + tuple(r[4:])
                lines = [_p(r) for r in lines]
        self.sig = hash(body[:n * 13])                          # (the picture: a new one, or the last drawn again)
        if not two:
            return lines, body[n * 13:], None
        from array import array
        a = array('h'); a.frombytes(body[n * 13:])
        return lines, a[0::2].tobytes(), a[1::2].tobytes()
    def close(self):
        try:
            self.p.stdin.write(bytes([128, 128, 0, 0x80])); self.p.stdin.close()
        except OSError:
            pass
        try:
            self.p.wait(timeout=10)
            if self.sd:                                         # (what the game wrote on its card: into the folder)
                fatimg.sync_back(*self.sd)
        except subprocess.TimeoutExpired:
            self.p.kill()
            try:
                self.p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        if getattr(self, 'net_img', None):                      # (the online card: only for that game, ~300 MB)
            try:
                os.remove(self.net_img)
            except OSError:
                pass
        return ''

# ------------------------------------------------------------------ the game loop (worker thread)
TEST_LOG = bool(os.environ.get('VECTREX_LOG_SPEED'))                  # (test: print the speed meter)
TEST_LOAD = float(os.environ.get('VECTREX_TEST_LOAD', 0)) / 1000.0   # (test: extra ms per frame)
PAUSE_HZ = 60.0                  # paused: the still picture is redrawn this often (a covered window is painted again, the knob turns smoothly)

class Runner(threading.Thread):
    beam_of = None                                      # (the app: is Tube > Beam trace on)
    def __init__(self, core, inp, audio, inf):
        super().__init__(daemon=True)
        self.core, self.inp, self.audio = core, inp, audio
        self.inf = inf; self.paused = False; self.stop = False; self.demo = False
        self.slow = 0
        self.cpu_ms = None                              # (the Python work per game frame, its core's wait not counted)
        self.emu_share = None                           # (the share of a game frame's length this thread and the core take)
        self.latest = None; self.frames = 0; self.error = None
        self.pend = None; self.dframes = 0              # (screen frames: see the queue)
        self.gframes = 0                                # (frames with a new picture: the game's own frame rate)
        self.queue = collections.deque(maxlen=16)
        self.emu_time = 0.0; self.emu_mark = None   # (lines, real start time, start cycle) per frame
        self.crt = CrtSound()
        self.lock = threading.Lock()
        self.flags = 0                      # one-shot: 4 = RESET button, 8 = power on
        self.power = True
        self.net = None                     # online play (netplay.py): the other player's input each frame
        self.net_lost = False
    def demo_input(self):
        import random
        f = self.frames
        if os.environ.get('ARENA_DEMO_IDLE'): return 0, 0, 0    # (screenshots: stay on the title)
        seq = os.environ.get('VECTREX_DEMO_SEQ')               # (tests: 'frame:jx,jy,btn;...' from that frame on)
        if seq:
            cur = (0, 0, 0)
            for item in seq.split(';'):
                fr, v = item.split(':'); jx, jy, b = (int(x) for x in v.split(','))
                if f >= int(fr): cur = (jx, jy, b)
            return cur
        if 680 <= f < 684: return 0, 0, 8
        if f < 900: return 0, 0, 0
        if f % 25 == 0:
            self.d = (random.randint(-127, 127), random.randint(-127, 127), random.choice(DIR_MASK))
        return getattr(self, 'd', (0, 0, 0))

    def run(self):
        t_next = time.perf_counter()
        try:
            while not self.stop:
                if not self.power:                      # switched off: the power-off sound, the knob
                    self.audio.play(self.crt.idle(RATE // 50))
                    t_next += 0.02
                    while time.perf_counter() < t_next: time.sleep(0.001)
                    if t_next < time.perf_counter() - 0.1: t_next = time.perf_counter()
                    continue
                if self.paused:
                    time.sleep(0.02); t_next = time.perf_counter(); continue
                if self.demo:
                    (jx, jy, btn), p2 = self.demo_input(), (0, 0, 0)
                else:
                    (jx, jy, btn), p2 = self.inp.read()                 # (both players)
                fl, self.flags = self.flags, 0
                if self.net is not None:                # online: both players' input for this frame (the RESET button
                    got = self.net.exchange((jx, jy, btn, fl & 4))   #  too: it reaches both machines on one frame)
                    if got is None:
                        self.net = None; self.net_lost = True
                    else:
                        a, b = got
                        (jx, jy, btn), p2 = a[:3], b[:3]
                        fl = (fl & ~4) | ((a[3] | b[3]) & 4)
                hide = False
                if fl & 8:                              # power on: the click now, the picture a frame later
                    self.audio.flush(); hide = True
                # the speed meter: real and emulated time, both at this frame's start (marking after
                # it counted its whole length at its first moment: a frame rate change read as 99 %)
                self.emu_mark = (time.perf_counter(), self.emu_time)
                bt = bool(self.beam_of()) if self.beam_of else False     # (Tube > Beam trace: the core's mode, see
                if bt != getattr(self.core, 'beam_sent', None):          #  vecx.c bp_step - sent again to a new core)
                    self.core.set_analog(11, 1.0 if bt else 0.0); self.core.beam_sent = bt
                t_cpu = time.perf_counter()
                feed = None
                if fl & 8 and fl & 16:                  # (power on, the intro skipped: the core runs ~0.8 s for this
                    done = threading.Event()             #  frame - the switch's click and the knob's scratch play on
                    def fill(done=done):                 #  meanwhile, kept ~40 ms ahead)
                        while not done.is_set():
                            if self.audio.queued_samples() < RATE * 0.04:
                                self.audio.play(self.crt.idle(RATE // 100))
                            time.sleep(0.004)
                    feed = threading.Thread(target=fill, daemon=True); feed.start()
                try:
                    lines, pcm, speech = self.core.frame(jx, jy, btn, (1 if self.inf else 0) | fl, p2)
                    if self.net is not None:            # (online: the sync check - this frame's state against the other's)
                        self.net.mark(self.net.f - 1, getattr(self.core, 'state_sum', 0))
                finally:
                    if feed:
                        done.set(); feed.join()
                if TEST_LOAD: time.sleep(TEST_LOAD)                     # (test: a host too slow)
                self.frames += 1
                sig = getattr(self.core, 'sig', None)               # (the game's own frame rate: pictures that changed)
                if sig != getattr(self, 'last_sig', None):
                    self.gframes += 1; self.last_sig = sig
                if self.frames == 1:
                    self.t_first = time.perf_counter()
                with self.lock:
                    self.latest = [] if hide else lines
                    self.latest_t0 = self.core.frame_t0                # the frame's start (cycles)
                    self.latest_len = max(len(pcm) // 2, 1) / RATE     # this frame's emulated length
                    # a screen frame per refresh period: a game that calls Wait_Recal twice in one (Mine
                    # Storm does) would split it into a short and a long piece - pieces shorter than
                    # 15 ms join the next one (its line times shifted to the first piece's start)
                    t0 = self.core.frame_t0
                    if self.pend is None:
                        rec = getattr(self.latest, '_rec', None)   # (not turned into tuples yet: shares the records)
                        cp = Lines.lazy(rec) if rec is not None else Lines(self.latest); cp.np = getattr(self.latest, 'np', None)
                        self.pend = [cp, t_next, t0, self.latest_len]
                    else:
                        sh = (t0 - self.pend[2]) % (1 << 32)
                        pa, pb = getattr(self.pend[0], 'np', None), getattr(self.latest, 'np', None)
                        self.pend[0] = Lines(list(self.pend[0]) + [ln[:6] + (ln[6] + sh,) if len(ln) > 6 else ln for ln in self.latest])
                        if (pa is not None and pb is not None and pa.shape[1] == pb.shape[1]
                                and len(pa) + len(pb) == len(self.pend[0])):   # (the arrays joined too: the 3D Imager's colours)
                            pb = pb.copy(); pb[:, 6] += sh
                            self.pend[0].np = _np.concatenate([pa, pb])
                        self.pend[3] += self.latest_len
                    if self.pend[3] >= 0.015:
                        self.queue.append(tuple(self.pend)); self.pend = None   # (t_next: its start)
                        self.dframes += 1
                self.audio.play(self.crt.process(pcm, lines, speech, getattr(self.core, 'arr', None)))
                ms = 1000.0 * (time.perf_counter() - t_cpu - getattr(self.core, 'wait_s', 0.0))   # (this thread's own
                self.cpu_ms = ms if self.cpu_ms is None else 0.9 * self.cpu_ms + 0.1 * ms            #  work for a frame)
                sh = (time.perf_counter() - t_cpu) / (max(len(pcm) // 2, 1) / RATE)       # (the OSD's CPU meter: the share of
                if self.frames > 10 and sh < 3.0:                                             #  the frame's time used - not the
                    self.emu_share = sh if self.emu_share is None else 0.9 * self.emu_share + 0.1 * sh   # first frames (the core
                                                                #  starting, the BIOS, a UVMC2's SD card: 999 %), nor a single
                                                                #  stall; a PC that can not keep up stays above 1 every frame)
                self.emu_time += max(len(pcm) // 2, 1) / RATE           # (the speed meter, see emu_mark)
                # pace by emulated time (this frame's samples = its length in CPU cycles): real time
                # is the clock; the sound queue (40 ms head start) only caps runaway buffering
                t_next += max(len(pcm) // 2, 1) / RATE
                if self.net is not None:                # (online: the PC ahead of the other slows a little - no waits)
                    t_next += self.net.pace_s()
                now = time.perf_counter()
                if now - t_next > 0.03:
                    self.slow += 1
                if t_next < now - 0.1:
                    t_next = now
                while True:
                    now = time.perf_counter()
                    q = self.audio.queued_samples() / RATE if self.audio.ok else 0
                    if now >= t_next - 0.002 and q < 0.15:
                        break
                    time.sleep(0.001)
        except Exception as e:      # noqa: the window reports it
            self.error = str(e)
            p = getattr(self.core, 'p', None)                   # (the core gone: say so, with its exit code - a pipe
            rc = None                                           #  error alone, "[Errno 22] Invalid argument", told nothing)
            for _ in range(20):
                rc = p.poll() if p is not None else None
                if rc is not None: break
                time.sleep(0.05)
            if rc is not None:
                self.error = 'the emulation core stopped (exit code %d, 0x%08X) - details in cache/core_log.txt' % (rc, rc & 0xffffffff)
                try:
                    with open(os.path.join(CACHE_DIR, 'core_log.txt'), 'a', encoding='utf-8') as f:
                        f.write('%s  the core stopped: exit code %d (0x%08X), frame %d; the window: %s\n' % (
                            datetime.datetime.now().strftime('%H:%M:%S'), rc, rc & 0xffffffff, self.frames, e))
                except OSError:
                    pass

# ------------------------------------------------------------------ window
class App:
    def __init__(self, args):
        self.args = args
        if not args.rom and not args.demo:                      # no ROM given: the one used last, if it is
            try:                                                #  still there (else no cartridge: Mine Storm)
                last = json.load(open(SETTINGS_F)).get('last_rom')
                if last and os.path.isfile(last):
                    args.rom = last
            except Exception:   # noqa
                pass
        if args.rom:
            args.rom = os.path.abspath(args.rom)
        try:
            rom, self.rom_name = self.cartridge(args.rom)
        except (ValueError, OSError) as e:                     # (a .v4e that cannot run here: Mine Storm)
            self.v4e_err = '%s:\n%s' % (os.path.basename(args.rom), e)
            args.rom = None
            rom, self.rom_name = self.cartridge(None)
        try:                                                    # (the controls: the settings)
            c0 = json.load(open(SETTINGS_F))
        except Exception:   # noqa
            c0 = {}
        ctl = c0.get('controls')
        self.cart_hw = {True: 'auto', False: 'off'}.get(c0.get('cart_hw', 'auto'), c0.get('cart_hw'))   # (older confs: on / off)
        if self.cart_hw not in CART_HW:
            self.cart_hw = 'auto'
        self.show_um2 = bool(c0.get('show_um2', True))          # Add-ons: UVMC2 games (.um2) in the ROM list
        self.start_on = bool(c0.get('start_on', False))         # starts switched off: turn the knob, as on a Vectrex
        self.pad_shortcuts = bool(c0.get('pad_shortcuts', c0.get('pad_browser', True)))   # stick + all four buttons held 1 s
        self.mouse_stick = bool(c0.get('mouse_stick', False))   # Vectrex > Mouse / touchpad steers player 1
        self.smooth = bool(c0.get('smooth_motion', True))    # Smooth motion (on by default, 2026-10-28), see smooth_frame
        self.sm_buf = []
        self.sm_st = type('SmoothState', (), {})()     # (the alternate-halves check's memory)
        self.imager_note = bool(c0.get('imager_note', True))    # the 3D Imager: a note while its disc spins up
        self.usb_hid = bool(c0.get('usb_hid', True))            # a UVMC2 game: the PC's mouse (and keyboard: Ctrl+K) as its USB devices
        self.usb_typing = False; self.usb_btn = 0; self.usb_last = None; self.usb_down = []
        self.skip_boot = bool(c0.get('skip_boot', False))       # power on straight into the cartridge
        self.p2_auto = bool(c0.get('p2_auto', True))            # port 2: the accessory a game needs, plugged in for it (port2_auto)
        self.inp = Input(ctl)
        self.audio = Audio() if IS_WIN else osdep.Audio(RATE)
        self.speech = c0.get('speech', 'vecvoice' if c0.get('vecvoice') else 'off')   # (port 2: a speech accessory)
        if self.speech not in PORT2 or (self.speech in ('vecvoice', 'vecvox') and not sp_rom()):
            self.speech = 'off'
        if self.p2_auto and self.speech in ('imager', 'lightpen'):   # (auto: the Imager and the pen are plugged in for the
            self.speech = 'off'                                 #  games that need them - never everybody's choice; a
                                                                #  settings file from before 2026-10-27 could hold one)
        self.p2_games = dict(c0.get('p2_games', {}) or {})      # (auto: a hand choice per game - ROM file name -> port 2)
        self.speech_user = self.speech                          # (the player's choice; self.speech: this game's - port2_auto)
        self.speech = self.port2_auto(args.rom)
        self.sd_dir = _card_dir(c0.get('uvm2_sd_dir'), SD_DEFAULT)   # the UVMC2's SD card
        ensure_sd_dir(self.sd_dir)                              # (plug and play: there from the first start)
        self.px_dir = _card_dir(c0.get('pitrex_sd_dir'), PX_DEFAULT)   # Add-ons: the PiTrex's SD card
        u = c0.get('unit') if isinstance(c0.get('unit'), dict) else {}
        self.unit = dict(UNIT_PRESETS['owner'], **{k: float(v) for k, v in u.items() if k in UNIT_PRESETS['owner']})
        if self.unit.get('lagx') == 8.0 and self.unit.get('lagy') == 8.0:      # (the lag's first default, a day: now 6 -
            self.unit['lagx'] = self.unit['lagy'] = UNIT_BEAM['lagx']          #  testcart/gap_test; a value chosen stays)
        if self.unit.get('zerotx') == 12.0 and self.unit.get('zeroty') == 8.0:  # (ZERO's first default, a day: too slow -
            self.unit['zerotx'], self.unit['zeroty'] = UNIT_BEAM['zerotx'], UNIT_BEAM['zeroty']   #  Vectorblade broke)
        self.unit_name = c0.get('unit_name', 'owner') if c0.get('unit_name') in UNIT_NAMES else 'owner'
        self.core = Core(rom, args.ideal, self.cart_hw, self.speech, self.sd_dir, self.px_dir, self.unit, self.usb_hid)
        self.run = Runner(self.core, self.inp, self.audio, False)
        self.run.beam_of = lambda: getattr(self.ren, 'beam_trace', False) if self.ren else False
        self.run.crt.jack = bool(args.rom and args.rom.lower().endswith('.um2'))   # (a UVMC2: its headphone jack)
        self.run.crt.motor = self.speech == 'imager'            # (the 3D Imager: its motor's sound)
        self.root = tk.Tk()
        try: self.root.attributes('-alpha', 0.0)        # (seen from the first picture on: shown at once, finished -
        except tk.TclError: pass                         #  not a black window with a light title bar resizing)
        self.root.after(4000, self.root_visible)        # (no picture by then - OpenGL slow or missing: shown anyway)
        for d in (os.path.join(RES, 'icons'), os.path.join(ROOT, 'icons')):    # (the window's icon: a little Vectrex)
            try:
                self.icon_img = tk.PhotoImage(file=os.path.join(d, 'vectrex_emu.png'))
                self.root.iconphoto(True, self.icon_img)
                break
            except Exception:
                pass
        if getattr(self, 'v4e_err', None):
            self.root.after(800, lambda: self.rom_error(self.v4e_err))
        self.root.configure(bg='black')
        if not IS_WIN:
            osdep.KEYS.attach(self.root)
        if IS_MAC:                                              # (Ctrl+click: the right button, one-button mice)
            self.root.bind_class('all', '<Control-Button-1>', lambda e: e.widget.event_generate('<Button-2>', x=e.x, y=e.y))
        # picture size: as tall as the screen allows (or --height-mm on this monitor); the window
        # size you leave it at is remembered in the settings
        sh = self.root.winfo_screenheight()
        try:                                                    # the screen minus the task bar
            wa = W.RECT(); ctypes.windll.user32.SystemParametersInfoW(0x30, 0, ctypes.byref(wa), 0)
            sh = wa.bottom - wa.top
        except Exception:   # noqa
            pass
        ui = self.root.winfo_fpixels('1i') / 96.0               # (Windows display scaling: 1.5 at 150 %)
        # leave room for the title bar, the menu bar, the two OSD rows and the borders (+10 px from the top)
        fit = sh - 10 - int(150 * ui)
        if args.height_mm:
            ppmm = vecrender.pixels_per_mm() or self.root.winfo_fpixels('1i') / 25.4
            self.H0 = int(args.height_mm * ppmm)
        else:
            self.H0 = fit
        try:
            conf0 = json.load(open(SETTINGS_F))
        except Exception:   # noqa
            conf0 = {}
        self.knob_on = conf0.get('knob', True)                # the volume knob + RESET button (K)
        self.cart_on = conf0.get('cart', True)                # the cartridge pictogram: the ROM browser (L)
        self.browser = None
        self.fps_on = conf0.get('fps_counter', True)            # the OSD (Ctrl+F)
        self.quality_base = conf0.get('quality_base', 'ultra')   # (the Quality profile chosen last: Custom's starting point;
        self.quality_opt = dict(conf0.get('quality_opt', {}) or {})   #  'optimized': Optimize's result, quality_opt)
        self.osd_meters = conf0.get('osd_meters', True)         # (the OSD's CPU and GPU meters: how much of each frame is used)
        self.net_server_s = conf0.get('net_server', '')         # (Online settings: the meeting point; '' = netplay.SERVER)
        self.gamer = conf0.get('gamer_name', '')                # (the game room: the name a public game is listed by)
        self.net_osd = conf0.get('net_osd', True)               # (the online state as an OSD row)
        self.head_hist = {'CPU': [], 'GPU': []}                 # (their last 10 seconds)
        self.fit_h = fit                                        # (the window's screen area at most)
        fit = int(fit / (1.0 + self.knob_reserve()))             # (room for the black above, the strip below)
        self.H0 = min(fit, int(self.H0 * args.scale))
        self.W0 = int(self.H0 * vecrender.VIEW_AW)
        self.H0w = int(self.H0 * (1.0 + self.knob_reserve()))
        self.settings_f = SETTINGS_F
        os.makedirs(os.path.dirname(SETTINGS_F), exist_ok=True)               # (a new folder starts with the defaults: no settings
        geo = None                                              #  are carried over from %APPDATA% - an early test version's
                                                                #  settings.json there gave a fresh download its buzz and volume)
        if not args.height_mm and args.scale == 1.0:
            try:
                geo = json.load(open(self.settings_f)).get('geometry')
            except Exception:       # noqa: first run
                pass
        self.bar = tk.Frame(self.root, bg='black')             # the status lines (hidden in full screen)
        self.bar.pack(side='bottom', fill='x')
        row = tk.Frame(self.bar, bg='black'); row.pack(side='top', fill='x')
        lab = lambda: tk.Label(row, fg='#6a8', bg='black', font=(MONO, 9), anchor='w', padx=0)
        self.num_a = lab(); self.num_a.pack(side='left')
        self.num_speed = lab(); self.num_speed.pack(side='left')      # (emulation speed: green / orange / red)
        self.num_b = lab(); self.num_b.pack(side='left')              # (the volume, unless the knob shows it)
        self.look_lbl = tk.Label(self.bar, fg='#6a8', bg='black', font=(MONO, 9), anchor='w', justify='left')
        self.look_lbl.pack(side='top', fill='x')                     # (the look: wraps to the window's width)
        self.bar.bind('<Configure>', lambda e: self.look_lbl.configure(wraplength=max(200, e.width - 8)))
        self.status = tk.Label(self.bar, fg='#6a8', bg='black', font=(MONO, 9), anchor='w', justify='left')
        # (bg '': Tk never paints the frame's background - OpenGL draws all of it; a black fill over the picture
        #  between two frames flashed on some drivers, e.g. AMD on Windows, GitHub issue 1; until OpenGL's
        #  first picture it is black: unpainted, the start showed a white block for a moment)
        self.screen = tk.Frame(self.root, bg='black', width=self.W0, height=self.H0w, highlightthickness=0)
        self.screen.pack(fill='both', expand=True)
        self.root.update_idletasks()
        self.root.geometry(geo or '%dx%d+%d+10' % (self.W0, self.H0w,
                                                 (self.root.winfo_screenwidth() - self.W0) // 2))
        self.zoom = 1.0; self.full = False
        self.root.after(500, lambda: self.start_fit(geo))        # (once the frame and the menu bar are on screen)
        self.ren = None; self.lines = []
        if not args.tk:
            try:
                self.root.update()
                self.ren = vecrender.make_renderer(self.screen.winfo_id(), self.screen)
                self.ren.reserve = self.knob_reserve()
                self.ren.resize(self.screen.winfo_width(), self.screen.winfo_height())
            except Exception as e:                              # noqa: fall back to the Tk canvas
                print('renderer unavailable (%s): using the Tk canvas' % e)
                self.gl_error = str(e)                          # (said on screen too: GitHub issue 3)
        # display settings are kept in the settings file; each menu's Reset ... settings to default restores its own
        self.look_keys = [k for k in ('phosphor', 'bloom', 'trail_on', 'trail', 'wob_on', 'wob_strength',
                                      'mains_hz', 'bend_on', 'bend_strength', 'bend_type', 'curve_on', 'geom_k', 'glass_on', 'glass_room', 'light_on', 'light_k', 'light_x', 'light_y', 'light_ov', 'light_ov_k', 'halo_on', 'halo_k', 'dot_k', 'even_beam', 'beam_trace', 'white_k', 'phos_spread', 'phos_grow', 'phos_smooth', 'line_gap',
                                      'art_on', 'art_tint', 'art_room', 'art_print', 'art_parallax',
                                      'art_glow', 'art_glow_k', 'scr_on', 'scr_wear', 'scr_fine', 'scr_medium', 'scr_large', 'scr_glint', 'scr_reach', 'scr_scrib',
                                      'case_on', 'case_refl', 'case_wear', 'passes', 'bend_smooth', 'dither_on',
                                      'phos_scale', 'half_rate')
                          if self.ren and hasattr(self.ren, k)]
        self.look_default = {k: getattr(self.ren, k) for k in self.look_keys}
        self.sound_keys = ['buzz_on', 'buzz_vol', 'speaker_on', 'volume', 'voice_vol', 'voice_full', 'jack_mode', 'jack_vol',
                           'motor_on', 'motor_vol']
        self.sound_default = {k: getattr(self.run.crt, k) for k in self.sound_keys}
        try:
            saved = json.load(open(self.settings_f)).get('display', {})
        except Exception:           # noqa: first run
            saved = {}
        old_gap(saved)
        for k in self.look_keys:
            if k in saved:
                setattr(self.ren, k, type(self.look_default[k])(saved[k]))
        q_old = c0.get('quality')                               # (before profiles: 'auto' or '0'..'3')
        if self.ren and q_old in ('0', '1') and 'dither_on' not in saved:   # (Low / Medium chosen before
            for k, v in self.profile_values({'0': 'medium', '1': 'high'}[q_old]).items():   #  profiles: that
                setattr(self.ren, k, v)                                                              #  profile, once)
        self.unit['knob'] = min(1.0, max(0.0, self.unit.get('knob', KNOB_DEFAULT)))   # (see KNOB_LO)
        if self.ren:
            self.ren.knob_bright = knob_bias(self.unit['knob'])   # (the brightness knob at the back)
        self.apply_picture()                                    # (the size / voltage / yoke pots)
        if self.ren and hasattr(self.ren, 'line_gap'):
            self.core.set_gap(self.ren.line_gap)                # (Tube > Line gaps: the core's stop delay)
            self.unit['gap'] = self.ren.line_gap
        try:                                                    # per-game memory (the control deck: Remember for
            self.per_game = json.load(open(self.settings_f)).get('per_game', {}) or {}   #  this game): its look + controls
        except Exception:           # noqa
            self.per_game = {}
        self.game_global = None                                 # (the everyday settings while a game has its own)
        self.game_memory()
        try:
            c1 = json.load(open(self.settings_f))
        except Exception:           # noqa
            c1 = {}
        self.art_auto = bool(c1.get('art_auto', True))          # Overlay > Choose automatically
        self.art_for = dict(c1.get('overlay_for', {}))          # per game: an overlay file, or 'none'
        self.art_adj = dict(c1.get('overlay_adjust2', {}))      # per overlay: [dx, dy, scale] (2: the picture moves, the sheet
                                                                #  stays on the frame - since 2026-10-28; the old ones dropped)
        self.art_path = None
        self.rot = int(c1.get('rotate', 0)) if c1.get('rotate', 0) in (0, 90, 180, 270) else 0
        self.monitor_inch = float(c1.get('monitor_inch') or 0)   # (Display > Monitor size; 0: as the monitor reports it)
        if self.ren and hasattr(self.ren, 'scr_seed'):            # (Overlay > New random scratches)
            self.ren.scr_seed = int(c1.get('scratch_seed', 1))
        self.real_on = bool(c1.get('real_size', False))           # (Display > Real size: kept until + / - / 0)
        if self.ren and hasattr(self.ren, 'set_art'):
            if self.rot:
                self.ren.rot = self.rot; self.apply_scale()
            self.load_art()
        for k in self.sound_keys:
            if k in saved:
                setattr(self.run.crt, k, type(self.sound_default[k])(saved[k]))
        if self.start_on or args.demo or not self.ren or IN_GAMESCOPE:   # (no OpenGL / Game Mode: on at start)
            self.run.crt.volume = max(0.05, self.run.crt.volume)  # (on at start: the knob turned up)
        else:                                                   # off at start, like a Vectrex: the knob at
            self.run.crt.volume = 0.0                           #  its stop; turning it up switches it on
            self.run.power = False
        if not self.ren:
            self.k = self.H0 / 410
            self.W, self.H = self.W0, self.H0
            self.cv = tk.Canvas(self.screen, width=self.W, height=self.H, bg='black', highlightthickness=0)
            self.cv.pack(fill='both', expand=True)
        self.screen.bind('<Configure>', self.on_resize)
        # the shortcuts (the owner, 2026-10-09): only what full screen needs - F11, Esc, Page Up / Down
        # and Ctrl+...; everything else is in the menu bar, so the keyboard stays free for the players
        for key in ('<Key-plus>', '<Key-equal>', '<KP_Add>'):    # (+ - 0: no Ctrl; the players can not have them)
            self.root.bind(key, lambda e: self.zoom_step(1.1))
        for key in ('<Key-minus>', '<KP_Subtract>'):
            self.root.bind(key, lambda e: self.zoom_step(1 / 1.1))
        for key in ('<Key-0>', '<KP_0>', '<KP_Insert>'):
            self.root.bind(key, lambda e: self.zoom_step(None))
        for key in ('<Key-9>', '<KP_9>'):                     # (9: the real size)
            self.root.bind(key, lambda e: self.real_size())
        try:                                                    # the picture size in full screen
            conf = json.load(open(self.settings_f))
        except Exception:           # noqa
            conf = {}
        self.full_scale = conf.get('full_scale')
        self.recent = [g for g in conf.get('recent', []) if isinstance(g, str)][:10]
        self.note_recent(self.args.rom)
        self.deck_pos = tuple(conf['deck_pos']) if isinstance(conf.get('deck_pos'), list) and len(conf['deck_pos']) == 2 else None
        self.ctl_pos = tuple(conf['ctl_pos']) if isinstance(conf.get('ctl_pos'), list) and len(conf['ctl_pos']) == 2 else None
        self.help_geo = conf.get('help_geo') if isinstance(conf.get('help_geo'), str) else None
        self.win_pos = {k: tuple(v) for k, v in (conf.get('win_pos') or {}).items()
                        if isinstance(v, list) and len(v) == 2}   # (the other windows' places: deck.place)
        if isinstance(conf.get('deck_strip'), str):              # (Config: the strip it was on, and each strip's row)
            self.deck_strip = conf['deck_strip']
        self.deck_focus = {k: int(v) for k, v in conf.get('deck_focus', {}).items()} if isinstance(conf.get('deck_focus'), dict) else {}
        bl = conf.get('browse_last')                            # (the ROM browser's last folder)
        self.browse_last = (bl[0], list(bl[1]), bl[2], int(bl[3]) if len(bl) > 3 else 0) if isinstance(bl, list) and len(bl) in (3, 4) and isinstance(bl[0], str) else None
        self.show_bar()
        self.vrr = conf.get('gsync', False)                     # G-Sync / FreeSync mode (off by default: any monitor)
        self.rate = conf.get('screen_rate', 'max')              # 'max' (the monitor's full rate, the default: even on 144 / 165 Hz
                                                                #  screens, where 60 can only be paced by the clock) or '60'
        self.setup_display()
        self.ultra_rate = None
        self.root.bind('<F11>', self.toggle_full)
        self.root.bind('<Motion>', self.pointer_moved)
        self.screen.bind('<Motion>', self.mouse_moved, add='+')
        self.screen.bind('<Leave>', lambda e: self.mouse_moved(None), add='+')
        self.screen.bind('<ButtonRelease-1>', lambda e: self.mouse_button(1, False), add='+')
        self.screen.bind('<ButtonPress-3>', lambda e: self.mouse_button(3, True), add='+')
        self.screen.bind('<ButtonRelease-3>', lambda e: self.mouse_button(3, False), add='+')
        self.screen.bind('<ButtonPress-2>', lambda e: self.mouse_button(2, True), add='+')
        self.screen.bind('<ButtonRelease-2>', lambda e: self.mouse_button(2, False), add='+')
        self.screen.bind('<Double-Button-1>', self.browse_double)
        vis = lambda: self.browser_geom()[3]
        for key, f in (('<Up>', lambda: self.browse(-1)), ('<Down>', lambda: self.browse(1)),
                       ('<Left>', lambda: self.browse(-vis())), ('<Right>', lambda: self.browse(vis())),
                       ('<Return>', self.browse_load), ('<BackSpace>', self.browser_back)):
            self.root.bind(key, lambda e, f=f: (f(), 'break')[1] if self.browser else None)
        self.screen.bind('<Button-1>', self.knob_click)
        self.screen.bind('<Motion>', self.knob_motion, add='+')
        self.screen.bind('<Leave>', lambda e: self.knob_motion(None), add='+')
        self.screen.bind('<ButtonRelease-1>', self.knob_release, add='+')
        self.screen.bind(RCLICK, self.cart_right, add='+')
        self.root.bind('<KeyPress>', self.any_key, add='+')    # (switched off: any key switches on)
        self.screen.bind('<MouseWheel>', self.knob_wheel)
        if not IS_WIN and not IS_MAC:                           # (X11, Tk 8.6: the wheel is buttons 4 / 5)
            wheel = lambda e, d: self.knob_wheel(type('E', (), dict(delta=d, x=e.x, y=e.y, widget=e.widget)))
            self.screen.bind('<Button-4>', lambda e: wheel(e, 120))
            self.screen.bind('<Button-5>', lambda e: wheel(e, -120))
        self.build_menu()
        # P L O without Ctrl (the players can not have them); only Ctrl+E (eject) and Ctrl+R (RESET) need
        # Ctrl - they end the game
        for k in ('<Key-l>', '<Key-L>'):
            self.root.bind(k, lambda e: (self.close_browser() if self.browser else self.open_browser(), 'break')[1])
        for k in ('<Key-o>', '<Key-O>'):
            self.root.bind(k, lambda e: (self.toggle_art(), 'break')[1])
        for k in ('<Key-c>', '<Key-C>'):
            self.root.bind(k, lambda e: (self.toggle_case(), 'break')[1])
        self.items = []; self.item_col = []; self.shown = 0
        self.colors = ['#%02x%02x%02x' % (int(v * 190), int(v * 220), int(v * 255))
                       for v in [min(1.0, 0.30 + c / 127 * 0.85) for c in range(128)]]
        self.root.bind('<FocusIn>', lambda e: setattr(self.inp, 'focused', True))
        self.root.bind('<FocusOut>', lambda e: setattr(self.inp, 'focused', False))
        self.root.bind('<Control-e>', self.eject)
        for k in ('<Key-p>', '<Key-P>'):
            self.root.bind(k, lambda e: (self.toggle_pause(), 'break')[1])
        self.root.bind('<Control-r>', self.reset_button)
        for k in ('<Prior>', '<KP_Prior>'):                     # (also the numpad's Page Up / Down)
            self.root.bind(k, lambda e: self.turn_volume(+0.05))
        for k in ('<Next>', '<KP_Next>'):
            self.root.bind(k, lambda e: self.turn_volume(-0.05))
        for m in ('Control', 'Command') if IS_MAC else ('Control',):   # (Ctrl: the speech module's volume)
            for k in ('Prior', 'KP_Prior'):
                self.root.bind('<%s-%s>' % (m, k), lambda e: self.turn_voice(+0.05))
            for k in ('Next', 'KP_Next'):
                self.root.bind('<%s-%s>' % (m, k), lambda e: self.turn_voice(-0.05))
        self.root.bind('<Escape>', self.escape)
        self.root.bind('<Tab>', self.open_deck)                 # (the control deck)
        for k in ('<Control-t>', '<Control-T>'):
            self.root.bind(k, self.chat_window)                 # (online: the Chat window)
        for k in ('<Control-k>', '<Control-K>'):
            self.root.bind(k, self.toggle_usb_typing)          # (a UVMC2: the keyboard types on its USB keyboard)
        self.root.bind_class('usbtype', '<KeyPress>', lambda e: self.usb_key(e, True))
        self.root.bind_class('usbtype', '<KeyRelease>', lambda e: self.usb_key(e, False))
        self.root.protocol('WM_DELETE_WINDOW', self.quit)
        if IS_MAC:                                              # macOS: Command does what Ctrl does, Ctrl+Command+F
            for seq in self.root.bind():                        #  is full screen, Command+Q quits (settings saved)
                if seq.startswith('<Control-Key-') or seq.startswith('<Control-KP_'):
                    self.root.bind(seq.replace('<Control-', '<Command-'), self.root.bind(seq))
            self.root.bind('<Control-Command-Key-f>', self.toggle_full)
            self.root.createcommand('tk::mac::Quit', self.quit)
        self.last_t = self.t_start = time.perf_counter(); self.last_f = 0; self.fps = 0; self.disp_fps = 0
        self.speed = 100.0
        # Python's cycle collector only on this (the Tk) thread: run by itself it can start in the emulation thread
        # while that holds run.lock, and a dead Tk variable's __del__ (the Config window rebuilt: a slider dragged)
        # then waits for the Tk thread - which waits for run.lock: both stood still (2026-10-26, the brightness slider)
        gc.disable()
        self.root.after(2000, self.gc_tick)
        self.title()
        if args.demo:
            self.run.demo = True
            self.root.after(int(args.demo * 1000), self.quit)
        self.draw_ms = []; self.draw_t = []
        if self.run.power:
            self.crt_on(1e9)                                    # first start: a cold tube, and its click
            self.run.crt.overlay = list(self.run.crt.snd_on)
            self.run.flags = self.power_flags()
        self.run.start()
        if os.environ.get('VECTREX_TEST_KNOB'):                 # (test: turn the knob up after n ms)
            self.root.after(int(os.environ['VECTREX_TEST_KNOB']), lambda: self.set_volume(0.5))
        self.root.after(10, self.tick)
        if getattr(args, 'net_host', None) or getattr(args, 'net_join', None) or getattr(args, 'net_online', False):   # (online play: netplay.py)
            self.root.after(600, self.net_start)
        self.win_q = []                                         # (from the window procedure: dropped files, the close button)
        self.root.after(400, self.win_hooks)
        if IS_MAC:
            try: self.root.createcommand('::tk::mac::OpenDocument', lambda *fs: self.win_q.append(('drop', list(fs))))
            except tk.TclError: pass
        if args.fullscreen:                                     # (a restart from full screen stays there)
            self.root.after(100, self.toggle_full)
        self.root.after(150, self.focus)
        self.root.after(2500, self.check_um2_data)
        if getattr(self, 'gl_error', None):
            self.root.after(1200, self.gl_notice)
        if not c1_setup_done(self.settings_f) and IS_PI and self.ren:   # (the first start on a Raspberry Pi: Low - at
            self.root.after(200, lambda: self.set_profile('low'))       #  Ultra its V3D draws ~300 ms a picture and its
                                                                         #  driver crashes now and then, 2026-10-28)
        if not c1_setup_done(self.settings_f) and not args.demo:   # (the first start: the setup, once)
            if IN_GAMESCOPE:                                    # (Steam Deck Game Mode: its window would stay
                self.setup_done = True                          #  hidden - the defaults; View > Setup later)
                self.root.after(900, self.save_settings)
            else:
                self.root.after(900, lambda: deck.setup_window(self))
        self.root.after(500, self.pad_power)

    def gl_notice(self):
        """OpenGL did not start: the plain picture, and why (a black or plain window looked like a broken emulator)"""
        from tkinter import messagebox
        deck.dialog('warning', 'Vectrex_EMU', 'OpenGL could not start, so the picture is plain: no glow, afterglow, '
                               'overlays or on-screen knob.\n\nThe reason: %s\n\nPlease report it on GitHub '
                               "(Help > Copy system info puts your system's details on the clipboard)." % self.gl_error,
                               parent=self.root)
    def root_visible(self):
        """the main window seen (see tk.Tk() in __init__): alpha back to 1 - Tk then takes the layered style off again"""
        try: self.root.attributes('-alpha', 1.0)
        except tk.TclError: pass
    def focus(self):
        self.root.deiconify(); self.root.lift(); self.root.focus_force()
        try:                                                    # (Windows only lets the foreground
            u = ctypes.windll.user32                            #  thread give focus away: join it briefly)
            hwnd = int(self.root.wm_frame(), 16)
            fg = u.GetForegroundWindow()
            a = u.GetWindowThreadProcessId(fg, None); b = ctypes.windll.kernel32.GetCurrentThreadId()
            u.AttachThreadInput(a, b, True)
            u.BringWindowToTop(hwnd); u.SetForegroundWindow(hwnd)
            u.AttachThreadInput(a, b, False)
            self.root.focus_force()
        except Exception:       # noqa
            pass
        try:                                                    # bring it in front of everything once
            self.root.attributes('-topmost', True); self.root.after(300, lambda: self.root.attributes('-topmost', False))
        except tk.TclError:
            pass

    def title(self):
        self.root.title('Vectrex - %s%s%s' % (self.rom_name, '  [PAUSED]' if self.run.paused else '',
                                              '  [OFF - turn the volume knob up (Page Up) to switch on]' if not self.run.power else ''))
    def toggle_pause(self, e=None):
        self.run.paused = not self.run.paused; self.title()
        now = time.perf_counter()                               # (paused: the afterglow stands still)
        if self.run.paused:
            self.pause_t = now
        else:
            self.pause_acc += now - getattr(self, 'pause_t', now)
    def escape(self, e=None):
        """Esc: closes the ROM browser; in full screen back to the window; in the window: pause / go on
        (it never quits - close the window for that: a user pressed Esc to leave a settings window and lost
        the game)"""
        if self.browser:
            self.close_browser()
        elif self.full:
            self.toggle_full()
        elif self.run.power:
            self.toggle_pause()
        return 'break'
    def toggle_osd_meters(self):
        """Config > Display: the CPU and GPU meters in the on-screen display"""
        self.osd_meters = not getattr(self, 'osd_meters', True)
        self.save_settings()
    def headroom(self):
        """(CPU, GPU) - the share of each frame's time used, 0..1+ (None: not known yet). CPU: the busier of the
        emulation (its thread and the core, against the game frame's length) and the drawing thread (against a
        picture's time); GPU: the graphics card's own time for a picture (GL_TIME_ELAPSED) against a picture's time"""
        r = self.ren
        pics = 50.0 if self.vrr else max(1.0, getattr(self, 'hz', 60.0))   # (pictures a second wanted)
        emu = getattr(self.run, 'emu_share', None)
        draw = (r.cpu_ms * pics / 1000.0) if (r is not None and getattr(r, 'cpu_ms', None)
                                              and self.run.frames > 50) else None   # (a new game's first second: its
                                                                                      #  overlay and the tube's start loading)
        cpu = max([v for v in (emu, draw) if v is not None], default=None)
        gpu = (r.gpu_ms * pics / 1000.0) if (r is not None and getattr(r, 'gpu_ms', None)) else None
        return cpu, gpu
    def toggle_fps(self, e=None):
        """Ctrl+F: the OSD (game fps, screen Hz, G-Sync, emulation speed, volume): full screen in the
        corner, in the window the row below the picture"""
        self.fps_on = not self.fps_on
        self.strip_changed()
        return 'break'
    def show_bar(self):
        """the Tk status rows below the window: only to report an error (the OSD is drawn in the strip)"""
        if self.run.error and not self.full:
            self.bar.pack(side='bottom', fill='x', before=self.screen)
        else:
            self.bar.pack_forget()
    def toggle_vrr(self, e=None):
        """Ctrl+G: G-Sync / FreeSync mode (every game frame to the screen the moment it is drawn: the
        monitor runs at the game's rate - no tearing, the wobble stands still at 50 fps) or the fixed
        60 Hz screen (the smoothest afterglow; vsync)"""
        self.vrr = not self.vrr
        if self.ren and hasattr(self.ren, 'present'):
            self.ren.swap_interval(0 if self.vrr else self.vsync)
        self.next_present = time.perf_counter()
        self.save_settings()
        self.root.title('Vectrex - display: ' + ('G-Sync / FreeSync (the game\'s frame rate)' if self.vrr
                                                 else '%d Hz (vsync)' % round(self.hz) if self.vsync else '%d Hz' % round(self.hz)))
        self.root.after(2000, self.title)
        return 'break'
    # ---------------------------------------------------------------- the menu bar
    def build_menu(self):
        """every option, with its keyboard shortcut; ticks show what is on"""
        R = lambda: self.ren
        C = lambda: self.run.crt
        self.menu_checks = []                                   # (variable, current state) to refresh
        self.menu_deps = []                                     # (menu, entry, is it usable now) - greyed out when not
        def mac_key(key):
            """macOS: the menus show the Command shortcuts (as text: Tk's bindings do the work, so a key
            never fires twice through a menu key equivalent)"""
            if not IS_MAC or not key:
                return key
            return '⌃⌘F' if key == 'F11' else key.replace('Ctrl+', '⌘')
        def check(menu, label, key, get, cmd, tip=None):
            key = mac_key(key)
            v = tk.BooleanVar(value=False)
            kw = dict(tip=tip) if DARK_MENU else {}             # (an info box on hover: the dark menus only)
            menu.add_checkbutton(label=label, accelerator=key, variable=v, command=cmd, **kw)
            self.menu_checks.append((v, get))
        def item(menu, label, key, cmd, tip=None):
            key = mac_key(key)
            kw = dict(tip=tip) if DARK_MENU else {}
            menu.add_command(label=label, accelerator=key, command=cmd, **kw)
        def dep(menu, on):
            """the entry just added only matters while `on()` (its feature is on): greyed out otherwise"""
            self.menu_deps.append((menu, menu.index('end'), on))
        def refresh():
            for v, get in self.menu_checks:
                try:
                    v.set(bool(get()))
                except Exception:   # noqa
                    pass
            for menu, i, on in self.menu_deps:
                try:
                    menu.entryconfigure(i, state='normal' if on() else 'disabled')
                except Exception:   # noqa
                    pass
        Menu = darkmenu.DMenu if DARK_MENU else tk.Menu
        def fill(mb):
            m = Menu(mb, tearoff=0, postcommand=refresh); mb.add_cascade(label='Vectrex', menu=m)
            item(m, 'ROM browser', 'L', self.open_browser, "Your games in the roms folder, with their folders; the cartridge below the picture opens it too.")
            rec = Menu(m, tearoff=0, postcommand=lambda r=None: self.fill_recent(rec))   # (the last games played)
            m.add_cascade(label='Recent games', menu=rec)
            item(m, 'Open ROM file...', '', self.open_rom)
            item(m, 'Get games & extras...', '', lambda: getter.window(self),
                 "One click downloads the GCE Vectrex games, the overlays and the PiTrex's SD card files from their own sites, "
                 "unpacked and named for the ROM browser. Not part of the emulator: you download them yourself.")
            item(m, 'Open UVMC2 game...', '', self.open_um2, "A game for the Ultimate Vectrex Multicart 2 (.um2): it runs on the cartridge's own computer.")
            item(m, 'Vector movies', '', self.start_movies,
                 "Vectrex vector movies (Bad Apple, ...) played by the UVMC2 Movie Player; button 4 skips to the next one.")
            if PITREX_UI:
                pm = Menu(m, tearoff=0, postcommand=refresh); m.add_cascade(label='PiTrex', menu=pm)
                item(pm, 'Start the PiTrex', '', self.start_pitrex,
                     "A Raspberry Pi in the cartridge slot: its loader starts, the menu of its games and emulators (button 4 skips the intro).")
                item(pm, 'Open PiTrex program...', '', self.open_pitrex, "One PiTrex program (.img / .pit) on its own, without the loader.")
                item(pm, 'SD card folder...', '', self.choose_px_dir, "The folder the PiTrex sees as its SD card: the files of a PiTrex SD card.")
            item(m, 'Eject / insert cartridge', 'Ctrl+E', self.eject,
                 "Out of the slot: the Vectrex plays its built-in game, Mine Storm. Already out: the last cartridge goes back in. "
                 "A right click on the cartridge below the picture does the same.")
            m.add_separator()
            item(m, 'RESET button', 'Ctrl+R', self.reset_button, "As the button on the Vectrex: the game starts again, the RAM is kept.")
            check(m, 'Pause', 'P', lambda: self.run.paused, self.toggle_pause)
            item(m, 'Volume...', 'PgDn / PgUp', lambda: self.slider('volume'), "Turned all the way down (0 %) the Vectrex switches off, as its own knob does.")
            m.add_separator()
            item(m, 'Setup wizard...', '', lambda: deck.setup_window(self, first=False),
                 "The first-start setup again: what was found, the quality for this PC, the display mode and player 1's keys.")
            m.add_separator()
            item(m, 'Quit', '', self.quit)
            m = Menu(mb, tearoff=0, postcommand=refresh); mb.add_cascade(label='View', menu=m, underline=1)
            check(m, 'Full screen', 'F11', lambda: self.full, self.toggle_full)
            item(m, 'Bigger', '+', lambda: self.zoom_step(1.1))
            item(m, 'Smaller', '-', lambda: self.zoom_step(1 / 1.1))
            item(m, 'Fit screen', '0', lambda: self.zoom_step(None), "The window as big as fits the desktop, above the task bar.")
            item(m, 'Authentic', '9', self.real_size, "As big as on a real Vectrex: the overlay 162 x 207 mm, worked out from your monitor's size.")
            m.add_separator()
            check(m, 'Overlay', 'O', lambda: bool(getattr(self.ren, 'art_on', False) and getattr(self.ren, 'art', None)), self.toggle_art,
                  "The game's plastic overlay in front of the screen, with its colours and artwork.")
            item(m, 'Choose overlay...', '', self.choose_art)
            check(m, 'Case', 'C', lambda: bool(getattr(self.ren, 'case_on', False)), self.toggle_case,
                  "The Vectrex's black plastic round the tube, mirroring the picture. Shown when there is no overlay.")
            check(m, 'Control deck', '', lambda: self.knob_on or self.fps_on or self.cart_on, self.toggle_strip,
                  "The strip below the picture - the on-screen display, P2 PORT, the cartridge, RESET and the volume knob - on or off at once. Off: the window gets smaller.")
            m.add_separator()
            qm = Menu(m, tearoff=0, postcommand=refresh); m.add_cascade(label='Quality', menu=qm)
            for key, tip in (('low', "For weak graphics (old laptops, a Raspberry Pi): Medium's cuts, the phosphor drawn at half "
                                     "size (the vectors stay sharp) and 30 pictures a second."),
                             ('medium', "The Vectrex look for old laptops: the afterglow, the glow, the corner dots and the overlay's colours "
                                        "kept; the tube curve, glass edge, scratches, plastic glow, room light and reflections off; the glow at 3/4 size."),
                             ('high', "All but the room light and its reflections; a lighter glow."),
                             ('ultra', "Every effect on.")):
                check(qm, self.QUALITY_NAMES[key], '', lambda key=key: self.current_profile() == key, lambda key=key: self.set_profile(key), tip)
            check(qm, 'Custom', '', lambda: self.current_profile() == 'custom', lambda: None,
                  "A profile changed by hand in Config: shown here, chosen there.")
            dep(qm, lambda: False)
            qm.add_separator()
            item(qm, 'Optimize...', '', self.test_pc,
                 "A few short tests find the best settings this PC can run at full speed; it shows what it turned off "
                 "(Undo puts it back). Nothing changes by itself while playing.")
            lm = Menu(m, tearoff=0, postcommand=refresh); m.add_cascade(label='Room light', menu=lm)
            for name, k, tip in (('Dark', 0.5, "Half the default room light: the plastic almost black, mostly the vectors."),
                                 ('Evening', 1.0, "The default: a dimly lit room."),
                                 ('Daylight', 2.0, "Twice the default: the overlay's colours and print clearly lit, the glass grey.")):
                check(lm, name, '', lambda k=k: self.room_preset() == k, lambda k=k: self.set_room(k), tip)
            lm.add_separator()
            item(lm, 'Finer...', '', lambda: (setattr(self, 'deck_strip', 'Room light'), self.open_deck()),
                 "Config, the Room light tab: the slider, and the ceiling light reflected on the glass and the overlay.")
            m = Menu(mb, tearoff=0, postcommand=refresh); mb.add_cascade(label='Controls', menu=m, underline=1)
            item(m, 'Keyboard and controllers...', '', self.controls_window,
                 "Which controller and which keys each player uses, the stick's dead zone, and a test field.")
            check(m, 'Mouse steers player 1', '', lambda: self.mouse_stick, self.toggle_mouse_stick,
                  "The pointer's place on the picture is the stick (from the middle); left button = button 4, right = button 1.")
            check(m, 'TV mode shortcuts', '', lambda: self.pad_shortcuts, self.toggle_pad_shortcuts,
                  "From the sofa: hold the stick one way with all four buttons for a second - up: the ROM browser, down: RESET, left: port 2 VecVox / joystick, right: the overlay. Switch off for a game that uses that combination.")
            m.add_separator()
            for k, lbl, tip in (('off', 'Port 2: Joystick', "The second player's controller."),
                                ('vecvoice', 'Port 2: VecVoice', "A speech module (SP0256 chip) in port 2, for games that talk. Needs the speech ROM in the system folder."),
                                ('vecvox', 'Port 2: VecVox', "A speech module (SpeakJet chip) in port 2, for games that talk. Needs the speech ROM in the system folder."),
                                ('lightpen', 'Port 2: Light pen', "The light pen in port 2, for Art Master, AnimAction and Melody Master: the mouse is the pen - point at the picture, hold the left button to touch the screen."),
                                ('imager', 'Port 2: 3D Imager', "The 3D Imager goggles in port 2: its spinning colour disc makes 3D Mine Storm, 3D Narrow Escape and 3D Crazy Coaster colourful (the disc spins up first).")):
                check(m, lbl, '', lambda k=k: self.speech == k, lambda k=k: self.set_speech(k), tip)
            m = Menu(mb, tearoff=0, postcommand=refresh); mb.add_cascade(label='Online', menu=m)
            item(m, 'Host this game...', '', lambda: self.online_window('host'),
                 "Play this game with someone over the internet: you are player 1. The Online window shows a five-letter code "
                 "to tell player 2 - no addresses, no router settings.")
            item(m, 'Host public game...', '', self.public_host_window,
                 "Put this game in the game room under your gamer name: anyone can see it in Find players and join as "
                 "player 2.")
            item(m, 'Join a game...', '', lambda: self.online_window('join'),
                 "Type the code player 1 tells you (or an address): you are player 2; the host's game starts here when you "
                 "have the same ROM.")
            item(m, 'Find players...', '', self.find_players_window,
                 "The game room: the public games waiting for a player 2, with their gamer names - join one with a click.")
            item(m, 'Chat...', 'Ctrl+T', self.chat_window,
                 "Write to the other player while you play: the Chat window (it also opens by itself when they write).")
            dep(m, lambda: bool(getattr(self, 'net_game', None)))
            item(m, 'Leave the online game', '', self.net_leave, "The game goes on here alone; the other player is told.")
            dep(m, self.net_busy)
            m.add_separator()
            item(m, 'Online settings...', '', lambda: self.online_window(),
                 "The meeting point that introduces the two players, hosting without it (a LAN, a forwarded port), and the "
                 "online state in the on-screen display.")
            if DARK_MENU:                                       # (Config: the Config window at once)
                mb.add_command(label='Config', command=self.open_deck)
            m = Menu(mb, tearoff=0); mb.add_cascade(label='Help', menu=m)
            item(m, 'Help...', '', lambda: self.help_window('start'))
            item(m, 'Config (Tab)...', '', lambda: self.help_window('deck'))
            item(m, 'What the menus and Config do...', '', lambda: self.help_window('vectrex'))
            item(m, 'The Vectrex: how it works...', '', lambda: self.help_window('system'))
            item(m, 'Cartridges, bank switching and saving...', '', lambda: self.help_window('carts'))
            m.add_separator()
            item(m, 'Copy system info', '', self.copy_system_info, "Your system and this program's version to the clipboard - for a GitHub issue.")
            item(m, 'About this emulator...', '', lambda: self.help_window('accuracy'))
        if DARK_MENU:
            mb = darkmenu.DBar(self.root, font=(SANS, 10)); fill(mb)
            mb.bind_alt(self.root)
            for k in ('<KeyPress-Alt_L>', '<KeyPress-Alt_R>', '<F10>'):   # (full screen: the bar shown with its menus)
                self.root.bind(k, lambda e: self.full_menu(True) if self.full else None, add='+')
            self.menubar = mb
            self.menu_line = tk.Frame(self.root, bg=darkmenu.LINE, height=1)
            mb.pack(side='top', fill='x', before=self.screen); self.menu_line.pack(side='top', fill='x', before=self.screen)
            for cls in ('Tk', 'Toplevel'):                     # (every window's title bar dark too)
                self.root.bind_class(cls, '<Map>', lambda e: dark_title(e.widget) if e.widget is e.widget.winfo_toplevel() else None, add='+')
            dark_title(self.root)
        else:
            mb = tk.Menu(self.root); fill(mb)
            self.menubar = mb
            self.root.config(menu=mb)
    def chrome_h(self):
        """the height our menu bar takes inside the window (0: full screen, or macOS's own menu bar)"""
        if not DARK_MENU or self.full or not getattr(self, 'menubar', None):
            return 0
        return self.menubar.winfo_reqheight() + 1
    # every adjustable value: (title, keys, the object it lives on, attribute, low, high, log scale, format) -
    # low / high only for the volume knob and values that are off by default; the others: SLIDER_SPAN
    SLIDERS = {
        'volume':        ('Volume (0 % = off)', 'Page Down / Page Up', 'crt', 'volume', 0.0, 1.0, False, '%.0f %%', 100),
        'voice_vol':     ('Speech module volume (100 % = MiSTer Normal)', 'Ctrl+Page Down / Ctrl+Page Up', 'crt', 'voice_vol', 0.0, 2.0, False, '%.0f %%', 100),
        'bloom':         ('Bloom', '', 'ren', 'bloom', 0.0, 3.0, False, '%.2f', 1),
        'white_k':       ('White balance (colour temperature)', '', 'ren', 'white_k', vecrender.WHITE_LO, vecrender.WHITE_HI, 'mired', '%.0f K', 1),
        'trail':         ('Afterglow length (x1 = measured)', '', 'ren', 'trail', 0.1, 10.0, True, 'x%.2f', 1),
        'halo_k':        ('Corner glow strength', '', 'ren', 'halo_k', 0.0, 0.4, False, '%.3f', 1),
        'dot_k':         ('Corner dot brightness (x1 = measured)', '', 'ren', 'dot_k', 0.0, 2.0, False, 'x%.2f', 1),
        'geom_k':        ('Tube geometry strength (x1 = the owner\'s Vectrex, 0 = ideal)', '', 'ren', 'geom_k', 0.0, 2.0, False, 'x%.2f', 1),
        'line_gap':      ('Line gaps (stop delay; 7 = a well-tuned Vectrex)', '', 'ren', 'line_gap', 4.0, 10.0, False, '%.1f cycles', 1),
        'art_tint':      ('Overlay tint strength', '', 'ren', 'art_tint', 0.0, 2.0, False, '%.2f', 1),
        'art_room':      ('Room light (the overlay; without one, the glass)', '', 'ren', 'art_room', 0.0, 1.0, False, '%.2f', 1),
        'light_k':       ('Ceiling light on the glass: strength', '', 'ren', 'light_k', 0.0, 2.0, False, 'x%.2f', 1),
        'light_ov_k':    ('Ceiling light on the overlay: strength', '', 'ren', 'light_ov_k', 0.0, 2.0, False, 'x%.2f', 1),
        'art_print':     ('Overlay printed art brightness', '', 'ren', 'art_print', 0.0, 1.0, False, '%.2f', 1),
        'scr_wear':      ('Scratches: wear - how much the sheet was handled (all of them)', '', 'ren', 'scr_wear', 0.0, 2.0, False, 'x%.2f', 1),
        'scr_fine':      ('Fine scratches (steps 1-3: the matte buff)', '', 'ren', 'scr_fine', 0.0, 2.0, False, 'x%.2f', 1),
        'scr_medium':    ('Medium scratches (steps 4-7)', '', 'ren', 'scr_medium', 0.0, 2.0, False, 'x%.2f', 1),
        'scr_large':     ('Damage (steps 8-10: deep, through the colour)', '', 'ren', 'scr_large', 0.0, 2.0, False, 'x%.2f', 1),
        'scr_scrib':     ('Scratches: scribbles - the share of curly ones', '', 'ren', 'scr_scrib', 0.0, 2.0, False, 'x%.2f', 1),
        'scr_glint':     ('Scratches: glint strength', '', 'ren', 'scr_glint', 0.0, 2.0, False, 'x%.2f', 1),
        'art_glow_k':    ('Overlay plastic glow strength', '', 'ren', 'art_glow_k', 0.0, 2.0, False, 'x%.2f', 1),
        'case_refl':     ('Reflections - the picture mirrored in the case', '', 'ren', 'case_refl', 0.0, 2.0, False, 'x%.2f', 1),
        'case_wear':     ('Wear - scratches, the band the overlays rubbed', '', 'ren', 'case_wear', 0.0, 2.0, False, 'x%.2f', 1),
        'scr_reach':     ('Scratches: glint reach (left: close to the lines, right: farther out)', '', 'ren', 'scr_reach', 0.0, 2.0, False, 'x%.2f', 1),
        'art_dx':        ('Overlay left / right (picture heights; the picture moves, the sheet stays on the case)', '', 'ren', 'art_dx', -0.05, 0.05, False, '%+.3f', 1),
        'art_dy':        ('Overlay up / down (picture heights)', '', 'ren', 'art_dy', -0.05, 0.05, False, '%+.3f', 1),
        'art_scale':     ('Overlay size (the picture is sized the other way)', '', 'ren', 'art_scale', 0.9, 1.1, False, 'x%.3f', 1),
        'phos_grow':     ('Afterglow joining (0 = separate copies)', '', 'ren', 'phos_grow', 0.0, 0.97, False, '%.2f', 1),
        'phos_smooth':   ('Afterglow overlap (px; 0 = off)', '', 'ren', 'phos_smooth', 0.0, 4.0, False, '%.2f', 1),
        'wob_strength':  ('Wobble strength', '', 'ren', 'wob_strength', 0.0, 1.0, False, '%.3f px', 1),
        'bend_strength': ('Bending strength', '', 'ren', 'bend_strength', 0.0, 10.0, False, '%.2f', 1),
        'buzz_vol':      ('Buzz volume', '', 'crt', 'buzz_vol', 0.0, 4.0, False, '%.3f', 1),
        'jack_vol':      ('UVMC2 headphone jack volume', '', 'crt', 'jack_vol', 0.0, 2.0, False, '%.0f %%', 100),
        'motor_vol':     ('Motor volume', '', 'crt', 'motor_vol', 0.0, 1.0, False, '%.0f %%', 100),
        'monitor_inch':  ('Monitor size (diagonal)', '', 'app', 'monitor_inch', 10.0, 100.0, True, '%.1f inch', 1),
    }
    def slider(self, name):
        """a small window with a slider for one value, at the mouse: drag to set, right click for the
        default; it follows the keys too. Esc or clicking elsewhere closes it."""
        title, keys, where, attr, lo, hi, log, fmt, show = self.SLIDERS[name]
        obj = lambda: self.run.crt if where == 'crt' else self if where == 'app' else self.ren
        if not obj() or not hasattr(obj(), attr):
            return
        default = (self.sound_default if where == 'crt' else self.look_default).get(attr, getattr(obj(), attr))
        if attr == 'monitor_inch':                              # (the size the monitor reports, else 24 inch)
            default = self.monitor_auto_inch() or 24.0
            if not self.monitor_inch:
                self.monitor_inch = default
        if attr == 'volume':
            default = 1.0
        # the defaults are the faithful values (measured, or the owner's side-by-side choice): the slider has
        # the default in its middle and goes 100 % below and above it (0 .. twice) - for small personal adjustments. (Not
        # the volume knob: 0 % is the power switch; not a value whose default is 0 = off.)
        default = ART_ADJ_DEFAULT.get(attr, default)            # (an overlay's position / size: 0, 0, x1)
        rel = attr not in ('volume', 'line_gap', 'monitor_inch', 'white_k') + tuple(ART_ADJ_DEFAULT) and default > 0   # (fixed ranges, see SLIDERS)
        if rel:
            lo, hi, log = default * (1 - SLIDER_SPAN), default * (1 + SLIDER_SPAN), False
        if getattr(self, 'slider_win', None):
            try: self.slider_win.destroy()
            except tk.TclError: pass
        N = 1000
        if log == 'mired':                                      # (even in mireds, 1e6 / kelvin: as the eye sees colour)
            to_pos = lambda v: N * (vecrender.white_pos(v) + 1.0) / 2.0     # (the middle neutral: white_pos)
            to_val = lambda p: vecrender.white_from_pos(2.0 * p / N - 1.0)
        else:
            to_pos = lambda v: N * (math.log(v / lo) / math.log(hi / lo) if log else (v - lo) / (hi - lo))
            to_val = lambda p: lo * (hi / lo) ** (p / N) if log else lo + (hi - lo) * p / N
        g, bg = deck.G, deck.PANEL                               # (styled as the Config window)
        t = tk.Toplevel(self.root, bg=bg, highlightthickness=1, highlightbackground=deck.EDGE2); self.slider_win = t
        t.withdraw()                                            # (shown once built, in its place)
        t.title(title); t.transient(self.root); t.resizable(False, False)
        try: t.attributes('-toolwindow', True)
        except tk.TclError: pass
        t.after(30, lambda t=t: deck.no_close_button(t))       # (no close button: Esc / Enter / clicking elsewhere)
        head = tk.Label(t, fg=g, bg=bg, font=(MONO, 10, 'bold'), anchor='w'); head.pack(fill='x', padx=10, pady=(8, 0))
        busy = [False]
        def value_text():
            v = getattr(obj(), attr)
            return '%s   %s%s' % (title, fmt % (v * show), '   (%+d %% from the default)' % round((v / default - 1) * 100) if rel else '')
        def moved(p):
            if busy[0]: return
            v = to_val(float(p))
            if attr == 'volume':
                self.set_volume(round(v * 20) / 20.0)
            else:
                setattr(obj(), attr, round(v, 4))
                if attr == 'line_gap': self.core.set_gap(getattr(obj(), attr))
            head.configure(text=value_text())
            if attr == 'monitor_inch':
                self.real_size(); return
            self.show_look(attr, obj())
        sc = tk.Scale(t, from_=0, to=N, orient='horizontal', length=420, showvalue=0, command=moved, bg=g, fg=g,
                      troughcolor='#123', highlightthickness=0, activebackground='#8cb', sliderrelief='flat', bd=0,
                      width=16, sliderlength=22)                # (the handle: green on a dark trough)
        sc.pack(padx=10, pady=4)
        tk.Label(t, fg=deck.DIM, bg=bg, font=(MONO, 9), anchor='w', justify='left',
                 text=('keys: %s   ' % keys if keys else '') + 'right click: default (%s)   Enter: keep   Esc: undo' % (fmt % (default * show))).pack(fill='x', padx=10, pady=(0, 8))
        def sync():                                             # (the keys change it too)
            if not t.winfo_exists(): return
            busy[0] = True; sc.set(to_pos(max(lo, min(hi, getattr(obj(), attr)))))
            busy[0] = False; head.configure(text=value_text())
            t.after(250, sync)
        def to_default(e=None):
            if attr == 'volume': self.set_volume(default)
            else: setattr(obj(), attr, default)
            if attr == 'line_gap': self.core.set_gap(default)
            if attr == 'monitor_inch': self.real_size()
            busy[0] = True; sc.set(to_pos(default)); busy[0] = False
            head.configure(text=value_text())
            return 'break'
        start = getattr(obj(), attr)                            # (Esc: back to this, as in any dialog)
        def undo(e=None):
            if attr == 'volume': self.set_volume(start)
            else: setattr(obj(), attr, start)
            if attr == 'line_gap': self.core.set_gap(start)
            if attr == 'monitor_inch': self.real_size()
            else: self.show_look(attr, obj())
            close()
            return 'break'
        def close(e=None):
            self.save_settings()
            try: t.destroy()
            except tk.TclError: pass
            self.slider_win = None
        def jump(e):                                            # (a click on the bar: the handle goes there)
            if 'trough' not in sc.identify(e.x, e.y):
                return None
            sc.set(max(0, min(N, (e.x - 11) / float(max(1, sc.winfo_width() - 22)) * N)))
            return 'break'
        sc.bind('<Button-1>', jump)
        sc.bind(RCLICK, to_default); t.bind(RCLICK, to_default)
        t.bind('<Escape>', undo); t.bind('<Return>', close); t.protocol('WM_DELETE_WINDOW', close)
        def lost(e=None):                                       # (focus went elsewhere: close)
            if not t.winfo_exists(): return
            f = t.focus_get()
            if f is None or not str(f).startswith(str(t)):
                close()
        t.bind('<FocusOut>', lambda e: t.after(150, lost))
        sync()
        t.update_idletasks()
        x, y = self.root.winfo_pointerxy()
        t.geometry('+%d+%d' % (max(0, x - t.winfo_reqwidth() // 2), max(0, y - 20)))
        deck.reveal(t); t.focus_force(); sc.focus_set()
    def sliders(self, title, names):
        """one small window with a slider for each of several values (renderer settings with the default in the
        middle, 0 .. twice): drag to set, right click on one for its default, Esc or clicking elsewhere closes"""
        if not self.ren:
            return
        if getattr(self, 'slider_win', None):
            try: self.slider_win.destroy()
            except tk.TclError: pass
        N = 1000; g, bg = deck.G, deck.PANEL                     # (styled as the Config window)
        t = tk.Toplevel(self.root, bg=bg, highlightthickness=1, highlightbackground=deck.EDGE2); self.slider_win = t
        t.withdraw()                                            # (shown once built, in its place)
        t.title(title); t.transient(self.root); t.resizable(False, False)
        try: t.attributes('-toolwindow', True)
        except tk.TclError: pass
        t.after(30, lambda t=t: deck.no_close_button(t))       # (no close button: Esc / Enter / clicking elsewhere)
        rows = []; busy = [False]
        for name in names:
            label, keys, where, attr, lo, hi, log, fmt, show = self.SLIDERS[name]
            default = self.look_default.get(attr, getattr(self.ren, attr))
            if attr not in ('scr_large',):                      # (Damage: its own range, a low default)
                lo, hi = default * (1 - SLIDER_SPAN), default * (1 + SLIDER_SPAN)
            head = tk.Label(t, fg=g, bg=bg, font=(MONO, 10, 'bold'), anchor='w'); head.pack(fill='x', padx=10, pady=(10, 0))
            r = dict(attr=attr, lo=lo, hi=hi, fmt=fmt, show=show, label=label, default=default, head=head)
            def text(r=r):
                v = getattr(self.ren, r['attr'])
                return '%s   %s' % (r['label'], r['fmt'] % (v * r['show']))
            def moved(p, r=r):
                if busy[0]: return
                setattr(self.ren, r['attr'], round(r['lo'] + (r['hi'] - r['lo']) * float(p) / N, 4))
                r['head'].configure(text=text(r)); self.show_look(r['attr'], self.ren)
            sc = tk.Scale(t, from_=0, to=N, orient='horizontal', length=420, showvalue=0, command=moved, bg=g, fg=g,
                          troughcolor='#123', highlightthickness=0, activebackground='#8cb', sliderrelief='flat', bd=0,
                          width=16, sliderlength=22)
            sc.pack(padx=10, pady=4)
            def to_default(e=None, r=r, sc=sc):
                setattr(self.ren, r['attr'], r['default'])
                busy[0] = True; sc.set(N * (r['default'] - r['lo']) / (r['hi'] - r['lo'])); busy[0] = False
                r['head'].configure(text=text(r)); self.show_look(r['attr'], self.ren)
                return 'break'
            def jump(e, sc=sc):                                 # (a click on the bar: the handle goes there)
                if 'trough' not in sc.identify(e.x, e.y):
                    return None
                sc.set(max(0, min(N, (e.x - 11) / float(max(1, sc.winfo_width() - 22)) * N)))
                return 'break'
            sc.bind('<Button-1>', jump); sc.bind(RCLICK, to_default)
            r.update(sc=sc, text=text); rows.append(r)
        tk.Label(t, fg=g, bg=bg, font=(MONO, 9), anchor='w', justify='left',
                 text='right click on a slider: its default   Enter: keep   Esc: undo').pack(fill='x', padx=10, pady=(4, 8))
        def sync():                                             # (other ways of changing them too)
            if not t.winfo_exists(): return
            busy[0] = True
            for r in rows:
                v = max(r['lo'], min(r['hi'], getattr(self.ren, r['attr'])))
                r['sc'].set(N * (v - r['lo']) / (r['hi'] - r['lo'])); r['head'].configure(text=r['text'](r))
            busy[0] = False
            t.after(250, sync)
        def close(e=None):
            self.save_settings()
            try: t.destroy()
            except tk.TclError: pass
            self.slider_win = None
        starts = [(r_, getattr(self.ren, r_['attr'])) for r_ in rows]   # (Esc: all back as they were)
        def undo(e=None):
            for r_, v in starts:
                setattr(self.ren, r_['attr'], v); self.show_look(r_['attr'], self.ren)
            close()
            return 'break'
        t.bind('<Escape>', undo); t.bind('<Return>', close); t.protocol('WM_DELETE_WINDOW', close)
        def lost(e=None):                                       # (focus went elsewhere: close)
            if not t.winfo_exists(): return
            f = t.focus_get()
            if f is None or not str(f).startswith(str(t)):
                close()
        t.bind('<FocusOut>', lambda e: t.after(150, lost))
        sync()
        t.update_idletasks()
        x, y = self.root.winfo_pointerxy()
        t.geometry('+%d+%d' % (max(0, x - t.winfo_reqwidth() // 2), max(0, y - 20)))
        deck.reveal(t); t.focus_force(); rows[0]['sc'].focus_set()
    # ---------------------------------------------------------------- the overlay
    def load_art(self):
        """the overlay for the cartridge in the slot: chosen for this game, else (Choose automatically) by
        its name, else none"""
        if not (self.ren and hasattr(self.ren, 'set_art')):
            return
        key = self.rom_name
        pick = self.art_for.get(key)
        path = None
        if pick == 'none':
            path = None
        elif pick and os.path.exists(pick):
            path = pick
        elif pick and moved_overlay(pick):                      # (its file moved to the overlays folder)
            path = self.art_for[key] = moved_overlay(pick)
        elif self.art_auto:
            if self.args.rom:
                path = find_overlay(key)
                if not path and _art_base(key).lower() == 'mine storm':
                    path = builtin_overlay()
            else:                                               # (no cartridge: the console's own Mine Storm -
                path = next((p for p in (find_overlay('bios'), find_overlay('Mine Storm (built-in game)'),   # yours,
                                         find_overlay('Mine Storm'), builtin_overlay()) if p), None)  # else the one supplied)
        art = None
        if path:
            try:
                art = vart.load_overlay(path)
            except Exception as e:                              # noqa: not an overlay
                print('overlay %s: %s' % (path, e)); path = None
        self.ren.set_art(art)
        self.ren.art_name = os.path.basename(path) if path else ''
        self.art_path = path
        a = self.art_adj.get(os.path.basename(path), [0.0, 0.0, 1.0]) if path else [0.0, 0.0, 1.0]
        self.ren.art_dx, self.ren.art_dy, self.ren.art_scale = (float(v) for v in a)
    def toggle_art(self, e=None):
        """O: the overlay on / off"""
        if not (self.ren and hasattr(self.ren, 'art_on')):
            return
        if not self.ren.art:
            self.show_look('no overlay for this game (Overlay > Choose overlay)')
            return
        self.ren.art_on = not self.ren.art_on
        self.show_look('overlay %s' % (('ON: ' + os.path.splitext(os.path.basename(self.art_path))[0]) if self.ren.art_on else 'OFF'))
    def toggle_scratches(self):
        """Overlay > Scratches: the sheet's wear from decades of handling (made on the graphics card)"""
        if self.ren and hasattr(self.ren, 'scr_on'):
            self.ren.scr_on = not self.ren.scr_on
            self.show_look('scr_on')
    def new_scratches(self):
        """Overlay > New random scratches: another pattern (each overlay its own variation of it)"""
        if self.ren and hasattr(self.ren, 'scr_seed'):
            self.ren.scr_seed = random.randint(2, 99999)
            if not self.ren.scr_on:
                self.ren.scr_on = True
            self.save_settings()
            self.show_look('new random scratches')
    def toggle_art_auto(self):
        self.art_auto = not self.art_auto
        self.load_art()
        self.show_look('overlay chosen automatically: %s' % ('ON' if self.art_auto else 'OFF'))
    def choose_art(self):
        """Overlay > Choose overlay: any .art file for this game (remembered for it)"""
        from tkinter import filedialog
        start = next((d for d in overlay_dirs() if os.path.isdir(d)), ROOT)
        fn = filedialog.askopenfilename(parent=self.root, title='Overlay for ' + self.rom_name, initialdir=start,
                                        filetypes=[('Vectrex overlay (.art, .png)', '*.art *.png'), ('All files', '*.*')])
        if not fn:
            return
        self.art_for[self.rom_name] = os.path.abspath(fn)
        if self.ren: self.ren.art_on = True
        self.load_art()
        self.show_look('overlay: ' + os.path.splitext(os.path.basename(fn))[0] if self.art_path else 'not an overlay file')
    def no_art(self):
        self.art_for[self.rom_name] = 'none'
        self.load_art()
        self.show_look('no overlay for ' + self.rom_name)
    def reset_art_adj(self):
        if self.ren:
            for k, v in ART_ADJ_DEFAULT.items(): setattr(self.ren, k, v)
        self.show_look('overlay position and size: the defaults')
    def set_rotation(self, deg):
        """Display > Rotate screen: the whole screen turned (a monitor on its side: tate)"""
        if deg == self.rot or not (self.ren and hasattr(self.ren, 'set_art')):
            return
        turn = (deg in (90, 270)) != (self.rot in (90, 270))
        self.rot = deg; self.ren.rot = deg
        if turn and not self.full:
            self.root.update_idletasks()
            w, h = self.root.winfo_width(), self.root.winfo_height() - self.chrome_h()
            self.root.geometry('%dx%d' % (h, w + self.chrome_h()))
            self.root.update_idletasks()
        self.apply_scale()
        self.show_look('screen turned %s' % ({0: 'back to normal', 90: '90 degrees right', 180: '180 degrees', 270: '90 degrees left'}[deg]))
    def set_voice_full(self, on):
        """Sound > Speech module: the speakers plugged into its audio out - full range (as it comes, no
        filtering) or a small speaker (no deep bass, rounded treble - still better than the Vectrex's)"""
        self.run.crt.voice_full = on
        self.show_look('voice_full')
    def set_volume(self, v):
        """the knob to a value (the slider): through 0 % it switches the Vectrex off / on"""
        c = self.run.crt
        v = round(max(0.0, min(1.0, v)), 3)
        if v == c.volume:
            return
        c.volume = v
        if v > 0:
            c.new_turn()
        if (v > 0) != self.run.power:
            self.set_power(v > 0)
    def help_window(self, topic='start'):
        """Help: the topics on the left, the text on the right; 'More' buttons unfold a deep dive
        (the history and the mechanics behind a menu option) in place. Text: help_text.py."""
        import help_text, re
        if getattr(self, 'help_win', None):
            try:
                self.help_win.lift(); self.help_show(topic); return
            except tk.TclError:
                pass
        g, dim, bg, hi, txt = deck.G, '#5f8a74', deck.BODY, deck.HI, '#c9ddd3'   # (styled as the Config window)
        t = tk.Toplevel(self.root, bg=deck.BG); self.help_win = t
        t.withdraw()                                            # (shown once built, in its place)
        t.title('Help'); t.transient(self.root)
        ui = self.root.winfo_fpixels('1i') / 96.0
        geo = getattr(self, 'help_geo', None)                   # (its size and place from last time; else over the window)
        if not geo:
            w, h, r = int(980 * ui), int(700 * ui), self.root
            geo = '%dx%d+%d+%d' % (w, h, r.winfo_rootx() + max(0, (r.winfo_width() - w) // 2), r.winfo_rooty() + max(0, (r.winfo_height() - h) // 3))
        m = re.match(r'(\d+)x(\d+)\+(-?\d+)\+(-?\d+)$', geo)          # (on the desktop, all of it: a short screen)
        if m:
            w, h, x, y = (int(v) for v in m.groups())
            x0, y0, x1, y1 = deck.work_area(self.root)
            w = min(w, x1 - x0 - 20); h = min(h, y1 - y0 - 60)
            x = max(x0, min(x, x1 - w)); y = max(y0, min(y, y1 - h - 40))
            geo = '%dx%d+%d+%d' % (w, h, x, y)
        t.geometry(geo)
        def close(e=None):
            try: self.help_geo = '%dx%d+%d+%d' % (t.winfo_width(), t.winfo_height(), t.winfo_x(), t.winfo_y())
            except tk.TclError: pass
            self.save_settings()
            try: t.destroy()
            except tk.TclError: pass
            self.help_win = None
        top = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); top.pack(fill='x')
        tk.Label(top, text='Esc: close   click a topic   More: the deep dive, in place', fg=deck.DIM, bg=deck.TOPBG,
                 font=(MONO, 9)).pack(side='left', padx=12, pady=6)
        foot = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); foot.pack(side='bottom', fill='x')
        deck._button(foot, 'Close', close, self, small=True).pack(side='right', padx=12, pady=6)
        mid = tk.Frame(t, bg=deck.BG); mid.pack(fill='both', expand=True, padx=10, pady=10)
        lf = tk.Frame(mid, bg=deck.PANEL, highlightthickness=1, highlightbackground=deck.EDGE); lf.pack(side='left', fill='y')
        tk.Label(lf, text='TOPICS', fg=deck.DIM, bg=deck.PANEL, font=(MONO, 8), anchor='w').pack(fill='x', padx=10, pady=(6, 2))
        side = tk.Listbox(lf, bg=deck.PANEL, fg=g, selectbackground=darkmenu.SEL, selectforeground=hi, activestyle='none',
                          font=(MONO, 11), width=28, borderwidth=0, highlightthickness=0, exportselection=False)
        side.pack(side='left', fill='y', padx=(6, 6), pady=(0, 8))
        for _, title, _ in help_text.TOPICS:
            side.insert('end', ' ' + title)
        rf = tk.Frame(mid, bg=bg, highlightthickness=1, highlightbackground=deck.EDGE2); rf.pack(side='left', fill='both', expand=True, padx=(8, 0))
        sb = darkmenu.DScroll(rf, bg=bg); sb.pack(side='right', fill='y', padx=(0, 3), pady=3)
        tx = tk.Text(rf, bg=bg, fg=txt, wrap='word', borderwidth=0, highlightthickness=0, padx=16, pady=12,
                     font=(SANS, 11), spacing1=2, spacing3=6, cursor='arrow', yscrollcommand=sb.set,
                     selectbackground=darkmenu.SEL, inactiveselectbackground=darkmenu.SEL)
        tx.pack(side='left', fill='both', expand=True)
        sb.config(command=tx.yview)
        tx.tag_configure('h1', font=(MONO, 18, 'bold'), foreground=hi, spacing3=10)
        tx.tag_configure('h2', font=(MONO, 13, 'bold'), foreground=hi, spacing1=12, spacing3=4)
        tx.tag_configure('item', font=(SANS, 11, 'bold'), foreground=hi, spacing1=8, spacing3=0)
        tx.tag_configure('ind', lmargin1=22, lmargin2=22)
        tx.tag_configure('b', font=(SANS, 11, 'bold'), foreground=hi)
        tx.tag_configure('deep', lmargin1=22, lmargin2=22, foreground='#a9c2b6', background=deck.PANEL,
                         font=(SANS, 10), spacing1=4, spacing3=8)
        def put(text, *tags):                                   # (*word* = bold)
            for i, part in enumerate(re.split(r'\*([^*]+)\*', text)):
                tx.insert('end', part, tags + (('b',) if i % 2 else ()))
        def show(key):
            idx = next((i for i, (k, _, _) in enumerate(help_text.TOPICS) if k == key), 0)
            side.selection_clear(0, 'end'); side.selection_set(idx); side.see(idx)
            _, title, blocks = help_text.TOPICS[idx]
            tx.config(state='normal'); tx.delete('1.0', 'end')
            tx.insert('end', title + '\n', 'h1')
            for n, b in enumerate(blocks):
                if b[0] == 'p':
                    put(b[1] + '\n')
                elif b[0] == 'h':
                    tx.insert('end', b[1] + '\n', 'h2')
                elif b[0] == 'item':
                    tx.insert('end', b[1] + '\n', 'item'); put(b[2] + '\n', 'ind')
                elif b[0] == 'more':
                    tag = 'more%d' % n
                    btn = tk.Label(tx, text='▸  More: ' + b[1], bg=bg, fg=g, cursor='hand2',
                                   font=(SANS, 10, 'underline'))
                    def flip(e, tag=tag, btn=btn, title=b[1]):
                        hidden = tx.tag_cget(tag, 'elide') in ('1', 'true', True)
                        tx.tag_configure(tag, elide=not hidden)
                        btn.config(text=('▾  ' if hidden else '▸  More: ') + title)
                    btn.bind('<Button-1>', flip)
                    tx.insert('end', '   ', 'ind'); tx.window_create('end', window=btn); tx.insert('end', '\n')
                    put(b[2] + '\n', 'deep', tag)
                    tx.tag_configure(tag, elide=True)
            tx.config(state='disabled'); tx.yview_moveto(0)
        self.help_show = show
        side.bind('<<ListboxSelect>>', lambda e: side.curselection() and show(help_text.TOPICS[side.curselection()[0]][0]))
        t.bind('<Escape>', close)
        t.protocol('WM_DELETE_WINDOW', close)
        tx.bind('<MouseWheel>', lambda e: tx.yview_scroll(-3 * self.wheel_steps(e), 'units'))
        show(topic)
        deck.reveal(t, lambda: deck.no_close_button(t))
    def controls_window(self):
        """Controls > Keyboard and controllers: per player a controller (only the connected ones are listed, by their own
        names; one player's controller is not offered to the other) and the keys - both work at once.
        Click a box, then press a key / a controller button, or push the stick the way the box says;
        Shift+click adds a second; right click clears (a stick box: back to the controller's default).
        Each controller keeps its own mapping and dead zone; the test field shows what the Vectrex gets.
        Shortcut keys (F11, Esc, Page Up / Down, Ctrl+...) can not be taken."""
        if getattr(self, 'ctl_win', None):
            try: self.ctl_win.lift(); return
            except tk.TclError: pass
        inp = self.inp
        g, dim, bg, hi, off = deck.G, '#5f8a74', deck.BODY, deck.HI, '#2c4538'   # (styled as the Config window)
        f9 = (MONO, 10)
        P = ('p1', 'p2')
        col = lambda n, m=0: 1 + 2 * n + m
        R = lambda r: 2 * r                                     # (the grid's rows: a section line between)
        t = tk.Toplevel(self.root, bg=deck.BG); self.ctl_win = t
        t.withdraw()                                            # (shown once built, in its place)
        t.title('Controls'); t.transient(self.root); t.resizable(False, False)
        top = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); top.pack(fill='x')
        tk.Label(top, text='Esc: close   click a box, then a key or a button   right click: clear', fg=deck.DIM,
                 bg=deck.TOPBG, font=(MONO, 9)).pack(side='left', padx=(12, 6), pady=6)
        how = ("Click a box, then press a key or a controller button, or push the stick the way the row says. Shift+click: "
               "add a second. Right click: clear (a stick box: back to the default axis).\n\n"
               "Stick bars: the ring is the controller's stick; drag the inner handles for the dead zone, the outer ones for "
               "the endpoint (where the Vectrex stick is already full); right click: back to the default.\n\n"
               "Test: the square is the full stick, the dark one the dead zone, the dashed one the endpoints; the ring is the "
               "controller's stick, the dot what the Vectrex gets. Keyboard and controller work together: the buttons add up, "
               "a direction key overrides the stick on its axis.\n\n"
               "Not allowed: F11, Esc, Page Up / Down, + - 0 9, P L O C and Ctrl+... (the emulator's shortcuts).")
        him = deck.info_mark(top, deck.TOPBG, how); him.pack(side='left')
        hl = tk.Label(top, text=' how it works', fg=deck.DIM, bg=deck.TOPBG, font=(MONO, 9)); hl.pack(side='left')
        deck.Tip(him, how, also=(hl,))
        sc = deck.Scroller(t, deck.BG); sc.frame.pack(padx=10, pady=(10, 0)); sc.bind_wheel(t)   # (a short screen: it scrolls)
        area = tk.Frame(sc.holder, bg=bg, highlightthickness=1, highlightbackground=deck.EDGE2); area.pack()
        body = tk.Frame(area, bg=bg); body.pack(padx=12, pady=10)
        def section(r, caption):
            """a caption and a line above grid row r"""
            sf = tk.Frame(body, bg=bg); sf.grid(row=R(r) - 1, column=0, columnspan=5, sticky='ew', pady=(6, 1) if caption else (4, 2))
            if caption:                                         # (none: only the line - the row's own name says it)
                tk.Label(sf, text=caption, fg=deck.DIM, bg=bg, font=(MONO, 8)).pack(side='left', padx=(4, 8))
            tk.Frame(sf, bg=deck.EDGE, height=1).pack(side='left', fill='x', expand=True, pady=(2, 0))
        inp.refresh_devices()
        for n, text in enumerate(('Player 1 (1st port)', 'Player 2 (2nd port)')):
            tk.Label(body, text=text, fg=hi, bg=bg, font=f9).grid(row=R(0), column=col(n), columnspan=2, sticky='w', padx=4)
        # the controllers
        tk.Label(body, text='Controller', fg=dim, bg=bg, font=f9).grid(row=R(1), column=0, sticky='w', padx=4)
        menus = {}
        section(1, '')
        for n, p in enumerate(P):
            v = tk.StringVar()
            om = tk.Label(body, textvariable=v, bg=deck.PANEL, fg=g, font=f9, width=38, anchor='w', padx=8, pady=2,
                          cursor='hand2', highlightthickness=1, highlightbackground='#3d6b55')
            dm = darkmenu.DMenu(om)
            om.bind('<Button-1>', lambda e, om=om, dm=dm: dm.post(om.winfo_rootx(), om.winfo_rooty() + om.winfo_height(), t))
            om.grid(row=R(1), column=col(n), columnspan=2, sticky='w', padx=4, pady=4)
            menus[p] = (v, om, dm)
        def dev_options(p):
            """[(id, name)]: keyboard only, the connected controllers the other player does not have,
            and this player's own when it is unplugged"""
            o = inp.resolve(inp.other(p))
            opts = [('keyboard', 'Keyboard only')] + [(d['id'], d['name']) for d in inp._devs if not o or d['id'] != o['id']]
            cur = inp.dev[p]
            if cur not in ('keyboard', 'auto') and all(i != cur for i, _ in opts):
                opts.append((cur, inp.map(cur).get('name', cur) + ' - not connected'))
            return opts
        def fill_menus():
            for p in P:
                v, om, m = menus[p]
                opts = dev_options(p)
                m.delete(0, 'end')
                for i, nm in opts:
                    m.add_command(label=nm, command=lambda p=p, i=i: set_dev(p, i))
                v.set(next((nm for i, nm in opts if i == inp.dev[p]), opts[0][1]))
        def set_dev(p, i):
            inp.set_dev(p, i); cap['what'] = None
            fill_menus(); refresh(); self.save_settings()
        section(2, 'BUTTONS AND STICK')
        for n in range(2):
            for m, text in enumerate(('Keyboard', 'Controller')):
                tk.Label(body, text=text, fg=dim, bg=bg, font=f9).grid(row=R(2), column=col(n, m), sticky='w', padx=4)
        boxes = {}
        cap = {'what': None}
        def axis_text(d, m, which):
            a, flip = m[which], m[which + 'flip']
            name = XI_AXIS_NAMES.get(a, a) if d['kind'] == 'xi' else '%s axis' % a
            return name + (' (rev)' if flip else '')
        def pad_text(p, k):
            d = inp.resolve(p)
            if not d:
                return '-'
            m = inp.map(d['id'])
            if m['mode'] == 'analog':
                if k in ('up', 'down'):
                    return axis_text(d, m, 'y')
                if k in ('left', 'right'):
                    return axis_text(d, m, 'x')
                if k == 'half':
                    return '(analog)'
            return ', '.join(('button %s' % x[1:]) if x.startswith('J') else x for x in m[k]) or '-'
        def pad_live(p, k):
            d = inp.resolve(p)
            return bool(d) and (k != 'half' or inp.map(d['id'])['mode'] == 'buttons')
        def key_text(p, k):
            return ', '.join(nm for _, nm in inp.keys[p][k]) or '-'
        def refresh():
            for (p, k, kind), w in boxes.items():
                if cap['what'] == (p, k, kind):
                    continue
                live = kind == 'key' or pad_live(p, k)
                w.configure(text=key_text(p, k) if kind == 'key' else pad_text(p, k), fg=g if live else off,
                            highlightbackground='#3d6b55' if live else off, cursor='hand2' if live else '')
            for p in P:                                     # (the stick: analog / buttons, link X and Y)
                d = inp.resolve(p); ana, btns, link = toggles[p]
                m = inp.map(d['id']) if d else None
                for w, on in ((ana, m and m['mode'] == 'analog'), (btns, m and m['mode'] == 'buttons')):
                    w.configure(fg=(hi if on else dim) if m else off, bg='#123' if on else bg,
                                highlightbackground='#3d6b55' if m else off, cursor='hand2' if m else '')
                lk = m and m['mode'] == 'analog'
                link.configure(text=('[x]' if m and m['link'] else '[ ]') + ' link X and Y', fg=g if lk else off,
                               cursor='hand2' if lk else '')
        def start(p, k, kind, add=False):
            d = inp.resolve(p)
            if kind == 'pad' and not pad_live(p, k):
                return
            analog = bool(d) and inp.map(d['id'])['mode'] == 'analog'
            cap.update(what=(p, k, kind), add=add, t=time.perf_counter())
            refresh()
            if kind == 'key':
                prompt = 'press a key...'
            elif analog and k in ('up', 'down', 'left', 'right'):
                prompt = 'push the stick %s...' % k
            else:
                prompt = 'press a button...'
            boxes[(p, k, kind)].configure(text=prompt, fg=hi, highlightbackground=g)
            if kind == 'pad':
                st = inp.pad_state(d)
                cap['base'] = st['buttons'] if st else set()
                cap['axes'] = dict(st['axes']) if st else {}
                poll_pad()
        def poll_pad():
            w = cap['what']
            if not w or w[2] != 'pad' or not t.winfo_exists():
                return
            p, k, _ = w
            d = inp.resolve(p)
            st = inp.pad_state(d)
            if st and k in ('up', 'down', 'left', 'right') and inp.map(d['id'])['mode'] == 'analog':
                # the axis that moved most from where it was: the stick's x (left / right) or y (up / down)
                moved = [(abs(v - cap['axes'].get(a, v)), a, v - cap['axes'].get(a, v)) for a, v in st['axes'].items()]
                big = max(moved, default=(0, None, 0))
                if big[0] > 0.5:
                    _, a, delta = big
                    m = inp.map(d['id'])
                    which = 'y' if k in ('up', 'down') else 'x'
                    o = 'x' if which == 'y' else 'y'
                    if m[o] == a:                       # (the other way had that axis: they swap)
                        m[o], m[o + 'flip'] = m[which], False
                    want = 1.0 if k in ('up', 'right') else -1.0
                    m[which] = a
                    m[which + 'flip'] = delta * inp.natural(d, which) * want < 0
                    cap['what'] = None; refresh(); self.save_settings(); return
            elif st:
                new = st['buttons'] - cap['base']
                if new:
                    b = sorted(new, key=lambda x: (len(x), x))[0]
                    m = inp.map(d['id'])
                    for k2 in CTRL_INPUTS:                      # (a button does one thing)
                        if b in m[k2]: m[k2].remove(b)
                    m[k] = (m[k] if cap.get('add') else []) + [b]
                    cap['what'] = None; refresh(); self.save_settings(); return
                cap['base'] &= st['buttons']
            if time.perf_counter() - cap['t'] > 8.0:
                cap['what'] = None; refresh(); return
            t.after(30, poll_pad)
        def key_pressed(e):
            w = cap['what']
            if not w or w[2] != 'key':
                return
            p, k, _ = w
            vk = e.keycode if IS_WIN else osdep.vk_of(e.keysym)
            if not vk:
                return 'break'
            box = boxes[w]
            if vk in RESERVED_VK or e.state & 0x4:
                name = RESERVED_VK.get(vk, 'Ctrl+' + e.keysym)
                box.configure(text='%s is a shortcut' % name, fg='#e93')
                t.after(1200, lambda: (cap.update(what=None), refresh()))
                return 'break'
            name = e.keysym
            name = {'Shift_L': 'Shift', 'Shift_R': 'Shift', 'Alt_L': 'Alt', 'Alt_R': 'Alt', 'space': 'Space',
                    'Return': 'Enter', 'BackSpace': 'Backspace'}.get(name, name)
            name = name.upper() if len(name) == 1 else name.replace('_', ' ')
            for pp in P:                                        # (a key does one thing)
                for kk in CTRL_INPUTS:
                    inp.keys[pp][kk] = [x for x in inp.keys[pp][kk] if x[0] != vk]
            inp.keys[p][k] = (inp.keys[p][k] if cap.get('add') else []) + [(vk, name)]
            cap['what'] = None; refresh(); self.save_settings()
            return 'break'
        def clear(p, k, kind):
            d = inp.resolve(p)
            if kind == 'key':
                inp.keys[p][k] = []
            elif d and inp.map(d['id'])['mode'] == 'buttons':
                inp.map(d['id'])[k] = []
            elif d and k in ('up', 'down', 'left', 'right'):  # (a stick box: the controller's default axis)
                which = 'y' if k in ('up', 'down') else 'x'
                m, dm = inp.map(d['id']), default_map(d['id'])
                m[which], m[which + 'flip'] = dm[which], False
                if m['x'] == m['y']:
                    o = 'x' if which == 'y' else 'y'
                    m[o], m[o + 'flip'] = dm[o], False
            elif d and k in ('b1', 'b2', 'b3', 'b4'):
                inp.map(d['id'])[k] = []
            cap['what'] = None; refresh(); self.save_settings()
        for r, k in enumerate(CTRL_INPUTS):
            tk.Label(body, text=CTRL_NAMES[k], fg=g, bg=bg, font=f9).grid(row=R(3 + r), column=0, sticky='w', padx=4, pady=2)
            for n, p in enumerate(P):
                for m, kind in enumerate(('key', 'pad')):
                    w = tk.Label(body, text='', fg=g, bg=bg, font=f9, width=18, anchor='w', padx=6,
                                 highlightthickness=1, highlightbackground='#3d6b55')
                    w.grid(row=R(3 + r), column=col(n, m), sticky='w', padx=4, pady=2)
                    w.bind('<Button-1>', lambda e, p=p, k=k, kind=kind: start(p, k, kind, bool(e.state & 1)))
                    w.bind(RCLICK, lambda e, p=p, k=k, kind=kind: clear(p, k, kind))
                    boxes[(p, k, kind)] = w
        # the stick (the player's controller's): analog or on buttons; per axis a bar with the stick on it
        # (ring) and two handles to drag: the dead zone (inner) and the endpoint (outer: where full
        # deflection is reached, for sticks with a long travel); link: X and Y move together
        row = 3 + len(CTRL_INPUTS)
        section(row, '')
        tk.Label(body, text='Stick', fg=g, bg=bg, font=f9).grid(row=R(row), column=0, sticky='w', padx=4, pady=(2, 2))
        toggles = {}
        def ctl_map(p):
            d = inp.resolve(p)
            return inp.map(d['id']) if d else None
        def set_mode(p, mode):
            m = ctl_map(p)
            if m and m['mode'] != mode:
                m['mode'] = mode; cap['what'] = None; refresh(); self.save_settings()
        def toggle_link(p):
            m = ctl_map(p)
            if m and m['mode'] == 'analog':
                m['link'] = not m['link']
                if m['link']:                                   # (linked: Y takes X's settings)
                    m['dzy'], m['epy'] = m['dzx'], m['epx']
                refresh(); self.save_settings()
        for n, p in enumerate(P):
            f = tk.Frame(body, bg=bg); f.grid(row=R(row), column=col(n), columnspan=2, sticky='w', padx=4, pady=(2, 2))
            ws = []
            for mode, text in (('analog', 'analog stick'), ('buttons', 'buttons')):
                w = tk.Label(f, text=text, fg=g, bg=bg, font=f9, padx=8, highlightthickness=1, highlightbackground='#3d6b55')
                w.pack(side='left'); w.bind('<Button-1>', lambda e, p=p, mode=mode: set_mode(p, mode)); ws.append(w)
            link = tk.Label(f, text='', fg=g, bg=bg, font=f9); link.pack(side='left', padx=(18, 0))
            link.bind('<Button-1>', lambda e, p=p: toggle_link(p))
            toggles[p] = ws + [link]
        BW, BH, BM = 436, 46, 14                                # (a bar canvas: width, height, side margin)
        bx = lambda v: BW / 2 + v * (BW / 2 - BM)
        bars = {}
        def bar_pos(e):
            return min(1.0, abs((e.x - BW / 2) / (BW / 2 - BM)))
        def bar_press(p, a, e):
            m = ctl_map(p)
            if not m or m['mode'] != 'analog':
                return
            v = bar_pos(e)
            cap['drag'] = (p, a, 'ep' if abs(v - m['ep' + a]) < abs(v - m['dz' + a]) else 'dz')
            bar_drag(e)
        def bar_drag(e):
            dr = cap.get('drag')
            m = dr and ctl_map(dr[0])
            if not m:
                return
            p, a, h = dr
            v = round(bar_pos(e), 2)
            for ax in (('x', 'y') if m['link'] else (a,)):
                if h == 'ep':
                    m['ep' + ax] = max(m['dz' + ax] + 0.1, min(1.0, v))
                else:
                    m['dz' + ax] = max(0.0, min(0.5, m['ep' + ax] - 0.1, v))
        def bar_release(e):
            if cap.pop('drag', None):
                self.save_settings()
        def bar_reset(p, a):                                    # (right click: the default dead zone, full travel)
            m = ctl_map(p)
            if m and m['mode'] == 'analog':
                for ax in (('x', 'y') if m['link'] else (a,)):
                    m['dz' + ax], m['ep' + ax] = DEFAULT_DEADZONE, 1.0
                self.save_settings()
        for r, a in enumerate(('x', 'y')):
            tk.Label(body, text='%s axis' % a.upper(), fg=g, bg=bg, font=f9).grid(row=R(row + 1 + r), column=0, sticky='w', padx=4, pady=2)
            for n, p in enumerate(P):
                cv = tk.Canvas(body, width=BW, height=BH, bg=bg, highlightthickness=0)
                cv.grid(row=R(row + 1 + r), column=col(n), columnspan=2, sticky='w', padx=4, pady=2)
                cv.bind('<Button-1>', lambda e, p=p, a=a: bar_press(p, a, e))
                cv.bind('<B1-Motion>', bar_drag)
                cv.bind('<ButtonRelease-1>', bar_release)
                cv.bind(RCLICK, lambda e, p=p, a=a: bar_reset(p, a))
                bars[(p, a)] = cv
        def draw_bar(cv, a, m, raw, out):
            cv.delete('all')
            y0, y1 = 25, 39
            if not m or m['mode'] != 'analog':
                cv.create_rectangle(BM, y0, BW - BM, y1, outline=off)
                cv.create_text(BM, 8, text='-' if not m else 'stick on buttons', fill=off, font=f9, anchor='w')
                return
            dz, ep = m['dz' + a], m['ep' + a]
            cv.create_rectangle(BM, y0, BW - BM, y1, outline='#3d6b55')
            cv.create_rectangle(bx(-dz), y0 + 1, bx(dz), y1 - 1, fill='#0c1712', outline='')
            cv.create_rectangle(BM + 1, y0 + 1, bx(-ep), y1 - 1, fill='#13221b', outline='')      # (beyond the
            cv.create_rectangle(bx(ep), y0 + 1, BW - BM - 1, y1 - 1, fill='#13221b', outline='')  #  endpoints)
            if out:
                cv.create_rectangle(min(bx(0), bx(out * ep)), y0 + 4, max(bx(0), bx(out * ep)), y1 - 4, fill=g, outline='')
            for v in (-dz, dz):
                cv.create_rectangle(bx(v) - 1, y0 - 3, bx(v) + 1, y1 + 3, fill=g, outline='')
            for v in (-ep, ep):
                cv.create_rectangle(bx(v) - 2, y0 - 6, bx(v) + 2, y1 + 5, fill=hi, outline='')
            if raw is not None:
                cv.create_oval(bx(raw) - 6, (y0 + y1) / 2 - 6, bx(raw) + 6, (y0 + y1) / 2 + 6, outline=dim)
            cv.create_text(BM, 8, anchor='w', fill=g, font=(MONO, 9),
                           text='in %d %%  out %d' % (round((raw or 0) * 100), round(out * 127)))
            cv.create_text(BW - BM, 8, anchor='e', fill=g, font=(MONO, 9),
                           text='dead zone %d %%  end %d %%' % (round(dz * 100), round(ep * 100)))
        # the test field: what the Vectrex gets from the keys and the controller
        row += 3
        section(row, 'TEST: WHAT THE VECTREX GETS')
        tk.Label(body, text='Test', fg=g, bg=bg, font=f9).grid(row=R(row), column=0, sticky='nw', padx=4, pady=(2, 2))
        S, C = 48, 56                                           # (the square's half size, its centre)
        tests = {}
        for n, p in enumerate(P):
            cv = tk.Canvas(body, width=330, height=2 * C, bg=bg, highlightthickness=0)
            cv.grid(row=R(row), column=col(n), columnspan=2, sticky='w', padx=4, pady=(2, 2))
            cv.create_rectangle(C - S, C - S, C + S, C + S, outline='#3d6b55')
            cv.create_line(C - S, C, C + S, C, fill='#1c2e25'); cv.create_line(C, C - S, C, C + S, fill='#1c2e25')
            it = dict(dz=cv.create_rectangle(C, C, C, C, outline='#2c4538', fill='#0c1712'),
                      ep=cv.create_rectangle(C, C, C, C, outline=dim, dash=(3, 3)),
                      raw=cv.create_oval(0, 0, 0, 0, outline=dim, width=1),
                      out=cv.create_oval(0, 0, 0, 0, fill=hi, outline=''),
                      lamps=[], text=cv.create_text(2 * C + 14, C + 34, text='', fill=g, font=f9, anchor='w'))
            for i in range(4):
                x = 2 * C + 22 + i * 44
                it['lamps'].append(cv.create_oval(x - 13, C - 30, x + 13, C - 4, outline=g, width=1))
                cv.create_text(x, C - 17, text=str(i + 1), fill=g, font=f9)
            tests[p] = (cv, it)
        row -= 1
        bar = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE)   # (the foot, as Config's)
        def test():
            if not t.winfo_exists():
                return
            if time.perf_counter() - cap.get('scan', -9) > 1.0:   # (controllers plugged in / out)
                cap['scan'] = time.perf_counter()
                known = [d['id'] for d in inp._devs]
                inp.refresh_devices()
                if known != [d['id'] for d in inp._devs]:
                    fill_menus(); refresh()
            inp.update_focus()
            for p in P:
                cv, it = tests[p]
                jx, jy, btn, raw = inp.read_player(p)
                m = ctl_map(p)
                ana = bool(m) and m['mode'] == 'analog'
                zx, zy = (S * m['dzx'], S * m['dzy']) if ana else (0, 0)
                ex, ey = (S * m['epx'], S * m['epy']) if ana else (0, 0)
                cv.coords(it['dz'], C - zx, C - zy, C + zx, C + zy)
                cv.coords(it['ep'], C - ex, C - ey, C + ex, C + ey)
                for a, r in (('x', raw[0] if raw else None), ('y', raw[1] if raw else None)):
                    draw_bar(bars[(p, a)], a, m, r if ana else None,
                             shape(r, m['dz' + a], m['ep' + a]) if ana and r is not None else 0.0)
                if raw:
                    x, y = C + raw[0] * S, C - raw[1] * S
                    cv.coords(it['raw'], x - 7, y - 7, x + 7, y + 7)
                else:
                    cv.coords(it['raw'], 0, 0, 0, 0)
                x, y = C + jx / 127.0 * S, C - jy / 127.0 * S
                cv.coords(it['out'], x - 4, y - 4, x + 4, y + 4)
                for i, lamp in enumerate(it['lamps']):
                    cv.itemconfigure(lamp, fill=hi if btn & (1 << i) else '')
                cv.itemconfigure(it['text'], text='x %+4d   y %+4d' % (jx, jy))
            t.after(30, test)
        def reset():
            inp.reset(); cap['what'] = None
            fill_menus(); refresh(); self.save_settings()
        def close():
            cap['what'] = None
            try: self.ctl_pos = (t.winfo_x(), t.winfo_y())     # (it opens there next time)
            except tk.TclError: pass
            self.save_settings()
            try: t.destroy()
            except tk.TclError: pass
            self.ctl_win = None
        def layout(kind):
            """player 1's buttons 1-4 on the keyboard: a cross under the left hand (W A S D: left, top, bottom,
            right) or a row as on the controller (A S D F)"""
            keys = {'cross': ((0x41, 'A'), (0x57, 'W'), (0x53, 'S'), (0x44, 'D')),
                    'row': ((0x41, 'A'), (0x53, 'S'), (0x44, 'D'), (0x46, 'F'))}[kind]
            for i, k in enumerate(('b1', 'b2', 'b3', 'b4')):
                inp.keys['p1'][k] = [keys[i]]
            cap['what'] = None; refresh(); self.save_settings()
        tk.Label(bar, text='Player 1 buttons:', fg=deck.DIM, bg=deck.TOPBG, font=(MONO, 9)).pack(side='left', padx=(12, 4), pady=6)
        for text, kind in (('cross W A S D', 'cross'), ('row A S D F', 'row')):
            deck._button(bar, text, lambda k=kind: layout(k), self, small=True).pack(side='left', padx=3)
        deck._button(bar, 'Close', close, self, small=True).pack(side='right', padx=12)
        deck._button(bar, 'Reset to defaults', reset, self, small=True).pack(side='right')
        section(row + 4, 'OPTIONS')
        opts = tk.Frame(body, bg=bg); opts.grid(row=R(row + 4), column=0, columnspan=5, sticky='w', pady=(2, 0))
        for text, get, cmd, info in (
                ('Mouse steers player 1', lambda: self.mouse_stick, self.toggle_mouse_stick,
                 "The pointer's place on the picture is the stick (from the middle); left button = button 4, right = "
                 "button 1. A touchpad works too."),
                ('TV mode shortcuts', lambda: self.pad_shortcuts, self.toggle_pad_shortcuts,
                 "From the sofa: hold the stick one way with all four buttons for a second - up: the ROM browser, down: "
                 "RESET, left: port 2 VecVox / joystick, right: the overlay. Switch off for a game that uses that combination.")):
            v = tk.BooleanVar(value=bool(get()))
            of = tk.Frame(opts, bg=bg); of.pack(side='left', padx=(4, 30))   # (side by side: one row)
            cb = tk.Checkbutton(of, text=text, variable=v, command=cmd, fg=g, bg=bg, selectcolor=bg, activebackground=bg,
                                activeforeground=hi, font=f9, anchor='w', highlightthickness=0, bd=0); cb.pack(side='left')
            im = deck.info_mark(of, bg, info); im.pack(side='left', padx=(4, 0))
            deck.Tip(im, info, also=(cb,))
        bar.pack(fill='x', pady=(10, 0))
        t.bind('<KeyPress>', key_pressed)
        t.bind('<Escape>', lambda e: (cap.update(what=None), refresh(), 'break')[2] if cap['what'] else close())
        t.protocol('WM_DELETE_WINDOW', close)
        fill_menus(); refresh(); test()
        sc.fit_window(t)
        t.update_idletasks()
        pos = getattr(self, 'ctl_pos', None)                    # (where it was last time, if still on a screen)
        vx, vy = t.winfo_vrootx(), t.winfo_vrooty()
        vw, vh = max(t.winfo_screenwidth(), t.winfo_vrootwidth()), max(t.winfo_screenheight(), t.winfo_vrootheight())
        if pos and vx - 50 <= pos[0] <= vx + vw - 100 and vy - 10 <= pos[1] <= vy + vh - 60:
            t.geometry('+%d+%d' % pos)
        else:
            r = self.root
            t.geometry('+%d+%d' % (r.winfo_rootx() + max(0, (r.winfo_width() - t.winfo_reqwidth()) // 2),
                                   r.winfo_rooty() + max(0, (r.winfo_height() - t.winfo_reqheight()) // 3)))
        deck.reveal(t, lambda: (deck.on_screen(t), deck.no_close_button(t)))
        t.focus_force()
    def set_cart_hw(self, k):
        """Vectrex > Cartridge hardware: Auto = the board the ROM's size calls for (64K PB6 carts,
        Vectorblade's 4 x 48K flash, the 512K latch flash, Sean Kelly's Multi-Cart), or one board for every
        ROM; Off = a bone-stock Vectrex: every ROM a plain 32K cartridge. The cartridge is put in again (a
        power cycle) with the new setting."""
        self.cart_hw = k
        self.restart(self.args.rom)
        self.show_look('cartridge: %s' % CART_HW[k].upper())
    def port2_auto(self, rom):
        """what is in port 2 for this game: the player's choice - or, with the joystick there, the accessory the game
        needs (port2_needed: the 3D Imager, the light pen), for this game only; a choice made by hand for this game
        stays"""
        user = getattr(self, 'speech_user', getattr(self, 'speech', 'off'))
        if not getattr(self, 'p2_auto', True):                  # (switched off: port 2 holds what was chosen, always)
            return user
        own = getattr(self, 'p2_games', {}).get(os.path.basename(rom)) if rom else None
        if own in PORT2 and not (own in ('vecvoice', 'vecvox') and not sp_rom()):
            return own                                          # (chosen by hand for this game: this game only)
        need = port2_needed(rom)
        return need if (need and user == 'off') else user
    def set_speech(self, k):
        """Vectrex > Controller port 2: the second joystick, or a speech accessory - the VecVoice (an
        SP0256-AL2) or the VecVox (a SpeakJet, spoken here with the SP0256's voice at the SpeakJet's
        pitch and lengths). Both need the SP0256-AL2 ROM in the bios folder (sp0256-al2.bin, from
        SP_ROM_URL); the Vectrex is switched off and on with the new setting."""
        if k in ('vecvoice', 'vecvox') and not sp_rom():
            self.speech_rom_help()
            return
        if getattr(self, 'p2_auto', True) and self.args.rom:    # (auto on: for this game only - the next game gets
            self.speech = k                                     #  what it needs, or the player's own choice)
            self.p2_games[os.path.basename(self.args.rom)] = k
            self.save_settings()
        else:
            self.speech = self.speech_user = k                  # (auto off, or no cartridge: every game, as a real Vectrex)
        self.restart(self.args.rom)
        dk = getattr(self, 'deck', None)                        # (Config open on Cartridge: the 3D Imager's rows come or go)
        if dk is not None and getattr(dk, 'strip', None) == 'Cartridge':
            try: dk.t.after_idle(lambda: dk.show('Cartridge'))
            except Exception: pass   # noqa
        self.show_look('port 2: %s' % PORT2[k].upper())
        self.flash('CONTROLLER PORT 2:', {'off': 'JOYSTICK', 'vecvoice': 'VECVOICE SPEECH', 'vecvox': 'VECVOX SPEECH',
                                          'imager': '3D IMAGER', 'lightpen': 'LIGHT PEN (THE MOUSE)'}[k])
    def speech_rom_help(self):
        """a speech module chosen without the speech ROM: a small window saying where to get it and where it
        goes, with buttons for the download page and the system folder (styled as the Config window)"""
        sysdir = os.path.join(ROOT, 'system')
        t = tk.Toplevel(self.root, bg=deck.BG); t.withdraw()
        t.title('Speech ROM needed'); t.transient(self.root); t.resizable(False, False)
        top = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); top.pack(fill='x')
        tk.Label(top, text='Esc: close', fg=deck.DIM, bg=deck.TOPBG, font=(MONO, 9)).pack(side='left', padx=12, pady=6)
        bx = tk.Frame(t, bg=deck.PANEL, highlightthickness=1, highlightbackground=deck.EDGE); bx.pack(fill='x', padx=10, pady=(10, 0))
        tk.Label(bx, text='THE SP0256-AL2 SPEECH ROM', fg=deck.DIM, bg=deck.PANEL, font=(MONO, 8), anchor='w').pack(fill='x', padx=10, pady=(4, 0))
        text = ("The speech modules (VecVoice, VecVox) need the SP0256-AL2 speech ROM.\n"
                "It is (c) Microchip and can not come with the emulator, but it is a free download:\n\n"
                "1.  Download al2.bin from\n      %s\n"
                "2.  Put it in the system folder next to the program:\n      %s\n"
                "      (as al2.bin or sp0256-al2.bin)\n"
                "3.  Choose the speech module again (Controls > Port 2)." % (SP_ROM_URL, sysdir))
        tk.Label(bx, text=text, fg=deck.G, bg=deck.PANEL, font=(MONO, 10), justify='left', anchor='w').pack(padx=10, pady=(2, 10), anchor='w')
        foot = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); foot.pack(fill='x', pady=(10, 0))
        def opened(target):
            try:
                if IS_WIN: os.startfile(target)
                else: subprocess.Popen(['open' if sys.platform == 'darwin' else 'xdg-open', target])
            except Exception as e:                              # noqa
                print('could not open %s: %s' % (target, e))
        def folder():
            os.makedirs(sysdir, exist_ok=True); opened(sysdir)
        deck._button(foot, 'Open the download page', lambda: opened(SP_ROM_URL), self, small=True).pack(side='left', padx=(12, 6), pady=6)
        deck._button(foot, 'Open the system folder', folder, self, small=True).pack(side='left')
        deck._button(foot, 'Close', t.destroy, self, small=True).pack(side='right', padx=12)
        t.bind('<Escape>', lambda e: t.destroy())
        t.update_idletasks()
        deck.place(t, 'speech_rom', self, lambda: deck.no_close_button(t))
    def set_mains(self, hz):
        if self.ren:
            self.ren.mains_hz = hz; self.ren.wob_on = True
            self.show_look('mains_hz')
    def set_bend_type(self, i):
        if self.ren:
            self.ren.bend_type = i; self.ren.bend_on = True
            self.show_look('bend_type')

    # ---------------------------------------------------------------- the volume knob on screen
    def knob_reserve(self):
        """the strip below the picture (share of the picture height): the knob panel and (window) the
        OSD; the picture and the strip are one block (in the window it grows by the strip; in full screen
        + makes the block at most as high as the screen)"""
        return vecrender.PAD_TOP + vecrender.PAD_BOT + (vecrender.KNOB_STRIP - 0.635 * vecrender.KNOB_R if self.knob_on or self.fps_on or self.cart_on else 0.0)
    def toggle_strip(self, e=None):
        """View > Control deck: the strip below the picture - the OSD, P2 PORT, the cartridge, RESET and the
        volume knob - on / off at once (the window shrinks by it); back on: as it was before"""
        parts = ('fps_on', 'knob_on', 'cart_on')
        if any(getattr(self, k) for k in parts):
            self.strip_was = {k: getattr(self, k) for k in parts}
            for k in parts: setattr(self, k, False)
            self.close_browser()
        else:
            was = getattr(self, 'strip_was', None) or {k: True for k in parts}
            for k in parts: setattr(self, k, was[k])
        self.strip_changed()
        self.show_look('control deck ' + ('on' if self.knob_on or self.fps_on or self.cart_on else 'off'))
        return 'break'
    def toggle_knob(self, e=None):
        """K: the volume knob and RESET button on screen, bottom right, outside the picture"""
        self.knob_on = not self.knob_on
        self.strip_changed()
        return 'break'
    def strip_changed(self):
        """the knob or the OSD switched: the window grows / shrinks by the strip below the picture, the
        picture keeps its size"""
        if self.ren and hasattr(self.ren, 'map') and not self.full:
            ph = ALG_MAX_Y * self.ren.map[0] / self.px()           # (Tk's units)
            hw = int(ph * (1.0 + self.knob_reserve()))
            fit = self.fit_h
            if hw > fit:
                ph *= fit / float(hw); hw = fit
            self.root.geometry('%dx%d' % (int(ph * vecrender.VIEW_AW), hw + self.chrome_h()))
        self.root.update_idletasks()
        self.apply_scale()
        self.save_settings()
    def pic_rect(self):
        """the picture's (left, top, right, bottom) in window pixels"""
        if not (self.ren and hasattr(self.ren, 'map')):        # (the Tk canvas: the whole of it)
            return 0, 0, self.W, self.H
        s, ox, oy = self.ren.map
        return ox * s, oy * s, (ox + ALG_MAX_X) * s, (oy + ALG_MAX_Y) * s
    def knob_geom(self):
        """(knob centre x, y, radius, RESET centre x) in window pixels: just below the picture, lined
        up with its right edge (see vecrender.KNOB_R)"""
        pl, pt, pr, pb = self.pic_rect()
        r = vecrender.KNOB_R * (pb - pt)
        ar = (pl + pr) / 2 + (vecrender.GLASS_DX + vecrender.ART_AW) * (pb - pt) / 2   # (the overlay's right edge)
        cx = ar - 2.4 * r                                       # (VOL ends at the overlay's right edge)
        cy = pb + vecrender.PAD_BOT * (pb - pt) + 1.8 * r   # (the labels' tops below the case as far as the black round it -
                                                                #  the same gap as on every side; the overlay lies on the case)
        return cx, cy, r, cx - 3.4 * r
    def hint_state(self):
        """a line or two in the middle of the picture when it needs saying: paused (how to go on)"""
        if self.browser or not (self.ren and hasattr(self.ren, 'map')):
            return None
        if self.run.paused and self.run.power:              # (switched off: dark, as a real Vectrex - the knob)
            st = self.pause_state()
            if st:                                              # (in the strip below the picture: the picture clean
                return st                                       #  for a screenshot of the paused game)
            rows = ['PAUSED', 'ESC OR P TO GO ON']
        elif time.perf_counter() < getattr(self, 'flash_until', 0):   # (a short message: what just changed)
            rows = self.flash_rows
        else:
            return self.imager_hint()
        pl, pt, pr, pb = self.pic_rect()
        return dict(rows=rows, cx=(pl + pr) / 2.0, cy=pt + 0.46 * (pb - pt), k=(pb - pt) / 330.0)
    def pause_state(self):
        """PAUSED in the strip below the picture, between the OSD and the P2 PORT pictogram (None without the strip:
        then in the picture)"""
        if not (self.fps_on or self.cart_on or self.knob_on):
            return None
        pl, pt, pr, pb = self.pic_rect()
        x, y, r = self.cart_geom()
        right = x - 3.6 * r - 1.9 * r                           # (the P2 PORT pictogram's left edge, a gap before it)
        od = self.osd_state()
        k = (1.2 if self.full else 1.0) * 0.3 * r / 6.0         # (as large as the OSD's text)
        mid = y - 0.5 * r + 8.5 * k
        if od:
            left = od['area'][0] + max(sum(len(t) for t, _ in row) for row in od['rows']) * 6.0 * od['k'] + r
            k = od['k']
            mid = od['area'][1] + len(od['rows']) * 9.0 * k / 2.0  # (level with the OSD's rows)
        else:
            left = (pl + pr) / 2 + (vecrender.GLASS_DX - vecrender.ART_AW) * (pb - pt) / 2
        rows, ks = ['PAUSED', 'ESC OR P TO GO ON'], [2.6, 1.3]   # (PAUSED large, how to go on below it)
        k = min(k, 0.95 * (right - left) / max(6.0 * len(t) * f for t, f in zip(rows, ks)))
        if k <= 0:
            return None
        hgt = 11.0 * k * ks[0] + 6.0 * k * ks[1]
        cols = [self.SPEED_COLORS[1][2], (0.40, 0.667, 0.533)]   # (PAUSED in the OSD's orange)
        return dict(rows=rows, ks=ks, cols=cols, cx=(left + right) / 2.0, cy=mid - hgt / 2.0, k=k)
    def imager_hint(self):
        """the 3D Imager's disc spinning up (the game draws nothing until it is at speed): a note in the middle of
        the picture with the speed, fading out once the game draws"""
        if not (getattr(self, 'speech', 'off') == 'imager' and getattr(self, 'imager_note', True) and self.run.power):
            return None
        now = time.perf_counter()
        v = getattr(self.core, 'imager_speed', 0.0)
        dark = now - getattr(self.core, 'lines_t', 0.0) > 0.4   # (nothing drawn for a while)
        a = getattr(self, 'im_note_a', 0.0)
        a = 1.0 if (dark and v > 0.3) else max(0.0, a - 0.05)   # (~ a third of a second to fade)
        self.im_note_a = a
        if a <= 0.0:
            return None
        pl, pt, pr, pb = self.pic_rect()
        top = 26.0                                              # (the games keep their disc at about 26)
        return dict(rows=['3D IMAGER', 'DISC SPINNING UP'], cx=(pl + pr) / 2.0, cy=pt + 0.42 * (pb - pt),
                    k=(pb - pt) / 330.0, bar=min(1.0, v / top), rows2=['%d / %d TURNS A SECOND' % (round(v), top)], a=a)
    def flash(self, *rows, secs=2.0):
        """a short message in the middle of the picture (seen in full screen and on a Steam Deck too, where the
        title bar is not)"""
        self.flash_rows = list(rows); self.flash_until = time.perf_counter() + secs
    def port2_state(self):
        """what is in controller port 2 - a joystick, the VecVox or the VecVoice - as a pictogram in the strip
        below the picture, between the OSD and the cartridge; a click on it plugs in the next (port2_click)"""
        if not (self.ren and hasattr(self.ren, 'map')):
            return None
        if not (self.fps_on or self.cart_on or self.knob_on):  # (no strip below the picture: only the message)
            return None
        x, y, r = self.cart_geom()
        return dict(cx=x - 3.6 * r, cy=y, r=r, kind=self.speech,
                    label='P2 PORT')
    def port2_click(self, e):
        """a click on the port 2 pictogram: joystick -> VecVox -> VecVoice -> 3D Imager -> light pen -> joystick
        (without the speech ROM: joystick -> 3D Imager -> light pen -> joystick)"""
        st = self.port2_state()
        if not st or math.hypot(e.x - st['cx'], e.y - st['cy']) > 1.3 * st['r']:
            return False
        nxt = {'off': 'vecvox', 'vecvox': 'vecvoice', 'vecvoice': 'imager', 'imager': 'lightpen', 'lightpen': 'off'}.get(self.speech, 'off')
        if nxt in ('vecvox', 'vecvoice') and not sp_rom():
            nxt = 'imager'                                      # (no speech ROM: the speech modules skipped)
        self.set_speech(nxt)
        return True
    def osd_state(self):
        """the window's OSD: its two rows in the strip below the picture, left of the knob panel (full
        screen: in the top left corner, see hud_text)"""
        if not (self.fps_on and self.ren and hasattr(self.ren, 'map')):
            return None
        # always the same area (knob on or off): below the picture, from its left edge to where the knob
        # panel starts
        pl, pt, pr, pb = self.pic_rect()
        ph = pb - pt; pad = 0.012 * ph
        cx, cy, r, rx = self.knob_geom()
        top = cy - 1.8 * r                                      # (level with the RESET and OFF/ON labels)
        gl_ = (pl + pr) / 2 + (vecrender.GLASS_DX - vecrender.ART_AW) * ph / 2   # (from the overlay's left edge)
        rows = self.hud_text()
        k = (1.2 if self.full else 1.0) * 0.3 * r / 6.0
        mrows = vecrender.METER_ROWS if getattr(self, 'osd_meters', True) else 0.0   # (the CPU / GPU meters below the rows)
        k = min(k, (pb + (vecrender.PAD_BOT + vecrender.KNOB_STRIP - 0.635 * vecrender.KNOB_R) * ph - top) / ((len(rows) + mrows) * 9.0))   # (more rows - a UVMC2: smaller, to fit)
        meters = None
        if getattr(self, 'osd_meters', True):                   # (CPU / GPU: the share of each frame used, now and 10 s)
            idle = self.run.paused or not self.run.power
            meters = [(n, None if idle or v is None else int(round(100 * v)), ())     # (no history line: the owner -
                      for n, v in zip(('CPU', 'GPU'), self.headroom())]                 #  too small to read, messy)
        x1 = rx - 1.25 * r
        bot = pb + (vecrender.PAD_BOT + vecrender.KNOB_STRIP - 0.635 * vecrender.KNOB_R) * ph
        if k < OSD_KMIN:                                        # (a small window: the strip's text would be unreadable -
            k = OSD_KMIN                                        #  readable size, in the black left of the picture, its
            chars = max(sum(len(t) for t, _ in row) for row in rows)   #  bottom level with the strip's)
            w, h = chars * 6.0 * k, (len(rows) + mrows) * 9.0 * k
            gl_ = max(8.0, min(gl_, pl - 12.0 - w))
            top = bot - h
            x1 = max(x1, gl_ + w)
        return dict(area=(gl_, top, x1, bot), rows=rows, k=k, meters=meters)
                    # (the text as high as the knob's labels; in full screen 20 % larger)
    # ---------------------------------------------------------------- the cartridge and the ROM browser
    def toggle_cart(self, e=None):
        """L: the cartridge pictogram below the picture (click it: the ROM browser)"""
        self.cart_on = not self.cart_on
        if not self.cart_on:
            self.close_browser()
        self.strip_changed()
        return 'break'
    def cart_geom(self):
        """(centre x, y, size) of the cartridge: level with the knob, the right half of the P2 PORT + cartridge
        block that is centred below the picture"""
        pl, pt, pr, pb = self.pic_rect()
        cx, cy, r, rx = self.knob_geom()
        return (pl + pr) / 2.0 + vecrender.GLASS_DX * (pb - pt) / 2 + 1.8 * r, cy, r
    def cart_state(self):
        if not (self.cart_on and self.ren and hasattr(self.ren, 'map')):
            return None
        x, y, r = self.cart_geom()
        out = 0.0 if self.args.rom else 1.0                     # (no cartridge in the slot: it lies out of it)
        t0 = getattr(self, 'cart_slide_t', None)
        if t0 is not None:                                      # (a quarter second's slide)
            f = min(1.0, (time.perf_counter() - t0) / 0.25)
            out = f if out else 1.0 - f
            if f >= 1.0: self.cart_slide_t = None
        return dict(cx=x, cy=y, r=r, open=bool(self.browser), out=out)
    def cart_right(self, e):
        """a right click on the cartridge: out of the slot (the Vectrex plays Mine Storm); again: the same cartridge
        back in"""
        e = self.px_event(e)
        if self.browser or not self.cart_state():
            return None
        x, y, r = self.cart_geom()
        if math.hypot(e.x - x, e.y - y) > 1.3 * r:
            return None
        return self.eject()
    ROM_EXT = ('.bin', '.vec', '.gam', '.rom', '.v4e')
    def scan_roms(self, d=None):
        """one folder of the roms folder next to the program: (name, path, kind) - in a subfolder first BACK
        ('up'), then its folders ('dir'), then its ROMs ('rom'), each by name (not the overlays folder)"""
        top = os.path.join(ROOT, 'roms')
        d = d or top
        out = []
        rdir = os.path.join(top, ':recent')
        def kind_of_(p):
            ext = os.path.splitext(p)[1].lower()
            return 'um2' if ext == '.um2' else 'pit' if ext in ('.img', '.pit') else 'rom'
        if os.path.abspath(d) == os.path.abspath(rdir):          # (Recent: the last games played, the newest first)
            out.append(('BACK', top, 'up'))
            for g in getattr(self, 'recent', []):
                if os.path.isfile(g):
                    if os.path.basename(g).lower() == 'pitrex.img':
                        out.append(('PiTrex', g, 'pitrex'))     # (the PiTrex's loader: as at the top of the roms folder)
                    else:
                        out.append((os.path.splitext(os.path.basename(g))[0], g, kind_of_(g)))
            return out
        if os.path.abspath(d) != os.path.abspath(top):
            out.append(('BACK', os.path.dirname(d), 'up'))
        try:
            names = sorted(os.listdir(d), key=str.lower)
        except OSError:
            return out
        def is_rom(p):
            ext = os.path.splitext(p)[1].lower()
            return (ext in self.ROM_EXT or (ext == '.um2' and getattr(self, 'show_um2', True))
                    or (ext in ('.img', '.pit') and PITREX_UI))
        def kind_of(p):                                         # (a UVMC2 game, a PiTrex program: tinted in the list)
            ext = os.path.splitext(p)[1].lower()
            return 'um2' if ext == '.um2' else 'pit' if ext in ('.img', '.pit') else 'rom'
        alias = {}                                              # (a Finder alias / Windows shortcut: what it points to)
        for f in names:
            p = os.path.join(d, f)
            if os.path.isfile(p) and not is_rom(p):
                t = links.target(p)
                if t:
                    alias[f] = t
        for f in names:
            p = os.path.join(d, f)
            if os.path.isdir(p) and f.lower() != 'overlays':
                out.append((f, p, 'dir'))
            elif f in alias and os.path.isdir(alias[f]):        # (an alias of a folder: shown as a subfolder)
                out.append((os.path.splitext(f)[0] if f.lower().endswith('.lnk') else f, alias[f], 'dir'))
        for f in names:
            p = os.path.join(d, f)
            if os.path.isfile(p) and is_rom(p):
                out.append((os.path.splitext(f)[0], p, kind_of(p)))
            elif f in alias and os.path.isfile(alias[f]) and is_rom(alias[f]):   # (an alias of a ROM)
                out.append((os.path.splitext(f)[0], alias[f], kind_of(alias[f])))
        # the PiTrex, at the top of the roms folder, when its SD card folder has its boot program
        boot = os.path.join(getattr(self, 'px_dir', '') or '', 'pitrex.img')
        if PITREX_UI and os.path.abspath(d) == os.path.abspath(top) and os.path.isfile(boot):
            out.insert(0, ('PiTrex', boot, 'pitrex'))
        if os.path.abspath(d) == os.path.abspath(top) and any(os.path.isfile(g) for g in getattr(self, 'recent', [])):
            out.insert(0, ('Recent', rdir, 'recent'))           # (the last games played - dropped ones too)
        return out
    def browser_dir(self, d, select=None):
        """show folder d in the ROM browser; select: the path to choose (else the first entry after BACK)"""
        b = self.browser
        b['dir'] = d; b['roms'] = self.scan_roms(d); b['top'] = 0
        hit = [i for i, (_, p, _k) in enumerate(b['roms']) if select and os.path.abspath(p) == os.path.abspath(select)]
        b['sel'] = hit[0] if hit else (1 if b['roms'] and b['roms'][0][2] == 'up' and len(b['roms']) > 1 else 0)
        self.scroll(b['sel'] - 3)
    def open_browser(self):
        if not self.ren:                                        # (no OpenGL: the browser is drawn in the picture -
            return self.open_rom()                              #  the file dialog instead, GitHub issue 3)
        top = os.path.join(ROOT, 'roms')
        cur = os.path.abspath(self.args.rom) if self.args.rom else None
        d = top                                                 # (the folder of the ROM in the slot, if it is in roms)
        stack = []
        ctx = getattr(self, 'browse_ctx', None)
        last = getattr(self, 'browse_last', None)
        sel = cur
        if last and os.path.isdir(last[0]):                     # (the folder it was last in)
            d, stack = last[0], list(last[1])
            if not (cur and os.path.abspath(os.path.dirname(cur)) == os.path.abspath(d)):
                sel = last[2]
        elif cur and ctx and os.path.abspath(os.path.dirname(cur)) == os.path.abspath(ctx[0]) and os.path.isdir(ctx[0]):
            d, stack = ctx[0], list(ctx[1])                     # (where it was loaded from: an alias's folder too)
        elif cur and os.path.abspath(os.path.dirname(cur)).lower().startswith(os.path.abspath(top).lower()):
            d = os.path.dirname(cur)
        self.browser = dict(roms=[], sel=0, top=0, dir=top, stack=stack)     # (the game runs on, without input, until a
        self.inp.block = True                                   #  ROM is loaded)
        self.browser_dir(d, sel)
        b = self.browser
        if last and len(last) > 3 and os.path.abspath(d) == os.path.abspath(last[0]) and b['roms']:
            vis = self.browser_geom()[3]                         # (the list scrolled as it was, the choice in view)
            top = max(0, min(int(last[3]), max(0, len(b['roms']) - vis)))
            if b['sel'] < top or b['sel'] >= top + vis:
                top = max(0, min(b['sel'] - 3, max(0, len(b['roms']) - vis)))
            b['top'] = top
        self.root.after(1000, lambda bb=self.browser: self.browser_watch(bb))
        self.root.after(30, lambda bb=self.browser: self.browser_pad(bb))
    def browser_pad(self, bb):
        """the ROM browser with player 1's controller: the stick up / down chooses (held: it repeats), left /
        right turns a page; button 4 loads the ROM (a folder: opens it), button 1 goes back to the roms
        folder - there it closes the browser. (The keyboard's arrows, Enter, Backspace and Esc as before.)"""
        b = self.browser
        if not b or b is not bb:
            return
        jx, jy, btn, raw = self.inp.read_player('p1', kbd_dirs=False)
        now = time.perf_counter()
        d = (1 if jy < -64 else -1 if jy > 64 else 0, 1 if jx > 64 else -1 if jx < -64 else 0)
        if d[0] and d[1]:                                       # (a diagonal: the stronger way)
            d = (d[0], 0) if abs(jy) >= abs(jx) else (0, d[1])
        if 'pad_d' not in b:                                    # (the stick as it was when it opened - held up
            b['pad_d'] = d if d != (0, 0) else None; b['pad_t'] = now + 0.8    #  for the shortcut: no step yet)
        if d != (0, 0):
            step = False
            if d != b.get('pad_d'):
                b['pad_d'] = d; b['pad_t'] = now + 0.40; step = True
            elif now >= b['pad_t']:
                b['pad_t'] = now + (0.08 if d[0] else 0.25); step = True
            if step:
                b['pad'] = True
                self.browse(d[0] if d[0] else d[1] * self.browser_geom()[3])
        else:
            b['pad_d'] = None
        new = btn & ~b.get('pad_btn', btn)                      # (pressed now: held when it opened does not count)
        b['pad_btn'] = btn
        if new & 8:                                             # button 4: load / open
            b['pad'] = True
            self.browse_load()
            if not self.browser:
                return self.hold_until_released()
        elif new & 1:                                           # button 1: the roms folder; there: close
            b['pad'] = True
            top = os.path.join(ROOT, 'roms')
            if os.path.abspath(b['dir']) != os.path.abspath(top):
                st = b.get('stack', [])
                came = st[1] if len(st) > 1 else b['dir']       # (the folder opened from the roms folder)
                b['stack'] = []
                self.browser_dir(top, came)
            else:
                self.close_browser()
                return self.hold_until_released()
        self.root.after(30, lambda: self.browser_pad(bb))
    def hold_until_released(self, t0=None):
        """a controller button closed the ROM browser: the game gets no input until the button (and the stick)
        is let go - button 4 would start the game just loaded at once (1.5 s at most)"""
        t0 = t0 or time.perf_counter()
        if self.browser:
            return
        jx, jy, btn, raw = self.inp.read_player('p1', kbd_dirs=False)
        if (not btn and abs(jx) < 64 and abs(jy) < 64) or time.perf_counter() - t0 > 1.5:
            self.inp.block = False
            return
        self.inp.block = True
        self.root.after(30, lambda: self.hold_until_released(t0))
    def browser_watch(self, bb):
        """the ROM browser open: its folder looked at every second - ROMs copied in (or taken out) appear at
        once, the chosen one stays chosen (a user had to restart the program to see games just added)"""
        b = self.browser
        if not b or b is not bb:                                # (closed, or opened anew: that one's own watch)
            return
        roms = self.scan_roms(b['dir'])
        if [p for _, p, _k in roms] != [p for _, p, _k in b['roms']]:
            sel = b['roms'][b['sel']][1] if b['roms'] and 0 <= b['sel'] < len(b['roms']) else None
            self.browser_dir(b['dir'], sel)
        self.root.after(1000, lambda: self.browser_watch(bb))
    def browser_back(self, e=None):
        """Backspace / BACK: the folder above (the folder you came from chosen)"""
        b = self.browser
        if b and b.get('stack'):                                # (the way you came: an alias's folder lies elsewhere)
            return self.browser_dir(b['stack'].pop(), b['dir'])
        if b and os.path.abspath(b['dir']) != os.path.abspath(os.path.join(ROOT, 'roms')):
            self.browser_dir(os.path.dirname(b['dir']), b['dir'])
    def close_browser(self):
        b = self.browser
        if b and b.get('dir'):                                  # (it opens there again: the folder, the way back, the row)
            sel = b['roms'][b['sel']][1] if b['roms'] and 0 <= b['sel'] < len(b['roms']) else None
            self.browse_last = (b['dir'], [x for x in b.get('stack', []) if isinstance(x, str)], sel, b.get('top', 0))
        self.browser = None
        self.inp.block = False
    def browser_geom(self):
        """the browser's box over the picture, its list rows and the plug"""
        pl, pt, pr, pb = self.pic_rect()
        pw, ph = pr - pl, pb - pt
        x0, x1 = pl + 0.1 * pw, pr - 0.1 * pw
        y0, y1 = pt + 0.12 * ph, pt + 0.82 * ph
        n = 14
        rh = (y1 - y0) / (n + 4.6)
        k = rh / 13.5                                           # (the names a little smaller than the rows)
        lst = (x0 + 0.6 * rh, y0 + 1.6 * rh, x1 - 0.6 * rh, y0 + 1.6 * rh + n * rh)
        plug = (x1 - 2.9 * rh, lst[3] + 0.55 * rh, x1 - 1.0 * rh, lst[3] + 1.7 * rh)  # (the load icon: smaller,
                                                                                # clear of the list, edge and help line)
        b = 1.2 * rh                                            # (the buttons: up, down below the list; close)
        up = (lst[0], lst[3] + 0.5 * rh, lst[0] + b, lst[3] + 0.5 * rh + b)
        dn = (lst[0] + 1.4 * b, up[1], lst[0] + 2.4 * b, up[3])
        cl = (x1 - 0.6 * rh - b, y0 + 0.3 * rh, x1 - 0.6 * rh, y0 + 0.3 * rh + b)
        return (x0, y0, x1, y1), lst, plug, n, rh, k, up, dn, cl
    def browser_state(self):
        b = self.browser
        if not (b and self.ren and hasattr(self.ren, 'map')):
            return None
        rect, lst, plug, n, rh, k, up, dn, cl = self.browser_geom()
        rel = os.path.relpath(b['dir'], os.path.join(ROOT, 'roms'))
        where = 'ROMS' if rel == '.' else 'ROMS / RECENT' if rel == ':recent' else 'ROMS / ' + rel.replace(os.sep, ' / ').upper()
        return dict(rect=rect, list=lst, plug=plug, up=up, dn=dn, close=cl, n_vis=n, row_h=rh, k=k,
                    names=[t for t, _, _k in b['roms']], kinds=[k_ for _, _, k_ in b['roms']],
                    top=b['top'], sel=b['sel'], title='%s  (%d)' % (where, sum(1 for r in b['roms'] if r[2] in ('rom', 'um2', 'pit'))),
                    empty='NO ROMS FOUND.\nPUT YOUR ROMS IN THE FOLDER\nROMS NEXT TO THE PROGRAM.',
                    help=(('STICK: CHOOSE / PAGE   BUTTON 4: LOAD   BUTTON 1: CLOSE' if rel == '.' else
                           'STICK: CHOOSE / PAGE   BUTTON 4: OPEN/LOAD   BUTTON 1: BACK') if b.get('pad') else
                          'UP/DOWN: CHOOSE   LEFT/RIGHT: PAGE   ENTER/PLUG: LOAD   ESC: CLOSE' if rel == '.' else
                          'UP/DOWN: CHOOSE   ENTER: OPEN/LOAD   BACKSPACE: BACK   ESC: CLOSE'))
    def browse(self, d):
        """move the choice by d rows (and keep it in view)"""
        b = self.browser
        if b and b['roms']:
            vis = self.browser_geom()[3]
            b['sel'] = max(0, min(len(b['roms']) - 1, b['sel'] + d))
            if b['sel'] < b['top']: b['top'] = b['sel']
            if b['sel'] >= b['top'] + vis: b['top'] = b['sel'] - vis + 1
    def scroll(self, d):
        """scroll the list by d rows"""
        b = self.browser
        if b:
            vis = self.browser_geom()[3]
            b['top'] = max(0, min(max(0, len(b['roms']) - vis), b['top'] + d))
    def browser_row(self, e):
        rect, lst, plug, n, rh, k, up, dn, cl = self.browser_geom()
        if lst[0] <= e.x <= lst[2] and lst[1] <= e.y < lst[3]:
            j = self.browser['top'] + int((e.y - lst[1]) / rh)
            return j if j < len(self.browser['roms']) else None
        return None
    def browser_click(self, e):
        rect, lst, plug, n, rh, k, up, dn, cl = self.browser_geom()
        inside = lambda bx: bx[0] <= e.x <= bx[2] and bx[1] <= e.y <= bx[3]
        if plug[0] - 6 * 6 * k <= e.x <= plug[2] and plug[1] <= e.y <= plug[3]:
            return self.browse_load()
        if inside(up): return self.scroll(-(n - 1))
        if inside(dn): return self.scroll(n - 1)
        if inside(cl): return self.close_browser()
        j = self.browser_row(e)
        if j is not None:
            self.browser['sel'] = j
        elif not (rect[0] <= e.x <= rect[2] and rect[1] <= e.y <= rect[3]):
            if not (self.cart_state() and math.hypot(e.x - self.cart_geom()[0], e.y - self.cart_geom()[1]) <= 1.3 * self.cart_geom()[2]):
                return self.close_browser()                     # (a click outside closes it)
            return self.close_browser()
    def browse_double(self, e):
        e = self.px_event(e)
        if self.browser and self.browser_row(e) is not None:
            self.browser['sel'] = self.browser_row(e)
            self.browse_load()
    def browse_load(self):
        """plug the chosen cartridge in (a power cycle with it); a folder: open it; BACK: the folder above"""
        b = self.browser
        if b and b['roms']:
            name, path, kind = b['roms'][b['sel']]
            if kind == 'up':
                return self.browser_back()
            if kind in ('dir', 'recent'):
                b.setdefault('stack', []).append(b['dir'])
                return self.browser_dir(path)
            self.browse_ctx = (b['dir'], list(b.get('stack', [])))   # (opened again there: rebuild, load, repeat)
            self.close_browser()
            self.restart(path)
    def knob_state(self):
        if not (self.knob_on and self.ren and hasattr(self.ren, 'map')):
            return None
        cx, cy, r, rx = self.knob_geom()
        vol = self.run.crt.volume if self.run.power else 0.0
        st = dict(cx=cx, cy=cy, r=r, rx=rx, vol=vol, pressed=time.perf_counter() < getattr(self, 'reset_shown_until', 0))
        drag = getattr(self, 'kdrag', None)
        if drag or getattr(self, 'knob_hover', False):        # (the pointer on it: the volume, and how to turn it)
            st['label'] = 'VOL %d%%' % round(vol * 100) if self.run.power else 'OFF'
            if not (drag and drag['moved']):
                st['hint'] = 'DRAG ROUND TO TURN'
        return st
    def px(self):
        """the picture's pixels per Tk point: 2 on a Mac's Retina screen (the picture is drawn at its full
        resolution), else 1 - the knob, the cartridge, the browser and the OSD are laid out in pixels"""
        return getattr(getattr(self.ren, 'ctxo', None), 'scale', 1.0) if self.ren else 1.0
    def px_event(self, e):
        """a mouse event on the picture in its pixels (a turned screen: in the turned picture's)"""
        k = self.px()
        rot = getattr(self, 'rot', 0)
        if k == 1.0 and not rot:
            return e
        x, y = e.x * k, e.y * k
        if rot and self.ren and hasattr(self.ren, 'real_size'):
            rw, rh = self.ren.real_size
            x, y = {90: (y, rw - x), 180: (rw - x, rh - y), 270: (rh - y, x)}[rot]
        return type('E', (), dict(x=x, y=y, delta=getattr(e, 'delta', 0), widget=e.widget))
    def clickable(self, e):
        """the pointer over something in the strip below the picture that a click works: the volume knob, RESET,
        the cartridge (the ROM browser), the port 2 pictogram (the hand pointer)"""
        if self.knob_hit(e):
            return True
        if self.cart_state():
            cx, cy, r = self.cart_geom()[:3]
            if math.hypot(e.x - cx, e.y - cy) <= 1.3 * r:
                return True
        st = self.port2_state()
        return bool(st and math.hypot(e.x - st['cx'], e.y - st['cy']) <= 1.3 * st['r']) or self.osd_hit(e)
    def osd_hit(self, e):
        """the pointer on the OSD's text (vecrender _osd: rows 9 k high, characters 6 k wide) - a click opens Config"""
        od = self.osd_state()
        if not od or not od['rows']:
            return False
        x0, y0 = od['area'][:2]; k = od['k']
        chars = max(sum(len(t) for t, _ in r) for r in od['rows'])
        return x0 - 2 * k <= e.x <= x0 + 6 * k * chars + 2 * k and y0 - 2 * k <= e.y <= y0 + 9 * k * len(od['rows']) + k
    def knob_hit(self, e):
        """'knob' / 'reset' / None for a mouse position"""
        if not self.knob_state():
            return None
        cx, cy, r, rx = self.knob_geom()
        if math.hypot(e.x - cx, e.y - cy) <= 1.15 * r: return 'knob'
        if math.hypot(e.x - rx, e.y - cy) <= 1.0 * r: return 'reset'
        return None
    def power_up(self, e=None):
        """switched off: on, the volume back where it was (a click on the picture or the knob, any key)"""
        if self.run.power or self.browser:
            return None
        self.turn_volume(getattr(self, 'vol_before_off', 0) or 0.5)
        return 'break'
    def pad_power(self):
        """player 1's controller beyond the game. Switched off: a button switches on (as any key does - a
        controller alone, a Steam Deck in Game Mode, had no way to turn the volume knob). The controller
        shortcuts (Vectrex menu, on by default): the stick and all four buttons held for a second -
        up: the ROM browser, down: the RESET button, left: port 2 VecVox / joystick, right: the overlay on /
        off (the keyboard's player keys too) - every game with a controller only. What was held does not
        reach the game afterwards (held back until let go)."""
        try:
            if not self.run.power and not self.browser:
                jx, jy, btn, raw = self.inp.read_player('p1', kbd_dirs=False)
                if btn and raw is not None:
                    self.power_up()
                    self.hold_until_released()
            elif self.run.power and not self.browser and self.pad_shortcuts and not self.demo_mode():
                jx, jy, btn, raw = self.inp.read_player('p1')
                d = None
                if btn & 15 == 15:
                    if abs(jy) >= abs(jx):
                        d = 'up' if jy > 64 else 'down' if jy < -64 else None
                    else:
                        d = 'right' if jx > 64 else 'left' if jx < -64 else None
                now = time.perf_counter()
                if d is None:
                    self.combo = None
                elif not getattr(self, 'combo', None) or self.combo[0] != d:
                    self.combo = [d, now, False]
                elif not self.combo[2] and now - self.combo[1] >= 1.0:
                    self.combo[2] = True                        # (once per hold)
                    self.pad_shortcut(d)
        finally:
            self.root.after(100, self.pad_power)
    def pad_shortcut(self, d):
        """a controller shortcut (see pad_power)"""
        if d == 'up':
            return self.open_browser()
        if d == 'down':
            self.reset_shown_until = time.perf_counter() + 0.18
            self.reset_button()
            self.show_look('RESET')
        elif d == 'left':
            self.set_speech('off' if self.speech == 'vecvox' else 'vecvox')
        elif d == 'right':
            self.toggle_art()
        self.hold_until_released()
    def demo_mode(self):
        return bool(getattr(self.args, 'demo', None))
    def any_key(self, e):
        """a key while switched off: on (not the emulator's own keys: Esc, F-keys, modifiers, with Ctrl / Alt)"""
        if self.run.power or self.browser or (e.state & 0x2000c):
            return None
        if e.keysym in ('Escape', 'Shift_L', 'Shift_R', 'Control_L', 'Control_R', 'Alt_L', 'Alt_R', 'Win_L', 'Win_R',
                        'Caps_Lock', 'Num_Lock', 'Prior', 'Next') or e.keysym.startswith('F') and e.keysym[1:].isdigit():
            return None
        return self.power_up()
    def knob_click(self, e):
        """a press on the knob grabs it (knob_motion: turned round as a real knob; let go without turning: a
        click, knob_release); switched off, a click on the picture switches on; on RESET: the RESET button"""
        e = self.px_event(e)
        if self.browser:
            return self.browser_click(e)
        if self.cart_state() and math.hypot(e.x - self.cart_geom()[0], e.y - self.cart_geom()[1]) <= 1.3 * self.cart_geom()[2]:
            return self.open_browser()
        if self.port2_click(e):
            return 'break'
        if self.osd_hit(e):                                     # (the OSD: the Config window)
            return self.open_deck()
        hit = self.knob_hit(e)
        if not self.mouse_stick and not hit and self.usb_live():  # (a UVMC2: the USB mouse's left button)
            pl, pt, pr, pb = self.pic_rect()
            if pl <= e.x <= pr and pt <= e.y <= pb:
                return self.mouse_button(1, True)
        if self.pen_live() and not hit:                          # (the light pen: a press on the picture: button 1, draw)
            pl, pt, pr, pb = self.pic_rect()
            if pl <= e.x <= pr and pt <= e.y <= pb:
                self.pen_update(e)
                self.mouse_button(1, True)
                return 'break'
        if self.mouse_stick and self.run.power and not hit:      # (steering with the mouse: a click on the picture fires)
            pl, pt, pr, pb = self.pic_rect()
            if pl <= e.x <= pr and pt <= e.y <= pb:
                return self.mouse_button(1, True)
        if hit == 'knob':
            cx = self.knob_geom()[0]
            self.kdrag = dict(a=self.knob_angle(e), x=e.x, y=e.y, moved=False, up=e.x >= cx, acc=0.0, vol0=self.run.crt.volume)
            return 'break'
        if not self.run.power and hit != 'reset':
            pl, pt, pr, pb = self.pic_rect()
            if pl <= e.x <= pr and pt <= e.y <= pb:
                return self.power_up()
        if hit == 'reset':
            self.reset_shown_until = time.perf_counter() + 0.18
            self.reset_button()
    def knob_angle(self, e):
        """the pointer's direction from the knob's centre: degrees clockwise from 12 o'clock (None: too close
        to the centre to say)"""
        cx, cy, r, rx = self.knob_geom()
        if math.hypot(e.x - cx, e.y - cy) < 0.3 * r:
            return None
        return math.degrees(math.atan2(e.x - cx, -(e.y - cy)))
    def knob_motion(self, e):
        """the pointer over the knob (its label shows the volume, the hand pointer) and the grabbed knob turned:
        round its centre, clockwise louder, one to one with its marker (KNOB_SWEEP) - the owner's tester
        (2026-10-26): a click on a half was not what a hand on a knob expects. Down to 0 % it switches off, up
        from there on, as on the Vectrex"""
        if e is None:
            self.knob_hover = self.hand_hover = False
            return
        e = self.px_event(e)
        self.knob_hover = self.knob_hit(e) == 'knob'            # (the knob's label: the volume)
        self.hand_hover = self.clickable(e)                     # (the hand pointer: anything a click works)
        d = getattr(self, 'kdrag', None)
        if not d:
            return
        r = self.knob_geom()[2]
        if not d['moved'] and math.hypot(e.x - d['x'], e.y - d['y']) >= max(3.0, 0.12 * r):
            d['moved'] = True                              # (a turn, not a click: see knob_release)
        a = self.knob_angle(e)                             # (it turns from the first movement, however slight)
        if a is None or d['a'] is None:
            d['a'] = a
            return
        da = (a - d['a'] + 180.0) % 360.0 - 180.0
        d['a'] = a
        d['acc'] += da / (2.0 * vecrender.KNOB_SWEEP)
        if abs(d['acc']) >= 0.005:                         # (the volume in 0.5 % steps, the rest carried over)
            step, d['acc'] = d['acc'], 0.0
            if not self.run.power:
                if step < 0:                                   # (off: down further does nothing)
                    return
                self.run.crt.volume = 0.0                      # (on from 0, as the knob turns)
            self.turn_volume(step)
    def knob_release(self, e=None):
        """the knob let go: not turned - a click: switched off, it switches on; else the right half +5 %, the
        left half -5 %"""
        d = getattr(self, 'kdrag', None)
        self.kdrag = None
        if not d or d['moved']:
            return None
        if self.run.crt.volume != d['vol0']:               # (a click: the hand's slight turn undone first)
            self.turn_volume(d['vol0'] - self.run.crt.volume)
        if not self.run.power:
            return self.power_up()
        return self.turn_volume(+0.05 if d['up'] else -0.05)
    def wheel_steps(self, e):
        """whole wheel notches in this event: a mouse wheel sends 120 a notch, a precision touchpad's two-finger
        scroll many small amounts - added up, so a short swipe is not a dozen notches (macOS: lines already)"""
        if IS_MAC:
            return int(e.delta) or (1 if e.delta > 0 else -1)
        self.wheel_acc = getattr(self, 'wheel_acc', 0) + e.delta
        if time.perf_counter() - getattr(self, 'wheel_t', 0.0) > 0.5:
            self.wheel_acc = e.delta                            # (a new gesture: what was left is forgotten)
        self.wheel_t = time.perf_counter()
        n = int(self.wheel_acc / 120)
        self.wheel_acc -= n * 120
        return n
    def knob_wheel(self, e):
        """the mouse wheel over the knob turns it (the ROM browser open: it scrolls)"""
        e = self.px_event(e)
        n = self.wheel_steps(e)
        if not n:
            return
        if self.browser:
            return self.scroll(-3 * n)
        if self.knob_hit(e) == 'knob':
            self.turn_volume(0.05 * max(-3, min(3, n)))
        elif self.usb_live():                                   # (a UVMC2: the USB mouse's wheel)
            self.core.usb_mouse_move(self.usb_btn, 0, 0, max(-127, min(127, n)))

    def setup_display(self):
        self.hz = 60.0
        self.pause_acc = 0.0
        self.presents = collections.deque(maxlen=2000)
        self.vsync = 0
        self.apply_rate()
    def apply_rate(self):
        """the screen rate (Display > Screen): fixed 60 Hz - on a 60 / 120 / 240 Hz monitor by vsync
        (every 1st / 2nd / 4th refresh), else paced by the clock; or the monitor's full rate - vsync on
        every refresh (120 / 144 / 165 / 240 Hz...: the afterglow fades in finer steps); or G-Sync /
        FreeSync - a screen frame per game frame (see tick_timed)"""
        self.next_present = time.perf_counter()
        r = self.ren
        if not (r and hasattr(r, 'present')):
            return
        try:
            refresh = r.refresh_rate()
        except Exception:   # noqa
            refresh = 0
        self.refresh = refresh
        self.hz = 60.0; self.vsync = 0
        vs_ok = os.environ.get('VECTREX_VSYNC', '1') != '0'
        if getattr(self, 'rate', '60') == 'max' and refresh >= 50:
            self.hz = float(refresh)
            if vs_ok: self.vsync = 1
            # (a UVMC2 game on a very fast screen: every refresh would cost the window thread time the emulation needs -
            # divided down to ~120 Hz, still smooth: 240 / 360 / 480 Hz -> 120)
            if vs_ok and refresh > 170 and (getattr(self.args, 'rom', '') or '').lower().endswith('.um2'):
                self.vsync = max(1, int(round(refresh / 120.0)))
                self.hz = refresh / self.vsync
        else:
            # matched to the monitor: 60 Hz where the monitor runs a multiple of it (60 / 120 / 240), else
            # its own rate divided down to the step nearest the game's 50 Hz - 144 Hz: 72, 165: 55, 75: 75,
            # 50: 50 - always in step with the refresh (vsync). Paced by the clock instead, a 144 or 165 Hz
            # laptop screen showed the frames on an uneven 2-3-2-3 refresh rhythm, torn: a hard flicker
            n = round(refresh / self.hz) if refresh else 0
            if vs_ok and n >= 1 and abs(refresh - n * self.hz) <= 1:
                self.vsync = n
            elif vs_ok and refresh >= 50:
                self.vsync = max(1, int(refresh // 50))
                self.hz = refresh / self.vsync
        if getattr(r, 'half_rate', False) and self.hz > 40 and not self.vrr:   # (Quality Low: every 2nd picture;
                                                                #  G-Sync / FreeSync: every 2nd game frame, tick_timed)
            if self.vsync: self.vsync *= 2
            self.hz /= 2.0
        self._hr_applied = bool(getattr(r, 'half_rate', False))
        if self.vsync > getattr(getattr(r, 'ctxo', None), 'max_swap_interval', 99):
            self.vsync = 0                                      # (macOS: only every refresh; else paced by the clock)
        if os.environ.get('VECTREX_HZ'):                       # (test: a rate by the clock)
            self.hz = float(os.environ['VECTREX_HZ']); self.vsync = 0
        r.swap_interval(0 if self.vrr else self.vsync)
    def set_unit(self, key, v, name='custom'):
        """one part of the Vectrex unit, live (gap: the line gaps, the renderer's line_gap)"""
        if key == 'dacofs' and getattr(self, 'net_game', None):  # (online: R302 moves the joystick comparator - the game
            self.flash("ONLINE: R302 IS THE HOST'S", 'CHANGE IT WHEN OFFLINE', secs=4)   #  itself - so it stays the host's)
            return
        self.unit[key] = v
        self.unit_name = name
        if key == 'gap':
            if self.ren and hasattr(self.ren, 'line_gap'): self.ren.line_gap = v
            self.core.set_gap(v); return
        if key == 'knob' and self.ren:
            self.ren.knob_bright = knob_bias(v)                 # (the renderer turns the lines; the core sends the web)
        if key in UNIT_PICTURE_KEYS:
            self.apply_picture(); return
        for k, i, label, lo, hi, fmt, var in UNIT_PARAMS:
            if k == key and not (i == 1 and self.core.rom.lower().endswith(ADDON_ROM)):   # (the add-ons: the standard
                self.core.set_analog(i, knob_bias(v) if k == 'knob' else v)              #  zero reference, see Core)
    def apply_picture(self):
        """the picture's pots and the yoke (unit_picture): the lines' transform to the core's frames, the high voltage's
        light and spot to the renderer"""
        xf, light, width = unit_picture(self.unit)
        core = getattr(self, 'core', None)
        if core is not None:
            core.picture = None if getattr(self.args, 'ideal', False) else xf
        if self.ren:
            self.ren.hv_light, self.ren.hv_width = light, width
    def reset_unit(self, part=None):
        """Config's reset: the Unit tab (part 'unit': its rows, switched off at the start, the intro shown), the Circuit
        tab ('circuit': its rows) or all - the owner's measured Vectrex, the pots as the factory set them"""
        own = UNIT_PRESETS['owner']
        if part is None:
            self.unit_preset('owner')
        else:
            keys = [k for k in own if (k in UNIT_TAB_KEYS) == (part == 'unit')]
            for k in keys:
                self.set_unit(k, own[k], self.unit_name)
            same = all(abs(float(self.unit.get(k, own[k])) - float(own[k])) < 1e-9 for k in own)
            self.unit_name = 'owner' if same else 'custom'
        if part in (None, 'unit'):
            self.start_on = False; self.skip_boot = False
        self.save_settings()
    def unit_preset(self, name):
        """the Vectrex unit: a preset ('owner', 'ideal'), or 'random' / 'cranky' - a new believable unit each time,
        slightly off or further off (UNIT_SPREAD)"""
        u = random_unit(name == 'cranky') if name in ('random', 'cranky') else dict(UNIT_PRESETS[name])
        for k, v in u.items():
            self.set_unit(k, v, name)
        self.save_settings()
        self.show_look('Vectrex unit: ' + UNIT_NAMES[name])
    def export_unit(self):
        from tkinter import filedialog
        fn = filedialog.asksaveasfilename(parent=self.root, title='Export this Vectrex unit', defaultextension='.vxunit',
                                          initialdir=ROOT, filetypes=[('Vectrex unit', '*.vxunit'), ('All files', '*.*')])
        if fn:
            try:
                json.dump(dict(vectrex_unit=1, name=UNIT_NAMES.get(self.unit_name, 'Custom'), **self.unit), open(fn, 'w'), indent=1)
                self.show_look('Vectrex unit exported: ' + os.path.basename(fn))
            except OSError as e:
                self.show_look('could not export: %s' % e)
    def import_unit(self):
        from tkinter import filedialog
        fn = filedialog.askopenfilename(parent=self.root, title='Import a Vectrex unit', initialdir=ROOT,
                                        filetypes=[('Vectrex unit', '*.vxunit'), ('All files', '*.*')])
        if not fn:
            return
        try:
            d = json.load(open(fn))
            u = {k: float(d[k]) for k in UNIT_PRESETS['owner'] if k in d}
        except (OSError, ValueError, TypeError) as e:
            self.show_look('not a Vectrex unit file: %s' % e); return
        for k, v in u.items():
            self.set_unit(k, v)
        self.save_settings()
        self.show_look('Vectrex unit imported: ' + os.path.basename(fn))
    QUALITY_NAMES = dict(low='Low', medium='Medium', high='High', ultra='Ultra', custom='Custom')
    def base_values(self):
        """the picture switches the settings are compared with (Config's changed marks): the Quality profile shown, or
        under Custom the one chosen last - or Optimize's result, when that came last"""
        cur = self.current_profile()
        base = cur if cur != 'custom' else getattr(self, 'quality_base', 'ultra')
        if base == 'optimized':
            vals = dict(self.profile_values('ultra'))
            vals.update({k: v for k, v in getattr(self, 'quality_opt', {}).items() if k in vals})
            return vals
        return self.profile_values(base)
    def base_name(self):
        cur = self.current_profile()
        base = cur if cur != 'custom' else getattr(self, 'quality_base', 'ultra')
        return 'Optimal' if base == 'optimized' else self.QUALITY_NAMES.get(base, base)
    def profile_values(self, key):
        """a Quality profile's switches (vecrender PROFILE_OFF): Ultra - the defaults, High, Medium and Low - some off"""
        r = self.ren
        if not r or not hasattr(r, 'PROFILE_KEYS'):
            return {}
        vals = {k: self.look_default.get(k, getattr(r, k)) for k in r.PROFILE_KEYS if hasattr(r, k)}
        vals.update({k: v for k, v in r.PROFILE_OFF.get(key, {}).items() if k in vals})
        return vals
    def current_profile(self):
        """'low' / 'medium' / 'high' when the picture settings are exactly that profile's - its switches, and every
        other picture setting (Tube, Room light, Case, Overlay) at its default - else 'custom': as in games, changing
        anything under a profile makes it Custom (the owner, 2026-10-26)"""
        r = self.ren
        if not r or not hasattr(r, 'PROFILE_KEYS'):
            return 'ultra'
        def same(v, d):
            return abs(float(v) - float(d)) <= 1e-6 if isinstance(d, float) else v == d
        rest = [k for k in self.look_keys if k not in r.PROFILE_KEYS and k != 'line_gap']   # (line_gap: the Vectrex unit's)
        if not all(same(getattr(r, k), self.look_default[k]) for k in rest):
            return 'custom'
        for key in ('ultra', 'high', 'medium', 'low'):
            if all(same(getattr(r, k), v) for k, v in self.profile_values(key).items()):
                return key
        return 'custom'
    def set_profile(self, key):
        """Quality: a profile (Low / Medium / High) - as a game's presets: every picture setting (Tube, Room light, Case,
        Overlay) to its default, then the profile's switches; changed by hand afterwards they make it Custom. (Not the
        Vectrex unit's line gaps, nor which overlay a game uses or where it sits)"""
        r = self.ren
        if not r:
            return
        art = False
        for k in self.look_keys:
            if k != 'line_gap' and getattr(r, k) != self.look_default[k]:
                setattr(r, k, self.look_default[k]); art = art or k.startswith(('art_', 'scr_'))
        for k, v in self.profile_values(key).items():
            setattr(r, k, v)
        self.quality_base = key
        if art:
            self.load_art()
        self.apply_rate()                                       # (Low: 30 pictures a second - half_rate)
        self.save_settings()
        if getattr(self, 'deck', None):
            try: self.deck.sync_all()
            except Exception: pass   # noqa
        self.show_look('quality: ' + self.QUALITY_NAMES[key])
        self.flash('QUALITY: ' + self.QUALITY_NAMES[key].upper())   # (in the middle of the picture: seen at once)
    def reset_all(self):
        """the Config window's Reset all: every strip to its defaults, under the Quality profile chosen - Low, Medium
        or High: that profile's; Custom: High"""
        base, name = self.base_values(), self.base_name()   # (before anything changes: the reference)
        if self.ren:
            for k, v in base.items():
                setattr(self.ren, k, v)
        self.reset_tube(); self.reset_light(); self.reset_case(); self.reset_overlay()
        self.reset_sound(); self.reset_unit(); self.reset_display(); self.reset_addons()
        self.save_settings()
        self.show_look('all settings: the defaults (quality %s)' % name)
        self.flash('DEFAULTS: ' + name.upper())
    def set_quality(self, mode):
        """(older callers: 'auto' / '0'..'2')"""
        self.set_profile({'0': 'medium', '1': 'high'}.get(mode, 'ultra'))
    def test_pc(self, parent=None):
        """Quality > Optimize...: the tests, what they turned off (pctest.py)"""
        return pctest.window(self, parent)
    def set_screen(self, mode):
        """Display > Screen: '60' (fixed 60 Hz), 'max' (the monitor's full rate), 'vrr' (G-Sync / FreeSync)"""
        self.vrr = mode == 'vrr'
        self.ultra_rate = None                                  # (chosen by hand: Ultra leaves it alone)
        if mode != 'vrr':
            self.rate = mode
        self.apply_rate()
        self.save_settings()
        self.show_look('screen: ' + ("G-Sync / FreeSync (the game's frame rate)" if self.vrr else
                                     '%d Hz' % round(self.hz) + (' (vsync)' if self.vsync else '')))
    def turn_volume(self, d):
        """Page Up / Page Down: the volume knob (and its dirty potentiometer). As on the Vectrex it is
        also the power switch: turned all the way down (0 %) it clicks off, turned up from there on.
        At either end stop it does not turn (no scratch)."""
        c = self.run.crt
        old = c.volume
        new = round(max(0.0, min(1.0, old + d)), 3)                 # (5 % steps)
        if new == old:
            return 'break'
        c.volume = new
        if new == 0 and old > 0:
            self.vol_before_off = old                           # (switched on again: back to this)
        if new > 0:
            c.new_turn()
        if (new > 0) != self.run.power:
            self.set_power(new > 0)
        self.show_look('volume')
        return 'break'
    def turn_voice(self, d):
        """Ctrl+Page Up / Ctrl+Page Down: the speech module's own volume (5 % steps, 0 - 200 %)"""
        c = self.run.crt
        c.voice_vol = round(max(0.0, min(2.0, c.voice_vol + d)), 3)
        self.show_look('voice_vol')
        return 'break'
    def reset_button(self, e=None):
        """Ctrl+R: the Vectrex RESET button (RAM kept: the BIOS warm-starts the cartridge)"""
        if self.run.power:
            self.run.flags |= 4
        return 'break'
    # ---------------------------------------------------------------- the tube switching on and off
    # On: the cathode heater warms up, so a cold tube fades in from black: 5 s after 30 s or more off
    # (or the first start), shorter the shorter it was off (15 s off: 2.5 s), at least 0.3 s. A still
    # warm tube comes on almost at once, but its high voltage builds up abruptly: the picture bulges
    # out and settles. Off: the deflection collapses in a flash: mostly a zap - the vector being drawn
    # cut off, one smooth bright freehand stroke (mostly 5-8 cm) anywhere on the screen; now and then
    # the picture shrinking into the centre in 0.05 s and stopping at 30 %. Everything the beam drew -
    # the last picture, the zap, every shrinking copy - then fades in the phosphor as measured
    # (vecrender.PHOS_A), like any picture.
    CRT_COLLAPSE = 0.05                 # (the shrinking picture: to 30 % in 0.05 s, then only its afterglow)
    def crt_on(self, off_for):
        self.crt_on_t = time.perf_counter()
        self.crt_fade = max(0.3, min(5.0, off_for / 30.0 * 5.0))
        self.crt_bulge = 0.07 * max(0.0, 1.0 - off_for / 30.0)
    def crt_off(self):
        self.crt_off_t = time.perf_counter(); self.zap_lit = False
        self.flush = True                                       # (the beam stops: all into the afterglow)
        # (only what was on screen: lines the core clamped to the border would streak in from the
        #  corners as the picture collapses)
        self.off_lines = [ln for ln in self.lines
                          if -6000 < ln[0] < ALG_MAX_X + 6000 and -6000 < ln[2] < ALG_MAX_X + 6000
                          and -6000 < ln[1] < ALG_MAX_Y + 6000 and -6000 < ln[3] < ALG_MAX_Y + 6000]   # (the core's margin, vecx.c ALG_CLIP)
        # the real Vectrex mostly flashes one cut-off stroke (the zap); now and then the picture
        # shrinks into the centre instead
        import random
        rnd = random.Random()
        self.crt_warp_ph0 = rnd.uniform(0, 6.3)                 # (the snapshot's fixed warp)
        self.crt_mode = os.environ.get('VECTREX_OFF_MODE') or ('streak' if rnd.random() < 0.7 else 'warp')
        # the zap: the last vector the beam was drawing, cut off - one smooth stroke anywhere within
        # the last picture
        if self.off_lines:
            xs = [v for ln in self.off_lines for v in (ln[0], ln[2])]; ys = [v for ln in self.off_lines for v in (ln[1], ln[3])]
            box = (min(xs), min(ys), max(xs), max(ys))
        else:
            box = (ALG_MAX_X * 0.2, ALG_MAX_Y * 0.2, ALG_MAX_X * 0.8, ALG_MAX_Y * 0.8)
        # 6 in 10 are 5-8 cm long on the tube, 3 in 10 are 2-5 cm, 1 in 10 is a squiggle (a small open
        # loop with a longer curved tail); always one freehand line, hardly ever straight, anywhere on
        # the screen. Its bend changes smoothly along the way.
        cm = ALG_MAX_Y / 24.0                                   # (the tube is about 24 cm tall)
        def bend():
            return rnd.choice((-1, 1)) * rnd.uniform(0.8, 3.2)
        roll = rnd.random()
        if roll < 0.6:
            length = rnd.uniform(5.0, 8.0) * cm
        elif roll < 0.9:
            length = rnd.uniform(2.0, 5.0) * cm
        else:
            length = rnd.uniform(4.0, 7.0) * cm
        squiggle = roll >= 0.9
        k0, k1 = bend(), bend()
        kw = rnd.choice((0.0, rnd.uniform(1.5, 4.0))); fw = rnd.uniform(0.6, 1.6); pw = rnd.uniform(0, 6.3)
        loop = rnd.choice((-1, 1)) * rnd.uniform(1.4, 1.9) * 2 * math.pi   # (squiggle: the loop's turn)
        x, y = rnd.uniform(0.08, 0.92) * ALG_MAX_X, rnd.uniform(0.08, 0.92) * ALG_MAX_Y
        a = rnd.uniform(0, 2 * math.pi)
        n = 80; step = length / n
        pts = [(x, y)]
        for i in range(n):
            u = i / float(n)
            if squiggle and u < 0.35:                         # a small loop, tightening, then the tail
                da = loop * (u / 0.35) * 2.0 / (0.35 * n)
            else:
                da = (k0 * (1.0 - u) ** 2 + k1 * u * u + kw * math.sin(2 * math.pi * fw * u + pw)) / n
            a += da
            x += step * math.cos(a); y += step * math.sin(a)
            pts.append((x, y))
        # within the visible picture (not past its edge, off the sheet or on the overlay's border):
        # shifted back inside, or (too big) made smaller - still one smooth stroke
        bx0, bx1, by0, by1 = 0.06 * ALG_MAX_X, 0.94 * ALG_MAX_X, 0.05 * ALG_MAX_Y, 0.95 * ALG_MAX_Y
        xs = [p_[0] for p_ in pts]; ys = [p_[1] for p_ in pts]
        k = min(1.0, (bx1 - bx0) / max(1.0, max(xs) - min(xs)), (by1 - by0) / max(1.0, max(ys) - min(ys)))
        cx0, cy0 = (min(xs) + max(xs)) / 2.0, (min(ys) + max(ys)) / 2.0
        pts = [(cx0 + (px_ - cx0) * k, cy0 + (py_ - cy0) * k) for px_, py_ in pts]
        xs = [p_[0] for p_ in pts]; ys = [p_[1] for p_ in pts]
        dx = max(0.0, bx0 - min(xs)) - max(0.0, max(xs) - bx1); dy = max(0.0, by0 - min(ys)) - max(0.0, max(ys) - by1)
        pts = [(px_ + dx, py_ + dy) for px_, py_ in pts]
        self.streaks = [(int(x0), int(y0), int(x1), int(y1), 90, 40, 0) for (x0, y0), (x1, y1) in zip(pts, pts[1:])]

    def crt_fx(self):
        """set the renderer's tube state for this draw; returns the lines to show while switched off"""
        r = self.ren
        if not r:
            return None
        now = time.perf_counter()
        if self.run.power:
            t = now - getattr(self, 'crt_on_t', now)
            f = min(1.0, t / getattr(self, 'crt_fade', 0.3))
            r.crt_gain = (f * f * (3.0 - 2.0 * f)) ** 2.6       # slow start, then up to full brightness
            s = 1.0
            if t < 0.5 and getattr(self, 'crt_bulge', 0):
                s = 1.0 + self.crt_bulge * math.sin(math.pi * t / 0.5) * (1.0 - t / 0.5) * 2.0
            r.crt_sx = r.crt_sy = s; r.crt_skew = r.crt_shift = r.crt_garble = r.crt_warp = 0.0
            r.overlay = None; r.crt_width = 1.0
            r.decay_override = None
            return None
        t = (now - getattr(self, 'crt_off_t', now - 99)) / float(os.environ.get('VECTREX_OFF_SLOW', 1))   # (test: slow motion)
        r.decay_override = None; r.overlay = None; r.imm_max = False
        r.crt_sx = r.crt_sy = 1.0; r.crt_skew = r.crt_shift = r.crt_garble = r.crt_warp = 0.0
        r.crt_gain = 1.0; r.crt_width = 1.0
        if getattr(self, 'crt_mode', 'warp') == 'streak':
            if not getattr(self, 'zap_lit', True):            # the zap: drawn once, bright, as one even
                self.zap_lit = True                           # line (overlapping pieces do not add up);
                r.crt_gain = 1.2; r.imm_max = True            # then it fades in the phosphor
                return self.streaks
            return []
        c = self.CRT_COLLAPSE
        if t < c:                                             # the picture shrinking very fast into the
            p = t / c                                         # centre, stopping at 30 %: every copy is
            s = 1.0 - 0.7 * p                                 # drawn into the phosphor and fades there
            r.crt_sx = s; r.crt_sy = s                        # (not warped: the picture, smaller - but the
            r.crt_width = 1.0 / s ** 0.15                     #  spot does not shrink, it even blooms, so
            r.crt_warp_ph = self.crt_warp_ph0                 #  the small copy loses its detail; about
            r.crt_gain = 0.15 + 0.85 * p * p                  #  1.2x at 30 %; the last copy is the brightest)
            return getattr(self, 'off_lines', [])
        return []

    def set_power(self, on):
        """the power switch (on the volume knob): off - dark, silent; on - a cold start"""
        if on == self.run.power:
            return
        self.run.power = on
        c = self.run.crt
        c.overlay = list(c.snd_on if self.run.power else c.snd_off)
        if self.run.power:
            self.run.flags |= self.power_flags()
            self.crt_on(time.perf_counter() - getattr(self, 'crt_off_t', -1e9))
        else:
            self.crt_off()
        self.title()
        return 'break'

    # ---------------------------------------------------------------- online play (netplay.py, a prototype)
    def net_start(self):
        """--net-host / --net-join / --net-online: as the Online window's Host and Join"""
        a = self.args
        online = getattr(a, 'net_online', False)                # (through the meeting point: a code instead of an address)
        if a.net_host or online:
            self.net_begin('host', direct_port=None if online else (a.net_host or None))
        else:
            self.net_begin('join', target=a.net_join)
    def net_server(self):
        """the meeting point (host, port): Online settings, else netplay.SERVER"""
        import netplay
        s = (getattr(self, 'net_server_s', '') or '').strip()
        if s:
            h, _, p = s.rpartition(':') if ':' in s else (s, '', '')
            try:
                return (h or s, int(p) if p else netplay.SERVER[1])
            except ValueError:
                pass
        return netplay.SERVER
    def net_busy(self):
        """online now: connected, or waiting for the other player"""
        return bool(getattr(self, 'net_game', None) or getattr(self, 'net_pending', None))
    def net_flash(self, *rows, secs=2.0):
        """an online message: in the picture, and the Online window's state line"""
        rows = [r.replace('ONLINE: ', '').replace('ONLINE ', '') for r in rows]
        self.net_msg = (' - '.join(r.lower() for r in rows if r) + '.').capitalize()
        self.flash(*rows, secs=secs)
    def net_leave(self):
        """Online > Leave: the game goes on here alone (or the waiting stops)"""
        if getattr(self, 'net_pending', None) is not None:
            self.net_cancel = True
            self.net_msg = 'Stopped waiting.'
        if getattr(self, 'net_game', None):
            self.net_end('ONLINE: YOU LEFT')
    def net_begin(self, role, target=None, direct_port=None, public=None):
        """host (player 1: this game, through the meeting point - or direct_port: straight on that UDP port) or join
        (target: a code, or an address[:port]) - waits in a thread; net_poll takes the result. public: a gamer name -
        the game is listed in the meeting point's game room (Find players) until someone joins"""
        import netplay
        a = self.args
        if self.net_busy():
            self.net_flash('ONLINE: LEAVE THE GAME FIRST', secs=4); return
        addr, port, server, code = None, netplay.PORT, None, None
        if role == 'host':
            port = direct_port or netplay.PORT
            if not (a.rom and os.path.isfile(a.rom)):
                self.net_flash('ONLINE: HOST A GAME', 'START WITH A VECTREX ROM', secs=8); return
            if not direct_port:
                server = self.net_server()
        elif netplay.is_code(target or ''):
            server, code = self.net_server(), target.strip().upper()
        else:
            addr, _, p = (target or '').strip().partition(':')
            if not addr:
                self.net_flash('ONLINE: TYPE A CODE', 'OR AN ADDRESS', secs=4); return
            port = int(p) if p.isdigit() else netplay.PORT
        self.net_log = []; self.net_msg = ''; self.net_cancel = False
        try:
            net = netplay.NetPlay(role, addr, port, log=self.net_log.append, server=server, code=code)
        except OSError as e:
            self.net_flash('ONLINE: THE NETWORK REFUSED', str(e)[:34].upper(), secs=8); return
        self.net_pending, self.net_role, self.net_direct = net, role, bool(direct_port)
        label = (target or '').strip().upper()
        game = None
        if role == 'host':
            game = netplay.new_game(a.rom, dict(skip_boot=self.skip_boot, cart_hw=self.cart_hw,   # (R302 moves the joystick
                                                dacofs=float(self.unit.get('dacofs', 0.0))))     #  comparator: the host's on both)
            if self.card_dir(a.rom):
                game['card'] = True                                 # (a UVMC2 / PiTrex: the SD card is agreed after)
            net.game_info = dict(name=game['name'], md5=game['md5'], size=game['size'])
            if public:
                net.game_info.update(pub=1, gamer=public)           # (the game room lists it under this name)
            self.net_public = public if server else None
            if server:
                self.flash('ONLINE: ASKING THE MEETING POINT', 'FOR A CODE', secs=600)
                def show_code(net=net):                           # (the code, as soon as the meeting point gave it)
                    if getattr(self, 'net_result', 1) is not None or net.lost:
                        return
                    if net.code and public:
                        self.flash('IN THE GAME ROOM AS ' + public, 'WAITING FOR A PLAYER 2', secs=600)
                    elif net.code:
                        self.flash('ONLINE CODE: ' + ' '.join(net.code), 'TELL PLAYER 2 THIS CODE', secs=600)
                    else:
                        self.root.after(200, show_code)
                self.root.after(200, show_code)
            else:
                self.flash('ONLINE: WAITING FOR PLAYER 2', 'UDP PORT %d' % port, secs=600)
        else:
            self.flash('ONLINE: JOINING', label, secs=600)
        self.net_result = None
        def work():
            stop = lambda: getattr(self, 'quitting', False) or getattr(self, 'net_cancel', False)
            g = net.connect(game, timeout=600.0, stop=stop)
            rom = None
            if g == 'missing':
                g, rom = None, 'missing'
            elif g == 'version':                                # (the other side has another emulator version)
                g, rom = None, 'version'
            elif isinstance(g, str):                            # (no meeting point, or its answer: no such code...)
                g, rom = None, 'server:' + g
            elif g and role == 'join':
                here = a.rom if (a.rom and netplay.rom_md5(a.rom) == g.get('md5')) else None
                dirs = [os.path.join(ROOT, 'roms')] + ([os.path.dirname(os.path.abspath(a.rom))] if a.rom else [])
                if g.get('card'):                                   # (a PiTrex program lives on its card)
                    dirs += [d for d in (self.px_dir, self.sd_dir) if d and os.path.isdir(d)]
                rom = here or netplay.find_rom(g.get('md5'), g.get('size', -1), dirs)
                net.accept(rom is not None, g.get('name', ''))
                if rom and g.get('card'):                           # (the host's card list: which files are the same here)
                    folder = self.card_dir(rom); mine = {}
                    def answer(m):
                        mine['ok'] = netplay.card_matches(m, folder, CARD_CACHE); return mine['ok']
                    m = net.recv_card(answer, stop=stop)
                    g['card_only'] = (folder, None if m is None else {e[0].lower() for e, k in zip(m, mine['ok']) if k},
                                      0 if m is None else len(m))
            elif g:
                rom = a.rom
                if g.get('card'):                                   # (ours to player 2: the files both have)
                    folder = self.card_dir(rom)
                    m = netplay.card_manifest(folder, CARD_CACHE)
                    ok = net.send_card(m, stop=stop)
                    g['card_only'] = (folder, None if ok is None else {e[0].lower() for e, k in zip(m, ok) if k}, len(m))
            self.net_result = (net, g, rom, role)
        threading.Thread(target=work, daemon=True).start()
        self.root.after(100, self.net_poll)
    def net_poll(self):
        """the Tk side: the connection made (the game loaded, both switched on at once), or lost"""
        res = getattr(self, 'net_result', None)
        if res is not None:
            self.net_result = None
            net, g, rom, role = res
            self.net_pending = None
            if getattr(self, 'net_cancel', False) and not g:
                net.close(); self.net_flash('ONLINE: STOPPED', secs=3)
                return
            if not g and isinstance(rom, str) and rom.startswith('server:'):
                why = rom[7:]
                net.close(); self.net_flash('ONLINE: NO MEETING POINT' if why == 'server' else 'ONLINE: THE MEETING POINT SAYS', ('THE SERVER DID NOT ANSWER' if why == 'server' else why)[:34].upper(), secs=8)
                return
            if not g and rom == 'version':
                net.close(); self.net_flash('ONLINE: THE OTHER PLAYER HAS', 'ANOTHER EMULATOR VERSION', secs=8)
                self.chat_add('sys', 'The other player has another version of Vectrex_EMU: online, both need the same '
                              'one (the latest from GitHub), or the two games could differ.')
                return
            if not g:
                net.close()
                self.net_flash(*(('ONLINE: PLAYER 2 DOES NOT HAVE', os.path.splitext(os.path.basename(self.args.rom))[0].upper())
                             if rom == 'missing' else ('ONLINE: NOBODY CAME',) if role == 'host' else ('ONLINE: NO GAME FROM THE HOST',)), secs=8)
                return
            if not rom:
                net.close(); self.net_flash('ONLINE: YOU DO NOT HAVE', os.path.splitext(g.get('name', '?'))[0].upper(), secs=8)
                return
            card = g.get('card_only')
            if g.get('card') and (not card or card[1] is None):
                net.close(); self.net_flash('ONLINE: THE SD CARDS', 'COULD NOT BE COMPARED', secs=8)
                return
            self.net, self.net_game, self.net_rom = net, g, os.path.abspath(rom)
            Core.force_seed = g.get('seed')
            Core.net_card = (card[0], card[1]) if card else None
            try:
                self.restart(rom)
            finally:
                Core.force_seed = None; Core.net_card = None
            if card and len(card[1]) < card[2]:                 # (the card both play from: the files both have)
                self.chat_add('sys', "SD card: %d of the host's %d files are the same on both sides - the game sees only "
                              "those (a file that is missing shows as missing on both screens)." % (len(card[1]), card[2]))
            self.net_msg = ''
            self.flash('ONLINE: CONNECTED' + (' (RELAYED)' if getattr(net, 'relay', False) else ''), 'YOU ARE PLAYER %d' % (1 if role == 'host' else 2), secs=4)
            self.show_look('online: player %d of %s' % (1 if role == 'host' else 2, os.path.basename(rom)))
        self.chat_poll()
        net = getattr(self, 'net', None)
        if net is not None and net.desync is not None and not getattr(net, 'desync_told', False):
            net.desync_told = True                              # (the two games have parted: say so, once)
            self.net_flash('ONLINE: OUT OF SYNC', 'THE GAMES DIFFER NOW', secs=8)
            self.chat_add('sys', 'Out of sync at frame %d: the two games are no longer the same. Leave and start '
                          'again to play together.' % net.desync)
        if getattr(self, 'net_game', None) and getattr(self.run, 'net_lost', False):
            self.net_end('ONLINE: CONNECTION LOST')
        if getattr(self, 'net_game', None) or res is None:
            self.root.after(200, self.net_poll)
    def net_end(self, why):
        net = getattr(self, 'net', None)
        self.net = None; self.net_game = None; self.net_rom = None
        if self.run is not None:
            self.run.net = None
        if net is not None:
            self.chat_add('sys', why.replace('ONLINE: ', '').capitalize() + ' - the chat ends here.')
            net.close()
            self.net_flash(why, 'THE GAME GOES ON HERE ALONE', secs=6)
    # ---------------------------------------------------------------- the chat (online play)
    def chat_add(self, who, text):
        """a line in the chat: who 'me', 'them' or 'sys' - kept for this session, shown in the Chat window"""
        log = self.__dict__.setdefault('chat_log', [])
        log.append((who, text)); del log[:-200]
        w = getattr(self, 'chat_win', None)
        if w is not None:
            try: w.add_line(who, text)
            except tk.TclError: self.chat_win = None
    def chat_poll(self):
        """the other player's lines (netplay: they arrive with the frames) - into the Chat window, which opens by itself
        (the keyboard stays with the game), and briefly in the picture (seen in full screen too)"""
        net = getattr(self, 'net', None)
        if net is None or not hasattr(net, 'chat_in'):
            return
        other = 'PLAYER %d' % (2 if getattr(self, 'net_role', 'host') == 'host' else 1)
        while net.chat_in:
            line = net.chat_in.popleft()
            self.chat_add('them', line)
            if getattr(self, 'chat_win', None) is None:
                self.chat_window(focus=False)
            self.flash(other + ':', line.upper()[:34], secs=4)
        while net.chat_failed:
            self.chat_add('sys', 'Not delivered (the other emulator has no chat yet): ' + net.chat_failed.popleft())
    def chat_window(self, e=None, focus=True):
        """Online > Chat (Ctrl+T): write to the other player while playing. Typing here is not the game's keyboard
        (a game controller still plays); Enter sends, Esc gives the keyboard back to the game"""
        if not getattr(self, 'net_game', None):
            if focus:
                self.flash('CHAT: ONLY IN AN ONLINE GAME', secs=2)
            return 'break'
        w = getattr(self, 'chat_win', None)
        if w is not None:
            try:
                w.deiconify(); w.lift()
                if focus: w.entry.focus_force()
                return 'break'
            except tk.TclError:
                self.chat_win = None
        g, dim, bg = deck.G, deck.DIM, deck.PANEL
        f9 = (MONO, 10)
        me = 'You'; them = 'Player %d' % (2 if getattr(self, 'net_role', 'host') == 'host' else 1)
        t = tk.Toplevel(self.root, bg=deck.BG); self.chat_win = t
        t.withdraw()
        t.title('Chat'); t.transient(self.root); t.resizable(False, False)
        top = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); top.pack(fill='x')
        tk.Label(top, text='Enter: send   Esc: back to the game', fg=dim, bg=deck.TOPBG, font=(MONO, 9)).pack(side='left', padx=12, pady=6)
        body = tk.Frame(t, bg=bg, highlightthickness=1, highlightbackground=deck.EDGE); body.pack(fill='both', padx=10, pady=(8, 0))
        log = tk.Text(body, width=46, height=9, bg='#06100d', fg='#bfe', font=f9, relief='flat', wrap='word', bd=0,
                      highlightthickness=0, padx=8, pady=6, cursor='arrow', takefocus=0)
        log.pack(fill='both')
        log.tag_configure('me', foreground=g); log.tag_configure('them', foreground='#bfe')
        log.tag_configure('sys', foreground=dim); log.tag_configure('who_me', foreground=g, font=(MONO, 10, 'bold'))
        log.tag_configure('who_them', foreground='#e8fff6', font=(MONO, 10, 'bold'))
        def add_line(who, text):
            log.configure(state='normal')
            if log.index('end-1c') != '1.0':
                log.insert('end', '\n')
            if who == 'sys':
                log.insert('end', text, 'sys')
            else:
                log.insert('end', (me if who == 'me' else them) + ': ', 'who_' + who); log.insert('end', text, who)
            log.configure(state='disabled'); log.see('end')
        t.add_line = add_line
        if not getattr(self, 'chat_log', None):
            self.__dict__.setdefault('chat_log', []).append(('sys', 'Connected - say hello to %s.' % them.lower()))
        for who, text in self.chat_log:
            add_line(who, text)
        row = tk.Frame(t, bg=deck.BG); row.pack(fill='x', padx=10, pady=8)
        v = tk.StringVar()
        en = tk.Entry(row, textvariable=v, bg='#06100d', fg='#e8fff6', insertbackground='#bfe', font=f9, relief='flat',
                      highlightthickness=1, highlightbackground='#2a5a48', highlightcolor=g)
        en.pack(side='left', fill='x', expand=True, ipady=3)
        t.entry = en
        def limit(*_):                                          # (a line: at most netplay.CHAT_MAX bytes)
            s_ = v.get()
            while len(s_.encode('utf-8')) > 200:
                s_ = s_[:-1]
            if s_ != v.get(): v.set(s_)
        v.trace_add('write', limit)
        def send(ev=None):
            txt = v.get().strip()
            net = getattr(self, 'net', None)
            if txt and net is not None and hasattr(net, 'say'):
                net.say(txt); self.chat_add('me', txt); v.set('')
            elif txt:
                self.chat_add('sys', 'Not connected - not sent.')
            return 'break'
        deck._button(row, 'Send', send, self, on=True, small=True).pack(side='left', padx=(6, 0))
        en.bind('<Return>', send)
        en.bind('<FocusIn>', lambda ev: setattr(self.inp, 'kbd_off', True))     # (typing: not the game's keyboard)
        en.bind('<FocusOut>', lambda ev: setattr(self.inp, 'kbd_off', False))
        def back(ev=None):                                      # (Esc: the keyboard back to the game, the window stays)
            self.inp.kbd_off = False
            try: self.root.focus_force()
            except tk.TclError: pass
            return 'break'
        def close():
            self.inp.kbd_off = False
            self.chat_win = None
            t.destroy()
        t.bind('<Escape>', back)
        t.protocol('WM_DELETE_WINDOW', close)
        deck.place(t, 'chat', self)
        deck.over_full(t, self)
        try: dark_title(t)
        except Exception: pass   # noqa
        if focus:
            en.focus_force()
        else:
            self.root.after(50, lambda: self.root.focus_force())    # (opened by a message: the keys stay with the game)
        return 'break'
    def _room_window(self, title, sub, attr):
        """a small window in the Online window's style: (toplevel, body frame, foot frame, close)"""
        t = tk.Toplevel(self.root, bg=deck.BG); setattr(self, attr, t)
        t.withdraw()
        t.title(title); t.transient(self.root); t.resizable(False, False)
        top = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); top.pack(fill='x')
        tk.Label(top, text=title.upper(), fg=deck.HI, bg=deck.TOPBG, font=(MONO, 11, 'bold'), anchor='w').pack(fill='x', padx=14, pady=(8, 0))
        tk.Label(top, text=sub, fg=deck.DIM, bg=deck.TOPBG, font=(MONO, 9), anchor='w').pack(fill='x', padx=14, pady=(0, 8))
        body = tk.Frame(t, bg=deck.PANEL, highlightthickness=1, highlightbackground=deck.EDGE); body.pack(fill='x', padx=10, pady=(10, 0))
        foot = tk.Frame(t, bg=deck.BG); foot.pack(fill='x', padx=10, pady=10)
        def close():
            self.inp.block = False
            setattr(self, attr, None)
            try: t.destroy()
            except tk.TclError: pass
        t.bind('<Escape>', lambda ev: close())
        t.protocol('WM_DELETE_WINDOW', close)
        return t, body, foot, close
    def public_host_window(self):
        """Online > Host public game: a gamer name, then this game in the meeting point's game room for anyone to join"""
        import netplay
        if getattr(self, 'pub_win', None):
            try: self.pub_win.lift(); return
            except tk.TclError: pass
        t, body, foot, close = self._room_window('Host public game', 'Anyone can see this game and join it as player 2.', 'pub_win')
        f9, bg = (MONO, 10), deck.PANEL
        r1 = tk.Frame(body, bg=bg); r1.pack(fill='x', padx=10, pady=(10, 4))
        tk.Label(r1, text='Gamer name', fg=deck.G, bg=bg, font=f9, width=12, anchor='w').pack(side='left')
        nv = tk.StringVar(value=getattr(self, 'gamer', ''))
        e = tk.Entry(r1, width=14, textvariable=nv, bg='#06100d', fg='#bfe', insertbackground='#bfe', font=f9, relief='flat',
                     highlightthickness=1, highlightbackground='#2a5a48', highlightcolor=deck.G)
        e.pack(side='left')
        e.bind('<FocusIn>', lambda ev: setattr(self.inp, 'block', True))
        e.bind('<FocusOut>', lambda ev: setattr(self.inp, 'block', False))
        tk.Label(r1, text='  up to 12: A-Z 0-9 _ -', fg=deck.DIM, bg=bg, font=(MONO, 9)).pack(side='left')
        r2 = tk.Frame(body, bg=bg); r2.pack(fill='x', padx=10, pady=(0, 4))
        tk.Label(r2, text='Game', fg=deck.G, bg=bg, font=f9, width=12, anchor='w').pack(side='left')
        rom = self.args.rom
        can = bool(rom and os.path.isfile(rom))
        tk.Label(r2, text=os.path.splitext(os.path.basename(rom))[0] if can else 'start a Vectrex ROM first', fg=deck.HI if can else deck.WARN,
                 bg=bg, font=f9, anchor='w').pack(side='left')
        msg = tk.Label(body, text='', fg=deck.WARN, bg=bg, font=(MONO, 9), anchor='w'); msg.pack(fill='x', padx=10, pady=(0, 8))
        tk.Label(t, text="Your gamer name and the game show in everyone's Find players list until someone joins.\n"
                         'A private game with a code stays in Host this game.', fg=deck.DIM, bg=deck.BG, font=(MONO, 9),
                 justify='left', anchor='w').pack(fill='x', padx=14, before=foot)
        state = {'net': None}
        def watch():
            if getattr(self, 'pub_win', None) is not t:
                return
            net = state['net']
            err = getattr(net, 'server_error', None) if net is not None else None
            if err:
                msg.configure(text={'name taken': 'That name is in use. Pick another.',
                                    'the game room is full': 'The game room is full. Try again in a minute.'}.get(err, err[:60]))
                deck._paint(go, True); state['net'] = None
            elif net is not None and getattr(net, 'code', None):
                close(); return
            elif net is not None and getattr(self, 'net_pending', None) is not net and not getattr(self, 'net_game', None):
                msg.configure(text='The meeting point did not answer.'); deck._paint(go, True); state['net'] = None
            t.after(200, watch)
        def host():
            if state['net'] is not None:
                return
            name = netplay.gamer_name(nv.get())
            if not name:
                msg.configure(text='A gamer name: 1 to 12 of A-Z 0-9 _ -'); return
            if not can:
                msg.configure(text='Start a Vectrex ROM first: that is the game you host.'); return
            nv.set(name); self.gamer = name; self.save_settings()
            msg.configure(text='Asking the meeting point...')
            self.net_begin('host', public=name)
            state['net'] = getattr(self, 'net_pending', None)
            deck._paint(go, False)
        go = deck._button(foot, 'Host public game', host, self, on=True); go.pack(side='left')
        deck._button(foot, 'Cancel', close, self).pack(side='right')
        e.bind('<Return>', lambda ev: host())
        watch()
        deck.place(t, 'pubhost', self)
        try: dark_title(t)
        except Exception: pass   # noqa
        e.focus_set()
    def find_players_window(self):
        """Online > Find players: the game room's public games waiting for a player 2 - Join one"""
        import netplay
        if getattr(self, 'find_win', None):
            try: self.find_win.lift(); return
            except tk.TclError: pass
        t, body, foot, close = self._room_window('Find players', 'Public games waiting for a player 2. You need the same game '
                                                 'in your roms folder.', 'find_win')
        f9, bg = (MONO, 10), deck.PANEL
        grid = tk.Frame(body, bg=bg); grid.pack(fill='x', padx=10, pady=8)
        note = tk.Label(t, text='', fg=deck.DIM, bg=deck.BG, font=(MONO, 9), anchor='w'); note.pack(fill='x', padx=14, before=foot)
        have = getattr(self, 'room_have', None) or {}               # (md5 -> the ROM here or None: looked up once)
        self.room_have = have
        def dirs_for(name):
            d = [os.path.join(ROOT, 'roms')] + ([os.path.dirname(os.path.abspath(self.args.rom))] if self.args.rom else [])
            if name.lower().endswith(('.um2', '.img', '.pit')):     # (as joining looks: the roms folder, the game's folder,
                d += [x for x in (self.px_dir, self.sd_dir) if x and os.path.isdir(x)]   #  the SD cards)
            return d
        def show(games):
            if getattr(self, 'find_win', None) is not t:
                return
            for w in grid.winfo_children():
                w.destroy()
            if games is None:
                note.configure(text='The meeting point did not answer (no network, or it has no game room).')
                return
            for c, h in enumerate(('GAMER', 'GAME', 'WAITING', 'YOU HAVE IT')):
                tk.Label(grid, text=h, fg=deck.DIM, bg=bg, font=(MONO, 9), anchor='w',
                         width=(14, 26, 9, 12)[c]).grid(row=0, column=c, sticky='w')
            for i, g in enumerate(games, 1):
                same = g.get('ver', 1) == netplay.NET_VER          # (the same emulator version: only then a game)
                mine = have.get(g['md5']) if same else None
                w = int(g['wait'])
                for c, (txt, col) in enumerate(((g['gamer'], deck.HI), (os.path.splitext(g['name'])[0][:26], deck.G),
                                                ('%d:%02d' % (w // 60, w % 60), deck.DIM),
                                                ('yes' if mine else 'no' if same else 'other ver', deck.G if mine else deck.RED))):
                    tk.Label(grid, text=txt, fg=col, bg=bg, font=f9, anchor='w', width=(14, 26, 9, 12)[c]).grid(row=i, column=c, sticky='w')
                def join(code=g['code']):
                    close(); self.net_last_join = code
                    self.net_begin('join', target=code)
                b = deck._button(grid, 'Join', join, self, small=True); b.grid(row=i, column=4, sticky='e', padx=(6, 0), pady=1)
                deck._paint(b, bool(mine) and not self.net_busy())
            note.configure(text=('%d game%s waiting.' % (len(games), '' if len(games) == 1 else 's')) if games else
                           'No public games waiting now. Host one: Online > Host public game.')
        def refresh():
            note.configure(text='Asking the meeting point...')
            server = self.net_server()
            def work():
                games = netplay.list_games(server)
                for g in games or []:
                    if g['md5'] not in have:                    # (the same ROM here: by its contents, any file name)
                        have[g['md5']] = netplay.find_rom(g['md5'], g['size'] or -1, dirs_for(g['name']))
                self.root.after(0, lambda: show(games))
            threading.Thread(target=work, daemon=True).start()
        deck._button(foot, 'Refresh', refresh, self).pack(side='left')
        deck._button(foot, 'Close', close, self).pack(side='right')
        refresh()
        deck.place(t, 'findplayers', self)
        try: dark_title(t)
        except Exception: pass   # noqa
    def online_window(self, what=None):
        """Online: host (a code for player 2), join (type the code), the state now, the settings. what: 'host' starts
        hosting this game at once, 'join' puts the cursor in the code box"""
        import netplay
        if what == 'host' and not self.net_busy():
            self.net_begin('host')
        if getattr(self, 'net_win', None):
            try:
                self.net_win.deiconify(); self.net_win.lift()
                if what == 'join': self.net_win.join_e.focus_set()
                return
            except tk.TclError:
                pass
        g, dim, bg = deck.G, deck.DIM, deck.PANEL
        f9 = (MONO, 10)
        t = tk.Toplevel(self.root, bg=deck.BG); self.net_win = t
        t.withdraw()
        t.title('Online'); t.transient(self.root); t.resizable(False, False)
        top = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); top.pack(fill='x')
        tk.Label(top, text='Esc: close   the game runs on behind this window', fg=dim, bg=deck.TOPBG, font=(MONO, 9)).pack(side='left', padx=12, pady=6)
        body = tk.Frame(t, bg=deck.BG); body.pack(fill='x', padx=10, pady=(4, 0))
        def box(caption):
            b = tk.Frame(body, bg=bg, highlightthickness=1, highlightbackground=deck.EDGE); b.pack(fill='x', pady=(6, 0))
            tk.Label(b, text=caption, fg=dim, bg=bg, font=(MONO, 8), anchor='w').pack(fill='x', padx=8, pady=(4, 0))
            inner = tk.Frame(b, bg=bg); inner.pack(fill='x', padx=8, pady=(2, 8))
            return inner
        def field(parent, width, var=None):
            e = tk.Entry(parent, width=width, textvariable=var, bg='#06100d', fg='#bfe', insertbackground='#bfe', font=f9,
                         relief='flat', highlightthickness=1, highlightbackground='#2a5a48', highlightcolor=g)
            e.bind('<FocusIn>', lambda ev: setattr(self.inp, 'block', True))       # (typing here: not the game's keys)
            e.bind('<FocusOut>', lambda ev: setattr(self.inp, 'block', False))
            return e
        # host
        h = box('HOST: YOU ARE PLAYER 1, YOU CHOOSE THE GAME')
        r1 = tk.Frame(h, bg=bg); r1.pack(fill='x')
        host_b = deck._button(r1, 'Host', lambda: (self.net_begin('host'), sync()), self, on=True, small=True); host_b.pack(side='left')
        tk.Label(r1, text='  code ', fg=dim, bg=bg, font=f9).pack(side='left')
        code_l = tk.Label(r1, text='', fg='#bfe', bg='#06100d', font=(MONO, 12, 'bold'), width=11, highlightthickness=1,
                          highlightbackground='#2a5a48'); code_l.pack(side='left')
        def copy():
            c = getattr(getattr(self, 'net_pending', None), 'code', None)
            if c:
                self.root.clipboard_clear(); self.root.clipboard_append(c)
        copy_b = deck._button(r1, 'Copy', copy, self, small=True); copy_b.pack(side='left', padx=(6, 0))
        host_note = tk.Label(h, text='', fg=dim, bg=bg, font=f9, anchor='w'); host_note.pack(fill='x', pady=(4, 0))
        # join
        j = box('JOIN: YOU ARE PLAYER 2')
        r2 = tk.Frame(j, bg=bg); r2.pack(fill='x')
        tk.Label(r2, text='code or address ', fg=dim, bg=bg, font=f9).pack(side='left')
        jv = tk.StringVar(value=getattr(self, 'net_last_join', ''))
        je = field(r2, 22, jv); je.pack(side='left'); t.join_e = je
        def join():
            self.net_last_join = jv.get().strip()
            self.net_begin('join', target=self.net_last_join); sync()
        join_b = deck._button(r2, 'Join', join, self, on=True, small=True); join_b.pack(side='left', padx=(6, 0))
        je.bind('<Return>', lambda ev: join())
        # now
        n = box('NOW')
        now_l = tk.Label(n, text='', fg=g, bg=bg, font=f9, anchor='w', justify='left', wraplength=560); now_l.pack(fill='x')
        # settings
        st = box('SETTINGS')
        r3 = tk.Frame(st, bg=bg); r3.pack(fill='x')
        tk.Label(r3, text='Meeting point ', fg=dim, bg=bg, font=f9).pack(side='left')
        sv = tk.StringVar(value=self.net_server_s or '%s:%d' % netplay.SERVER)
        se = field(r3, 24, sv); se.pack(side='left')
        def server_set(*_):
            s = sv.get().strip()
            self.net_server_s = '' if s in ('', '%s:%d' % netplay.SERVER) else s
            self.save_settings()
        se.bind('<FocusOut>', server_set, add='+'); se.bind('<Return>', server_set)
        deck._button(r3, 'Default', lambda: (sv.set('%s:%d' % netplay.SERVER), server_set()), self, small=True).pack(side='left', padx=(6, 0))
        r4 = tk.Frame(st, bg=bg); r4.pack(fill='x', pady=(6, 0))
        tk.Label(r4, text='Without it (LAN, or a forwarded port): ', fg=dim, bg=bg, font=f9).pack(side='left')
        direct_b = deck._button(r4, 'Host directly on UDP %d' % netplay.PORT,
                                lambda: (self.net_begin('host', direct_port=netplay.PORT), sync()), self, small=True)
        direct_b.pack(side='left')
        ov = tk.BooleanVar(value=self.net_osd)
        def osd_flip():
            self.net_osd = ov.get(); self.save_settings()
        tk.Checkbutton(st, text='Show the online state in the on-screen display', variable=ov, command=osd_flip, fg=g, bg=bg,
                       selectcolor=bg, activebackground=bg, activeforeground=deck.HI, font=f9, anchor='w', highlightthickness=0,
                       bd=0).pack(fill='x', pady=(6, 0))
        tips = ((host_b, "Wait for player 2 with the game in the slot now: the meeting point gives a code to tell them."),
                (copy_b, "The code to the clipboard - to paste into a chat."),
                (join_b, "Ask the host for their game: it starts here when you have the same ROM (found by its contents, "
                         "any file name, in the roms folder)."),
                (direct_b, "No meeting point: player 2 joins with your address (your LAN address, or your public one with "
                           "UDP %d forwarded to this PC)." % netplay.PORT),
                (se, "The server that introduces the two players and passes the game on when two routers will not "
                     "connect directly. Both players need the same one."))
        for w, txt in tips:
            deck.Tip(w, txt)
        foot = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); foot.pack(fill='x', pady=(10, 0))
        def close():
            self.inp.block = False
            self.net_win = None
            t.destroy()
        deck._button(foot, 'Close', close, self, small=True).pack(side='right', padx=(6, 12), pady=6)
        leave_b = deck._button(foot, 'Leave game', lambda: (self.net_leave(), sync()), self, small=True); leave_b.pack(side='right', pady=6)
        t.bind('<Escape>', lambda ev: close())
        t.protocol('WM_DELETE_WINDOW', close)
        def sync():
            if self.net_win is not t:
                return
            pend, ng, net = getattr(self, 'net_pending', None), getattr(self, 'net_game', None), getattr(self, 'net', None)
            busy = self.net_busy()
            rom = self.args.rom
            name = os.path.splitext(os.path.basename(rom))[0] if rom else ''
            can_host = bool(rom and os.path.isfile(rom))
            host_b.configure(text=('Host ' + (name if len(name) <= 26 else name[:25] + '…')) if can_host else 'Host (start a Vectrex ROM first)',
                             state='normal' if can_host and not busy else 'disabled')
            deck._paint(host_b, can_host and not busy)                  # (lit only while it can be pressed)
            deck._paint(join_b, not busy)
            hn = pend if pend is not None else net if ng else None     # (the code: while waiting, and kept while playing)
            code = getattr(hn, 'server', None) and getattr(hn, 'code', None) if getattr(self, 'net_role', '') == 'host' else None
            code_l.configure(text=' '.join(code) if code else '')
            copy_b.configure(state='normal' if code else 'disabled')
            if pend is not None and self.net_role == 'host':
                host_note.configure(text=('Tell player 2 this code. Waiting for player 2...' if code else
                                          'Waiting for player 2 on UDP %d...' % pend.port if self.net_direct else
                                          'Asking the meeting point for a code...'), fg=deck.WARN)
            else:
                host_note.configure(text='Then tell player 2 the code.' if not ng else '', fg=dim)
            for w in (join_b, direct_b):
                w.configure(state='disabled' if busy else 'normal')
            leave_b.configure(state='normal' if busy else 'disabled')
            if ng and net is not None:
                now = 'Connected: you are player %d  ·  %s  ·  round trip %d ms  ·  input delay %d frames  ·  %s' % (
                    1 if self.net_role == 'host' else 2, os.path.splitext(ng.get('name', '?'))[0], ng.get('rtt_ms', 0),
                    getattr(net, 'delay', 0), 'relayed by the meeting point' if getattr(net, 'relay', False) else 'direct')
            elif pend is not None:
                now = 'Waiting for player 2.' if self.net_role == 'host' else 'Joining %s...' % (getattr(self, 'net_last_join', '') or '')
            else:
                now = getattr(self, 'net_msg', '') or 'Not online.'
            now_l.configure(text=now)
            t.after(300, sync)
        sync()
        deck.place(t, 'online', self)
        try: dark_title(t)
        except Exception: pass   # noqa
        if what == 'join':
            je.focus_set()
    def power_flags(self):
        """the core's flags for switching on: 8 = a cold start, 16 = run the BIOS intro unseen"""
        ng = getattr(self, 'net_game', None)                    # (online: as the host has it)
        return 8 | (16 if (ng.get('skip_boot', self.skip_boot) if ng else self.skip_boot) else 0)
    def fill_recent(self, menu):
        """Vectrex > Recent games: the last games played (the newest first), one click starts one"""
        menu.delete(0, 'end')
        games = [g for g in getattr(self, 'recent', []) if os.path.isfile(g)]
        top = os.path.abspath(os.path.join(ROOT, 'roms'))
        for g in games:
            name = 'PiTrex' if os.path.basename(g).lower() == 'pitrex.img' else os.path.splitext(os.path.basename(g))[0]
            out = not os.path.abspath(g).lower().startswith(top.lower() + os.sep)   # (dropped on the window, or opened from elsewhere)
            if DARK_MENU:
                menu.add_command(label=name, icon=RECENT_MARK if out else '', command=lambda g=g: self.restart(g),
                                 tip=g if out else None)
            else:
                menu.add_command(label=(RECENT_MARK + ' ' if out else '') + name, command=lambda g=g: self.restart(g))
        if not games:
            menu.add_command(label='(none yet)', state='disabled')
    def note_recent(self, rom):
        if rom and not self.args.demo:
            r = [g for g in getattr(self, 'recent', []) if os.path.abspath(g) != os.path.abspath(rom)]
            self.recent = [os.path.abspath(rom)] + r[:9]
    def room_preset(self):
        """View > Room light: which step the room light is at (x0.5 / x1 / x2 the default), else None"""
        d = self.look_default.get('art_room') if hasattr(self, 'look_default') else None
        v = getattr(self.ren, 'art_room', None) if self.ren else None
        if not d or v is None:
            return None
        return next((k for k in (0.5, 1.0, 2.0) if abs(v - d * k) < 1e-4), None)
    def set_room(self, k):
        if self.ren and hasattr(self.ren, 'art_room'):
            self.ren.art_room = round(self.look_default['art_room'] * k, 4)
            self.save_settings()
            self.show_look('art_room', self.ren)
    def toggle_pad_shortcuts(self):
        self.pad_shortcuts = not self.pad_shortcuts
        self.save_settings()
        self.show_look('TV mode shortcuts (stick + all four buttons for a second): ' + ('on' if self.pad_shortcuts else 'off'))
    def toggle_start_on(self):
        self.start_on = not self.start_on
        self.save_settings()
        self.show_look('start: ' + ('switched on' if self.start_on else 'switched off (turn the volume knob)'))
    def toggle_p2_auto(self):
        """Config > Cartridge: plug in what a game needs (the 3D Imager, the light pen, a speech module) by itself, or
        leave port 2 to the player (as on a real Vectrex)"""
        self.p2_auto = not getattr(self, 'p2_auto', True)
        if self.p2_auto and getattr(self, 'speech_user', 'off') in ('imager', 'lightpen'):
            self.speech_user = 'off'                            # (auto: those two only for the games that need them -
                                                                #  from the next game on)
        self.save_settings()
        self.show_look('port 2: ' + ('the accessory a game needs, plugged in for it' if self.p2_auto else 'as chosen, always'))
    def toggle_skip_boot(self):
        self.skip_boot = not self.skip_boot
        self.save_settings()
        self.show_look('power on: ' + ('straight into the cartridge' if self.skip_boot else 'the Vectrex intro first'))
    def on_resize(self, e):
        if self.ren:
            self.ren.resize(e.width, e.height)
            if not hasattr(self.ren, 'present'):
                self.ren.draw(self.lines)
        else:
            self.W, self.H = e.width, e.height
    def zoom_step(self, f):
        """+ / - / 0: the window size (0: the start size, as big as fits the desktop); in full screen the
        picture size on the black screen (kept apart)"""
        self.real_on = False
        if not self.full:
            if f is None:
                self.measure_fit()
                return self.set_zoom(1.0)
            return self.set_zoom(self.cur_zoom() * f)
        self.full_scale = self.win_fit() if f is None else max(0.2, min(1.0, (self.full_scale or 1.0) * f))
        self.apply_scale()
        self.save_settings()
    def win_fit(self):
        """the windowed picture height as a share of the full-screen height"""
        sh = self.root.winfo_screenheight()
        rs = self.knob_reserve()                                # (full screen: the picture + its strip)
        return max(0.2, min(1.0, getattr(self, 'win_pic_h', sh) * (1.0 + rs) / float(sh)))
    def apply_scale(self):
        if self.ren:
            self.ren.view_scale = self.full_scale if self.full else 1.0
            self.ren.reserve = self.knob_reserve()
            self.ren.resize(self.screen.winfo_width(), self.screen.winfo_height())
            if hasattr(self.ren, 'clear'): self.ren.clear()
    def cur_zoom(self):
        """the picture's height now, relative to the start size"""
        try:
            pl, pt, pr, pb = self.pic_rect()
            return (pb - pt) / float(self.H0) if pb > pt else self.zoom
        except Exception:       # noqa
            return self.zoom
    def set_zoom(self, z):
        if self.full:
            return
        self.zoom = max(0.3, min(4.0, z))
        h = int(self.H0 * self.zoom); w = int(h * vecrender.VIEW_AW)
        h = int(h * (1.0 + self.knob_reserve()))                # (+ the strip)
        if getattr(self, 'rot', 0) in (90, 270):                # (a turned screen: a wide window)
            w, h = h, w
        self.root.geometry('%dx%d' % (w, h + self.chrome_h()))
        self.root.after(60, self.keep_on_desktop)
    def start_fit(self, geo):
        """after the start: the start size measured on the real frame; the first time (no remembered
        size) the window takes it, else it is only kept on the desktop"""
        if self.full:
            return
        self.measure_fit()
        if not geo and not self.args.height_mm:
            self.set_zoom(self.args.scale)
        else:
            self.strip_changed()                                # (a remembered size: the window's shape fitted to the
            self.keep_on_desktop()                              #  picture again - narrower since the tighter frame)
    def frame_overhead(self):
        """(width, height) the window's frame adds to its inside: borders, title bar, menu bar (measured
        once it is on screen; a guess before)"""
        fr = vecrender.frame_rect(self.root.winfo_id())
        iw, ih = self.root.winfo_width(), self.root.winfo_height()
        if fr and iw > 1:
            return fr[2] - fr[0] - iw, fr[3] - fr[1] - ih
        ui = self.root.winfo_fpixels('1i') / 96.0
        return int(16 * ui), int(150 * ui)
    def desktop(self):
        """the desktop without the task bar (left, top, right, bottom) of the window's monitor"""
        mi = vecrender.monitor_info(self.root.winfo_id())
        if mi:
            return mi['work']
        return 0, 0, self.root.winfo_screenwidth(), self.root.winfo_screenheight() - 80
    def measure_fit(self):
        """the start size: the window as big as fits the desktop, above the task bar"""
        l, t, r, b = self.desktop()
        ow, oh = self.frame_overhead()
        aw, ah = r - l - ow - 4, b - t - oh - 4 - self.chrome_h()
        s, a = 1.0 + self.knob_reserve(), vecrender.VIEW_AW
        if getattr(self, 'rot', 0) in (90, 270):                # (turned: the picture's height runs across)
            h0 = min(aw / s, ah / a)
        else:
            h0 = min(ah / s, aw / a)
        if not self.args.height_mm:
            self.H0 = max(200, int(h0))
        self.fit_h = int(h0 * s)
    def keep_on_desktop(self):
        """move the window back onto the desktop if it now reaches behind the task bar or off the screen"""
        fr = vecrender.frame_rect(self.root.winfo_id())
        if not fr or self.full:
            return
        l, t, r, b = self.desktop()
        dx = (l - fr[0]) if fr[0] < l else (r - fr[2]) if fr[2] > r and fr[2] - fr[0] <= r - l else 0
        dy = (t - fr[1]) if fr[1] < t else (b - fr[3]) if fr[3] > b and fr[3] - fr[1] <= b - t else 0
        if fr[3] - fr[1] > b - t:
            dy = t - fr[1]                                      # (taller than the desktop: the top on screen)
        if dx or dy:
            self.root.geometry('+%d+%d' % (self.root.winfo_x() + dx, self.root.winfo_y() + dy))
    def center_vertically(self):
        """the window in the middle of the desktop's height (above the task bar), left / right unchanged"""
        fr = vecrender.frame_rect(self.root.winfo_id())
        if not fr or self.full:
            return
        l, t, r, b = self.desktop()
        dy = t + ((b - t) - (fr[3] - fr[1])) // 2 - fr[1]
        if dy:
            self.root.geometry('+%d+%d' % (self.root.winfo_x(), self.root.winfo_y() + dy))
    def monitor_auto_inch(self):
        """the monitor's diagonal as it reports it (EDID), inches, or None"""
        mi = vecrender.monitor_info(self.root.winfo_id())
        if mi and mi['mm'][0]:
            return round(math.hypot(*mi['mm']) / 25.4, 1)
        return None
    def real_size(self, quiet=False):
        """Display > Real size (9): the overlay sheet as big as a real one, 162 x 207 mm - the picture as tall
        as the sheet (REAL_PIC_MM), from the monitor's
        size (Monitor size, else as it reports it) and resolution - in the window and in full screen (it
        stays until + / - / 0)"""
        inch = self.monitor_inch or self.monitor_auto_inch()
        mi = vecrender.monitor_info(self.root.winfo_id())
        if mi:
            px = mi['px']
        else:
            px = (self.root.winfo_screenwidth(), self.root.winfo_screenheight())
        if not inch:                                            # (unknown: ask for it)
            return self.slider('monitor_inch')
        ppmm = math.hypot(*px) / (inch * 25.4)
        h = REAL_PIC_MM * ppmm
        self.real_on = True
        if self.full:
            sh = self.root.winfo_screenheight()
            self.full_scale = max(0.2, min(1.0, h * (1.0 + self.knob_reserve()) / float(sh)))
            self.apply_scale(); self.save_settings()
        else:
            self.set_zoom(h / float(self.H0))
            self.root.after(120, self.center_vertically)        # (left / right: where the player keeps it)
        self.save_settings()
        if quiet:
            return
        self.show_look('real size: overlay %d x %d mm (%d px high) on a %.1f inch monitor%s' % (
            round(REAL_PIC_MM * 846 / 1080.0), REAL_PIC_MM, round(h), inch, '' if self.monitor_inch else ' (as it reports it)'))
    def look_text(self):
        r = self.ren
        if not self.look_keys:
            return ''
        if getattr(r, 'wob_on', False):
            w = '   wobble %.3f px %d Hz' % (r.wob_strength, getattr(r, 'mains_hz', 50))
        else:
            w = '   wobble OFF'
        if getattr(r, 'bend_on', False):
            w += '   bend %s %.2f' % (vecrender.BEND_TYPES[r.bend_type % len(vecrender.BEND_TYPES)], r.bend_strength)
        else:
            w += '   bend OFF'
        if getattr(r, 'curve_on', False):
            w += '   tube geometry'
        c = self.run.crt
        w += '   buzz %s   speaker %s' % ('%.3f' % c.buzz_vol if c.buzz_on else 'OFF', 'Vectrex' if c.speaker_on else 'clean')
        if not r.phosphor:
            return 'phosphor OFF (crisp lines)' + w
        tl = ('afterglow x%.2f%s' % (r.trail, ' (measured)' if abs(r.trail - 1) < 0.01 else '')) if getattr(r, 'trail_on', False) else 'afterglow OFF'
        return 'bloom %.2f  %s' % (r.bloom, tl) + w
    # what the title bar says about a changed setting: (name, format of its value; None: on / off)
    LOOK_LABELS = dict(volume=('volume', '%d %%'), bloom=('bloom', '%.2f'), trail=('afterglow length', 'x%.2f'),
                       wob_strength=('wobble strength', '%.3f px'), mains_hz=('mains', '%d Hz'), bend_strength=('bending strength', '%.2f'),
                       buzz_vol=('buzz volume', '%.3f'), phosphor=('phosphor', None), trail_on=('afterglow', None),
                       curve_on=('tube geometry', None), geom_k=('tube geometry strength', 'x%.2f'), glass_on=('glass edge', None), glass_room=('glass catches room light', None), light_on=('ceiling light on the glass', None), light_k=('glass reflection strength', 'x%.2f'), light_ov=('ceiling light on the overlay', None), light_ov_k=('overlay reflection strength', 'x%.2f'), halo_on=('corner glow', None), halo_k=('corner glow strength', '%.3f'), dot_k=('corner dot brightness', 'x%.2f'), even_beam=('even beam (an ideal vector display)', None), beam_trace=('beam trace', None), white_k=('white balance', '%.0f K'), phos_spread=('afterglow softness', '%.2f'), phos_grow=('afterglow joining', '%.2f'), phos_smooth=('afterglow overlap', '%.2f px'), line_gap=('line gaps (stop delay)', '%.1f cycles'), art_on=('overlay', None), art_tint=('overlay tint strength', '%.2f'), art_room=('overlay room light', '%.2f'), art_print=('overlay printed art brightness', '%.2f'), art_parallax=('overlay parallax', None), scr_on=('overlay scratches', None), scr_wear=('scratches: wear', 'x%.2f'), scr_fine=('fine scratches', 'x%.2f'), scr_medium=('medium scratches', 'x%.2f'), scr_large=('damage', 'x%.2f'), scr_glint=('scratches: glint strength', 'x%.2f'), scr_reach=('scratches: glint reach', 'x%.2f'), scr_scrib=('scratches: scribbles', 'x%.2f'), art_glow=('plastic glow', None), art_glow_k=('plastic glow strength', 'x%.2f'), case_on=('case', None), case_refl=('case reflections', 'x%.2f'), case_wear=('case wear', 'x%.2f'), art_dx=('overlay left / right', '%+.3f'), art_dy=('overlay up / down', '%+.3f'), art_scale=('overlay size', 'x%.3f'), wob_on=('mains hum wobble', None), bend_on=('brightness bending', None),
                       buzz_on=('Vectrex buzz', None), speaker_on=('Vectrex speaker', None), voice_vol=('speech module volume', '%d %%'), voice_full=('speech module speakers', None), jack_vol=('UVMC2 headphone volume', '%d %%'))
    def show_look(self, what=None, obj=None):
        """save, and show only the setting that changed in the title bar for 2 s"""
        self.save_settings()
        if what == 'bend_type':
            text = 'bending type: %s' % vecrender.BEND_TYPES[self.ren.bend_type % len(vecrender.BEND_TYPES)]
        elif what in self.LOOK_LABELS:
            obj = obj or (self.run.crt if what in ('volume', 'buzz_vol', 'buzz_on', 'speaker_on', 'voice_vol', 'voice_full') else self.ren)
            name, fmt = self.LOOK_LABELS[what]; v = getattr(obj, what)
            if what == 'volume':
                text = 'volume %d %%' % round(v * 100) if self.run.power else 'volume 0 % (off)'
            elif what == 'voice_vol':
                text = 'speech module volume %d %%' % round(v * 100)
            elif what == 'voice_full':
                text = 'speech module: %s' % ('full-range speakers' if v else 'small speaker')
            else:
                text = '%s %s' % (name, ('ON' if v else 'OFF') if fmt is None else fmt % v)
        else:
            text = what or ''
        self.root.title('Vectrex - ' + text)
        if getattr(self, 'title_job', None):
            self.root.after_cancel(self.title_job)
        self.title_job = self.root.after(2000, self.title)
    def tune(self, what, step, lo, hi):
        if self.ren and hasattr(self.ren, what):
            setattr(self.ren, what, round(max(lo, min(hi, getattr(self.ren, what) + step)), 2))
            self.show_look(what)
    def toggle_phosphor(self, e=None):
        if self.ren and hasattr(self.ren, 'phosphor'):
            self.ren.phosphor = not self.ren.phosphor
            self.ren.hist = []                                  # (clears)
            self.show_look('phosphor')
    def toggle_wobble(self, e=None):
        if self.ren and hasattr(self.ren, 'wob_on'):
            self.ren.wob_on = not self.ren.wob_on
            self.show_look('wob_on')
        return 'break'
    @staticmethod
    def cartridge(rom):
        """(file for the core, name to show); no ROM = an empty slot: the BIOS starts Mine Storm"""
        if rom:
            rom = os.path.abspath(rom)
            name = os.path.splitext(os.path.basename(rom))[0]
            if rom.lower().endswith('.v4e'):                    # (VecFever: unpacked first)
                return v4e_rom(rom), name
            d = os.path.dirname(rom)                            # a PiTrex program built for the Pi Zero 2 (ARMv7): the emulated
            if rom.lower().endswith(('.img', '.pit')) and os.path.basename(d).lower() == 'pizero2':   #  Pi is a Zero 1 - its own build of it
                alt = os.path.join(os.path.dirname(d), 'piZero1', os.path.basename(rom))
                if os.path.isfile(alt):
                    return alt, name
            return rom, name
        return NUL, 'Mine Storm (built in)'                    # (NUL reads as an empty cartridge)
    def restart(self, rom):
        """swap the cartridge and power-cycle the Vectrex inside this window: the window, full screen,
        sound and settings stay; the screen goes black until the new game draws"""
        if rom and rom.lower().endswith('.v4e'):               # (a .v4e that cannot run here: the game stays)
            try:
                v4e_rom(os.path.abspath(rom))
            except (ValueError, OSError) as e:
                self.rom_error('%s:\n%s' % (os.path.basename(rom), e)); return
        if getattr(self, 'net_game', None) and os.path.abspath(rom or '') != os.path.abspath(getattr(self, 'net_rom', '') or ''):
            self.net_end('ONLINE: GAME CHANGED')                # (the other side would not follow: online play ends)
        old = self.run
        old.stop = True; old.join(timeout=2)
        self.core.close()
        self.save_settings()                                    # (the old game's own settings kept)
        path, self.rom_name = self.cartridge(rom)
        if bool(rom) != bool(self.args.rom):
            self.cart_slide_t = time.perf_counter()             # (in, or out of, the slot: it slides)
        self.args.rom = os.path.abspath(rom) if rom else None
        self.note_recent(self.args.rom)
        was = self.speech
        self.speech = self.port2_auto(self.args.rom)
        if self.speech != was and self.speech == port2_needed(self.args.rom) and not getattr(self, 'net_game', None):   # (plugged in
            #  for this game: said so)
            self.root.after(300, lambda k=self.speech: self.flash(
                'THIS GAME TALKS' if k in ('vecvox', 'vecvoice') else 'THIS GAME NEEDS',
                {'imager': 'THE 3D IMAGER', 'lightpen': 'THE LIGHT PEN', 'vecvox': 'VECVOX SPEECH', 'vecvoice': 'VECVOICE SPEECH'}[k],
                'PLUGGED INTO PORT 2', secs=3.0))
        ng = getattr(self, 'net_game', None)                    # (online: the host's cartridge hardware, port 2 a joystick)
        if ng:
            self.speech = 'off'
        unit = dict(self.unit, dacofs=float(ng.get('dacofs', 0.0))) if ng else self.unit   # (online: the host's R302 -
        self.core = Core(path, self.args.ideal, ng.get('cart_hw', self.cart_hw) if ng else self.cart_hw, self.speech,   #  the rest
                         self.sd_dir, self.px_dir, unit, self.usb_hid)                                                  #  only moves the picture)
        self.usb_typing_off()
        if self.ren and hasattr(self.ren, 'line_gap'):
            self.core.set_gap(self.ren.line_gap)
        run = Runner(self.core, self.inp, self.audio, False)
        run.beam_of = old.beam_of
        run.crt = old.crt; run.crt.overlay = []
        run.crt.jack = bool(rom and rom.lower().endswith('.um2'))
        run.crt.motor = self.speech == 'imager'
        run.power = old.power                                   # (switched off: it stays off, cartridge in)
        net = getattr(self, 'net', None)
        if ng and net is not None and not net.lost:             # (online: on, and the frames counted from this power-on)
            run.power = True; run.crt.volume = run.crt.volume or 0.5
            net.reset(); run.net = net
        if run.power:
            run.flags = self.power_flags()
        self.run = run
        self.lines = []; self.last_f = 0
        if self.ren:                                            # the old game's picture fades in the phosphor
            if hasattr(self.ren, 'present'): self.flush = True
            else:
                for _ in range(12): self.draw([])
        if run.power:
            self.crt_on(0.0)                                    # a quick power cycle: warm tube, a bulge
        self.load_art()
        self.game_memory()
        self.save_settings()
        self.title()
        run.start()
        self.root.after(1500, self.check_um2_data)
    def system_info(self):
        """what a bug report needs: the version, the system, the graphics card, the monitor and how the emulator
        runs on it now"""
        import platform
        import vrrdetect
        ver = '?'
        for d in (ROOT, RES, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')):
            try:
                ver = open(os.path.join(d, 'VERSION')).read().strip(); break
            except OSError:
                pass
        gpu = glv = '?'
        try:
            gl = self.ren.gl; gl.glGetString.restype = ctypes.c_char_p
            gpu = (gl.glGetString(0x1F01) or b'?').decode(errors='replace')
            glv = (gl.glGetString(0x1F02) or b'?').decode(errors='replace')
        except Exception:   # noqa
            pass
        try:
            v = vrrdetect.detect(self.screen.winfo_id())
        except Exception as e:  # noqa
            v = dict(why=str(e), monitor='?')
        cpu = platform.processor() or platform.machine()
        if IS_WIN:
            try:
                import winreg
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as k:
                    cpu = winreg.QueryValueEx(k, 'ProcessorNameString')[0].strip()
            except OSError:
                pass
        rows = [('Vectrex_EMU', ver + (' (exe)' if getattr(sys, 'frozen', False) else ' (from source, Python %s)' % platform.python_version())),
                ('System', '%s %s (%s)' % (platform.system(), platform.release(), platform.version())),
                ('Processor', '%s, %d logical cores' % (cpu, os.cpu_count() or 0)),
                ('Graphics', '%s (OpenGL %s)' % (gpu, glv)),
                ('Monitor', '%s, %s Hz; G-Sync / FreeSync: %s' % (v.get('monitor', '?'), getattr(self, 'refresh', '?'), v.get('why', '?'))),
                ('Screen mode', ('G-Sync / FreeSync' if self.vrr else "the monitor's full rate" if self.rate == 'max' else 'matched to the monitor')
                 + ', %.1f Hz, vsync every %d refresh(es)' % (self.hz, self.vsync) + (', full screen' if self.full else ', window')),
                ('Picture', '%d x %d px' % tuple(self.ren.size) if self.ren and hasattr(self.ren, 'size') else 'Tk canvas'),
                ('Quality', self.QUALITY_NAMES[self.current_profile()]),
                ('Now', 'game %s, emulation %d %%, game %d fps, monitor %d Hz' % (self.rom_name, round(self.speed), round(self.fps), round(self.disp_fps))),
                ('numpy', 'yes' if _np is not None else 'no')]
        return '\n'.join('%-12s %s' % r for r in rows)
    def copy_system_info(self):
        """Help > Copy system info: on the clipboard, and shown - to paste into a GitHub issue"""
        text = self.system_info()
        self.root.clipboard_clear(); self.root.clipboard_append(text)
        t = tk.Toplevel(self.root, bg=deck.BG); t.withdraw()   # (styled as the Config window)
        t.title('System info'); t.transient(self.root); t.resizable(False, False)
        top = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); top.pack(fill='x')
        tk.Label(top, text='Copied to the clipboard - paste it into your GitHub issue.   Esc: close', fg=deck.DIM,
                 bg=deck.TOPBG, font=(MONO, 9)).pack(side='left', padx=12, pady=6)
        bx = tk.Frame(t, bg=deck.PANEL, highlightthickness=1, highlightbackground=deck.EDGE); bx.pack(fill='x', padx=10, pady=(10, 0))
        tk.Label(bx, text='YOUR SYSTEM', fg=deck.DIM, bg=deck.PANEL, font=(MONO, 8), anchor='w').pack(fill='x', padx=10, pady=(4, 0))
        box = tk.Text(bx, width=max(96, max(len(l) for l in text.splitlines()) + 2), height=len(text.splitlines()) + 1, fg=deck.G, bg=deck.PANEL, font=(MONO, 9), bd=0,
                      highlightthickness=0, selectbackground=darkmenu.SEL, inactiveselectbackground=darkmenu.SEL)
        box.insert('1.0', text); box.configure(state='disabled'); box.pack(padx=10, pady=(2, 8))
        foot = tk.Frame(t, bg=deck.TOPBG, highlightthickness=1, highlightbackground=deck.EDGE); foot.pack(fill='x', pady=(10, 0))
        def again():
            self.root.clipboard_clear(); self.root.clipboard_append(text); self.show_look('system info copied')
        deck._button(foot, 'Copy again', again, self, small=True).pack(side='left', padx=12, pady=6)
        deck._button(foot, 'Close', t.destroy, self, small=True).pack(side='right', padx=12)
        t.bind('<Escape>', lambda e: t.destroy())
        t.update_idletasks()
        x = self.root.winfo_rootx() + (self.root.winfo_width() - t.winfo_reqwidth()) // 2
        y = self.root.winfo_rooty() + (self.root.winfo_height() - t.winfo_reqheight()) // 3
        t.geometry('+%d+%d' % (max(0, x), max(0, y)))
        deck.reveal(t, lambda: deck.no_close_button(t)); t.focus_force()
    def open_deck(self, e=None):
        """Tab: the control deck - opens, or closes when open"""
        return deck.open_deck(self)
    def game_now(self):
        """this game's own settings, as they are now: the look and the controls"""
        return dict(look={k: getattr(self.ren, k) for k in self.look_keys} if self.ren else {}, controls=self.inp.save())
    def game_apply(self, s):
        old_gap(s.get('look', {}))
        for k, v in s.get('look', {}).items():
            if k in self.look_keys:
                setattr(self.ren, k, type(self.look_default[k])(v))
        if s.get('controls'):
            self.inp.load(s['controls']); self.inp.refresh_devices()
        if self.ren and hasattr(self.ren, 'line_gap'):
            self.core.set_gap(self.ren.line_gap); self.unit['gap'] = self.ren.line_gap
    def game_memory(self):
        """the cartridge just put in: its own look and controls if it has them (the everyday ones kept aside),
        else the everyday ones back"""
        own = self.per_game.get(self.rom_name)
        if own:
            if self.game_global is None:
                self.game_global = self.game_now()
            self.game_apply(own)
        elif self.game_global is not None:
            self.game_apply(self.game_global)
            self.game_global = None
    def game_state(self):
        """for the control deck: 'everyday' (this game uses the everyday settings, changes kept for all games),
        'own' (its own saved settings), 'changed' (its own, changed since saved)"""
        own = self.per_game.get(self.rom_name)
        if not own:
            return 'everyday'
        now = json.loads(json.dumps(self.game_now()))
        return 'own' if now == json.loads(json.dumps(own)) else 'changed'
    def save_game(self):
        """Save for this game: from now on this game starts with the look and controls it has now (the
        everyday settings stay as they were)"""
        if self.game_global is None:
            try:
                d = json.load(open(self.settings_f))
                self.game_global = dict(look=dict(d.get('display', {})), controls=d.get('controls') or self.inp.save())
            except Exception:   # noqa
                self.game_global = self.game_now()
        self.per_game[self.rom_name] = self.game_now()
        self.save_settings()
        self.show_look('%s: settings saved for this game' % self.rom_name)
    def revert_game(self):
        """Revert: back to this game's saved settings"""
        if self.rom_name in self.per_game:
            self.game_apply(self.per_game[self.rom_name]); self.show_look('%s: its saved settings' % self.rom_name)
    def remove_game(self):
        """Remove game settings: this game forgets its own settings, the everyday ones come back"""
        if self.rom_name in self.per_game:
            del self.per_game[self.rom_name]
            if self.game_global is not None:
                self.game_apply(self.game_global); self.game_global = None
            self.save_settings()
            self.show_look('%s: the everyday settings' % self.rom_name)
    def make_default(self):
        """Make default: the settings now (this game's) become the everyday settings for every game without
        its own"""
        if self.game_global is not None:
            self.game_global = self.game_now()
        self.save_settings()
        self.show_look('these settings are now the everyday settings')
    def check_um2_data(self):
        """a UVMC2 game that needs files of its own (Doom's WAD, Quake's pak0.pak) and has not got them on the SD card
        folder: what it needs and where, and the folder opened on request (a real UVMC2's card is the same: the folder
        is this emulator's card)"""
        miss = um2_missing(self.args.rom, self.sd_dir)
        if not miss:
            return
        from tkinter import messagebox
        folder, what = miss
        d = os.path.join(self.sd_dir, folder)
        if self.full:                                   # (pop-ups hide behind a full-screen picture)
            self.toggle_full()
        game = os.path.splitext(os.path.basename(self.args.rom))[0]
        if deck.dialog('question', 'Vectrex_EMU', '%s needs %s.\n\nUVMC2 games read such files from the cartridge\'s SD card. '
                               'In this emulator the SD card is a folder - put the file in:\n\n%s\n\nthen load the game '
                               'again (click the cartridge: the ROM browser).\n\nOpen that folder now?' % (game, what, d),
                               parent=self.root, yesno=True):
            open_folder(d)
    def cart_message(self, msg):
        from tkinter import messagebox
        name = 'PiTrex' if (self.args.rom or '').lower().endswith(('.img', '.pit')) else 'Ultimate Vectrex Multicart 2'
        if name == 'PiTrex' and 'undefined' in msg:            # (most likely a program for another Pi)
            msg += (chr(10) + chr(10) + 'The emulated PiTrex has a Raspberry Pi Zero (the first one). This program uses '
                    "instructions it does not have - it is probably made for the Pi Zero 2 (a PiTrex SD card's piZero2 "
                    'folder). Use its build in the piZero1 folder.')
        if 'without its JIT' in msg:                            # (a note: it runs on, only slower)
            deck.dialog('info', 'Vectrex_EMU', msg[0].upper() + msg[1:] + '.', parent=self.root)
            return
        deck.dialog('warning', 'Vectrex_EMU', 'The cartridge (' + name + ') stopped:' + chr(10) + chr(10) + msg, parent=self.root)
    def rom_error(self, msg):
        from tkinter import messagebox
        deck.dialog('error', 'Vectrex_EMU', 'This cartridge cannot be loaded.\n\n' + msg, parent=self.root)
    def open_rom(self, e=None):
        """Vectrex > Open ROM file: pick a ROM; the Vectrex power-cycles with it, in the same window"""
        from tkinter import filedialog
        start = os.path.dirname(os.path.abspath(self.args.rom)) if self.args.rom else ROOT
        fn = filedialog.askopenfilename(parent=self.root, title='Open a Vectrex ROM', initialdir=start,
                                        filetypes=[('Vectrex ROM', '*.bin *.vec *.gam *.rom *.v4e' + (' *.um2' if getattr(self, 'show_um2', True) else '')), ('All files', '*.*')])
        if fn:
            self.restart(fn)
        return 'break'
    def eject(self, e=None):
        """Ctrl+E, a right click on the cartridge: out of the slot (Mine Storm) - or, out, the last cartridge back in
        (the one taken out, else the last one played; none: nothing happens)"""
        if self.args.rom:
            self.ejected_rom = os.path.abspath(self.args.rom)
            self.restart(None)
            return 'break'
        last = [g for g in [getattr(self, 'ejected_rom', None)] + list(getattr(self, 'recent', [])) if g and os.path.isfile(g)]
        if last:
            self.restart(last[0])
        return 'break'
    def tune_mul(self, what, f, lo, hi, obj=None):
        obj = obj or self.ren
        if obj and hasattr(obj, what):
            v = getattr(obj, what) * f
            if v < 0.005: v = 0.005 if f > 1 else 0.0
            setattr(obj, what, round(max(lo, min(hi, v)), 4))
            self.show_look(what, obj)
        return 'break'
    def flip(self, obj, what):
        if obj and hasattr(obj, what):
            setattr(obj, what, not getattr(obj, what)); self.show_look(what, obj)
        return 'break'
    def cycle(self, obj, what, n, on=None):
        if obj and hasattr(obj, what):
            setattr(obj, what, (getattr(obj, what) + 1) % n)
            if on: setattr(obj, on, True)
            self.show_look(what, obj)
        return 'break'
    # each menu's reset puts back only what that menu sets
    def reset_tube(self, e=None):
        """Tube > Reset tube settings to default (not the overlay's: those are in the Overlay menu)"""
        base = self.base_values()                       # (the profile's switches: what Config's marks compare with)
        for k, v in self.look_default.items():
            if not k.startswith(('art_', 'scr_', 'light_', 'glass_room', 'case_')) and k not in ('phos_scale', 'half_rate', 'line_gap'):
                setattr(self.ren, k, base.get(k, v))
        if self.ren and hasattr(self.ren, 'hist'):
            self.ren.hist = []
        if self.ren and hasattr(self.ren, 'line_gap'):
            self.core.set_gap(self.ren.line_gap)
        self.show_look('tube settings: the defaults')
    def reset_overlay(self):
        """Overlay > Reset overlay settings to default: on, chosen automatically, tint, room light, print,
        parallax, scratches (off, wear, glint); for this game no chosen overlay, for its overlay no position
        / size of its own (other games keep theirs)"""
        base = self.base_values()
        for k, v in self.look_default.items():
            if k.startswith(('art_', 'scr_')) and k != 'art_room':
                setattr(self.ren, k, base.get(k, v))
        self.art_auto = True
        self.art_for.pop(self.rom_name, None)
        if getattr(self, 'art_path', None):
            self.art_adj.pop(os.path.basename(self.art_path), None)
        self.load_art()
        self.save_settings()
        self.show_look('overlay settings: the defaults')
    def reset_case(self):
        """the deck's Case tab: its reset - on, the reflections and the wear at their defaults"""
        base = self.base_values()
        for k, v in self.look_default.items():
            if k.startswith('case_'):
                setattr(self.ren, k, base.get(k, v))
        self.save_settings()
        self.show_look('case settings: the defaults')
    def toggle_case(self, e=None):
        """C: the plastic case round the glass on / off (it only shows without an overlay)"""
        if not (self.ren and hasattr(self.ren, 'case_on')):
            return
        self.ren.case_on = not self.ren.case_on
        on_art = bool(getattr(self.ren, 'art_on', False) and getattr(self.ren, 'art', None))
        self.show_look('case %s' % ('ON' + (' (shows without an overlay)' if on_art else '') if self.ren.case_on else 'OFF'))
    def reset_light(self):
        """Light > Reset light settings to default: the room light, the glass, the ceiling light"""
        base = self.base_values()
        for k, v in self.look_default.items():
            if k.startswith('light_') or k in ('art_room', 'glass_room'):
                setattr(self.ren, k, base.get(k, v))
        self.save_settings()
        self.show_look('light settings: the defaults')
    def light_pad(self):
        """Light > Light position: a small window with a field and a dot - drag it to move the ceiling light
        (left / right; up / down between just visible at the top and its middle a quarter down); right click:
        the default, Esc or clicking elsewhere closes it"""
        r = self.ren
        if not r:
            return
        if getattr(self, 'slider_win', None):
            try: self.slider_win.destroy()
            except tk.TclError: pass
        g, bg, dim = deck.G, deck.PANEL, '#3d6b55'               # (styled as the Config window)
        t = tk.Toplevel(self.root, bg=bg, highlightthickness=1, highlightbackground=deck.EDGE2); self.slider_win = t
        t.withdraw()                                            # (shown once built, in its place)
        t.title('Light position'); t.transient(self.root); t.resizable(False, False)
        try: t.attributes('-toolwindow', True)
        except tk.TclError: pass
        t.after(30, lambda t=t: deck.no_close_button(t))       # (no close button: Esc / Enter / clicking elsewhere)
        head = tk.Label(t, fg=g, bg=bg, font=(MONO, 10, 'bold'), anchor='w'); head.pack(fill='x', padx=10, pady=(8, 0))
        W, Hh, M = 420, 180, 12
        cv = tk.Canvas(t, width=W + 2 * M, height=Hh + 2 * M, bg=bg, highlightthickness=0); cv.pack(padx=10, pady=4)
        cv.create_rectangle(M, M, M + W, M + Hh, outline=g, width=2)
        cv.create_text(M + 6, M + 4, text='top: just visible', anchor='nw', fill=dim, font=(MONO, 9))
        cv.create_text(M + 6, M + Hh - 4, text='bottom: a quarter down', anchor='sw', fill=dim, font=(MONO, 9))
        dot = cv.create_oval(0, 0, 0, 0, fill=g, outline=g)
        dx0, dy0 = self.look_default.get('light_x', 0.0), self.look_default.get('light_y', 0.273)
        def show():
            x, y = M + r.light_x * W, M + r.light_y * Hh
            cv.coords(dot, x - 8, y - 8, x + 8, y + 8)
            head.configure(text='Light position   left / right %d %%   height %d %%' % (round(r.light_x * 100), round(r.light_y * 100)))
        def move(e):
            r.light_x = round(max(0.0, min(1.0, (e.x - M) / float(W))), 3)
            r.light_y = round(max(0.0, min(1.0, (e.y - M) / float(Hh))), 3)
            show(); return 'break'
        def default(e=None):
            r.light_x, r.light_y = dx0, dy0; show(); return 'break'
        cv.bind('<Button-1>', move); cv.bind('<B1-Motion>', move); cv.bind(RCLICK, default); t.bind(RCLICK, default)
        tk.Label(t, fg=g, bg=bg, font=(MONO, 9), anchor='w', text='drag the dot   right click: default   Esc: close').pack(fill='x', padx=10, pady=(0, 8))
        def close(e=None):
            self.save_settings()
            try: t.destroy()
            except tk.TclError: pass
            self.slider_win = None
        t.bind('<Escape>', close); t.protocol('WM_DELETE_WINDOW', close)
        def lost(e=None):
            if not t.winfo_exists(): return
            f = t.focus_get()
            if f is None or not str(f).startswith(str(t)):
                close()
        t.bind('<FocusOut>', lambda e: t.after(150, lost))
        show(); t.update_idletasks()
        x, y = self.root.winfo_pointerxy()                      # (at the mouse, kept on the screen)
        x = min(max(0, x - t.winfo_reqwidth() // 2), t.winfo_screenwidth() - t.winfo_reqwidth())
        y = min(max(0, y - 20), t.winfo_screenheight() - t.winfo_reqheight() - 60)
        t.geometry('+%d+%d' % (x, y)); deck.reveal(t); t.focus_force(); cv.focus_set()
    def reset_sound(self):
        """Sound > Reset sound settings to default (not the volume knob: it is the power switch)"""
        for k, v in self.sound_default.items():
            if k != 'volume' and not k.startswith('jack_'):
                setattr(self.run.crt, k, v)
        self.save_settings()
        self.show_look('sound settings: the defaults')
    def reset_addons(self):
        """Add-ons > Reset add-on settings to default: UVMC2 games listed, the jack clean at 100 %, the 3D Imager's
        motor heard and its note shown"""
        for k, v in self.sound_default.items():
            if k.startswith(('jack_', 'motor_')):
                setattr(self.run.crt, k, v)
        self.imager_note = True
        self.show_um2 = True
        self.sd_dir = SD_DEFAULT
        self.px_dir = PX_DEFAULT
        self.usb_hid = True
        self.p2_auto = True; self.p2_games = {}
        again = self.cart_hw != 'auto' or getattr(self, 'speech_user', 'off') != 'off'
        self.cart_hw = 'auto'; self.speech_user = 'off'
        if again:                                       # (the hardware or port 2 changed: the cartridge in again)
            self.speech = self.port2_auto(self.args.rom)
            self.restart(self.args.rom)
        self.save_settings()
        self.show_look('add-on settings: the defaults')
    def toggle_motor_snd(self):
        self.run.crt.motor_on = not self.run.crt.motor_on
        self.save_settings()
        self.show_look('3D Imager motor sound: ' + ('on' if self.run.crt.motor_on else 'off'))
    def toggle_imager_note(self):
        self.imager_note = not self.imager_note
        self.save_settings()
        self.show_look('3D Imager spin-up note: ' + ('on' if self.imager_note else 'off'))
    def set_jack(self, mode):
        self.run.crt.jack_mode = mode
        self.save_settings()
        self.show_look('UVMC2 headphone jack: ' + {'clean': 'clean, full range', 'speaker': 'through the Vectrex speaker', 'off': 'off'}[mode])
    def toggle_um2 (self):
        self.show_um2 = not self.show_um2
        self.save_settings()
        if getattr(self, 'browser', None):
            self.browser_dir(self.browser['dir'])
        self.show_look('UVMC2 games in the ROM list: ' + ('shown' if self.show_um2 else 'hidden'))
    def open_sd_dir(self):
        open_folder(self.sd_dir)
    def open_px_dir(self):
        """Config > Cartridge, PiTrex: its SD card folder in the file manager"""
        os.makedirs(self.px_dir, exist_ok=True)
        open_folder(self.px_dir)
    def choose_sd_dir(self):
        """Add-ons > SD card folder: the folder a UVMC2 game sees as its SD card (from its next start on)"""
        from tkinter import filedialog
        d = filedialog.askdirectory(parent=self.root, title='The folder a UVMC2 game sees as its SD card',
                                    initialdir=self.sd_dir if os.path.isdir(self.sd_dir) else ROOT)
        if d:
            self.sd_dir = os.path.abspath(d)
            self.save_settings()
            self.show_look('UVMC2 SD card: ' + self.sd_dir + ' (from the next start of the game)')
    def choose_px_dir(self):
        """Add-ons > PiTrex SD card folder: the folder the PiTrex sees as its SD card (the files of a PiTrex
        baremetal SD card: pitrex.img, loader.pit, piZero1/..., the ini files)"""
        from tkinter import filedialog
        d = filedialog.askdirectory(parent=self.root, title='The folder the PiTrex sees as its SD card',
                                    initialdir=self.px_dir if os.path.isdir(self.px_dir) else ROOT)
        if d:
            self.px_dir = os.path.abspath(d)
            self.save_settings()
            self.show_look('PiTrex SD card: ' + self.px_dir + ' (from the next start)')
    def start_pitrex(self, e=None):
        """Add-ons > Start the PiTrex: as when a Vectrex with a PiTrex is switched on - its boot program
        (pitrex.img on its SD card) starts the loader, the menu of its programs"""
        boot = os.path.join(self.px_dir, 'pitrex.img')
        if not os.path.isfile(boot):
            from tkinter import messagebox
            deck.dialog('info', 'Vectrex_EMU', 'The PiTrex needs its SD card: put the files of a PiTrex baremetal SD card '
                                '(pitrex.img, loader.pit, the piZero1 folder, ...) in' + chr(10) + chr(10) + self.px_dir + chr(10) + chr(10) +
                                'or choose another folder in Vectrex > PiTrex > SD card folder.', parent=self.root)
            return 'break'
        self.restart(boot)
        return 'break'
    def open_pitrex(self, e=None):
        """Add-ons > Open PiTrex program: one program (.img / .pit) started on its own, without the loader"""
        from tkinter import filedialog
        start = os.path.join(self.px_dir, 'piZero1') if os.path.isdir(os.path.join(self.px_dir, 'piZero1')) else self.px_dir if os.path.isdir(self.px_dir) else ROOT
        fn = filedialog.askopenfilename(parent=self.root, title='Open a PiTrex program', initialdir=start,
                                        filetypes=[('PiTrex program', '*.img *.pit'), ('All files', '*.*')])
        if fn:
            self.restart(fn)
        return 'break'
    GAME_EXT = ('.bin', '.vec', '.gam', '.rom', '.v4e', '.um2', '.img', '.pit')
    def win_hooks(self):
        """Windows: the window takes files dropped on it (WM_DROPFILES), and its close button closes the emulator
        even while a menu or a message has the mouse (Tk would only beep). Both noted here and handled in tick."""
        if not IS_WIN or getattr(self, '_wndproc', None):
            return
        try:
            u, sh = ctypes.windll.user32, ctypes.windll.shell32
            hwnd = int(self.root.wm_frame(), 16)
            LRESULT = ctypes.c_ssize_t
            WNDPROC = ctypes.WINFUNCTYPE(LRESULT, W.HWND, W.UINT, W.WPARAM, W.LPARAM)
            u.CallWindowProcW.argtypes = [ctypes.c_void_p, W.HWND, W.UINT, W.WPARAM, W.LPARAM]; u.CallWindowProcW.restype = LRESULT
            u.SetWindowLongPtrW.argtypes = [W.HWND, ctypes.c_int, ctypes.c_void_p]; u.SetWindowLongPtrW.restype = ctypes.c_void_p
            sh.DragQueryFileW.argtypes = [ctypes.c_void_p, W.UINT, ctypes.c_wchar_p, W.UINT]; sh.DragQueryFileW.restype = W.UINT
            sh.DragFinish.argtypes = [ctypes.c_void_p]
            old = [None]
            def proc(h, msg, wp, lp):
                if msg == 0x0233:                               # WM_DROPFILES
                    files = []
                    for i in range(sh.DragQueryFileW(wp, 0xFFFFFFFF, None, 0)):
                        n = sh.DragQueryFileW(wp, i, None, 0)
                        buf = ctypes.create_unicode_buffer(n + 1); sh.DragQueryFileW(wp, i, buf, n + 1)
                        files.append(buf.value)
                    sh.DragFinish(wp)
                    self.win_q.append(('drop', files))
                    return 0
                if msg == 0x00A1 and wp == 20:                  # WM_NCLBUTTONDOWN on the close button (HTCLOSE)
                    self.win_q.append(('close', None))
                    return 0
                return u.CallWindowProcW(old[0], h, msg, wp, lp)
            self._wndproc = WNDPROC(proc)                       # (kept: the window calls it)
            old[0] = u.SetWindowLongPtrW(hwnd, -4, ctypes.cast(self._wndproc, ctypes.c_void_p))
            sh.DragAcceptFiles(hwnd, True)
        except Exception:                                       # noqa  (no drop, the usual close button)
            pass
    def win_events(self):
        q, self.win_q = self.win_q, []
        for what, arg in q:
            if what == 'close':
                self.quit_anyway(); return True
            if what == 'drop':
                self.drop_files(arg)
    def drop_files(self, files):
        """files dropped onto the window: the first game among them starts at once (it stays where it is - Recent
        games finds it again)"""
        games = [f for f in files if os.path.isfile(f) and os.path.splitext(f)[1].lower() in self.GAME_EXT]
        if not games:
            if files:
                self.show_look('Not a Vectrex game: ' + os.path.basename(files[0]))
            return
        if self.browser:
            self.close_browser()
        self.restart(games[0])
        try: self.focus()
        except Exception: pass                                  # noqa
    def quit_anyway(self):
        """the close button: whatever is open - a menu, a message - the emulator closes"""
        try:
            mb = getattr(self, 'menubar', None)
            if mb is not None and getattr(mb, 'open', None) is not None:
                mb.open.close_all()
        except Exception:                                       # noqa
            pass
        try:
            g = self.root.grab_current()
            if g is not None: g.grab_release()
        except Exception:                                       # noqa
            pass
        self.quit()
    def start_movies(self, e=None):
        """Vectrex > Vector movies: the UVMC2 Movie Player (in the roms folder, or anywhere below it); it plays the
        movies in the UVMC2's SD card folder, movies (and says where they go when there are none)"""
        top = os.path.join(ROOT, 'roms')
        found = None
        for dp, dn, fn in os.walk(top):
            dn[:] = [x for x in dn if x.lower() != 'overlays']
            for f in fn:
                if f.lower().endswith('.um2') and 'movie' in f.lower():
                    found = os.path.join(dp, f); break
            if found:
                break
        if not found:
            deck.dialog('info', 'Vectrex_EMU', 'The movie player (UVMC2 Movie Player.um2) is not in the roms folder:'
                        + chr(10) + chr(10) + top + chr(10) + chr(10) + 'It comes with the emulator - copy it back there from the download.',
                        parent=self.root)
            return 'break'
        self.restart(found)
        return 'break'
    def open_um2(self, e=None):
        """Add-ons > Open UVMC2 game: a .um2 file (native ARM for the cartridge's RP2350)"""
        from tkinter import filedialog
        start = os.path.dirname(os.path.abspath(self.args.rom)) if self.args.rom else os.path.join(ROOT, 'roms')
        fn = filedialog.askopenfilename(parent=self.root, title='Open an Ultimate Vectrex Multicart 2 game', initialdir=start,
                                        filetypes=[('UVMC2 game', '*.um2'), ('All files', '*.*')])
        if fn:
            self.restart(fn)
        return 'break'
    def reset_display(self):
        """Display > Reset display settings to default: the start size (no real size, the monitor size as
        it reports it), not turned, the OSD, the knob and the cartridge on, the monitor's full rate with smooth
        motion on (full screen stays as it is; its picture as big as in the window)"""
        self.monitor_inch = 0.0; self.real_on = False
        if self.rot:
            self.set_rotation(0)
        for on, flip in ((self.fps_on, self.toggle_fps), (self.knob_on, self.toggle_knob), (self.cart_on, self.toggle_cart)):
            if not on:
                flip()
        self.osd_meters = True
        if not self.smooth:
            self.toggle_smooth()
        if self.ren:                                    # (the weak-graphics switches: the profile's)
            base = self.base_values()
            for k in ('phos_scale', 'half_rate'):
                if k in base:
                    setattr(self.ren, k, base[k])
        self.vrr = False; self.rate = 'max'; self.apply_rate()
        if self.full:
            self.full_scale = self.win_fit(); self.apply_scale()
        else:
            self.full_scale = None
            self.zoom_step(None)
        self.save_settings()
        self.show_look('display settings: the defaults')
    def reset_vectrex(self):
        """Vectrex > Reset Vectrex settings to default: cartridge hardware Auto, port 2 the joystick, start
        switched off, with the intro (the Vectrex is switched off and on if the hardware changes; the
        controls keep theirs - the Controls window has its own reset; the volume knob stays)"""
        again = self.cart_hw != 'auto' or self.speech != 'off'
        self.cart_hw = 'auto'; self.speech = self.speech_user = 'off'; self.p2_games = {}
        self.start_on = False; self.skip_boot = False; self.pad_shortcuts = True; self.p2_auto = True
        self.save_settings()
        if again:
            self.restart(self.args.rom)
        self.show_look('Vectrex settings: the defaults')
    def save_settings(self):
        try:
            d = json.load(open(self.settings_f))
        except Exception:           # noqa
            d = {}
        if not self.full:
            d['geometry'] = self.root.geometry()
        if not self.args.demo:
            d['last_rom'] = self.args.rom or None               # (the cartridge in the slot: none = Mine Storm)
        d['recent'] = list(getattr(self, 'recent', []))
        d['fps_counter'] = getattr(self, 'fps_on', True)
        d['quality_base'] = getattr(self, 'quality_base', 'ultra')
        d['quality_opt'] = dict(getattr(self, 'quality_opt', {}))
        d['osd_meters'] = getattr(self, 'osd_meters', True)
        d['net_server'] = getattr(self, 'net_server_s', '')
        d['gamer_name'] = getattr(self, 'gamer', '')
        d['net_osd'] = getattr(self, 'net_osd', True)
        d['gsync'] = getattr(self, 'vrr', False)
        d['screen_rate'] = getattr(self, 'rate', '60')
        own = hasattr(self, 'per_game') and self.rom_name in self.per_game   # (a game's own settings: saved only
        g = getattr(self, 'game_global', None) if own else None              #  by the deck's Save for this game)
        d['controls'] = g['controls'] if g else self.inp.save()
        if hasattr(self, 'per_game'):
            d['per_game'] = self.per_game
        d['cart_hw'] = getattr(self, 'cart_hw', 'auto')
        d['speech'] = getattr(self, 'speech_user', getattr(self, 'speech', 'off'))
        d['p2_games'] = dict(getattr(self, 'p2_games', {}))
        d['show_um2'] = getattr(self, 'show_um2', True)
        d['uvm2_sd_dir'] = getattr(self, 'sd_dir', None)
        d['pitrex_sd_dir'] = getattr(self, 'px_dir', None)
        if hasattr(self, 'art_for'):
            if self.art_path and self.ren:                      # (this overlay's position and size)
                a = [round(getattr(self.ren, k), 4) for k in ('art_dx', 'art_dy', 'art_scale')]
                if a == [0.0, 0.0, 1.0]: self.art_adj.pop(os.path.basename(self.art_path), None)
                else: self.art_adj[os.path.basename(self.art_path)] = a
            d['art_auto'] = self.art_auto; d['overlay_for'] = self.art_for; d['overlay_adjust2'] = self.art_adj
            d['rotate'] = self.rot
            d['monitor_inch'] = self.monitor_inch
            if self.ren and hasattr(self.ren, 'scr_seed'):
                d['scratch_seed'] = self.ren.scr_seed
            d['real_size'] = self.real_on
        d['start_on'] = getattr(self, 'start_on', False)
        d['pad_shortcuts'] = getattr(self, 'pad_shortcuts', True)
        d['mouse_stick'] = getattr(self, 'mouse_stick', False)
        d['usb_hid'] = getattr(self, 'usb_hid', True)
        d['imager_note'] = getattr(self, 'imager_note', True)
        d['smooth_motion'] = getattr(self, 'smooth', False)
        d['quality'] = self.current_profile() if self.ren else 'ultra'
        if getattr(self, 'setup_done', False): d['setup_done'] = True
        if hasattr(self, 'unit'):
            d['unit'] = {k: v for k, v in self.unit.items() if k != 'gap'}; d['unit_name'] = self.unit_name
        d['ultra_rate'] = getattr(self, 'ultra_rate', None)
        d['skip_boot'] = getattr(self, 'skip_boot', False)
        d['p2_auto'] = getattr(self, 'p2_auto', True)
        d['knob'] = getattr(self, 'knob_on', True)
        d['cart'] = getattr(self, 'cart_on', True)
        if getattr(self, 'deck_pos', None):
            d['deck_pos'] = list(self.deck_pos)
        if getattr(self, 'ctl_pos', None):
            d['ctl_pos'] = list(self.ctl_pos)
        if getattr(self, 'help_geo', None):
            d['help_geo'] = self.help_geo
        if getattr(self, 'win_pos', None):
            d['win_pos'] = {k: list(v) for k, v in self.win_pos.items()}
        if getattr(self, 'deck_strip', None):
            d['deck_strip'] = self.deck_strip
        if getattr(self, 'deck_focus', None):
            d['deck_focus'] = dict(self.deck_focus)
        if getattr(self, 'browse_last', None):
            d['browse_last'] = list(self.browse_last)
        if getattr(self, 'full_scale', None):
            d['full_scale'] = round(self.full_scale, 4)
        if self.look_keys:
            d['display'] = dict(g['look']) if g else {k: getattr(self.ren, k) for k in self.look_keys}
        d.setdefault('display', {}).update({k: getattr(self.run.crt, k) for k in self.sound_keys})
        try:
            json.dump(d, open(self.settings_f, 'w'))
        except OSError:
            pass
    def toggle_full(self, e=None):
        shown = []                                              # (the open windows - Config, Online, Help ...: Windows
        for w in self.root.winfo_children():                    #  hides them with the main window's full-screen switch)
            try:
                if (isinstance(w, tk.Toplevel) and w.state() == 'normal' and not w.overrideredirect()
                        and w.winfo_x() > -10000):              # (not the parked cover, menus or tips)
                    shown.append(w)
            except tk.TclError:
                pass
        if not self.full:                                       # the picture height in the window, now
            self.win_pic_h = (ALG_MAX_Y * self.ren.map[0] / self.px() if self.ren and hasattr(self.ren, 'map') else
                              min(self.screen.winfo_height(), self.screen.winfo_width() / vecrender.VIEW_AW))
        self.full = not self.full
        if DARK_MENU:                                           # (full screen: the menu bar only when wanted, over the
            self.full_menu(False)                               #  picture - full_menu)
            if self.full:
                self.menubar.pack_forget(); self.menu_line.pack_forget()
            else:
                self.menubar.pack(side='top', fill='x', before=self.screen)
                self.menu_line.pack(side='top', fill='x', before=self.screen)
        else:
            self.root.config(menu='' if self.full else self.menubar)
        self.root.attributes('-fullscreen', self.full)
        self.show_bar()
        if self.full and not self.full_scale:                   # first time: as big as it was in the window
            self.full_scale = self.win_fit()
        self.root.update_idletasks()
        if self.real_on:                                        # (Real size: the same size in full screen and back)
            self.real_size(quiet=True)
        self.apply_scale()
        self.pointer_moved()
        self.full_share()
        for ms in (150, 600):                                   # (they are shown again: above the full-screen picture, on
            for w in shown:                                     #  its screen - or ordinary windows again; twice: once
                self.root.after(ms, lambda w=w: deck.over_full(w, self))   # Windows has settled the main window)

    def full_share(self):
        """full screen: while a window (Config, Online, a menu ...) is open the picture stops 1 pixel short of the
        screen's bottom edge (black - not seen). A picture covering the whole monitor is shown by the graphics driver
        directly, past Windows' compositor ("independent flip"): a window opened over it later is drawn but never
        reaches the monitor (the owner, 2026-10-27: F11, then Tab - no Config). With no window open the picture has
        the whole screen again (G-Sync, the shortest delay)"""
        want = 0
        if self.full:
            for w in self.root.winfo_children():
                try:
                    if (isinstance(w, tk.Toplevel) and w.state() == 'normal' and w.winfo_x() > -10000
                            and w.winfo_width() > 1):
                        want = 1; break
                except tk.TclError:
                    pass
        if want != getattr(self, 'full_gap', 0):
            self.full_gap = want
            try:
                self.screen.pack_configure(pady=(0, want))
            except tk.TclError:
                pass
        if getattr(self, 'full_share_job', None):              # (one check running at a time)
            try: self.root.after_cancel(self.full_share_job)
            except tk.TclError: pass
        self.full_share_job = self.root.after(200, self.full_share) if self.full else None

    def pen_cursor(self):
        """the mouse pointer while the light pen is in port 2: the pen of its pictogram (vecrender LPEN_ICON), small,
        green with a dark edge, its tip the hot spot - a Windows .cur drawn once into the cache folder at the screen's
        scaling; elsewhere (or if that fails) Tk's own pencil"""
        if getattr(self, 'pen_cur', None):
            return self.pen_cur
        self.pen_cur = 'pencil'
        if not IS_WIN:
            return self.pen_cur
        try:
            from PIL import Image, ImageDraw
            n = max(32, min(96, int(round(32 * self.root.winfo_fpixels('1i') / 96.0))))   # (the pointer's size)
            ss = 4; N = n * ss
            img = Image.new('RGBA', (N, N), (0, 0, 0, 0)); d = ImageDraw.Draw(img)
            k = N / 104.0                                   # (the icon's 0..100 square, its tip near the corner)
            pts = lambda pl: [(x * k, (102 - y) * k) for x, y in pl]   # (turned up: the tip top left, the wire bottom right)
            for col, wid in (((5, 8, 10, 255), 3.2), ((102, 170, 136, 255), 1.3)):   # (the dark edge, then the green)
                for pl in vecrender.LPEN_ICON:
                    d.line(pts(pl), fill=col, width=max(1, int(round(wid * ss * n / 32.0))), joint='curve')
            img = img.resize((n, n), Image.LANCZOS)
            hx, hy = int(round(12 * k / ss)), int(round((102 - 88) * k / ss))   # (the hot spot: the pen's tip)
            px = img.tobytes('raw', 'BGRA')
            rows = [px[(n - 1 - y) * n * 4:(n - y) * n * 4] for y in range(n)]          # (bottom-up)
            mask_row = ((n + 31) // 32) * 4
            dib = struct.pack('<IiiHHIIiiII', 40, n, 2 * n, 1, 32, 0, n * n * 4 + mask_row * n, 0, 0, 0, 0)
            body = dib + b''.join(rows) + bytes(mask_row * n)
            head = struct.pack('<HHH', 0, 2, 1) + struct.pack('<BBBBHHII', n % 256, n % 256, 0, 0, hx, hy, len(body), 22)
            os.makedirs(CACHE_DIR, exist_ok=True)
            fn = os.path.join(CACHE_DIR, 'light_pen.cur')
            with open(fn, 'wb') as f:
                f.write(head + body)
            spec = '{@%s}' % fn.replace('\\', '/')
            self.root.configure(cursor=spec)                # (Tk can read it: else the pencil)
            self.pen_cur = spec
        except Exception:   # noqa
            self.pen_cur = 'pencil'
        return self.pen_cur
    def set_cursor(self, c):
        """the pointer over the window - set only when it changes (a configure() makes Tk redraw the frame)"""
        if getattr(self, 'cursor_now', '') != c:
            self.cursor_now = c
            for w in (self.root, self.screen):
                w.configure(cursor=c)

    def toggle_mouse_stick(self):
        """Vectrex > Mouse / touchpad steers player 1: the pointer's place on the picture is the stick (from the
        middle; a third of the picture's width away = all the way), left button = button 4, right = button 1"""
        self.mouse_stick = not self.mouse_stick
        self.mouse_btn = 0; self.inp.mouse = None
        self.save_settings()
        self.show_look('mouse / touchpad: ' + ('steers player 1 (left button: 4, right: 1)' if self.mouse_stick else 'off'))
    def usb_cart(self):
        """a cartridge with a USB port in the slot: a UVMC2 game or a PiTrex program"""
        return (self.args.rom or '').lower().endswith(('.um2', '.img', '.pit'))
    def usb_live(self):
        """the PC's mouse / keyboard go to the cartridge's USB port now (a UVMC2, a PiTrex; not in an online game)"""
        return (self.usb_hid and self.run.power and not self.browser and not self.run.paused and self.usb_cart()
                and not getattr(self, 'net_game', None))
    def card_dir(self, rom):
        """the SD card folder of a UVMC2 game / PiTrex program (None for other cartridges)"""
        r = (rom or '').lower()
        return self.sd_dir if r.endswith('.um2') else self.px_dir if r.endswith(('.img', '.pit')) else None
    # Tk key names -> USB HID usages (the base key: shifted symbols map to their key)
    HID_KEYS = dict(Return=0x28, KP_Enter=0x58, Escape=0x29, BackSpace=0x2A, Tab=0x2B, space=0x2C, minus=0x2D, underscore=0x2D,
                    equal=0x2E, plus=0x2E, bracketleft=0x2F, braceleft=0x2F, bracketright=0x30, braceright=0x30, backslash=0x31,
                    bar=0x31, semicolon=0x33, colon=0x33, apostrophe=0x34, quotedbl=0x34, grave=0x35, asciitilde=0x35,
                    comma=0x36, less=0x36, period=0x37, greater=0x37, slash=0x38, question=0x38, Caps_Lock=0x39,
                    Insert=0x49, Home=0x4A, Prior=0x4B, Delete=0x4C, End=0x4D, Next=0x4E, Right=0x4F, Left=0x50, Down=0x51, Up=0x52,
                    exclam=0x1E, at=0x1F, numbersign=0x20, dollar=0x21, percent=0x22, asciicircum=0x23, ampersand=0x24,
                    asterisk=0x25, parenleft=0x26, parenright=0x27)
    HID_MODS = dict(Control_L=1, Shift_L=2, Alt_L=4, Super_L=8, Win_L=8, Control_R=16, Shift_R=32, Alt_R=64, Super_R=128, Win_R=128)
    def hid_usage(self, ks):
        if len(ks) == 1 and ks.isalpha():
            return 0x04 + ord(ks.lower()) - ord('a')
        if len(ks) == 1 and ks.isdigit():
            return 0x27 if ks == '0' else 0x1E + int(ks) - 1
        if re.match(r'F([1-9]|1[0-2])$', ks):
            return 0x3A + int(ks[1:]) - 1
        return self.HID_KEYS.get(ks, 0)
    def toggle_usb_typing(self, e=None):
        """Ctrl+K (a UVMC2 game, a PiTrex program): the keyboard types on the cartridge's USB keyboard - the
        emulator's own keys and the controller keys rest until Ctrl+K again"""
        if not self.usb_typing and not (self.usb_hid and self.usb_cart()):
            self.show_look('Ctrl+K: only for a UVMC2 game or a PiTrex program (Cartridge tab: the USB keyboard and mouse)')
            return 'break'
        self.usb_typing = not self.usb_typing
        for w in (self.root, self.screen):
            tags = [x for x in w.bindtags() if x != 'usbtype']
            w.bindtags(tuple((['usbtype'] if self.usb_typing else []) + tags))
        self.inp.kbd_off = self.usb_typing
        self.usb_down = []; self.usb_mods = 0
        if not self.usb_typing:
            self.core.usb_key_report(0, [])
        what = 'the PiTrex' if (self.args.rom or '').lower().endswith(('.img', '.pit')) else 'the UVMC2'
        self.show_look('USB keyboard: ' + ('typing on %s (Ctrl+K: back to the controller keys)' % what if self.usb_typing else 'off'))
        return 'break'
    def toggle_usb_hid(self):
        """the control deck: a UVMC2 game or a PiTrex program gets the PC's mouse and keyboard as its USB devices
        (plugged in at the start: the cartridge in the slot starts again)"""
        self.usb_hid = not self.usb_hid
        self.save_settings()
        if self.usb_cart():
            self.restart(self.args.rom)
        self.show_look('USB keyboard and mouse (UVMC2, PiTrex): ' + ('plugged in' if self.usb_hid else 'unplugged'))
    def usb_typing_off(self):
        if getattr(self, 'usb_typing', False):
            self.toggle_usb_typing()
    def usb_key(self, e, down):
        """a key while typing on the UVMC2's USB keyboard: its report (modifiers and the last 3 keys held);
        Ctrl+K, F11 and Tab stay the emulator's"""
        ks = e.keysym
        if ks in ('F11',) or (ks.lower() == 'k' and e.state & 4):
            return None
        mod = self.HID_MODS.get(ks, 0)
        if mod:
            self.usb_mods = (self.usb_mods | mod) if down else (self.usb_mods & ~mod)
        else:
            u = self.hid_usage(ks)
            if not u:
                return 'break'
            if down and u not in self.usb_down:
                self.usb_down.append(u)
            elif not down and u in self.usb_down:
                self.usb_down.remove(u)
        if self.usb_live():
            self.core.usb_key_report(self.usb_mods, self.usb_down[-3:])
        return 'break'
    # Smooth motion (the control deck's Display tab): on a screen that is not 50 Hz (most are 60 Hz, without
    # G-Sync / FreeSync) every refresh gets a picture of its own - the vectors where they are between the two game
    # frames around that moment (one game frame later: ~20 ms more delay). The game runs at its own speed. Where
    # two frames do not match line for line (other lines, objects appearing), the earlier one is shown as is.
    SMOOTH_MATCH = 2500                                 # (a line moved by more: not the same line - 7 % of the screen)
    def smooth_on(self):
        return getattr(self, 'smooth', False) and not self.vrr and bool(self.ren) and hasattr(self.ren, 'present')
    def toggle_smooth(self):
        self.smooth = not self.smooth
        self.sm_buf = []
        self.save_settings()
        self.show_look('smooth motion: ' + ('on (vectors between game frames, every refresh)' if self.smooth else 'off'))
    def smooth_frame(self, r, T):
        """this refresh's picture: the game frames a and b around the moment shown, the vectors in between"""
        buf = self.sm_buf
        if len(buf) < 1:
            return
        per = (buf[-1][1] - buf[0][1]) / (len(buf) - 1) if len(buf) > 1 else 0.02
        per = min(0.1, max(0.01, per))
        D = T - self.pause_acc - per - 1.0 / max(1.0, self.hz)     # (one game frame and one refresh back)
        while len(buf) > 2 and buf[1][1] <= D:
            self.sm_prev = buf.pop(0)
        a = buf[0]
        if D < a[1] - 0.25:                              # (far behind: after a pause or a stall, start again)
            del buf[:-1]; a = buf[0]
        b = buf[1] if len(buf) > 1 and buf[1][1] > a[1] else None
        alpha = 0.0 if b is None else max(0.0, min(1.0, (D - a[1]) / (b[1] - a[1])))
        k = (1.0 / max(1.0, self.hz)) / per             # (the light of one refresh: the frame's timeline squeezed)
        la = a[0]
        if _np is not None:
            arr = lambda ls: ls.np if getattr(ls, 'np', None) is not None and len(ls.np) == len(ls) else ls   # (with the 3D
            A = _np.asarray(arr(la), dtype=_np.float64)                                                       #  Imager's colours)
            if A.ndim != 2 or not len(A):
                return
            A = A.copy()
            if b is not None and alpha > 0 and len(b[0]) == len(la):
                B = _np.asarray(arr(b[0]), dtype=_np.float64)
                if (_np.abs(B[:, :4] - A[:, :4]).max() < self.SMOOTH_MATCH) and (B[:, 4] == A[:, 4]).all():
                    A[:, :4] += (B[:, :4] - A[:, :4]) * alpha
            A[:, 5] *= k; A[:, 6] *= k
            lines = A
        else:
            match = b is not None and alpha > 0 and len(b[0]) == len(la) and all(
                p[4] == q[4] and max(abs(p[j] - q[j]) for j in range(4)) < self.SMOOTH_MATCH for p, q in zip(la, b[0]))
            lines = [((p[0] + (q[0] - p[0]) * alpha, p[1] + (q[1] - p[1]) * alpha, p[2] + (q[2] - p[2]) * alpha,
                       p[3] + (q[3] - p[3]) * alpha) if match else tuple(p[:4])) + (p[4], p[5] * k, p[6] * k)
                     for p, q in zip(la, b[0] if match else la)]
        # (a game that draws its picture in halves on alternate frames: both halves, as the tube shows them)
        if getattr(self, 'sm_alt_for', None) is not a:
            self.sm_alt_for = a
            self.sm_alt = vecrender.frame_alternates(self.sm_st, a[0])
        if self.sm_alt and getattr(self, 'sm_prev', None):
            pv = self.sm_prev[0]
            pv = pv.np if getattr(pv, 'np', None) is not None and len(pv.np) == len(pv) else pv     # (with its colours)
            P = _np.asarray(pv, dtype=_np.float64).copy() if _np is not None else None
            if P is not None and not isinstance(lines, list) and P.ndim == 2 and P.shape[1] != lines.shape[1]:
                w_ = min(P.shape[1], lines.shape[1]); P = P[:, :w_]; lines = lines[:, :w_]   # (the Imager plugged in or out)
            if P is not None and P.ndim == 2 and len(P):
                P[:, 5] *= k; P[:, 6] *= k
                lines = _np.concatenate([P, lines]) if _np is not None and not isinstance(lines, list) else lines
        r.frame_t0 = a[2] + ((b[2] - a[2]) * alpha if b is not None else 0.0)
        r.add_frame(lines, T - self.pause_acc - 1.0 / max(1.0, self.hz), 1.0 / max(1.0, self.hz), period=1.0 / max(1.0, self.hz))
    def pen_live(self):
        """the light pen in port 2, the game running: the mouse is the pen"""
        return (getattr(self, 'speech', 'off') == 'lightpen' and self.run.power and not self.browser
                and not self.run.paused and self.ren is not None and hasattr(self.ren, 'window_to_vectrex'))
    def pen_update(self, e=None, down=None):
        """the light pen to the core (imager.c lp_set): where the mouse points on the picture - held to the screen
        while the pointer is over the picture, as the manuals say (the buttons are the controller's: mouse_button)"""
        if e is not None:
            self.pen_xy = (e.x, e.y)
        xy = getattr(self, 'pen_xy', None)
        if not self.pen_live() or xy is None:
            if getattr(self, 'pen_sent', None) != 'off':
                self.core.set_analog(26, 0.0); self.pen_sent = 'off'
            return
        pl, pt, pr, pb = self.pic_rect()
        on = pl <= xy[0] <= pr and pt <= xy[1] <= pb
        x, y = self.ren.window_to_vectrex(xy[0], xy[1])
        if getattr(self.core, 'picture', None) is not None:     # (the size / centring pots moved the picture: the pen
            x, y = picture_undo(self.core.picture, x, y)        #  sees the spot where it is drawn)
        touch = 1.0 if on else 0.0
        self.core.set_analog(24, x); self.core.set_analog(25, y); self.core.set_analog(26, touch)
        self.pen_sent = touch
        self.pen_over = on
    def mouse_moved(self, e):
        if e is not None and not self.mouse_stick and self.usb_live():     # (a UVMC2: the USB mouse moves)
            p = (e.x_root, e.y_root)
            if self.usb_last:
                # (VecOS moves its pointer ~0.0015 picture heights per mouse step: scaled so it follows the PC's
                # pointer one to one, the fractions carried over)
                pl, pt, pr, pb = self.pic_rect()
                k = 1.0 / max(0.2, 0.0015 * (pb - pt))
                fx, fy = getattr(self, 'usb_frac', (0.0, 0.0))
                fx += (p[0] - self.usb_last[0]) * k; fy += (p[1] - self.usb_last[1]) * k
                dx, dy = int(fx), int(fy)
                self.usb_frac = (fx - dx, fy - dy)
                if dx or dy:
                    self.core.usb_mouse_move(self.usb_btn, dx, dy)
            self.usb_last = p
        elif e is None:
            self.usb_last = None
        if getattr(self, 'speech', 'off') == 'lightpen':        # (the light pen: the mouse is the pen)
            if e is None:
                self.pen_xy = None
            self.pen_update(self.px_event(e) if e is not None else None)
            return
        if not self.mouse_stick or not self.run.power or self.browser or e is None:
            self.inp.mouse = None if not getattr(self, 'mouse_btn', 0) else (0, 0, self.mouse_btn)
            return
        e = self.px_event(e)
        pl, pt, pr, pb = self.pic_rect()
        rad = (pr - pl) / 3.0
        f = lambda d: int(max(-127, min(127, 0 if abs(d) < 0.08 else d * 127)))
        self.inp.mouse = (f((e.x - (pl + pr) / 2.0) / rad), f(-(e.y - (pt + pb) / 2.0) / rad), getattr(self, 'mouse_btn', 0))
    def mouse_button(self, b, down):
        """a mouse button steering player 1: 1 = the Vectrex's button 4 (fire), 3 = button 1 - or, a UVMC2
        game, its USB mouse's buttons"""
        if getattr(self, 'speech', 'off') == 'lightpen':        # (the light pen: the mouse buttons are player 1's buttons
            bit = {1: 1, 3: 2, 2: 4}.get(b, 0)                  #  1, 2, 3 - Art Master: draw, straight line, menu)
            pb_ = getattr(self, 'pen_btn', 0)
            pb_ = (pb_ | bit) if (down and self.pen_live()) else (pb_ & ~bit)
            self.pen_btn = pb_
            self.inp.mouse = (0, 0, pb_) if pb_ else None
            return None
        if not self.mouse_stick and self.usb_live():
            bit = {1: 1, 3: 2, 2: 4}.get(b, 0)
            self.usb_btn = (self.usb_btn | bit) if down else (self.usb_btn & ~bit)
            self.core.usb_mouse_move(self.usb_btn)
            return None
        if not self.mouse_stick or not self.run.power or self.browser:
            return None
        bit = 8 if b == 1 else 1
        self.mouse_btn = (getattr(self, 'mouse_btn', 0) | bit) if down else (getattr(self, 'mouse_btn', 0) & ~bit)
        ms = self.inp.mouse or (0, 0, 0)
        self.inp.mouse = (ms[0], ms[1], self.mouse_btn)
        return None
    def full_menu(self, on):
        """full screen: the menu bar laid over the top of the picture (the mouse at the screen's top edge, or Alt) -
        hidden again when the mouse leaves it and no menu is open. The picture does not move"""
        if not (DARK_MENU and getattr(self, 'menubar', None)):
            return
        if on and self.full:
            if not getattr(self, 'full_menu_on', False):
                self.menubar.place(in_=self.root, x=0, y=0, relwidth=1.0)
                self.menu_line.place(in_=self.root, x=0, y=self.menubar.winfo_reqheight(), relwidth=1.0)
                self.menubar.lift(); self.menu_line.lift()
                self.full_menu_on = True
                self.root.after(400, self.full_menu_watch)
        elif getattr(self, 'full_menu_on', False):
            self.menubar.place_forget(); self.menu_line.place_forget()
            self.full_menu_on = False
    def full_menu_watch(self):
        """the full-screen menu bar: away once the mouse is well below it and no menu is open"""
        if not getattr(self, 'full_menu_on', False):
            return
        try:
            y = self.root.winfo_pointery() - self.root.winfo_rooty()
            away = y > self.menubar.winfo_height() + 40 and getattr(self.menubar, 'open', None) is None
        except tk.TclError:
            away = True
        if away or not self.full:
            self.full_menu(False)
        else:
            self.root.after(300, self.full_menu_watch)
    def pointer_moved(self, e=None):
        """the mouse pointer: in full screen it hides 2 s after it last moved (a hand on the volume knob)"""
        if self.full and e is not None and DARK_MENU:            # (full screen: the menu bar at the top edge)
            try:
                if e.y_root - self.root.winfo_rooty() <= 4:
                    self.full_menu(True)
            except (tk.TclError, AttributeError):
                pass
        self.set_cursor('hand2' if getattr(self, 'hand_hover', False) or getattr(self, 'kdrag', None) else
                        self.pen_cursor() if self.pen_live() else '')   # (the light pen: a pen, its tip the hot spot)
        if getattr(self, 'hide_job', None):
            self.root.after_cancel(self.hide_job); self.hide_job = None
        if self.full:
            self.hide_job = self.root.after(2000, self.hide_pointer)
    def hide_pointer(self):
        self.hide_job = None
        if self.full and not self.pen_live():               # (the light pen: its pointer stays)
            self.set_cursor('none')

    def draw(self, lines):
        self.lines = lines
        if self.ren:
            if self.run.power:
                self.crt_fx()
            self.ren.frame_no = self.run.frames
            self.ren.frame_t0 = getattr(self.run, 'latest_t0', 0)
            return self.ren.draw(lines)
        # Tk canvas fallback: one Tcl script per frame (hundreds of single Tk calls from Python are too slow)
        sx, sy = self.W / ALG_MAX_X, self.H / ALG_MAX_Y
        cv = self.cv; w = cv._w
        while len(self.items) < len(lines):
            self.items.append(cv.create_line(0, 0, 0, 0, fill='#000', width=max(1, round(self.k * 1.3)),
                                             capstyle='round'))
            self.item_col.append(None)
        cmd = []
        col = self.item_col; colors = self.colors; items = self.items
        for i, ln in enumerate(lines):
            x0, y0, x1, y1, c = ln[:5]                          # (a line: also its energy and start cycle)
            X0, Y0, X1, Y1 = x0 * sx, y0 * sy, x1 * sx, y1 * sy
            if abs(X1 - X0) < 1 and abs(Y1 - Y0) < 1:
                X1 += 1.5; Y1 += 0.5          # a dot
            it = items[i]
            cmd.append('%s coords %d %.1f %.1f %.1f %.1f' % (w, it, X0, Y0, X1, Y1))
            if col[i] != c:
                cmd.append('%s itemconfigure %d -fill %s -state normal' % (w, it, colors[min(127, c)]))
                col[i] = c
        for i in range(len(lines), self.shown):
            cmd.append('%s itemconfigure %d -state hidden' % (w, items[i]))
            col[i] = None
        self.shown = len(lines)
        cv.tk.eval('\n'.join(cmd))

    def gc_tick(self):
        """the cycle collector, every 2 s on the Tk thread (see gc.disable above): young objects each time, all every
        30 s"""
        self.gc_n = getattr(self, 'gc_n', 0) + 1
        gc.collect(2 if self.gc_n % 15 == 0 else 1)
        try:
            self.perf_tick()
        except Exception:   # noqa: a diagnostic must never stop the display loop
            pass
        self.root.after(2000, self.gc_tick)
    def perf_tick(self):
        """every 2 s: the graphics card's and the processor's time per picture. Over 25 ms (or the emulation below
        90 %) for 6 s while a game runs (not on Low): said once - the PC may not carry these effects (Quality >
        Optimize). Every 10 s a line in cache/render_log.txt:
        the graphics card, the picture's size, the quality, pictures a second, the card's and the processor's time
        per picture - to send along when the picture is slow"""
        now = time.perf_counter()
        n = sum(1 for t_ in self.presents if t_ > now - 2.0)
        fps = n / 2.0
        r = self.ren
        running = self.run.power and not self.run.paused and not self.browser
        # slow: this PC's own time per picture, not the pictures a second - a game drawing 25 frames a second (Web
        # Wars; with G-Sync every game frame is one picture) is not a slow PC (the owner's RTX 5090, 2026-10-26)
        g_ms, c_ms = getattr(r, 'gpu_ms', None) or 0.0, getattr(r, 'cpu_ms', None) or 0.0
        heavy = g_ms > 25.0 or c_ms > 25.0 or getattr(self, 'speed', 100.0) < 90.0
        slow = running and heavy and r is not None and self.current_profile() != 'low'
        self.slow_n = getattr(self, 'slow_n', 0) + 1 if slow else 0
        if self.slow_n == 3 and not getattr(self, 'slow_told', False):
            self.slow_told = True
            self.flash('SLOW PICTURE: %d FPS' % round(fps), 'QUALITY > OPTIMIZE', secs=6.0)
            self.show_look('this PC needs %.0f ms a picture (%d FPS): View > Quality > Optimize finds what it can carry '
                           '(or choose Low)' % (max(g_ms, c_ms), round(fps)))
        self.perf_n = getattr(self, 'perf_n', 0) + 1
        if self.perf_n % 5 == 1 and r is not None:
            try:
                os.makedirs(CACHE_DIR, exist_ok=True)
                fn = os.path.join(CACHE_DIR, 'render_log.txt')
                new = not getattr(self, 'perf_log_started', False)
                self.perf_log_started = True
                with open(fn, 'w' if new else 'a', encoding='utf-8') as f:
                    if new:
                        gpu = '?'
                        try:
                            gl = r.gl; gl.glGetString.restype = ctypes.c_char_p
                            gpu = ' / '.join((gl.glGetString(k) or b'?').decode(errors='replace') for k in (0x1F00, 0x1F01, 0x1F02))
                        except Exception:   # noqa
                            pass
                        f.write('%s  %s\ngraphics: %s\n' % (datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'), platform.platform(), gpu))
                    sz = getattr(r, 'size', (0, 0))
                    on = [k for k in getattr(r, 'PROFILE_KEYS', ()) if getattr(r, k, None)]
                    f.write('%s  %s  picture %dx%d  quality %s  overlay %s  %.0f fps  card %s ms  processor %s ms  emu %.0f %%  on: %s\n' % (
                        datetime.datetime.now().strftime('%H:%M:%S'), os.path.basename(self.args.rom or 'Mine Storm'),
                        sz[0], sz[1], self.current_profile(), 'on' if (getattr(r, 'art_on', False) and getattr(r, 'art', None)) else 'off',
                        fps, ('%.1f' % r.gpu_ms) if getattr(r, 'gpu_ms', None) else '-',
                        ('%.1f' % r.cpu_ms) if getattr(r, 'cpu_ms', None) else '-', getattr(self, 'speed', 0.0), ' '.join(on)))
            except OSError:
                pass
    def tick(self):
        if self.win_q and self.win_events():
            return                                              # (closed)
        if getattr(self.core, 'msgs', None):                    # (the cartridge's own messages: shown once)
            m = self.core.msgs.pop(0)
            self.root.after(0, lambda m=m: self.cart_message(m))
        if self.run.error:
            self.status.configure(text='ERROR: ' + self.run.error, fg='#f66'); self.status.pack(side='top', fill='x')
            self.show_bar()
            return
        if self.ren and hasattr(self.ren, 'present'):
            if bool(getattr(self.ren, 'half_rate', False)) != getattr(self, '_hr_applied', False):
                self.apply_rate()                               # (half_rate changed by hand: the rate follows)
            try:
                return self.tick_timed()
            except Exception:   # noqa: one bad screen frame must not stop the display loop - logged, on with the next
                import traceback
                if not getattr(self, 'tick_errs', 0) or TEST_LOG:
                    traceback.print_exc()
                self.tick_errs = getattr(self, 'tick_errs', 0) + 1
                self.root.after(5, self.tick)
                return None
        with self.run.lock:
            lines, self.run.latest = self.run.latest, None
        if not self.run.power:                                  # power off: the tube collapses, fades
            now = time.perf_counter()
            if now - getattr(self, 'off_t', 0) >= 1 / 60.0 - 0.001:   # (60 Hz: no game frames to follow)
                self.off_t = now
                if self.ren:
                    self.ren.draw(self.crt_fx() or [])
        if lines is not None:
            self.draw(lines)
        self.status_line()
        self.root.after(1, self.tick)

    def tick_timed(self):
        """one display frame: take the new game frames, show the phosphor as it is now - at a fixed
        60 Hz, or (G-Sync / FreeSync) once per game frame, the moment it is drawn"""
        r = self.ren
        vrr = self.vrr
        idle = self.run.paused or (vrr and not self.run.power)  # (no game frames to wait for)
        if idle or (not self.vsync and not vrr):                # paced by the clock: wait for the moment
            wait = self.next_present - time.perf_counter()
            if wait > 0.0025:
                self.root.after(max(1, int(wait * 1000) - 2), self.tick)
                return
            while time.perf_counter() < self.next_present:
                pass
        elif self.vsync and not vrr and getattr(self, 'vbl', 0.0):
            # vsync: sleep until shortly before the refresh this picture goes out with, instead of waiting in the
            # graphics driver (glFinish spins a whole core until the refresh); the margin is the drawing time seen
            # lately x1.5 plus 2.5 ms (measured: no refresh missed in 830 frames, Doom too)
            due = self.vbl + self.vsync / float(self.refresh if getattr(self, 'refresh', 0) >= 50 else 60)
            wait = due - time.perf_counter() - 0.0025 - 1.5 * getattr(self, 'draw_cost', 0.002)
            if wait > 0.0015:
                self.root.after(max(1, int(wait * 1000)), self.tick)
                return
        t_work = time.perf_counter()                            # (the work before the refresh: frames + drawing)
        with self.run.lock:
            frames = list(self.run.queue); self.run.queue.clear()
            self.run.latest = None
        smooth = self.smooth_on()
        for lines, t_start, t0, ln in frames:
            self.lines = lines
            if self.run.power:
                self.crt_fx()
            r.frame_no = self.run.frames; r.frame_t0 = t0
            if smooth:
                self.sm_buf.append((lines, t_start - self.pause_acc, t0))
                continue
            r.add_frame(lines, t_start - self.pause_acc, ln)
        now = time.perf_counter()
        if self.run.paused:
            # paused: the picture stands still (the phosphor clock stopped when it was paused), redrawn
            # PAUSE_HZ times a second - a window that was covered is painted again, the knob turns
            show = now >= self.next_present
            T = getattr(self, 'pause_t', now)
        elif vrr:
            # a game frame: shown at once, its lines all drawn (the phosphor clock at the frame's end);
            # switched off, or no game frame for 50 ms (the intro skipped at power on: ~0.8 s without frames): 60 Hz
            # by the clock - the knob, the OSD keep moving
            stalled = not self.presents or now - self.presents[-1] > 0.05
            show = bool(frames) or ((not self.run.power or stalled) and now >= self.next_present)
            if (show and frames and getattr(r, 'half_rate', False) and self.run.power and self.presents
                    and now - self.presents[-1] < 0.034):
                show = False                                    # (Quality Low: every 2nd game frame a picture - 25 a
                                                                #  second; its lines are in the phosphor already)
            T = frames[-1][1] + frames[-1][3] if frames else now
            if frames:
                self.frame_due = T                              # (the next game frame: about then)
        else:
            show = True
            T = now
        if show:
            imm = None
            if not self.run.power:                              # power off: the tube collapses, fades
                imm = self.crt_fx() or []
            r.hud = None                                        # (the OSD: below the picture, osd_state)
            r.knob = self.knob_state()
            r.osd = self.osd_state()
            r.cart = self.cart_state()
            r.port2 = self.port2_state()
            r.browser = self.browser_state()
            r.hint = self.hint_state()
            if smooth and not self.run.paused:
                self.smooth_frame(r, T)
            flush, self.flush = getattr(self, 'flush', False), False
            if flush:
                self.sm_buf = []
            r.present(T - self.pause_acc, imm, flush)
            if not getattr(self, 'gl_shown', False):            # (the first picture: OpenGL paints it all now)
                self.gl_shown = True
                self.screen.configure(bg='')
                self.root.after(30, self.root_visible)
            if self.vsync and not vrr:
                t_fin = time.perf_counter()
                self.draw_cost = max(t_fin - t_work, 0.9 * getattr(self, 'draw_cost', 0.002) + 0.1 * (t_fin - t_work))
                # the picture is handed in; the refresh it goes out with is about `due`: sleep until just before it
                # (glFinish spins a whole core while it waits - 3.5 ms of every 16.7 on this PC's driver, measured
                # 2026-10-27, and on a slow laptop that spin starves the emulation thread and the core), then
                # glFinish for the last fraction of a millisecond
                if getattr(self, 'vbl', 0.0):
                    due = self.vbl + self.vsync / float(self.refresh if getattr(self, 'refresh', 0) >= 50 else 60)
                    while True:
                        rem = due - time.perf_counter() - self.VBL_MARGIN
                        if rem <= 0.0002:
                            break
                        time.sleep(min(rem, 0.001))
                    t_fin = time.perf_counter()
                r.gl.glFinish()                                 # (wait for the refresh: the pace. Tried instead 2026-10-27:
                # DwmFlush - a little less CPU on light games in a window, but 13-16 refreshes missed in 15 s of VecDoom and
                # 2-3 in full screen; D3DKMTWaitForVerticalBlankEvent then glFinish - 80-100 missed; neither saved CPU
                # under load. This sleep-then-glFinish keeps every refresh: 0-2 missed in 900)
                now_f = time.perf_counter()
                # (the refresh: when glFinish came back after a wait - a frame that came back at once missed it)
                self.vbl = now_f if now_f - t_fin > 0.0005 or not getattr(self, 'vbl', 0.0) else                     self.vbl + self.vsync / float(self.refresh if getattr(self, 'refresh', 0) >= 50 else 60)
            self.presents.append(time.perf_counter())
            if self.run.paused:
                self.next_present += 1.0 / PAUSE_HZ
            elif not vrr or not frames:
                self.next_present += 1.0 / self.hz
        now = time.perf_counter()
        if self.next_present < now - 0.05:
            self.next_present = now
        self.status_line()
        # G-Sync, running: sleep until just before the next game frame is due (the emulation thread
        # hands it over at about the end of the last one), then look every millisecond - not a
        # thousand wake-ups a second for ~50 frames; late (a stalled game): every 5 ms
        due = getattr(self, 'frame_due', 0.0) if vrr and not idle else 0.0
        if due and due - now > 0.004:
            self.root.after(int((due - now) * 1000) - 2, self.tick)
        else:
            self.root.after(5 if vrr and not idle and now - due > 0.05 else 1, self.tick)

    # the emulation speed: emulated time against real time (100 % = as fast as a real Vectrex)
    VBL_MARGIN = 0.0007              # s before the expected refresh at which glFinish takes over from sleeping (tick_timed)
    SPEED_COLORS = ((99.0, '#6a8', (0.40, 0.667, 0.533)), (90.0, '#e93', (0.93, 0.60, 0.20)), (-1.0, '#f44', (1.0, 0.27, 0.27)))
    def speed_color(self, k):
        return next(c[k] for c in self.SPEED_COLORS if self.speed >= c[0])
    def hud_text(self):
        """the OSD, one value per row: the game's frame rate, the monitor's refresh, the emulation rate
        (coloured) and, online, the connection (in the row the volume had: the OSD keeps its height over the overlay;
        the volume shows on the knob and on the status line when it is turned)"""
        g = (0.40, 0.667, 0.533)
        idle = self.run.paused or not self.run.power or self.run.frames < 50   # (a new game's first second: --)
        vals = [('INTERNAL', '%d FPS' % round(self.fps), g), ('MONITOR', '%d HZ' % round(self.disp_fps), g),
                ('EMU RATE', '--' if idle else '%d%%' % self.speed_shown(), g if idle else self.speed_color(2))]
        # (the same rows for every system - a UVMC2 or a PiTrex too: the CPU / GPU meters below them tell the load)
        if getattr(self, 'net_osd', True) and self.net_busy():  # (online: the other player's state)
            ng, net = getattr(self, 'net_game', None), getattr(self, 'net', None)
            if ng and net is not None:
                vals.append(('ONLINE', '%s%d MS' % ('RELAY ' if getattr(net, 'relay', False) else '', ng.get('rtt_ms', 0)), g))
            else:
                vals.append(('ONLINE', 'WAITING', (0.93, 0.60, 0.20)))
        width = max(len(n) + 2 + len(v) for n, v, _ in vals)   # (the values end in one column)
        return [[(n + ':' + ' ' * (width - len(n) - 1 - len(v)), g), (v, c)] for n, v, c in vals]
    def speed_shown(self):
        return 100 if self.speed >= 99.5 else int(self.speed)

    def status_line(self):
        now = time.perf_counter()
        if now - self.last_t > 1:
            self.fps = (self.run.dframes - self.last_f) / (now - self.last_t)
            self.game_fps = (self.run.gframes - getattr(self, 'last_g', 0)) / (now - self.last_t)
            p = [t for t in self.presents if t > self.last_t]
            self.disp_fps = len(p) / (now - self.last_t)
            self.last_t, self.last_f, self.last_g = now, self.run.dframes, self.run.gframes
            m, m0 = self.run.emu_mark, getattr(self, 'emu_mark0', None)
            if self.run.frames < 50:                            # (a new game's first second: the core starting - and a
                m = m0 = None; self.speed = 100.0               #  UVMC2's SD card - is not the emulation falling behind;
                                                                #  measured from the second second on)
            if m and m0 and m[0] > m0[0] and m[1] >= m0[1]:      # (frame completions a second apart)
                self.speed = 100.0 * (m[1] - m0[1]) / (now - m0[0]) if now - m[0] > 0.25 else \
                             100.0 * (m[1] - m0[1]) / (m[0] - m0[0])
            elif m0 and (not m or m[0] <= m0[0]) and self.run.power and not self.run.paused:
                self.speed = 0.0                                # (no frame for a second: stalled)
            self.emu_mark0 = m
            for name, v in zip(('CPU', 'GPU'), self.headroom()):    # (the OSD meters: a value a second, 10 seconds)
                h = self.head_hist[name]
                h.append(None if v is None or self.run.paused or not self.run.power else v)
                del h[:-10]
            if TEST_LOG: print('speed %.2f %%  game %.1f fps  screen %.1f Hz' % (self.speed, self.fps, self.disp_fps), flush=True)
            idle = self.run.paused or not self.run.power
            self.num_a.configure(text='%d FPS  %d HZ  G-SYNC %s  ' % (round(self.fps), round(self.disp_fps),
                                                                      'ON' if self.vrr else 'OFF'))
            self.num_speed.configure(text='--' if idle else '%d%%' % self.speed_shown(),
                                     fg='#6a8' if idle else self.speed_color(1))
            vol = '' if self.knob_on else ('  VOL %d%%' % round(self.run.crt.volume * 100) if self.run.power else '  OFF')
            self.num_b.configure(text=vol)
            items = re.split(r'\s{2,}', 'look: ' + self.look_text().strip())   # (wraps between items only:
            self.look_lbl.configure(text='   '.join(i.replace(' ', '\u00a0') for i in items))   #  no-break spaces inside)

    def quit(self):
        self.quitting = True
        if getattr(self, 'net', None) is not None:              # (online: the other side told; a frame waiting for it ends)
            self.net.lost = True; self.net.close()
        self.run.stop = True
        self.run.join(timeout=2)
        self.save_settings()
        size = self.ren.size if self.ren else None
        if TEST_LOG and self.ren and hasattr(self.ren, 'map'):  # (test: the layout)
            print('layout: window %s  picture %s  knob %s  osd %s' % (self.ren.size, tuple(round(v) for v in self.pic_rect()),
                  tuple(round(v) for v in self.knob_geom()), tuple(round(v) for v in self.osd_state()['area']) if self.osd_state() else None))
        if self.ren:
            self.ren.close(); self.ren = None
        self.core.close()
        self.audio.close()
        self.root.destroy()
        p = list(self.presents)
        if len(p) > 60:
            dt = sorted((b - a) * 1000 for a, b in zip(p, p[1:]))
            print('screen frames %d (vsync %d, refresh %s Hz): spacing ms median %.2f  p1 %.2f  p99 %.2f  max %.2f'
                  % (len(p), self.vsync, getattr(self, 'refresh', '?'), dt[len(dt) // 2], dt[len(dt) // 100],
                     dt[len(dt) * 99 // 100], dt[-1]))
        print('render size', size, 'full screen', self.full)
        print('game frames %d, late %d, audio %s, underruns %d'
              % (self.run.frames, self.run.slow, self.audio.ok, self.audio.underruns))

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', nargs='?', default=None, help='cartridge ROM (none: Mine Storm, the built-in game)')
    ap.add_argument('--height-mm', type=float, default=0,
                    help='picture height in mm on your monitor (e.g. 190 = the real Vectrex picture); '
                         'default: as tall as the screen, then the size you last used')
    ap.add_argument('--scale', type=float, default=1.0, help='start size relative to that')
    ap.add_argument('--tk', action='store_true', help='plain Tk canvas (no anti-aliasing)')
    ap.add_argument('--fullscreen', action='store_true', help='start in full screen (F11)')
    ap.add_argument('--ideal', action='store_true',
                    help='a perfectly trimmed Vectrex (default: the drift of a real one)')
    ap.add_argument('--demo', type=float, default=0, help='test: scripted input, quit after N seconds')
    ap.add_argument('--net-host', nargs='?', type=int, const=7744, default=None, metavar='PORT',
                    help='online play (a prototype): host this game for a second player (UDP port, default 7744)')
    ap.add_argument('--net-join', default=None, metavar='ADDRESS[:PORT]|CODE',
                    help='online play (a prototype): join a game hosted at this address, or with the host\'s five-letter code, as player 2')
    ap.add_argument('--net-online', action='store_true',
                    help='online play (a prototype): host this game through the meeting point - you get a code for player 2')
    args = ap.parse_args()
    vecrender.set_dpi_aware()
    # 1 ms Windows timer resolution while playing (Tk's polling and the sleeps would otherwise wake
    # up on the 15.6 ms default tick, and frames would reach the screen unevenly: with G-Sync each
    # frame is shown the moment it is handed over)
    try:
        ctypes.windll.winmm.timeBeginPeriod(1)
    except Exception:       # noqa
        pass
    try:
        if not (os.path.exists(CORE) and os.path.exists(BIOS)):
            from tkinter import messagebox
            r = tk.Tk(); r.withdraw()
            missing = [f for f in (CORE, BIOS) if not os.path.exists(f)]
            msg = 'Missing:\n' + '\n'.join(missing)
            if BIOS in missing:
                msg += '\n\nThe Vectrex BIOS goes in a folder called system next to the program: system/bios.bin.'
                if not IS_WIN and osdep.translocated(ROOT):
                    msg += ('\n\nmacOS runs this downloaded app from a hidden copy, which can not see that folder. '
                            'In Finder, drag Vectrex_EMU.app out of its folder and back in, then start it again '
                            '(macOS then runs it where it is).')
            messagebox.showerror('Vectrex_EMU', msg); return
        App(args).root.mainloop()
    finally:
        try:
            ctypes.windll.winmm.timeEndPeriod(1)
        except Exception:   # noqa
            pass

if __name__ == '__main__':
    main()
