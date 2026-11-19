#!/usr/bin/env python3
"""vxreport.py - receives Vectrex_EMU debug reports (Help > Debug report > Send) and gives each a short number.

POST /report  body: the report's zip (at most 2 MB)  header X-Vectrex-Version: the sender's version
  -> 200 text/plain 'R-XXXXXX' (the number the user gives the author), or 4xx/5xx with a short reason.
Stored as DIR/R-XXXXXX.zip plus DIR/R-XXXXXX.json (time, version, size, and email when the user gave one: contact.txt
in the zip, v1.4.5 on - older reports have none). The sender's IP is kept in memory only
for the rate limit (REPORTS_PER_DAY per IP), never written. The folder is kept under MAX_TOTAL (oldest go first).

Install (Linode, next to vxrelay):
  cp vxreport.py /usr/local/bin/ && mkdir -p /var/lib/vxreports
  /etc/systemd/system/vxreport.service:
    [Unit]
    Description=Vectrex_EMU debug reports
    After=network.target
    [Service]
    ExecStart=/usr/bin/python3 /usr/local/bin/vxreport.py
    Restart=always
    DynamicUser=yes
    StateDirectory=vxreports
    [Install]
    WantedBy=multi-user.target
  systemctl daemon-reload && systemctl enable --now vxreport && ufw allow 7746/tcp comment 'Vectrex_EMU debug reports'
Read: ls -lt /var/lib/vxreports | head; unzip -p /var/lib/vxreports/R-XXXXXX.zip system.txt

Telegram (optional): each new report to the author's chat - a summary and the zip. The bot's token and the chat
in /etc/vxreport.env (root only, never in git): TELEGRAM_TOKEN=... and TELEGRAM_CHAT=..., and in the unit
EnvironmentFile=/etc/vxreport.env
"""
import os, json, time, secrets, threading, zipfile, io, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TG_TOKEN, TG_CHAT = os.environ.get('TELEGRAM_TOKEN', ''), os.environ.get('TELEGRAM_CHAT', '')
SUMMARY = ('Vectrex_EMU', 'System', 'Processor', 'Graphics', 'Now', 'On mains power', 'Power mode', 'Game', 'Quality',
           'Found')                                     # (system.txt rows for the message)


def contact(data):
    """the email the user added (contact.txt in the zip, optional since v1.4.5): '' when none - printable, short"""
    try:
        t = zipfile.ZipFile(io.BytesIO(data)).read('contact.txt')[:200].decode('utf-8', 'replace').strip()
    except Exception:   # noqa (no contact.txt: an older emulator, or none given)
        return ''
    t = ''.join(c for c in t.splitlines()[0] if c.isprintable())[:120] if t else ''
    return t if '@' in t else ''


def telegram(num, ver, data, email=''):
    """the new report to the author's Telegram chat: its number, a few rows of system.txt, the zip (in a thread)"""
    if not (TG_TOKEN and TG_CHAT):
        return
    try:                                                # (/debug off in the bot: silent - vxbot.py)
        if not json.load(open('/var/lib/vxbot/state.json')).get('debug', True):
            return
    except (OSError, ValueError):
        pass
    try:
        txt = zipfile.ZipFile(io.BytesIO(data)).read('system.txt').decode('utf-8', 'replace')
    except Exception:   # noqa
        txt = ''
    rows = [ln.strip() for ln in txt.splitlines() if ln.strip().startswith(SUMMARY)][:12]
    cap = ('New debug report %s (%s)\n' % (num, ver) + ('Email: %s\n' % email if email else 'Email: none given\n')
           + '\n'.join(r[:150] for r in rows))[:1000]
    b = '----vx' + secrets.token_hex(8)
    body = b''.join([
        ('--%s\r\nContent-Disposition: form-data; name="chat_id"\r\n\r\n%s\r\n' % (b, TG_CHAT)).encode(),
        ('--%s\r\nContent-Disposition: form-data; name="caption"\r\n\r\n' % b).encode() + cap.encode('utf-8') + b'\r\n',
        ('--%s\r\nContent-Disposition: form-data; name="document"; filename="%s.zip"\r\n'
         'Content-Type: application/zip\r\n\r\n' % (b, num)).encode() + data + b'\r\n',
        ('--%s--\r\n' % b).encode()])
    try:
        req = urllib.request.Request('https://api.telegram.org/bot%s/sendDocument' % TG_TOKEN, data=body,
                                     headers={'Content-Type': 'multipart/form-data; boundary=' + b})
        urllib.request.urlopen(req, timeout=30).read()
    except Exception as e:   # noqa: the report is stored anyway
        print('telegram failed: %s' % type(e).__name__, flush=True)

PORT = int(os.environ.get('VXREPORT_PORT', '7746'))
DIR = os.environ.get('VXREPORT_DIR', os.environ.get('STATE_DIRECTORY', '/var/lib/vxreports'))
MAX_SIZE = 2 * 1024 * 1024
MAX_TOTAL = 500 * 1024 * 1024
REPORTS_PER_DAY = 10
IOSLOG_TOKEN = os.environ.get('IOSLOG_TOKEN', '')       # (/ioslog: the owner's iOS test build only - off when unset)
IOS_MAX = 16 * 1024 * 1024                             # (a screenshot at an iPad's size)
IOS_TOTAL = 500 * 1024 * 1024
ABC = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'              # (no 0/O, 1/I: easy to read out)
lock = threading.Lock()
recent = {}                                             # ip -> [times] (memory only)


def new_number():
    while True:
        n = 'R-' + ''.join(secrets.choice(ABC) for _ in range(6))
        if not os.path.exists(os.path.join(DIR, n + '.zip')):
            return n


def trim():
    """the folder under MAX_TOTAL: the oldest reports go"""
    fs = sorted((os.path.join(DIR, f) for f in os.listdir(DIR)), key=os.path.getmtime)
    total = sum(os.path.getsize(f) for f in fs)
    for f in fs:
        if total <= MAX_TOTAL:
            break
        total -= os.path.getsize(f); os.remove(f)


class H(BaseHTTPRequestHandler):
    server_version = 'vxreport/1'
    timeout = 30

    def reply(self, code, text):
        b = text.encode()
        self.send_response(code); self.send_header('Content-Type', 'text/plain'); self.send_header('Content-Length', str(len(b)))
        self.end_headers(); self.wfile.write(b)

    def do_GET(self):
        IOSX = {'/iosextra': 'extra_roms.zip', '/iosextra/pitrex': 'pitrex.zip', '/iosextra/pack': 'pack.tgz',
                '/iosextra/packver': 'pack.ver'}
        import re as _re
        m = _re.fullmatch(r'/iosextra/part/([a-z0-9]{1,16})', self.path)   # (the pack in parts: pack-<id>.tgz, and pack.json)
        if m:
            IOSX[self.path] = 'pack-%s.tgz' % m.group(1)
        IOSX['/iosextra/packjson'] = 'pack.json'
        if self.path in IOSX:                  # (the owner's private iOS test builds: the owner's own extra ROMs and the
            # PiTrex's files as the PC has them, at the RetroArch build's build; the test app's content pack - everything
            # the PC has, tools/ios_pack.py - and its version, at the app's start; put there by hand; the /ioslog token)
            if not IOSLOG_TOKEN or not secrets.compare_digest(self.headers.get('X-Vectrex-Token', ''), IOSLOG_TOKEN):
                return self.reply(403, 'no')
            p = os.path.join(DIR, 'iosextra', IOSX[self.path])
            if not os.path.isfile(p):
                return self.reply(404, 'none')
            self.send_response(200); self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(os.path.getsize(p))); self.end_headers()
            with open(p, 'rb') as f:               # (streamed: the pack is ~130 MB)
                while True:
                    b = f.read(1 << 20)
                    if not b:
                        break
                    self.wfile.write(b)
            return
        self.reply(200, 'Vectrex_EMU debug reports: use Help > Debug report in the emulator.\n')

    def do_POST(self):
        if self.path == '/ioslog':
            return self.ioslog()
        if self.path != '/report':
            return self.reply(404, 'not here')
        try:
            n = int(self.headers.get('Content-Length', '0'))
        except ValueError:
            n = 0
        if n <= 0 or n > MAX_SIZE:
            return self.reply(413, 'the report must be 1 byte to 2 MB')
        ip, now = self.client_address[0], time.time()
        with lock:
            t = [x for x in recent.get(ip, []) if now - x < 86400]
            if len(t) >= REPORTS_PER_DAY:
                recent[ip] = t
                return self.reply(429, 'too many reports today - try again tomorrow')
            t.append(now); recent[ip] = t
            if len(recent) > 10000:
                recent.clear()
        data = self.rfile.read(n)
        try:
            zipfile.ZipFile(io.BytesIO(data)).testzip()   # (a zip, readable - nothing else is kept)
        except Exception:   # noqa
            return self.reply(400, 'not a report')
        ver = ''.join(c for c in self.headers.get('X-Vectrex-Version', '?')[:20] if c.isalnum() or c in '.-_')
        email = contact(data)
        with lock:
            num = new_number()
            with open(os.path.join(DIR, num + '.zip'), 'wb') as f:
                f.write(data)
            with open(os.path.join(DIR, num + '.json'), 'w') as f:
                meta = {'time': time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(now)) + ' UTC', 'version': ver, 'size': n}
                if email:
                    meta['email'] = email
                json.dump(meta, f)
            trim()
        self.reply(200, num)
        threading.Thread(target=telegram, args=(num, ver, data, email), daemon=True).start()

    def ioslog(self):
        """the owner's private iOS test build: its logs as text (POST /ioslog, header X-Vectrex-Token = IOSLOG_TOKEN
        from /etc/vxreport.env) -> DIR/ios/<time>-<kind>.txt; no daily limit, no Telegram, the folder under IOS_TOTAL"""
        if not IOSLOG_TOKEN or not secrets.compare_digest(self.headers.get('X-Vectrex-Token', ''), IOSLOG_TOKEN):
            return self.reply(403, 'no')
        try:
            n = int(self.headers.get('Content-Length', '0'))
        except ValueError:
            n = 0
        if n <= 0 or n > IOS_MAX:
            return self.reply(413, 'too big')
        data = self.rfile.read(n)
        kind = ''.join(c for c in self.headers.get('X-Vectrex-Kind', 'log')[:64] if c.isalnum() or c in '-_') or 'log'
        ext = self.headers.get('X-Vectrex-Ext', 'txt')
        ext = ext if ext in ('txt', 'png', 'json') else 'txt'   # (logs and the status, screenshots, iOS crash reports)
        d = os.path.join(DIR, 'ios'); os.makedirs(d, exist_ok=True)
        with lock:
            name = time.strftime('%Y%m%d-%H%M%S', time.gmtime()) + '-' + secrets.token_hex(2) + '-' + kind + '.' + ext
            with open(os.path.join(d, name), 'wb') as f:
                f.write(data)
            fs = sorted((os.path.getmtime(os.path.join(d, x)), os.path.join(d, x)) for x in os.listdir(d))
            total = sum(os.path.getsize(p) for _, p in fs)
            while fs and total > IOS_TOTAL:
                _, p = fs.pop(0); total -= os.path.getsize(p); os.remove(p)
        self.reply(200, name)

    def log_message(self, fmt, *a):                      # (no IPs in the journal)
        pass


if __name__ == '__main__':
    os.makedirs(DIR, exist_ok=True)
    ThreadingHTTPServer(('', PORT), H).serve_forever()
