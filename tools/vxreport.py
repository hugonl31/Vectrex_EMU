#!/usr/bin/env python3
"""vxreport.py - receives Vectrex_EMU debug reports (Help > Debug report > Send) and gives each a short number.

POST /report  body: the report's zip (at most 2 MB)  header X-Vectrex-Version: the sender's version
  -> 200 text/plain 'R-XXXXXX' (the number the user gives the author), or 4xx/5xx with a short reason.
Stored as DIR/R-XXXXXX.zip plus DIR/R-XXXXXX.json (time, version, size). The sender's IP is kept in memory only
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


def telegram(num, ver, data):
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
    cap = ('New debug report %s (%s)\n' % (num, ver) + '\n'.join(r[:150] for r in rows))[:1000]
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
        self.reply(200, 'Vectrex_EMU debug reports: use Help > Debug report in the emulator.\n')

    def do_POST(self):
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
        with lock:
            num = new_number()
            with open(os.path.join(DIR, num + '.zip'), 'wb') as f:
                f.write(data)
            with open(os.path.join(DIR, num + '.json'), 'w') as f:
                json.dump({'time': time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(now)) + ' UTC', 'version': ver, 'size': n}, f)
            trim()
        self.reply(200, num)
        threading.Thread(target=telegram, args=(num, ver, data), daemon=True).start()

    def log_message(self, fmt, *a):                      # (no IPs in the journal)
        pass


if __name__ == '__main__':
    os.makedirs(DIR, exist_ok=True)
    ThreadingHTTPServer(('', PORT), H).serve_forever()
