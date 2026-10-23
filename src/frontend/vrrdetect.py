"""vrrdetect.py - does the monitor showing a window do variable refresh (a G-Sync module, G-Sync Compatible /
FreeSync / VESA Adaptive-Sync)? Read from the monitor's own EDID (Windows: the registry entry of the monitor the
window is on): an NVIDIA G-Sync block, an AMD FreeSync block, a DisplayID Adaptive-Sync block, or a refresh range wide enough.
Returns dict(vrr=True/False/None (unknown), lo=Hz, hi=Hz, why=text)."""
import sys

def parse(edid):
    """the facts in one EDID (bytes): its refresh range, an AMD FreeSync block, a DisplayID Adaptive-Sync block"""
    r = dict(lo=None, hi=None, amd=False, adaptive=False, nvidia=False)
    if len(edid) < 128:
        return r
    for o in (54, 72, 90, 108):                            # (the four descriptors: 0xFD = range limits)
        d = edid[o:o + 18]
        if d[0] == 0 and d[1] == 0 and d[3] == 0xFD:
            lo, hi = d[5] + (255 if d[4] & 1 else 0), d[6] + (255 if d[4] & 2 else 0)
            r['lo'], r['hi'] = lo, hi
    for b in range(1, len(edid) // 128):                     # (extensions)
        x = edid[b * 128:(b + 1) * 128]
        if x[0] == 0x02:                                       # CTA-861: its data blocks
            end = x[2]; i = 4
            while 4 <= i < min(end, 127):
                tag, n = x[i] >> 5, x[i] & 31
                if tag == 3 and n >= 3 and x[i + 1:i + 4] == bytes([0x4B, 0x04, 0x00]):   # (NVIDIA's OUI 00-04-4B: a G-Sync module)
                    r['nvidia'] = True
                if tag == 3 and n >= 3 and x[i + 1:i + 4] == bytes([0x1A, 0x00, 0x00]):   # (AMD's OUI 00-00-1A)
                    r['amd'] = True
                    if n >= 7:                                 # (FreeSync's own range)
                        r['lo'], r['hi'] = x[i + 6], x[i + 7]
                i += n + 1
        elif x[0] == 0x70:                                     # DisplayID: an Adaptive-Sync block (0x2B)
            i = 5
            while i < 126:
                tag, n = x[i], x[i + 2]
                if tag == 0:
                    break
                if tag == 0x2B:
                    r['adaptive'] = True
                i += 3 + n
    return r

def verdict(r):
    if r['nvidia'] or r['amd'] or r['adaptive']:
        what = 'G-Sync (its own module)' if r['nvidia'] else 'FreeSync' if r['amd'] else 'Adaptive-Sync'
        if r['nvidia']:                                    # (a G-Sync module repeats frames below its range: any rate works)
            return dict(vrr=True, lo=None, hi=r['hi'], why='the monitor reports %s, up to %s Hz' % (what, r['hi'] or '?'))
        rng = ' %d-%d Hz' % (r['lo'], r['hi']) if r['lo'] and r['hi'] else ''
        return dict(vrr=True, lo=r['lo'], hi=r['hi'], why='the monitor reports %s%s' % (what, rng))
    if r['hi'] and r['hi'] >= 100 and r['lo'] and r['lo'] <= 50:
        return dict(vrr=None, lo=r['lo'], hi=r['hi'],
                    why='a %d Hz monitor (range %d-%d Hz): it may do G-Sync / FreeSync - try it' % (r['hi'], r['lo'], r['hi']))
    rng = ' (%d-%d Hz)' % (r['lo'], r['hi']) if r['lo'] and r['hi'] else ''
    return dict(vrr=False, lo=r['lo'], hi=r['hi'], why='not reported by the monitor%s' % rng)

def detect(hwnd):
    """the monitor the window `hwnd` is on (Windows); elsewhere unknown"""
    if not sys.platform.startswith('win'):
        return dict(vrr=None, lo=None, hi=None, why='unknown on this system - try it')
    try:
        import ctypes, ctypes.wintypes as W, winreg
        u = ctypes.WinDLL('user32')
        u.MonitorFromWindow.restype = W.HANDLE; u.MonitorFromWindow.argtypes = [W.HWND, W.DWORD]
        class MI(ctypes.Structure):
            _fields_ = [('cbSize', W.DWORD), ('rcMonitor', W.RECT), ('rcWork', W.RECT), ('dwFlags', W.DWORD),
                        ('szDevice', W.WCHAR * 32)]
        class DD(ctypes.Structure):
            _fields_ = [('cb', W.DWORD), ('DeviceName', W.WCHAR * 32), ('DeviceString', W.WCHAR * 128),
                        ('StateFlags', W.DWORD), ('DeviceID', W.WCHAR * 128), ('DeviceKey', W.WCHAR * 128)]
        mon = u.MonitorFromWindow(hwnd, 2)
        mi = MI(); mi.cbSize = ctypes.sizeof(MI)
        if not u.GetMonitorInfoW(mon, ctypes.byref(mi)):
            raise OSError('no monitor info')
        dd = DD(); dd.cb = ctypes.sizeof(DD)
        if not u.EnumDisplayDevicesW(mi.szDevice, 0, ctypes.byref(dd), 1):   # (EDD_GET_DEVICE_INTERFACE_NAME)
            raise OSError('no display device')
        parts = dd.DeviceID.split('#')                         # \\?\DISPLAY#GSM5B7F#5&...&UID4353#{guid}
        key = r'SYSTEM\CurrentControlSet\Enum\DISPLAY\%s\%s\Device Parameters' % (parts[1], parts[2])
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key) as k:
            edid = bytes(winreg.QueryValueEx(k, 'EDID')[0])
        v = verdict(parse(edid))
        v['monitor'] = parts[1]
        return v
    except Exception as e:      # noqa
        return dict(vrr=None, lo=None, hi=None, why='could not read the monitor (%s) - try it' % e)

if __name__ == '__main__':
    import ctypes
    print(detect(ctypes.windll.user32.GetForegroundWindow()))
