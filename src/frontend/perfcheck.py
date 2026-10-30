"""perfcheck.py - why a PC may run the emulator slower than real time, and the debug report.

Found here (each one only where the system can tell): on battery, battery saver, the Windows power mode, the
picture drawn on a built-in graphics chip while a faster one is there. The JIT blocked by a security tool is told
by the core itself (vectrex_emu.Core: 'no JIT'). no_throttle() asks Windows not to slow a process down as
background work (EcoQoS: lower clocks, the small cores) - the emulator and its core are neither."""
import ctypes, os, sys, json, time, zipfile, platform, datetime

IS_WIN, IS_MAC = sys.platform == 'win32', sys.platform == 'darwin'

# the Windows power mode (Settings > System > Power): its overlay scheme
MODES = {'961cc777-2547-4f9d-8174-7d86181b8a7a': 'Best power efficiency',
         '00000000-0000-0000-0000-000000000000': 'Balanced',
         '3af9b8d9-7c97-431d-ad78-34a8bfea439f': 'Better performance',
         'ded574b5-45a0-4f42-8737-46345c09c238': 'Best performance'}
PLANS = {'381b4222-f694-41f0-9685-ff5bb260df2e': 'Balanced',     # (the power plans)
         '8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c': 'High performance',
         'e9a42b02-d5df-448d-aa00-03f14749eb61': 'Ultimate performance',
         'a1841308-3593-43eb-9a5c-6ef6ab0a6fe9': 'Power saver'}


def no_throttle(handle=None):
    """Windows 11: this process (or the one with this handle) not treated as background work - no EcoQoS. True when
    it was set. Elsewhere, or on an older Windows, nothing (False)"""
    if not IS_WIN:
        return False
    class STATE(ctypes.Structure):
        _fields_ = [('Version', ctypes.c_ulong), ('ControlMask', ctypes.c_ulong), ('StateMask', ctypes.c_ulong)]
    try:
        k = ctypes.windll.kernel32
        h = handle if handle is not None else k.GetCurrentProcess()
        s = STATE(1, 1, 0)                               # (version 1; EXECUTION_SPEED controlled: off)
        k.SetProcessInformation.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_ulong]
        return bool(k.SetProcessInformation(ctypes.c_void_p(int(h)), 4, ctypes.byref(s), ctypes.sizeof(s)))   # (4: ProcessPowerThrottling)
    except Exception:   # noqa
        return False


def power():
    """{'ac': True/False/None, 'saver': True/False/None, 'mode': name or None} - None: not known here"""
    out = {'ac': None, 'saver': None, 'mode': None}
    if IS_WIN:
        class SPS(ctypes.Structure):
            _fields_ = [('ACLineStatus', ctypes.c_ubyte), ('BatteryFlag', ctypes.c_ubyte), ('BatteryLifePercent', ctypes.c_ubyte),
                        ('SystemStatusFlag', ctypes.c_ubyte), ('BatteryLifeTime', ctypes.c_ulong), ('BatteryFullLifeTime', ctypes.c_ulong)]
        try:
            s = SPS()
            if ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(s)):
                if s.BatteryFlag != 128:                 # (128: no battery - a desktop: always on mains)
                    out['ac'] = {0: False, 1: True}.get(s.ACLineStatus)
                else:
                    out['ac'] = True
                out['saver'] = bool(s.SystemStatusFlag & 1)
        except Exception:   # noqa
            pass
        try:                                             # (the plan, and over Balanced the power mode slider's overlay)
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SYSTEM\CurrentControlSet\Control\Power\User\PowerSchemes') as k:
                def val(n):
                    try: return str(winreg.QueryValueEx(k, n)[0]).lower()
                    except OSError: return None
                plan = val('ActivePowerScheme')
                over = val('ActiveOverlayAcPowerScheme' if out['ac'] is not False else 'ActiveOverlayDcPowerScheme')
            mode = PLANS.get(plan) if plan else None
            if mode == 'Balanced' and over in MODES:
                mode = MODES[over]
            out['mode'] = mode
        except Exception:   # noqa
            pass
    elif IS_MAC:
        try:
            import subprocess
            t = subprocess.run(['pmset', '-g', 'batt'], capture_output=True, text=True, timeout=2).stdout
            out['ac'] = "'AC Power'" in t if 'Power' in t else None
            t = subprocess.run(['pmset', '-g'], capture_output=True, text=True, timeout=2).stdout
            for ln in t.splitlines():
                if ln.strip().startswith('lowpowermode'):
                    out['saver'] = ln.split()[-1] == '1'
        except Exception:   # noqa
            pass
    else:
        try:
            import glob
            for d in glob.glob('/sys/class/power_supply/*'):
                if open(os.path.join(d, 'type')).read().strip() == 'Mains':
                    out['ac'] = open(os.path.join(d, 'online')).read().strip() == '1'
                    break
            else:
                if not glob.glob('/sys/class/power_supply/BAT*'):
                    out['ac'] = True                      # (no battery)
        except Exception:   # noqa
            pass
        try:
            out['mode'] = open('/sys/firmware/acpi/platform_profile').read().strip() or None   # (low-power / balanced / performance)
        except OSError:
            pass
    return out


def adapters():
    """the graphics chips Windows knows (their names); [] elsewhere"""
    if not IS_WIN:
        return []
    class DD(ctypes.Structure):
        _fields_ = [('cb', ctypes.c_ulong), ('DeviceName', ctypes.c_wchar * 32), ('DeviceString', ctypes.c_wchar * 128),
                    ('StateFlags', ctypes.c_ulong), ('DeviceID', ctypes.c_wchar * 128), ('DeviceKey', ctypes.c_wchar * 128)]
    out, i = [], 0
    try:
        while i < 16:
            d = DD(); d.cb = ctypes.sizeof(d)
            if not ctypes.windll.user32.EnumDisplayDevicesW(None, i, ctypes.byref(d), 0):
                break
            if d.DeviceString and d.DeviceString not in out:
                out.append(d.DeviceString)
            i += 1
    except Exception:   # noqa
        pass
    return out


def _discrete(name):
    n = name.lower()
    return any(k in n for k in ('nvidia', 'geforce', 'rtx', 'gtx', 'quadro', 'radeon rx', 'radeon pro', 'arc a', 'arc b'))


def wrong_gpu(gl_name):
    """the picture is drawn on a built-in chip while a faster one is in the PC: (used, the faster one) - else None"""
    if not gl_name or _discrete(gl_name):
        return None
    fast = [a for a in adapters() if _discrete(a)]
    return (gl_name, fast[0]) if fast else None


def causes(gl_name=None, jit_blocked=None):
    """what may slow this PC down, as short lines for the window (worst first); [] when nothing is found"""
    out = []
    p = power()
    if jit_blocked:
        out.append(('The %s runs without its JIT: a security tool or Exploit protection blocks it' % jit_blocked,
                    'allow Vectrex_EMU to run generated code (Exploit protection: Arbitrary code guard off for it)'))
    if p['ac'] is False:
        out.append(('Running on battery', 'plug in the charger for full speed'))
    if p['saver']:
        out.append(('Battery saver / Low power mode is on', 'switch it off'))
    if p['mode'] in ('Best power efficiency', 'Power saver', 'low-power', 'quiet', 'cool'):
        out.append(('Power mode: %s' % p['mode'], 'set Best performance' if IS_WIN else 'set performance'))
    g = wrong_gpu(gl_name)
    if g:
        out.append(('The picture runs on %s, not on %s' % g,
                    'Windows Settings > System > Display > Graphics: Vectrex_EMU on High performance' if IS_WIN else
                    'choose the faster graphics chip for Vectrex_EMU'))
    return out


def _private(text):
    """the user's home folder (it holds the Windows user name) as ~"""
    home = os.path.expanduser('~')
    for h in {home, home.replace(os.sep, '/'), home.replace(os.sep, os.sep * 2)}:   # (also as JSON writes it)
        if len(h) > 3:
            text = text.replace(h, '~')
    return text


def report(path, info_text, settings_file=None, core_log=None, extra=None):
    """the debug report: one zip to send - the system info, the power state, the speed history, the core's log,
    the settings (without the online key). Returns the path"""
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        p = power()
        lines = [info_text, '',
                 'Report made: %s' % datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
                 'Platform: %s' % platform.platform(),
                 'Processors: %s' % os.cpu_count(),
                 'On mains power: %s' % p['ac'], 'Battery saver: %s' % p['saver'], 'Power mode: %s' % p['mode'],
                 'Graphics chips: %s' % (', '.join(adapters()) or '?')]
        for k, v in (extra or {}).items():
            lines.append('%s: %s' % (k, v))
        z.writestr('system.txt', _private('\n'.join(lines) + '\n'))
        if settings_file and os.path.isfile(settings_file):
            try:
                d = json.load(open(settings_file, encoding='utf-8'))
                for k in ('player_key', 'gamer_name'):  # (the online key and player name: private)
                    d.pop(k, None)
                z.writestr('settings.json', _private(json.dumps(d, indent=1)))
            except (OSError, ValueError):
                pass
        if core_log and os.path.isfile(core_log):
            try: z.writestr('core_log.txt', _private(open(core_log, encoding='utf-8', errors='replace').read()))
            except OSError: pass
    return path


def report_path():
    """where the report goes: Downloads (or the Desktop, or home), a new name each time"""
    home = os.path.expanduser('~')
    for d in (os.path.join(home, 'Downloads'), os.path.join(home, 'Desktop'), home):
        if os.path.isdir(d):
            break
    return os.path.join(d, 'Vectrex_EMU_report_%s.zip' % time.strftime('%Y-%m-%d_%H%M%S'))


REPORT_URL = os.environ.get('VECTREX_REPORT_URL', 'http://172.233.60.40:7746/report')   # (tools/vxreport.py on the meeting point)


def upload(path, version):
    """sends the report to the author's server: its number ('R-XXXXXX'). Raises with a short reason when it fails"""
    import urllib.request, urllib.error
    data = open(path, 'rb').read()
    req = urllib.request.Request(REPORT_URL, data=data, method='POST',
                                 headers={'Content-Type': 'application/zip', 'X-Vectrex-Version': str(version),
                                          'User-Agent': 'Vectrex_EMU ' + str(version)})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            num = r.read(64).decode('ascii', 'replace').strip()
    except urllib.error.HTTPError as e:
        raise RuntimeError(e.read(200).decode('utf-8', 'replace').strip() or 'server error %d' % e.code)
    except (urllib.error.URLError, OSError) as e:
        raise RuntimeError('no connection to the server')
    if not num.startswith('R-'):
        raise RuntimeError('unexpected answer')
    return num
