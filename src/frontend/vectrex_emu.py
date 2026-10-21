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
Controls (Vectrex > Controls): each player's controller (the connected ones, by name) and keys - both
  work at once; each controller keeps its own axes, buttons and dead zone; a test field per player.
  Defaults: player 1 arrows + A W S D (buttons 1-4), Shift half stick, and the first controller found
  (its X / Y axes, buttons 1-4; an Xbox pad: the left stick, X Y A B); player 2 keyboard, no keys.
Settings are kept in Vectrex_EMU.conf next to the exe (or in %APPDATA%\Vectrex_EMU if that folder
is read-only). The screen is shown at a fixed 60 Hz whatever the game's frame rate: every line lights
the phosphor at the moment the beam drew it and the spot fades from there to black, so afterglow and
trails move smoothly (vsync on a 60 / 120 / 240 Hz monitor, else paced to 60 Hz - G-Sync shows it as is).
"""
import argparse, ctypes, ctypes.wintypes as W, json, math, os, queue, random, shutil, struct, subprocess
import sys, threading, time, datetime, collections, re
import tkinter as tk
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vecrender
import vart

IS_WIN, IS_MAC = sys.platform == 'win32', sys.platform == 'darwin'
if not IS_WIN:                                          # macOS / Linux: the window, keys, controllers, sound
    import osdep
if getattr(sys, 'frozen', False):                       # Vectrex_EMU.exe (PyInstaller; macOS: next to Vectrex_EMU.app)
    ROOT = os.path.dirname(os.path.abspath(sys.executable)) if IS_WIN else osdep.app_root(sys.executable)
else:                                                   # src\frontend\vectrex_emu.py
    ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
# the core, the BIOS and the power sounds travel inside Vectrex_EMU.exe (unpacked to its temp folder at
# start); from the sources they come from the project folders
RES = getattr(sys, '_MEIPASS', ROOT)
CORE = os.path.join(RES, 'core', 'vectrex_core.exe' if IS_WIN else 'vectrex_core')
BIOS = os.path.join(RES, 'bios', 'bios.bin')
if not os.path.exists(BIOS):                            # (macOS / Linux: the bios folder next to the program)
    BIOS = os.path.join(ROOT, 'bios', 'bios.bin')
# the VecVoice's speech ROM (SP0256-AL2, (c) Microchip Technology Inc.): not shipped - the user downloads
# it from Joe Zbiciak's page (Microchip let him distribute it) into the bios folder next to the program
SP_ROM_URL = 'http://spatula-city.org/~im14u2c/sp0256-al2/'
def sp_rom():
    for d in (os.path.join(ROOT, 'bios'), os.path.join(RES, 'bios')):
        for n in ('sp0256-al2.bin', 'al2.bin'):
            if os.path.exists(os.path.join(d, n)):
                return os.path.join(d, n)
    return None
def _writable(d):
    try:
        t = os.path.join(d, '.write_test'); open(t, 'w').close(); os.remove(t); return True
    except OSError:
        return False
# Vectrex_EMU.conf (the settings, JSON) lives next to the exe (portable: copy the folder and the settings come along); a
# folder that cannot be written (e.g. Program Files) falls back to %APPDATA%\Vectrex_EMU
APPDATA_DIR = os.path.join(os.environ.get('APPDATA', ROOT), 'Vectrex_EMU') if IS_WIN else osdep.settings_dir()
SETTINGS_DIR = ROOT if _writable(ROOT) else APPDATA_DIR
NUL = 'NUL' if IS_WIN else os.devnull      # the core's input recording and an empty cartridge slot: nothing is written or kept
ALG_MAX_X, ALG_MAX_Y = 33000, 41000
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
                           b1=[(0x41, 'A')], b2=[(0x57, 'W')], b3=[(0x53, 'S')], b4=[(0x44, 'D')],
                           half=[(0x10, 'Shift')]),
                'p2': {k: [] for k in CTRL_INPUTS}}
DEFAULT_DEADZONE = 0.2
# keys the emulator keeps for itself (a player can not have them): F11, Esc, Page Up / Down, + - 0 9 (the
# window size, the real size), P L O (pause, the ROM browser, the overlay) and everything with Ctrl
RESERVED_VK = {0x7A: 'F11', 0x1B: 'Esc', 0x21: 'Page Up', 0x22: 'Page Down', 0x11: 'Ctrl', 0xA2: 'Ctrl', 0xA3: 'Ctrl',
               0xBB: '+', 0xBD: '-', 0x30: '0', 0x39: '9', 0x6B: 'Numpad +', 0x6D: 'Numpad -', 0x60: 'Numpad 0',
               0x69: 'Numpad 9', 0x50: 'P', 0x4C: 'L', 0x4F: 'O'}

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
    Controls window and kept in Vectrex_EMU.conf."""
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
    def keydown(self, vk):
        if not IS_WIN:
            return self.focused and osdep.KEYS.held(vk)
        return self.focused and bool(self.user32.GetAsyncKeyState(vk) & 0x8000)
    def read_player(self, p):
        """(jx, jy, buttons, the controller's stick before the dead zone or None). The keyboard and the
        controller work at once: the buttons add up, a direction key overrides the stick on its axis."""
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
        kx = (1.0 if down('right') else 0.0) - (1.0 if down('left') else 0.0)
        ky = (1.0 if down('up') else 0.0) - (1.0 if down('down') else 0.0)
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
        self.buzz_on = True; self.buzz_vol = 0.152      # (the owner's choice, 2026-10-19; the buzz does not follow the volume knob)
        self.speaker_on = True                           # the Vectrex's small speaker (see below)
        self.volume = 1.0                                # the volume knob (Page Up / Page Down)
        # the speech module (VecVoice / VecVox): its own amplifier and audio out - its own volume (Ctrl+Page
        # Up / Down) and the speakers owners plug into it: full range (as it comes) or a small speaker
        self.voice_vol = 1.0                             # (100 % = the MiSTer core's Normal level)
        self.voice_full = True
        self.voice_spk = [Biquad('hp', 140, 0.7), Biquad('pk', 1800, 1.0, 2.0), Biquad('lp', 7500, 0.7)]
        # full-range speakers: an arcade cabinet's warm bass (+6 dB below ~150 Hz; no rumble under 40 Hz)
        self.voice_bass = [Biquad('hp', 40, 0.7), Biquad('ls', 150, 0.7, 6.0)]
        self.scruff_until = 0.0                          # turning the knob: a dirty potentiometer crackles
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
    def process(self, pcm, lines, speech=None):
        if speech is None and not ((self.buzz_on and self.buzz_vol > 0) or self.speaker_on or self.overlay or self.dead
                or self.volume != 1.0 or time.perf_counter() < self.scruff_until):
            return pcm
        from array import array
        a = array('h'); a.frombytes(pcm)
        n = len(a)
        if not n:
            return pcm
        out = [float(v) for v in a]
        self.knob(out)                                          # the volume knob: the game sound only (the
        if self.buzz_on and self.buzz_vol > 0:                  # buzz goes on, even on a dead spot)
            act = [0.0] * n
            k = RATE / 1.5e6                                    # samples per CPU cycle
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
           'hb512': '512 KB flash with bank latch (HB v2 board)', 'multi': '1 MB multi-cart (4 KB pages, latch at $7FFF)'}

# ------------------------------------------------------------------ overlays
# The screen overlays: MiSTer .art files named like the ROM, in roms/overlays (or overlays next to the
# program); the ROM browser does not list that folder. Rotated packages (_90CW / _90CCW) are not needed:
# the emulator turns the whole screen, the overlay with it.
def overlay_dirs():
    return [os.path.join(ROOT, 'roms', 'overlays'), os.path.join(ROOT, 'overlays')]

def builtin_overlay():
    """the one overlay that comes with the emulator: Mine Storm's, for the console's own game (bios/bios.art,
    inside the exe or next to the program)"""
    for d in (RES, ROOT):
        p = os.path.join(d, 'bios', 'bios.art')
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
PORT2 = {'off': 'Joystick', 'vecvoice': 'VecVoice speech (SP0256)', 'vecvox': 'VecVox speech (SpeakJet)'}

# ------------------------------------------------------------------ emulator link
class Core:
    def __init__(self, rom, ideal=False, cart_hw='auto', speech='off'):
        env = dict(os.environ)
        env.update({'VT_ZIDEAL': '1'} if ideal else {'VT_ZREFK': '0.38', 'VT_CALSHIFT': '2.9'})  # perfect / a real Vectrex
        # cartridge hardware: auto = the board by the ROM's size (64K PB6, Vectorblade's PB6 + IRQ with its
        # flash, the 512K latch flash, Sean Kelly's Multi-Cart latch), or one board by name; off = a
        # bone-stock Vectrex with a plain 32K cartridge
        env['VT_CARTHW'] = {'auto': '1', 'off': '0'}.get(cart_hw, cart_hw)
        env['VT_POWERSEED'] = str(random.getrandbits(31))         # (the RAM's random state at every power-on)
        if speech != 'off' and sp_rom():
            env['VT_VECVOICE'] = sp_rom()                           # (a speech accessory on port 2)
            env['VT_SPEECHDEV'] = speech
            env['VT_SPEECHOUT'] = '1'                               # (its sound on a channel of its own)
        # no input recording, no log: the core records to NUL, its messages are discarded
        self.rom = rom
        self.p = subprocess.Popen([CORE, BIOS, rom, '--live', NUL], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, env=env, bufsize=0,
                                  creationflags=0x08000000 if IS_WIN else 0)            # CREATE_NO_WINDOW
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
        self.gap = cycles
    def frame(self, jx, jy, btn, flags, p2=None):
        """one game frame: player 1's stick and buttons, the flags, player 2's (the 2nd port: flags bit 6),
        and a changed setting (flags bit 5: the stop delay, 1/100 cycles)"""
        g, self.gap = self.gap, None
        extra = b''
        if g is not None:
            v = max(0, min(4000, int(round(g * 100)))); flags |= 0x20; extra = bytes([v & 255, v >> 8])
        if p2:
            self.p.stdin.write(bytes([jx + 128, jy + 128, btn, flags | 0x40, p2[0] + 128, p2[1] + 128, p2[2]]) + extra)
        else:
            self.p.stdin.write(bytes([jx + 128, jy + 128, btn, flags]) + extra)
        h = self._read(10)
        if h[:2] != b'VF':
            raise IOError('bad frame header %r' % h)
        n, ns, ft = struct.unpack('<HHI', h[2:])
        if ft < self.ft_last: self.ft_hi += 1 << 32                   # (32-bit cycle counter wraps)
        self.ft_last = ft; self.frame_t0 = self.ft_hi + ft
        two = ns & 0x8000; ns &= 0x7fff                # (bit 15: two channels - the console, the speech module)
        body = self._read(n * 13 + ns * (4 if two else 2))             # 16-bit samples
        lines = [struct.unpack_from('<HHHHBHH', body, i * 13) for i in range(n)]  # + energy, start cycle
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
        except subprocess.TimeoutExpired:
            self.p.kill()
        return ''

# ------------------------------------------------------------------ the game loop (worker thread)
TEST_LOG = bool(os.environ.get('VECTREX_LOG_SPEED'))                  # (test: print the speed meter)
TEST_LOAD = float(os.environ.get('VECTREX_TEST_LOAD', 0)) / 1000.0   # (test: extra ms per frame)
PAUSE_HZ = 60.0                  # paused: the still picture is redrawn this often (a covered window is painted again, the knob turns smoothly)

class Runner(threading.Thread):
    def __init__(self, core, inp, audio, inf):
        super().__init__(daemon=True)
        self.core, self.inp, self.audio = core, inp, audio
        self.inf = inf; self.paused = False; self.stop = False; self.demo = False
        self.slow = 0
        self.latest = None; self.frames = 0; self.error = None
        self.pend = None; self.dframes = 0              # (screen frames: see the queue)
        self.queue = collections.deque(maxlen=16)
        self.emu_time = 0.0; self.emu_mark = None   # (lines, real start time, start cycle) per frame
        self.crt = CrtSound()
        self.lock = threading.Lock()
        self.flags = 0                      # one-shot: 4 = RESET button, 8 = power on
        self.power = True
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
                hide = False
                if fl & 8:                              # power on: the click now, the picture a frame later
                    self.audio.flush(); hide = True
                # the speed meter: real and emulated time, both at this frame's start (marking after
                # it counted its whole length at its first moment: a frame rate change read as 99 %)
                self.emu_mark = (time.perf_counter(), self.emu_time)
                lines, pcm, speech = self.core.frame(jx, jy, btn, (1 if self.inf else 0) | fl, p2)
                if TEST_LOAD: time.sleep(TEST_LOAD)                     # (test: a host too slow)
                self.frames += 1
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
                        self.pend = [list(self.latest), t_next, t0, self.latest_len]
                    else:
                        sh = (t0 - self.pend[2]) % (1 << 32)
                        self.pend[0] += [ln[:6] + (ln[6] + sh,) if len(ln) > 6 else ln for ln in self.latest]
                        self.pend[3] += self.latest_len
                    if self.pend[3] >= 0.015:
                        self.queue.append(tuple(self.pend)); self.pend = None   # (t_next: its start)
                        self.dframes += 1
                self.audio.play(self.crt.process(pcm, lines, speech))
                self.emu_time += max(len(pcm) // 2, 1) / RATE           # (the speed meter, see emu_mark)
                # pace by emulated time (this frame's samples = its length in CPU cycles): real time
                # is the clock; the sound queue (40 ms head start) only caps runaway buffering
                t_next += max(len(pcm) // 2, 1) / RATE
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

# ------------------------------------------------------------------ window
class App:
    def __init__(self, args):
        self.args = args
        if not args.rom and not args.demo:                      # no ROM given: the one used last, if it is
            try:                                                #  still there (else no cartridge: Mine Storm)
                last = json.load(open(os.path.join(SETTINGS_DIR, 'Vectrex_EMU.conf'))).get('last_rom')
                if last and os.path.isfile(last):
                    args.rom = last
            except Exception:   # noqa
                pass
        if args.rom:
            args.rom = os.path.abspath(args.rom)
        rom, self.rom_name = self.cartridge(args.rom)
        try:                                                    # (the controls: Vectrex_EMU.conf)
            c0 = json.load(open(os.path.join(SETTINGS_DIR, 'Vectrex_EMU.conf')))
        except Exception:   # noqa
            c0 = {}
        ctl = c0.get('controls')
        self.cart_hw = {True: 'auto', False: 'off'}.get(c0.get('cart_hw', 'auto'), c0.get('cart_hw'))   # (older confs: on / off)
        if self.cart_hw not in CART_HW:
            self.cart_hw = 'auto'
        self.start_on = bool(c0.get('start_on', False))         # starts switched off: turn the knob, as on a Vectrex
        self.skip_boot = bool(c0.get('skip_boot', False))       # power on straight into the cartridge
        self.inp = Input(ctl)
        self.audio = Audio() if IS_WIN else osdep.Audio(RATE)
        self.speech = c0.get('speech', 'vecvoice' if c0.get('vecvoice') else 'off')   # (port 2: a speech accessory)
        if self.speech not in PORT2 or not sp_rom():
            self.speech = 'off'
        self.core = Core(rom, args.ideal, self.cart_hw, self.speech)
        self.run = Runner(self.core, self.inp, self.audio, False)
        self.root = tk.Tk()
        self.root.configure(bg='black')
        if not IS_WIN:
            osdep.KEYS.attach(self.root)
        if IS_MAC:                                              # (Ctrl+click: the right button, one-button mice)
            self.root.bind_class('all', '<Control-Button-1>', lambda e: e.widget.event_generate('<Button-2>', x=e.x, y=e.y))
        # picture size: as tall as the screen allows (or --height-mm on this monitor); the window
        # size you leave it at is remembered in Vectrex_EMU.conf
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
            conf0 = json.load(open(os.path.join(SETTINGS_DIR, 'Vectrex_EMU.conf')))
        except Exception:   # noqa
            conf0 = {}
        self.knob_on = conf0.get('knob', True)                # the volume knob + RESET button (K)
        self.cart_on = conf0.get('cart', True)                # the cartridge pictogram: the ROM browser (L)
        self.browser = None
        self.fps_on = conf0.get('fps_counter', True)            # the OSD (Ctrl+F)
        self.fit_h = fit                                        # (the window's screen area at most)
        if self.knob_on or self.fps_on or self.cart_on:         # (room for the strip below the picture)
            fit = int(fit / (1.0 + vecrender.KNOB_STRIP))
        self.H0 = min(fit, int(self.H0 * args.scale))
        self.W0 = int(self.H0 * vecrender.ALG_MAX_X / vecrender.ALG_MAX_Y)
        self.H0w = int(self.H0 * (1.0 + (vecrender.KNOB_STRIP if self.knob_on or self.fps_on or self.cart_on else 0.0)))
        self.settings_f = os.path.join(SETTINGS_DIR, 'Vectrex_EMU.conf')
        here = os.path.join(SETTINGS_DIR, 'settings.json')     # (the earlier name: rename it)
        if not os.path.exists(self.settings_f) and os.path.exists(here):
            try:
                os.replace(here, self.settings_f)
            except OSError:
                pass
        os.makedirs(SETTINGS_DIR, exist_ok=True)
        old = os.path.join(APPDATA_DIR, 'settings.json')       # (carry over the settings of earlier versions)
        if not os.path.exists(self.settings_f) and os.path.exists(old):
            shutil.copy(old, self.settings_f)
        geo = None
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
                self.ren.reserve = vecrender.KNOB_STRIP if self.knob_on or self.fps_on or self.cart_on else 0.0
                self.ren.resize(self.screen.winfo_width(), self.screen.winfo_height())
            except Exception as e:                              # noqa: fall back to the Tk canvas
                print('renderer unavailable (%s): using the Tk canvas' % e)
        # display settings are kept in Vectrex_EMU.conf; each menu's Reset ... settings to default restores its own
        self.look_keys = [k for k in ('phosphor', 'bloom', 'trail_on', 'trail', 'wob_on', 'wob_strength',
                                      'mains_hz', 'bend_on', 'bend_strength', 'bend_type', 'curve_on', 'halo_on', 'halo_k', 'even_beam', 'phos_spread', 'phos_grow', 'phos_smooth', 'line_gap',
                                      'art_on', 'art_tint', 'art_room', 'art_print', 'art_parallax',
                                      'art_glow', 'art_glow_k', 'scr_on', 'scr_wear', 'scr_fine', 'scr_medium', 'scr_large', 'scr_glint', 'scr_reach', 'scr_scrib')
                          if self.ren and hasattr(self.ren, k)]
        self.look_default = {k: getattr(self.ren, k) for k in self.look_keys}
        self.sound_keys = ['buzz_on', 'buzz_vol', 'speaker_on', 'volume', 'voice_vol', 'voice_full']
        self.sound_default = {k: getattr(self.run.crt, k) for k in self.sound_keys}
        try:
            saved = json.load(open(self.settings_f)).get('display', {})
        except Exception:           # noqa: first run
            saved = {}
        for k in self.look_keys:
            if k in saved:
                setattr(self.ren, k, type(self.look_default[k])(saved[k]))
        if self.ren and hasattr(self.ren, 'line_gap'):
            self.core.set_gap(self.ren.line_gap)                # (Tube > Line gaps: the core's stop delay)
        try:
            c1 = json.load(open(self.settings_f))
        except Exception:           # noqa
            c1 = {}
        self.art_auto = bool(c1.get('art_auto', True))          # Overlay > Choose automatically
        self.art_for = dict(c1.get('overlay_for', {}))          # per game: an overlay file, or 'none'
        self.art_adj = dict(c1.get('overlay_adjust', {}))       # per overlay: [dx, dy, scale]
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
        if self.start_on or args.demo:
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
        self.show_bar()
        self.vrr = conf.get('gsync', False)                     # G-Sync / FreeSync mode (off by default: any monitor)
        self.rate = conf.get('screen_rate', '60')               # '60' or 'max' (the monitor's full rate)
        self.setup_display()
        self.root.bind('<F11>', self.toggle_full)
        self.root.bind('<Motion>', self.pointer_moved)
        self.screen.bind('<Double-Button-1>', self.browse_double)
        vis = lambda: self.browser_geom()[3]
        for key, f in (('<Up>', lambda: self.browse(-1)), ('<Down>', lambda: self.browse(1)),
                       ('<Left>', lambda: self.browse(-vis())), ('<Right>', lambda: self.browse(vis())),
                       ('<Return>', self.browse_load)):
            self.root.bind(key, lambda e, f=f: (f(), 'break')[1] if self.browser else None)
        self.screen.bind('<Button-1>', self.knob_click)
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
        self.root.protocol('WM_DELETE_WINDOW', self.quit)
        if IS_MAC:                                              # macOS: Command does what Ctrl does, Ctrl+Command+F
            for seq in self.root.bind():                        #  is full screen, Command+Q quits (settings saved)
                if seq.startswith('<Control-Key-') or seq.startswith('<Control-KP_'):
                    self.root.bind(seq.replace('<Control-', '<Command-'), self.root.bind(seq))
            self.root.bind('<Control-Command-Key-f>', self.toggle_full)
            self.root.createcommand('tk::mac::Quit', self.quit)
        self.last_t = self.t_start = time.perf_counter(); self.last_f = 0; self.fps = 0; self.disp_fps = 0
        self.speed = 100.0
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
        if args.fullscreen:                                     # (a restart from full screen stays there)
            self.root.after(100, self.toggle_full)
        self.root.after(150, self.focus)

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
        """Esc: closes the ROM browser; in full screen back to the window; in the window: quit"""
        if self.browser:
            self.close_browser()
        elif self.full:
            self.toggle_full()
        else:
            self.quit()
        return 'break'
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
                                                 else 'fixed 60 Hz (vsync)' if self.vsync else 'fixed 60 Hz'))
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
        def check(menu, label, key, get, cmd):
            key = mac_key(key)
            v = tk.BooleanVar(value=False)
            menu.add_checkbutton(label=label, accelerator=key, variable=v, command=cmd)
            self.menu_checks.append((v, get))
        def item(menu, label, key, cmd):
            key = mac_key(key)
            menu.add_command(label=label, accelerator=key, command=cmd)
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
        def fill(mb):
            m = tk.Menu(mb, tearoff=0, postcommand=refresh); mb.add_cascade(label='Vectrex', menu=m)
            item(m, 'ROM browser', 'L', self.open_browser)
            item(m, 'Open ROM file...', '', self.open_rom)
            item(m, 'Eject cartridge (Mine Storm)', 'Ctrl+E', self.eject)
            m.add_separator()
            item(m, 'RESET button', 'Ctrl+R', self.reset_button)
            item(m, 'Volume (0 % = off)...', 'PgDn / PgUp', lambda: self.slider('volume'))
            check(m, 'Pause', 'P', lambda: self.run.paused, self.toggle_pause)
            m.add_separator()
            item(m, 'Controls (keyboard, controllers)...', '', self.controls_window)
            hw = tk.Menu(m, tearoff=0); m.add_cascade(label='Cartridge hardware', menu=hw)
            for k, label in CART_HW.items():
                check(hw, label, '', lambda k=k: self.cart_hw == k, lambda k=k: self.set_cart_hw(k))
            p2 = tk.Menu(m, tearoff=0); m.add_cascade(label='Controller port 2', menu=p2)
            for k, label in PORT2.items():
                check(p2, label, '', lambda k=k: self.speech == k, lambda k=k: self.set_speech(k))
            check(m, 'Start switched on', '', lambda: self.start_on, self.toggle_start_on)
            check(m, 'Skip the Vectrex intro (start screen)', '', lambda: self.skip_boot, self.toggle_skip_boot)
            m.add_separator()
            item(m, 'Reset Vectrex settings to default', '', self.reset_vectrex)
            m.add_separator()
            item(m, 'Quit', 'Esc', self.quit)
            m = tk.Menu(mb, tearoff=0, postcommand=refresh); mb.add_cascade(label='Display', menu=m)
            check(m, 'Full screen', 'F11', lambda: self.full, self.toggle_full)
            item(m, 'Bigger', '+', lambda: self.zoom_step(1.1))
            item(m, 'Smaller', '-', lambda: self.zoom_step(1 / 1.1))
            item(m, 'Start size (fits the desktop)', '0', lambda: self.zoom_step(None))
            item(m, 'Real size (as big as a Vectrex)', '9', self.real_size)
            item(m, 'Monitor size (for Real size)...', '', lambda: self.slider('monitor_inch'))
            rm = tk.Menu(m, tearoff=0, postcommand=refresh); m.add_cascade(label='Rotate screen', menu=rm)
            for deg, label in ((0, 'Normal'), (90, '90 degrees right (clockwise)'), (180, '180 degrees'), (270, '90 degrees left')):
                check(rm, label, '', lambda deg=deg: self.rot == deg, lambda deg=deg: self.set_rotation(deg))
            m.add_separator()
            check(m, 'On-screen display (fps, Hz, speed, volume)', '', lambda: self.fps_on, self.toggle_fps)
            check(m, 'Volume knob and RESET button', '', lambda: self.knob_on, self.toggle_knob)
            check(m, 'Cartridge (click: the ROM browser)', '', lambda: self.cart_on, self.toggle_cart)
            m.add_separator()
            sc = tk.Menu(m, tearoff=0, postcommand=refresh)
            m.add_cascade(label='Screen', menu=sc)
            check(sc, 'Fixed 60 Hz (smooth on any monitor)', '', lambda: not self.vrr and self.rate != 'max', lambda: self.set_screen('60'))
            check(sc, "The monitor's full rate (%s; the finest afterglow)" % ('%d Hz' % self.refresh if getattr(self, 'refresh', 0) else '120, 144, 240 Hz...'),
                  '', lambda: not self.vrr and self.rate == 'max', lambda: self.set_screen('max'))
            check(sc, "G-Sync / FreeSync (the game's frame rate, no tearing)", '', lambda: self.vrr, lambda: self.set_screen('vrr'))
            m.add_separator()
            item(m, 'Reset display settings to default', '', self.reset_display)
            m = tk.Menu(mb, tearoff=0, postcommand=refresh); mb.add_cascade(label='Tube', menu=m)
            check(m, 'Phosphor (afterglow and bloom)', '', lambda: R().phosphor, self.toggle_phosphor)
            PH = lambda: R().phosphor; AG = lambda: R().phosphor and R().trail_on
            item(m, 'Bloom...', '', lambda: self.slider('bloom')); dep(m, PH)
            check(m, 'Afterglow', '', lambda: R().trail_on, lambda: self.flip(self.ren, 'trail_on')); dep(m, PH)
            item(m, 'Afterglow length...', '', lambda: self.slider('trail')); dep(m, AG)
            item(m, 'Afterglow joining...', '', lambda: self.slider('phos_grow')); dep(m, AG)
            item(m, 'Afterglow overlap...', '', lambda: self.slider('phos_smooth')); dep(m, AG)
            check(m, 'Curved tube', '', lambda: R().curve_on, lambda: self.flip(self.ren, 'curve_on'))
            check(m, 'Corner glow (halo where the beam rests)', '', lambda: R().halo_on, lambda: self.flip(self.ren, 'halo_on'))
            dep(m, lambda: not R().even_beam)
            item(m, 'Corner glow strength...', '', lambda: self.slider('halo_k')); dep(m, lambda: R().halo_on and not R().even_beam)
            check(m, 'Even beam (an ideal vector display)', '', lambda: R().even_beam, lambda: self.flip(self.ren, 'even_beam'))
            item(m, 'Line gaps (analog delay)...', '', lambda: self.slider('line_gap'))
            m.add_separator()
            check(m, 'Mains hum wobble', '', lambda: R().wob_on, self.toggle_wobble)
            item(m, 'Wobble strength...', '', lambda: self.slider('wob_strength')); dep(m, lambda: R().wob_on)
            mh = tk.Menu(m, tearoff=0, postcommand=refresh)
            m.add_cascade(label='Mains', menu=mh); dep(m, lambda: R().wob_on)
            for hz, where in zip(vecrender.MAINS_HZ, ('Europe, most of the world', 'USA, Canada, ...')):
                check(mh, '%d Hz (%s)' % (hz, where), '', lambda hz=hz: R().mains_hz == hz,
                      lambda hz=hz: self.set_mains(hz))
            check(m, 'Brightness bending', '', lambda: R().bend_on, lambda: self.flip(self.ren, 'bend_on'))
            bt = tk.Menu(m, tearoff=0, postcommand=refresh)
            m.add_cascade(label='Bending type', menu=bt); dep(m, lambda: R().bend_on)
            for i, name in enumerate(vecrender.BEND_TYPES):
                check(bt, name, '', lambda i=i: R().bend_type % len(vecrender.BEND_TYPES) == i,
                      lambda i=i: self.set_bend_type(i))
            item(m, 'Bending strength...', '', lambda: self.slider('bend_strength')); dep(m, lambda: R().bend_on)
            m.add_separator()
            item(m, 'Reset tube settings to default', '', self.reset_tube)
            m = tk.Menu(mb, tearoff=0, postcommand=refresh); mb.add_cascade(label='Overlay', menu=m)
            A = lambda k, d: getattr(self.ren, k, d) if self.ren else d
            check(m, 'Overlay', 'O', lambda: A('art_on', False) and bool(A('art', None)), self.toggle_art)
            check(m, 'Choose automatically (by ROM name)', '', lambda: self.art_auto, self.toggle_art_auto)
            item(m, 'Choose overlay...', '', self.choose_art)
            item(m, 'No overlay for this game', '', self.no_art)
            m.add_separator()
            OV = lambda: bool(A('art_on', False) and A('art', None))           # (an overlay is shown)
            SC = lambda: OV() and A('scr_on', False)
            item(m, 'Tint strength...', '', lambda: self.slider('art_tint')); dep(m, OV)
            item(m, 'Room light...', '', lambda: self.slider('art_room')); dep(m, OV)
            item(m, 'Printed art brightness...', '', lambda: self.slider('art_print')); dep(m, OV)
            check(m, 'Parallax (seen from the usual height)', '', lambda: A('art_parallax', False), lambda: self.flip(self.ren, 'art_parallax')); dep(m, OV)
            if getattr(self.ren, 'gprog', None):
                check(m, 'Plastic glow', '', lambda: A('art_glow', False), lambda: self.flip(self.ren, 'art_glow')); dep(m, OV)
                item(m, 'Plastic glow strength...', '', lambda: self.slider('art_glow_k')); dep(m, lambda: OV() and A('art_glow', False))
            if getattr(self.ren, 'gprog', None):
                m.add_separator()
                check(m, 'Scratches (decades of handling)', '', lambda: A('scr_on', False), self.toggle_scratches); dep(m, OV)
                item(m, 'Wear (how much handled)...', '', lambda: self.slider('scr_wear')); dep(m, SC)
                item(m, 'Wear type...', '', lambda: self.sliders('Wear type', ('scr_fine', 'scr_medium', 'scr_large'))); dep(m, SC)
                item(m, 'Scribbles...', '', lambda: self.slider('scr_scrib')); dep(m, SC)
                item(m, 'Glint strength...', '', lambda: self.slider('scr_glint')); dep(m, SC)
                item(m, 'Glint reach...', '', lambda: self.slider('scr_reach')); dep(m, SC)
                item(m, 'New random scratches', '', self.new_scratches); dep(m, SC)
                m.add_separator()
            pm = tk.Menu(m, tearoff=0); m.add_cascade(label='Position and size (this overlay)', menu=pm); dep(m, OV)
            item(pm, 'Left / right...', '', lambda: self.slider('art_dx'))
            item(pm, 'Up / down...', '', lambda: self.slider('art_dy'))
            item(pm, 'Size...', '', lambda: self.slider('art_scale'))
            item(pm, 'Reset position and size', '', self.reset_art_adj)
            m.add_separator()
            item(m, 'Reset overlay settings to default', '', self.reset_overlay)
            m = tk.Menu(mb, tearoff=0, postcommand=refresh); mb.add_cascade(label='Sound', menu=m)
            check(m, 'Vectrex buzz', '', lambda: C().buzz_on, lambda: self.flip(self.run.crt, 'buzz_on'))
            item(m, 'Buzz volume...', '', lambda: self.slider('buzz_vol')); dep(m, lambda: C().buzz_on)
            check(m, 'Vectrex speaker', '', lambda: C().speaker_on, lambda: self.flip(self.run.crt, 'speaker_on'))
            sm = tk.Menu(m, tearoff=0); m.add_cascade(label='Speech module (VecVoice / VecVox)', menu=sm)
            dep(m, lambda: self.speech != 'off')                        # (only with a module in port 2)
            item(sm, 'Volume...', 'Ctrl+PgDn / Ctrl+PgUp', lambda: self.slider('voice_vol'))
            check(sm, 'Full-range speakers', '', lambda: C().voice_full, lambda: self.set_voice_full(True))
            check(sm, 'Small speaker', '', lambda: not C().voice_full, lambda: self.set_voice_full(False))
            m.add_separator()
            item(m, 'Reset sound settings to default', '', self.reset_sound)
            m = tk.Menu(mb, tearoff=0); mb.add_cascade(label='Help', menu=m)
            item(m, 'Help...', '', lambda: self.help_window('start'))
            item(m, 'What the menus do...', '', lambda: self.help_window('vectrex'))
            item(m, 'The Vectrex: how it works...', '', lambda: self.help_window('system'))
            item(m, 'Cartridges, bank switching and saving...', '', lambda: self.help_window('carts'))
            m.add_separator()
            item(m, 'About this emulator...', '', lambda: self.help_window('accuracy'))
        mb = tk.Menu(self.root); fill(mb)
        self.menubar = mb
        self.root.config(menu=mb)
    # every adjustable value: (title, keys, the object it lives on, attribute, low, high, log scale, format) -
    # low / high only for the volume knob and values that are off by default; the others: SLIDER_SPAN
    SLIDERS = {
        'volume':        ('Volume (0 % = off)', 'Page Down / Page Up', 'crt', 'volume', 0.0, 1.0, False, '%.0f %%', 100),
        'voice_vol':     ('Speech module volume (100 % = MiSTer Normal)', 'Ctrl+Page Down / Ctrl+Page Up', 'crt', 'voice_vol', 0.0, 2.0, False, '%.0f %%', 100),
        'bloom':         ('Bloom', '', 'ren', 'bloom', 0.0, 3.0, False, '%.2f', 1),
        'trail':         ('Afterglow length (x1 = measured)', '', 'ren', 'trail', 0.1, 10.0, True, 'x%.2f', 1),
        'halo_k':        ('Corner glow strength', '', 'ren', 'halo_k', 0.0, 0.4, False, '%.3f', 1),
        'line_gap':      ('Line gaps (stop delay; 12.3 = a well-tuned Vectrex)', '', 'ren', 'line_gap', 9.3, 15.3, False, '%.1f cycles', 1),
        'art_tint':      ('Overlay tint strength', '', 'ren', 'art_tint', 0.0, 2.0, False, '%.2f', 1),
        'art_room':      ('Overlay room light', '', 'ren', 'art_room', 0.0, 1.0, False, '%.2f', 1),
        'art_print':     ('Overlay printed art brightness', '', 'ren', 'art_print', 0.0, 1.0, False, '%.2f', 1),
        'scr_wear':      ('Scratches: wear - how much the sheet was handled (all of them)', '', 'ren', 'scr_wear', 0.0, 2.0, False, 'x%.2f', 1),
        'scr_fine':      ('Fine scratches (steps 1-3: the matte buff)', '', 'ren', 'scr_fine', 0.0, 2.0, False, 'x%.2f', 1),
        'scr_medium':    ('Medium scratches (steps 4-7)', '', 'ren', 'scr_medium', 0.0, 2.0, False, 'x%.2f', 1),
        'scr_large':     ('Damage (steps 8-10: deep, through the colour)', '', 'ren', 'scr_large', 0.0, 2.0, False, 'x%.2f', 1),
        'scr_scrib':     ('Scratches: scribbles - the share of curly ones', '', 'ren', 'scr_scrib', 0.0, 2.0, False, 'x%.2f', 1),
        'scr_glint':     ('Scratches: glint strength', '', 'ren', 'scr_glint', 0.0, 2.0, False, 'x%.2f', 1),
        'art_glow_k':    ('Overlay plastic glow strength', '', 'ren', 'art_glow_k', 0.0, 2.0, False, 'x%.2f', 1),
        'scr_reach':     ('Scratches: glint reach (left: close to the lines, right: farther out)', '', 'ren', 'scr_reach', 0.0, 2.0, False, 'x%.2f', 1),
        'art_dx':        ('Overlay left / right (picture heights)', '', 'ren', 'art_dx', -0.05, 0.05, False, '%+.3f', 1),
        'art_dy':        ('Overlay up / down (picture heights)', '', 'ren', 'art_dy', -0.05, 0.05, False, '%+.3f', 1),
        'art_scale':     ('Overlay size', '', 'ren', 'art_scale', 0.9, 1.1, False, 'x%.3f', 1),
        'phos_grow':     ('Afterglow joining (0 = separate copies)', '', 'ren', 'phos_grow', 0.0, 0.97, False, '%.2f', 1),
        'phos_smooth':   ('Afterglow overlap (px; 0 = off)', '', 'ren', 'phos_smooth', 0.0, 4.0, False, '%.2f', 1),
        'wob_strength':  ('Wobble strength', '', 'ren', 'wob_strength', 0.0, 1.0, False, '%.3f px', 1),
        'bend_strength': ('Bending strength', '', 'ren', 'bend_strength', 0.0, 10.0, False, '%.2f', 1),
        'buzz_vol':      ('Buzz volume', '', 'crt', 'buzz_vol', 0.0, 4.0, False, '%.3f', 1),
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
        rel = attr not in ('volume', 'line_gap', 'monitor_inch') + tuple(ART_ADJ_DEFAULT) and default > 0   # (fixed ranges, see SLIDERS)
        if rel:
            lo, hi, log = default * (1 - SLIDER_SPAN), default * (1 + SLIDER_SPAN), False
        if getattr(self, 'slider_win', None):
            try: self.slider_win.destroy()
            except tk.TclError: pass
        N = 1000
        to_pos = lambda v: N * (math.log(v / lo) / math.log(hi / lo) if log else (v - lo) / (hi - lo))
        to_val = lambda p: lo * (hi / lo) ** (p / N) if log else lo + (hi - lo) * p / N
        g, bg = '#6a8', '#000'
        t = tk.Toplevel(self.root, bg=bg); self.slider_win = t
        t.title(title); t.transient(self.root); t.resizable(False, False)
        try: t.attributes('-toolwindow', True)
        except tk.TclError: pass
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
        tk.Label(t, fg=g, bg=bg, font=(MONO, 9), anchor='w', justify='left',
                 text=('keys: %s   ' % keys if keys else '') + 'right click: default (%s)   Esc: close' % (fmt % (default * show))).pack(fill='x', padx=10, pady=(0, 8))
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
        t.bind('<Escape>', close); t.protocol('WM_DELETE_WINDOW', close)
        def lost(e=None):                                       # (focus went elsewhere: close)
            if not t.winfo_exists(): return
            f = t.focus_get()
            if f is None or not str(f).startswith(str(t)):
                close()
        t.bind('<FocusOut>', lambda e: t.after(150, lost))
        sync()
        t.update_idletasks()
        x, y = self.root.winfo_pointerxy()
        t.geometry('+%d+%d' % (max(0, x - t.winfo_width() // 2), max(0, y - 20)))
        t.focus_force(); sc.focus_set()
    def sliders(self, title, names):
        """one small window with a slider for each of several values (renderer settings with the default in the
        middle, 0 .. twice): drag to set, right click on one for its default, Esc or clicking elsewhere closes"""
        if not self.ren:
            return
        if getattr(self, 'slider_win', None):
            try: self.slider_win.destroy()
            except tk.TclError: pass
        N = 1000; g, bg = '#6a8', '#000'
        t = tk.Toplevel(self.root, bg=bg); self.slider_win = t
        t.title(title); t.transient(self.root); t.resizable(False, False)
        try: t.attributes('-toolwindow', True)
        except tk.TclError: pass
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
                 text='right click on a slider: its default   Esc: close').pack(fill='x', padx=10, pady=(4, 8))
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
        t.bind('<Escape>', close); t.protocol('WM_DELETE_WINDOW', close)
        def lost(e=None):                                       # (focus went elsewhere: close)
            if not t.winfo_exists(): return
            f = t.focus_get()
            if f is None or not str(f).startswith(str(t)):
                close()
        t.bind('<FocusOut>', lambda e: t.after(150, lost))
        sync()
        t.update_idletasks()
        x, y = self.root.winfo_pointerxy()
        t.geometry('+%d+%d' % (max(0, x - t.winfo_width() // 2), max(0, y - 20)))
        t.focus_force(); rows[0]['sc'].focus_set()
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
            w, h = self.root.winfo_width(), self.root.winfo_height()
            self.root.geometry('%dx%d' % (h, w))
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
        g, dim, bg, hi, txt = '#6a8', '#5f8a74', '#000', '#9fe1cb', '#c9ddd3'
        t = tk.Toplevel(self.root, bg=bg); self.help_win = t
        t.title('Vectrex_EMU - Help'); t.transient(self.root)
        ui = self.root.winfo_fpixels('1i') / 96.0
        t.geometry('%dx%d' % (int(980 * ui), int(700 * ui)))
        side = tk.Listbox(t, bg=bg, fg=g, selectbackground='#123', selectforeground=hi, activestyle='none',
                          font=(MONO, 11), width=28, borderwidth=0, highlightthickness=0, exportselection=False)
        side.pack(side='left', fill='y', padx=(12, 6), pady=12)
        for _, title, _ in help_text.TOPICS:
            side.insert('end', ' ' + title)
        sb = tk.Scrollbar(t); sb.pack(side='right', fill='y')
        tx = tk.Text(t, bg=bg, fg=txt, wrap='word', borderwidth=0, highlightthickness=0, padx=16, pady=12,
                     font=(SANS, 11), spacing1=2, spacing3=6, cursor='arrow', yscrollcommand=sb.set)
        tx.pack(side='left', fill='both', expand=True, pady=6)
        sb.config(command=tx.yview)
        tx.tag_configure('h1', font=(MONO, 18, 'bold'), foreground=hi, spacing3=10)
        tx.tag_configure('h2', font=(MONO, 13, 'bold'), foreground=hi, spacing1=12, spacing3=4)
        tx.tag_configure('item', font=(SANS, 11, 'bold'), foreground=hi, spacing1=8, spacing3=0)
        tx.tag_configure('ind', lmargin1=22, lmargin2=22)
        tx.tag_configure('b', font=(SANS, 11, 'bold'), foreground=hi)
        tx.tag_configure('deep', lmargin1=22, lmargin2=22, foreground='#a9c2b6', background='#0b1410',
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
        t.bind('<Escape>', lambda e: t.destroy())
        tx.bind('<MouseWheel>', lambda e: tx.yview_scroll((-3 if e.delta > 0 else 3), 'units'))
        show(topic)
    def controls_window(self):
        """Vectrex > Controls: per player a controller (only the connected ones are listed, by their own
        names; one player's controller is not offered to the other) and the keys - both work at once.
        Click a box, then press a key / a controller button, or push the stick the way the box says;
        Shift+click adds a second; right click clears (a stick box: back to the controller's default).
        Each controller keeps its own mapping and dead zone; the test field shows what the Vectrex gets.
        Shortcut keys (F11, Esc, Page Up / Down, Ctrl+...) can not be taken."""
        if getattr(self, 'ctl_win', None):
            try: self.ctl_win.lift(); return
            except tk.TclError: pass
        inp = self.inp
        g, dim, bg, hi, off = '#6a8', '#5f8a74', '#000', '#9fe1cb', '#2c4538'
        f9 = (MONO, 10)
        P = ('p1', 'p2')
        col = lambda n, m=0: 1 + 2 * n + m
        t = tk.Toplevel(self.root, bg=bg); self.ctl_win = t
        t.title('Controls'); t.transient(self.root); t.resizable(False, False)
        body = tk.Frame(t, bg=bg); body.pack(padx=14, pady=12)
        inp.refresh_devices()
        for n, text in enumerate(('Player 1 (1st port)', 'Player 2 (2nd port)')):
            tk.Label(body, text=text, fg=hi, bg=bg, font=f9).grid(row=0, column=col(n), columnspan=2, sticky='w', padx=4)
        # the controllers
        tk.Label(body, text='Controller', fg=dim, bg=bg, font=f9).grid(row=1, column=0, sticky='w', padx=4)
        menus = {}
        for n, p in enumerate(P):
            v = tk.StringVar()
            om = tk.OptionMenu(body, v, '')
            om.configure(bg=bg, fg=g, activebackground='#123', activeforeground=hi, highlightthickness=0, font=f9,
                         width=36, anchor='w')
            om['menu'].configure(bg=bg, fg=g, activebackground='#123', activeforeground=hi, font=f9)
            om.grid(row=1, column=col(n), columnspan=2, sticky='w', padx=4, pady=4)
            menus[p] = (v, om)
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
                v, om = menus[p]
                opts = dev_options(p)
                m = om['menu']; m.delete(0, 'end')
                for i, nm in opts:
                    m.add_command(label=nm, command=lambda p=p, i=i: set_dev(p, i))
                v.set(next((nm for i, nm in opts if i == inp.dev[p]), opts[0][1]))
        def set_dev(p, i):
            inp.set_dev(p, i); cap['what'] = None
            fill_menus(); refresh(); self.save_settings()
        for n in range(2):
            for m, text in enumerate(('Keyboard', 'Controller')):
                tk.Label(body, text=text, fg=dim, bg=bg, font=f9).grid(row=2, column=col(n, m), sticky='w', padx=4)
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
            tk.Label(body, text=CTRL_NAMES[k], fg=g, bg=bg, font=f9).grid(row=3 + r, column=0, sticky='w', padx=4, pady=2)
            for n, p in enumerate(P):
                for m, kind in enumerate(('key', 'pad')):
                    w = tk.Label(body, text='', fg=g, bg=bg, font=f9, width=18, anchor='w', padx=6,
                                 highlightthickness=1, highlightbackground='#3d6b55')
                    w.grid(row=3 + r, column=col(n, m), sticky='w', padx=4, pady=2)
                    w.bind('<Button-1>', lambda e, p=p, k=k, kind=kind: start(p, k, kind, bool(e.state & 1)))
                    w.bind(RCLICK, lambda e, p=p, k=k, kind=kind: clear(p, k, kind))
                    boxes[(p, k, kind)] = w
        # the stick (the player's controller's): analog or on buttons; per axis a bar with the stick on it
        # (ring) and two handles to drag: the dead zone (inner) and the endpoint (outer: where full
        # deflection is reached, for sticks with a long travel); link: X and Y move together
        row = 3 + len(CTRL_INPUTS)
        tk.Label(body, text='Stick', fg=g, bg=bg, font=f9).grid(row=row, column=0, sticky='w', padx=4, pady=(10, 2))
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
            f = tk.Frame(body, bg=bg); f.grid(row=row, column=col(n), columnspan=2, sticky='w', padx=4, pady=(10, 2))
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
            tk.Label(body, text='%s axis' % a.upper(), fg=g, bg=bg, font=f9).grid(row=row + 1 + r, column=0, sticky='w', padx=4, pady=2)
            for n, p in enumerate(P):
                cv = tk.Canvas(body, width=BW, height=BH, bg=bg, highlightthickness=0)
                cv.grid(row=row + 1 + r, column=col(n), columnspan=2, sticky='w', padx=4, pady=2)
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
        tk.Label(body, text='Test', fg=g, bg=bg, font=f9).grid(row=row, column=0, sticky='nw', padx=4, pady=(10, 2))
        S, C = 48, 56                                           # (the square's half size, its centre)
        tests = {}
        for n, p in enumerate(P):
            cv = tk.Canvas(body, width=330, height=2 * C, bg=bg, highlightthickness=0)
            cv.grid(row=row, column=col(n), columnspan=2, sticky='w', padx=4, pady=(10, 2))
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
        tk.Label(body, text='Stick bars: the ring is the controller\'s stick; drag the inner handles for the dead zone, the outer ones\n'
                            'for the endpoint (where the Vectrex stick is already full); right click: back to the default.\n'
                            'Test: the square is the full stick, the dark one the dead zone, the dashed one the endpoints; the ring\n'
                            'is the controller\'s stick, the dot what the Vectrex gets. Keyboard and controller work together:\n'
                            'the buttons add up, a direction key overrides the stick on its axis.\n'
                            'Click a box, then press a key or a controller button, or push the stick the way the row says -\n'
                            'Shift+click: add a second - right click: clear (a stick box: back to the default axis).\n'
                            'Not allowed: F11, Esc, Page Up / Down, + - 0 9, P L O and Ctrl+... (the emulator\'s shortcuts).',
                 fg=dim, bg=bg, font=(MONO, 9), justify='left').grid(row=row + 2, column=0, columnspan=5, sticky='w', padx=4, pady=(8, 4))
        bar = tk.Frame(body, bg=bg); bar.grid(row=row + 3, column=0, columnspan=5, sticky='e', pady=(6, 0))
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
            cap['what'] = None; self.save_settings()
            try: t.destroy()
            except tk.TclError: pass
            self.ctl_win = None
        for text, cmd in (('Reset to defaults', reset), ('Close', close)):
            tk.Button(bar, text=text, command=cmd, fg=g, bg=bg, activebackground='#123', activeforeground=hi, font=f9,
                      relief='flat', highlightthickness=1, highlightbackground=g, bd=1, padx=10).pack(side='left', padx=5)
        t.bind('<KeyPress>', key_pressed)
        t.bind('<Escape>', lambda e: (cap.update(what=None), refresh(), 'break')[2] if cap['what'] else close())
        t.protocol('WM_DELETE_WINDOW', close)
        fill_menus(); refresh(); test()
        t.focus_force()
    def set_cart_hw(self, k):
        """Vectrex > Cartridge hardware: Auto = the board the ROM's size calls for (64K PB6 carts,
        Vectorblade's 4 x 48K flash, the 512K latch flash, Sean Kelly's Multi-Cart), or one board for every
        ROM; Off = a bone-stock Vectrex: every ROM a plain 32K cartridge. The cartridge is put in again (a
        power cycle) with the new setting."""
        self.cart_hw = k
        self.restart(self.args.rom)
        self.show_look('cartridge: %s' % CART_HW[k].upper())
    def set_speech(self, k):
        """Vectrex > Controller port 2: the second joystick, or a speech accessory - the VecVoice (an
        SP0256-AL2) or the VecVox (a SpeakJet, spoken here with the SP0256's voice at the SpeakJet's
        pitch and lengths). Both need the SP0256-AL2 ROM in the bios folder (sp0256-al2.bin, from
        SP_ROM_URL); the Vectrex is switched off and on with the new setting."""
        if k != 'off' and not sp_rom():
            self.show_look('SPEECH NEEDS BIOS/SP0256-AL2.BIN - SEE THE README')
            return
        self.speech = k
        self.restart(self.args.rom)
        self.show_look('port 2: %s' % PORT2[k].upper())
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
        return vecrender.KNOB_STRIP if self.knob_on or self.fps_on or self.cart_on else 0.0
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
            self.root.geometry('%dx%d' % (int(ph * ALG_MAX_X / ALG_MAX_Y), hw))
        self.root.update_idletasks()
        self.apply_scale()
        self.save_settings()
    def pic_rect(self):
        """the picture's (left, top, right, bottom) in window pixels"""
        s, ox, oy = self.ren.map
        return ox * s, oy * s, (ox + ALG_MAX_X) * s, (oy + ALG_MAX_Y) * s
    def knob_geom(self):
        """(knob centre x, y, radius, RESET centre x) in window pixels: just below the picture, lined
        up with its right edge (see vecrender.KNOB_R)"""
        pl, pt, pr, pb = self.pic_rect()
        r = vecrender.KNOB_R * (pb - pt)
        cx = pr - 0.35 * r - 3.41 * r                           # (VOLUME ends at the picture's right edge)
        cy = pb + 2.3 * r                                       # (OFF/ON and RESET 0.5 r below the picture - clear of an overlay's
                                                                #  lower edge, which the parallax puts 0.32 r below it - black below)
        return cx, cy, r, cx - 3.4 * r
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
        return dict(area=(pl + pad, top, rx - 1.25 * r, pb + vecrender.KNOB_STRIP * ph),
                    rows=self.hud_text(), k=(1.2 if self.full else 1.0) * 0.3 * r / 6.0)
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
        """(centre x, y, size) of the cartridge: below the middle of the picture, level with the knob"""
        pl, pt, pr, pb = self.pic_rect()
        cx, cy, r, rx = self.knob_geom()
        return (pl + pr) / 2.0, cy, r
    def cart_state(self):
        if not (self.cart_on and self.ren and hasattr(self.ren, 'map')):
            return None
        x, y, r = self.cart_geom()
        return dict(cx=x, cy=y, r=r, open=bool(self.browser))
    def scan_roms(self):
        """the ROMs in the roms folder next to the exe (and its subfolders), by name"""
        out = []
        d = os.path.join(ROOT, 'roms')
        if os.path.isdir(d):
            for base, dirs, files in os.walk(d):
                dirs[:] = sorted((x for x in dirs if x.lower() != 'overlays'), key=str.lower)   # (not the overlays)
                for f in files:
                    if os.path.splitext(f)[1].lower() in ('.bin', '.vec', '.gam', '.rom'):
                        p = os.path.join(base, f)
                        out.append((os.path.splitext(os.path.relpath(p, d))[0].replace(os.sep, ' / '), p))
        out.sort(key=lambda t: t[0].lower())
        return out
    def open_browser(self):
        roms = self.scan_roms()
        cur = os.path.abspath(self.args.rom) if self.args.rom else None
        sel = next((i for i, (_, p) in enumerate(roms) if os.path.abspath(p) == cur), 0)
        self.browser = dict(roms=roms, sel=sel, top=0)          # (the game runs on, without input, until a
        self.inp.block = True                                   #  ROM is loaded)
        self.scroll(sel - 3)
    def close_browser(self):
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
        return dict(rect=rect, list=lst, plug=plug, up=up, dn=dn, close=cl, n_vis=n, row_h=rh, k=k,
                    names=[t for t, _ in b['roms']],
                    top=b['top'], sel=b['sel'], title='ROMS  (%d)' % len(b['roms']),
                    empty='NO ROMS: PUT THEM IN THE FOLDER ROMS NEXT TO VECTREX_EMU.EXE',
                    help='UP/DOWN: CHOOSE   LEFT/RIGHT: PAGE   ENTER/PLUG: LOAD   ESC: CLOSE')
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
        """plug the chosen cartridge in (a power cycle with it)"""
        b = self.browser
        if b and b['roms']:
            path = b['roms'][b['sel']][1]
            self.close_browser()
            self.restart(path)
    def knob_state(self):
        if not (self.knob_on and self.ren and hasattr(self.ren, 'map')):
            return None
        cx, cy, r, rx = self.knob_geom()
        return dict(cx=cx, cy=cy, r=r, rx=rx, vol=self.run.crt.volume if self.run.power else 0.0,
                    pressed=time.perf_counter() < getattr(self, 'reset_shown_until', 0))
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
    def knob_hit(self, e):
        """'knob' / 'reset' / None for a mouse position"""
        if not self.knob_state():
            return None
        cx, cy, r, rx = self.knob_geom()
        if math.hypot(e.x - cx, e.y - cy) <= 1.15 * r: return 'knob'
        if math.hypot(e.x - rx, e.y - cy) <= 1.0 * r: return 'reset'
        return None
    def knob_click(self, e):
        """a click on the knob: right half turns it up, left half down; on RESET: the RESET button"""
        e = self.px_event(e)
        if self.browser:
            return self.browser_click(e)
        if self.cart_state() and math.hypot(e.x - self.cart_geom()[0], e.y - self.cart_geom()[1]) <= 1.3 * self.cart_geom()[2]:
            return self.open_browser()
        hit = self.knob_hit(e)
        if hit == 'knob':
            self.turn_volume(+0.05 if e.x >= self.knob_geom()[0] else -0.05)
        elif hit == 'reset':
            self.reset_shown_until = time.perf_counter() + 0.18
            self.reset_button()
    def knob_wheel(self, e):
        """the mouse wheel over the knob turns it (the ROM browser open: it scrolls)"""
        e = self.px_event(e)
        if self.browser:
            return self.scroll(-3 if e.delta > 0 else 3)
        if self.knob_hit(e) == 'knob':
            self.turn_volume(+0.05 if e.delta > 0 else -0.05)

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
        else:
            n = round(refresh / self.hz) if refresh else 0
            if vs_ok and n >= 1 and abs(refresh - n * self.hz) <= 1:
                self.vsync = n
        if self.vsync > getattr(getattr(r, 'ctxo', None), 'max_swap_interval', 99):
            self.vsync = 0                                      # (macOS: only every refresh; else paced by the clock)
        if os.environ.get('VECTREX_HZ'):                       # (test: a rate by the clock)
            self.hz = float(os.environ['VECTREX_HZ']); self.vsync = 0
        r.swap_interval(0 if self.vrr else self.vsync)
    def set_screen(self, mode):
        """Display > Screen: '60' (fixed 60 Hz), 'max' (the monitor's full rate), 'vrr' (G-Sync / FreeSync)"""
        self.vrr = mode == 'vrr'
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
                          if 0 < ln[0] < ALG_MAX_X and 0 < ln[2] < ALG_MAX_X and 0 < ln[1] < ALG_MAX_Y and 0 < ln[3] < ALG_MAX_Y]
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

    def power_flags(self):
        """the core's flags for switching on: 8 = a cold start, 16 = run the BIOS intro unseen"""
        return 8 | (16 if self.skip_boot else 0)
    def toggle_start_on(self):
        self.start_on = not self.start_on
        self.save_settings()
        self.show_look('start: ' + ('switched on' if self.start_on else 'switched off (turn the volume knob)'))
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
        h = int(self.H0 * self.zoom); w = int(h * vecrender.ALG_MAX_X / vecrender.ALG_MAX_Y)
        h = int(h * (1.0 + self.knob_reserve()))                # (+ the strip)
        if getattr(self, 'rot', 0) in (90, 270):                # (a turned screen: a wide window)
            w, h = h, w
        self.root.geometry('%dx%d' % (w, h))
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
            self.keep_on_desktop()
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
        aw, ah = r - l - ow - 4, b - t - oh - 4
        s, a = 1.0 + self.knob_reserve(), vecrender.ALG_MAX_X / float(vecrender.ALG_MAX_Y)
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
            w += '   curved tube'
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
                       curve_on=('curved tube', None), halo_on=('corner glow', None), halo_k=('corner glow strength', '%.3f'), even_beam=('even beam (an ideal vector display)', None), phos_spread=('afterglow softness', '%.2f'), phos_grow=('afterglow joining', '%.2f'), phos_smooth=('afterglow overlap', '%.2f px'), line_gap=('line gaps (stop delay)', '%.1f cycles'), art_on=('overlay', None), art_tint=('overlay tint strength', '%.2f'), art_room=('overlay room light', '%.2f'), art_print=('overlay printed art brightness', '%.2f'), art_parallax=('overlay parallax', None), scr_on=('overlay scratches', None), scr_wear=('scratches: wear', 'x%.2f'), scr_fine=('fine scratches', 'x%.2f'), scr_medium=('medium scratches', 'x%.2f'), scr_large=('damage', 'x%.2f'), scr_glint=('scratches: glint strength', 'x%.2f'), scr_reach=('scratches: glint reach', 'x%.2f'), scr_scrib=('scratches: scribbles', 'x%.2f'), art_glow=('plastic glow', None), art_glow_k=('plastic glow strength', 'x%.2f'), art_dx=('overlay left / right', '%+.3f'), art_dy=('overlay up / down', '%+.3f'), art_scale=('overlay size', 'x%.3f'), wob_on=('mains hum wobble', None), bend_on=('brightness bending', None),
                       buzz_on=('Vectrex buzz', None), speaker_on=('Vectrex speaker', None), voice_vol=('speech module volume', '%d %%'), voice_full=('speech module speakers', None))
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
            return rom, os.path.splitext(os.path.basename(rom))[0]
        return NUL, 'Mine Storm (built in)'                    # (NUL reads as an empty cartridge)
    def restart(self, rom):
        """swap the cartridge and power-cycle the Vectrex inside this window: the window, full screen,
        sound and settings stay; the screen goes black until the new game draws"""
        old = self.run
        old.stop = True; old.join(timeout=2)
        self.core.close()
        path, self.rom_name = self.cartridge(rom)
        self.args.rom = os.path.abspath(rom) if rom else None
        self.save_settings()
        self.core = Core(path, self.args.ideal, self.cart_hw, self.speech)
        if self.ren and hasattr(self.ren, 'line_gap'):
            self.core.set_gap(self.ren.line_gap)
        run = Runner(self.core, self.inp, self.audio, False)
        run.crt = old.crt; run.crt.overlay = []
        run.power = old.power                                   # (switched off: it stays off, cartridge in)
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
        self.title()
        run.start()
    def open_rom(self, e=None):
        """Vectrex > Open ROM file: pick a ROM; the Vectrex power-cycles with it, in the same window"""
        from tkinter import filedialog
        start = os.path.dirname(os.path.abspath(self.args.rom)) if self.args.rom else ROOT
        fn = filedialog.askopenfilename(parent=self.root, title='Open a Vectrex ROM', initialdir=start,
                                        filetypes=[('Vectrex ROM', '*.bin *.vec *.gam *.rom'), ('All files', '*.*')])
        if fn:
            self.restart(fn)
        return 'break'
    def eject(self, e=None):
        """Ctrl+E: no cartridge (Mine Storm)"""
        self.restart(None)
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
        for k, v in self.look_default.items():
            if not k.startswith('art_'):
                setattr(self.ren, k, v)
        if self.ren and hasattr(self.ren, 'hist'):
            self.ren.hist = []
        if self.ren and hasattr(self.ren, 'line_gap'):
            self.core.set_gap(self.ren.line_gap)
        self.show_look('tube settings: the defaults')
    def reset_overlay(self):
        """Overlay > Reset overlay settings to default: on, chosen automatically, tint, room light, print,
        parallax, scratches (off, wear, glint); for this game no chosen overlay, for its overlay no position
        / size of its own (other games keep theirs)"""
        for k, v in self.look_default.items():
            if k.startswith(('art_', 'scr_')):
                setattr(self.ren, k, v)
        self.art_auto = True
        self.art_for.pop(self.rom_name, None)
        if getattr(self, 'art_path', None):
            self.art_adj.pop(os.path.basename(self.art_path), None)
        self.load_art()
        self.save_settings()
        self.show_look('overlay settings: the defaults')
    def reset_sound(self):
        """Sound > Reset sound settings to default (not the volume knob: it is the power switch)"""
        for k, v in self.sound_default.items():
            if k != 'volume':
                setattr(self.run.crt, k, v)
        self.show_look('sound settings: the defaults')
    def reset_display(self):
        """Display > Reset display settings to default: the start size (no real size, the monitor size as
        it reports it), not turned, the OSD, the knob and the cartridge on, fixed 60 Hz (full screen stays
        as it is; its picture as big as in the window)"""
        self.monitor_inch = 0.0; self.real_on = False
        if self.rot:
            self.set_rotation(0)
        for on, flip in ((self.fps_on, self.toggle_fps), (self.knob_on, self.toggle_knob), (self.cart_on, self.toggle_cart)):
            if not on:
                flip()
        self.vrr = False; self.rate = '60'; self.apply_rate()
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
        self.cart_hw = 'auto'; self.speech = 'off'
        self.start_on = False; self.skip_boot = False
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
        d['fps_counter'] = getattr(self, 'fps_on', True)
        d['gsync'] = getattr(self, 'vrr', False)
        d['screen_rate'] = getattr(self, 'rate', '60')
        d['controls'] = self.inp.save()
        d['cart_hw'] = getattr(self, 'cart_hw', 'auto')
        d['speech'] = getattr(self, 'speech', 'off')
        if hasattr(self, 'art_for'):
            if self.art_path and self.ren:                      # (this overlay's position and size)
                a = [round(getattr(self.ren, k), 4) for k in ('art_dx', 'art_dy', 'art_scale')]
                if a == [0.0, 0.0, 1.0]: self.art_adj.pop(os.path.basename(self.art_path), None)
                else: self.art_adj[os.path.basename(self.art_path)] = a
            d['art_auto'] = self.art_auto; d['overlay_for'] = self.art_for; d['overlay_adjust'] = self.art_adj
            d['rotate'] = self.rot
            d['monitor_inch'] = self.monitor_inch
            if self.ren and hasattr(self.ren, 'scr_seed'):
                d['scratch_seed'] = self.ren.scr_seed
            d['real_size'] = self.real_on
        d['start_on'] = getattr(self, 'start_on', False)
        d['skip_boot'] = getattr(self, 'skip_boot', False)
        d['knob'] = getattr(self, 'knob_on', True)
        d['cart'] = getattr(self, 'cart_on', True)
        if getattr(self, 'full_scale', None):
            d['full_scale'] = round(self.full_scale, 4)
        if self.look_keys:
            d['display'] = {k: getattr(self.ren, k) for k in self.look_keys}
        d.setdefault('display', {}).update({k: getattr(self.run.crt, k) for k in self.sound_keys})
        try:
            json.dump(d, open(self.settings_f, 'w'))
        except OSError:
            pass
    def toggle_full(self, e=None):
        if not self.full:                                       # the picture height in the window, now
            self.win_pic_h = (ALG_MAX_Y * self.ren.map[0] / self.px() if self.ren and hasattr(self.ren, 'map') else
                              min(self.screen.winfo_height(), self.screen.winfo_width() * ALG_MAX_Y / ALG_MAX_X))
        self.full = not self.full
        self.root.config(menu='' if self.full else self.menubar)   # (full screen: no menu bar)
        self.root.attributes('-fullscreen', self.full)
        self.show_bar()
        if self.full and not self.full_scale:                   # first time: as big as it was in the window
            self.full_scale = self.win_fit()
        self.root.update_idletasks()
        if self.real_on:                                        # (Real size: the same size in full screen and back)
            self.real_size(quiet=True)
        self.apply_scale()
        self.pointer_moved()

    def pointer_moved(self, e=None):
        """the mouse pointer: in full screen it hides 2 s after it last moved"""
        for w in (self.root, self.screen):
            w.configure(cursor='')
        if getattr(self, 'hide_job', None):
            self.root.after_cancel(self.hide_job); self.hide_job = None
        if self.full:
            self.hide_job = self.root.after(2000, self.hide_pointer)
    def hide_pointer(self):
        self.hide_job = None
        if self.full:
            for w in (self.root, self.screen):
                w.configure(cursor='none')

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
        for i, (x0, y0, x1, y1, c) in enumerate(lines):
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

    def tick(self):
        if self.run.error:
            self.status.configure(text='ERROR: ' + self.run.error, fg='#f66'); self.status.pack(side='top', fill='x')
            self.show_bar()
            return
        if self.ren and hasattr(self.ren, 'present'):
            return self.tick_timed()
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
        with self.run.lock:
            frames = list(self.run.queue); self.run.queue.clear()
            self.run.latest = None
        for lines, t_start, t0, ln in frames:
            self.lines = lines
            if self.run.power:
                self.crt_fx()
            r.frame_no = self.run.frames; r.frame_t0 = t0
            r.add_frame(lines, t_start - self.pause_acc, ln)
        now = time.perf_counter()
        if self.run.paused:
            # paused: the picture stands still (the phosphor clock stopped when it was paused), redrawn
            # PAUSE_HZ times a second - a window that was covered is painted again, the knob turns
            show = now >= self.next_present
            T = getattr(self, 'pause_t', now)
        elif vrr:
            # a game frame: shown at once, its lines all drawn (the phosphor clock at the frame's end);
            # switched off (no frames): 60 Hz by the clock
            show = bool(frames) or (not self.run.power and now >= self.next_present)
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
            r.browser = self.browser_state()
            flush, self.flush = getattr(self, 'flush', False), False
            r.present(T - self.pause_acc, imm, flush)
            if self.vsync and not vrr:
                r.gl.glFinish()                                 # (wait for the refresh: the pace)
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
    SPEED_COLORS = ((99.0, '#6a8', (0.40, 0.667, 0.533)), (90.0, '#e93', (0.93, 0.60, 0.20)), (-1.0, '#f44', (1.0, 0.27, 0.27)))
    def speed_color(self, k):
        return next(c[k] for c in self.SPEED_COLORS if self.speed >= c[0])
    def hud_text(self):
        """the OSD, one value per row: the game's frame rate, the monitor's refresh, the emulation rate
        (coloured) and the volume"""
        g = (0.40, 0.667, 0.533)
        idle = self.run.paused or not self.run.power
        vals = [('INTERNAL', '%d FPS' % round(self.fps), g), ('MONITOR', '%d HZ' % round(self.disp_fps), g),
                ('EMU RATE', '--' if idle else '%d%%' % self.speed_shown(), g if idle else self.speed_color(2)),
                ('VOLUME', '%d%%' % round(self.run.crt.volume * 100) if self.run.power else 'OFF', g)]
        if getattr(self, 'speech', 'off') != 'off':             # (a speech module plugged in: its volume too)
            vals.append(('SPEECH VOL', '%d%%' % round(self.run.crt.voice_vol * 100), g))
        width = max(len(n) + 2 + len(v) for n, v, _ in vals)   # (the values end in one column)
        return [[(n + ':' + ' ' * (width - len(n) - 1 - len(v)), g), (v, c)] for n, v, c in vals]
    def speed_shown(self):
        return 100 if self.speed >= 99.5 else int(self.speed)

    def status_line(self):
        now = time.perf_counter()
        if now - self.last_t > 1:
            self.fps = (self.run.dframes - self.last_f) / (now - self.last_t)
            p = [t for t in self.presents if t > self.last_t]
            self.disp_fps = len(p) / (now - self.last_t)
            self.last_t, self.last_f = now, self.run.dframes
            m, m0 = self.run.emu_mark, getattr(self, 'emu_mark0', None)
            if m and m0 and m[0] > m0[0] and m[1] >= m0[1]:      # (frame completions a second apart)
                self.speed = 100.0 * (m[1] - m0[1]) / (now - m0[0]) if now - m[0] > 0.25 else \
                             100.0 * (m[1] - m0[1]) / (m[0] - m0[0])
            elif m0 and (not m or m[0] <= m0[0]) and self.run.power and not self.run.paused:
                self.speed = 0.0                                # (no frame for a second: stalled)
            self.emu_mark0 = m
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
            if not IS_WIN and BIOS in missing:
                msg += '\n\nThe Vectrex BIOS goes in a folder called bios next to the program: bios/bios.bin.'
                if osdep.translocated(ROOT):
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
