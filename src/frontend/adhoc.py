"""adhoc.py - play nearby without a router or the internet (Online > Nearby): this PC makes a Wi-Fi network of its own and
hosts its game on it; phones and PCs join that network in their Wi-Fi settings and find the game under Games on this
network, or use Join the nearby host. The network's name and password are made once and kept (system/adhoc.json): the
second time the other device knows the network.
  Windows: a Wi-Fi Direct access point for ordinary devices (WinRT WiFiDirectAdvertisementPublisher, legacy mode - no
  internet connection needed), run by a PowerShell of its own while the network is up.
  Linux: NetworkManager's hotspot (nmcli).  macOS: no way for a program - System Settings > Sharing > Internet Sharing.
The host of an ad-hoc network is its router: 192.168.49.1 (an Android phone's Wi-Fi Direct group), 172.20.10.1 (an
iPhone's hotspot), 192.168.137.1 (Windows), 10.42.0.1 (Linux).
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import json, os, secrets, subprocess, sys, threading

ADHOC_NETS = ('192.168.49.', '172.20.10.', '192.168.137.', '10.42.0.')
_proc = None
_state = {'on': False, 'msg': '', 'ssid': ''}


def creds(sysdir):
    """the network's name and password: made once on this PC, kept"""
    p = os.path.join(sysdir, 'adhoc.json')
    try:
        with open(p) as f:
            c = json.load(f)
        if c.get('ssid') and len(c.get('password', '')) >= 8:
            return c['ssid'], c['password']
    except (OSError, ValueError):
        pass
    a = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'
    c = {'ssid': 'VECTREX-EMU-' + ''.join(secrets.choice(a) for _ in range(4)),
         'password': 'vectrex' + ''.join(secrets.choice('0123456789') for _ in range(4))}
    try:
        os.makedirs(sysdir, exist_ok=True)
        with open(p, 'w') as f:
            json.dump(c, f)
    except OSError:
        pass
    return c['ssid'], c['password']


_PS = r'''
$ErrorActionPreference = 'Stop'
try {
  Add-Type -AssemblyName System.Runtime.WindowsRuntime
  $null = [Windows.Devices.WiFiDirect.WiFiDirectAdvertisementPublisher, Windows.Devices.WiFiDirect, ContentType = WindowsRuntime]
  $null = [Windows.Security.Credentials.PasswordCredential, Windows.Security.Credentials, ContentType = WindowsRuntime]
  $p = New-Object Windows.Devices.WiFiDirect.WiFiDirectAdvertisementPublisher
  $p.Advertisement.IsAutonomousGroupOwnerEnabled = $true
  $p.Advertisement.LegacySettings.IsEnabled = $true
  $p.Advertisement.LegacySettings.Ssid = $env:VX_SSID
  $c = New-Object Windows.Security.Credentials.PasswordCredential
  $c.Password = $env:VX_PASS
  $p.Advertisement.LegacySettings.Passphrase = $c
  $p.Start()
  $last = ''
  while ($true) {
    $s = [string]$p.Status
    if ($s -ne $last) { [Console]::Out.WriteLine('status ' + $s); [Console]::Out.Flush(); $last = $s }
    if ($s -eq 'Aborted' -or $s -eq 'Stopped') { exit 2 }
    Start-Sleep -Milliseconds 500
  }
} catch { [Console]::Out.WriteLine('error ' + $_.Exception.Message); exit 1 }
'''


def start(sysdir, done):
    """the network made, on a thread; done(ok, message) - ok: hosting can begin"""
    global _proc
    ssid, pw = creds(sysdir)
    _state.update(on=False, msg='', ssid=ssid)

    def run():
        global _proc
        if sys.platform == 'win32':
            env = dict(os.environ, VX_SSID=ssid, VX_PASS=pw)
            try:
                _proc = subprocess.Popen(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', _PS], env=env,
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                         creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            except OSError as e:
                return done(False, 'PowerShell could not start: %s' % e)
            for line in _proc.stdout:
                line = line.strip()
                if line == 'status Started':
                    _state['on'] = True
                    done(True, '')
                elif line.startswith('error') or line in ('status Aborted', 'status Stopped'):
                    if not _state['on']:
                        why = line[6:] if line.startswith('error') else 'Windows stopped it'
                        done(False, 'The network could not be made (%s). Is Wi-Fi on? Or use Settings > Mobile hotspot, '
                                    'then Host on local network' % why[:120])
                    _state['on'] = False
                    break
            return
        if sys.platform.startswith('linux'):
            try:
                r = subprocess.run(['nmcli', 'dev', 'wifi', 'hotspot', 'con-name', 'Vectrex_EMU', 'ssid', ssid, 'password', pw],
                                   capture_output=True, text=True, timeout=30)
            except (OSError, subprocess.TimeoutExpired) as e:
                return done(False, 'NetworkManager (nmcli) is needed: %s' % e)
            if r.returncode == 0:
                _state['on'] = True
                return done(True, '')
            return done(False, 'The network could not be made: %s' % (r.stderr or r.stdout).strip()[:160])
        done(False, 'On a Mac: System Settings > General > Sharing > Internet Sharing (Wi-Fi), then Host on local network')
    threading.Thread(target=run, daemon=True).start()
    return ssid, pw


def stop():
    """the network ended (Leave, the window closed, the app's end)"""
    global _proc
    if _proc is not None:
        try:
            _proc.terminate()
        except OSError:
            pass
        _proc = None
    if _state['on'] and sys.platform.startswith('linux'):
        try:
            subprocess.run(['nmcli', 'con', 'down', 'Vectrex_EMU'], capture_output=True, timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            pass
    _state['on'] = False


def is_on():
    return _state['on']


def host_addr(addresses):
    """the nearby host: the address .1 of the network this PC joined (an ad-hoc network's host is its router); the
    ad-hoc kinds first"""
    best = None
    for a in addresses:
        gw = a.rsplit('.', 1)[0] + '.1'
        if a.startswith(ADHOC_NETS):
            return gw
        if best is None and a.startswith(('192.168.', '10.', '172.')):
            best = gw
    return best
