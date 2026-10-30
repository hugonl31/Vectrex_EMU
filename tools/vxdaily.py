#!/usr/bin/env python3
"""vxdaily.py - once a day to the author's Telegram chat: the online games of the last 24 hours (from vxrelay's own
log lines 'CODE host IP:PORT GAME [(public: GAMER)]' and 'CODE join IP:PORT'), the debug reports filed, and
whether the meeting point and the report receiver run. Nothing is stored; IPs are only counted.

Install (Linode): cp vxdaily.py /usr/local/bin/ ; the token and chat in /etc/vxreport.env (as vxreport.py)
  /etc/systemd/system/vxdaily.service:  [Service] Type=oneshot EnvironmentFile=/etc/vxreport.env
                                         ExecStart=/usr/bin/python3 /usr/local/bin/vxdaily.py
  /etc/systemd/system/vxdaily.timer:    [Timer] OnCalendar=*-*-* 11:00 Europe/Amsterdam  Persistent=true
                                         [Install] WantedBy=timers.target
  systemctl daemon-reload && systemctl enable --now vxdaily.timer
Test: VXDAILY_PRINT=1 python3 /usr/local/bin/vxdaily.py (prints, sends nothing)
"""
import os, re, json, time, subprocess, collections, urllib.request, urllib.parse

HOURS = int(os.environ.get('VXDAILY_HOURS', '24'))
REPORTS = os.environ.get('VXREPORT_DIR', '/var/lib/private/vxreports')
HOST = re.compile(r'^(\w+) host ([\d.:a-f\[\]]+):\d+ (.*?)( \(public: (.*)\))?$')
JOIN = re.compile(r'^(\w+) join ')


def main():
    log = subprocess.run(['journalctl', '-u', 'vxrelay', '--since', '-%dh' % HOURS, '-o', 'cat', '--no-pager'],
                         capture_output=True, text=True).stdout.splitlines()
    pub = priv = 0
    games, ips, hosted, joined = collections.Counter(), set(), set(), set()
    for ln in log:
        m = HOST.match(ln.strip())
        if m:
            hosted.add(m.group(1)); ips.add(m.group(2))
            if m.group(4): pub += 1
            else: priv += 1
            games[os.path.splitext(m.group(3))[0] or '?'] += 1
            continue
        m = JOIN.match(ln.strip())
        if m:
            joined.add(m.group(1))
    played = len(hosted & joined)
    reports = 0
    try:
        for f in os.listdir(REPORTS):
            if f.endswith('.zip') and time.time() - os.path.getmtime(os.path.join(REPORTS, f)) < HOURS * 3600:
                reports += 1
    except OSError:
        pass
    def active(u):
        return subprocess.run(['systemctl', 'is-active', u], capture_output=True, text=True).stdout.strip()
    lines = ['Vectrex_EMU, last %d hours' % HOURS,
             'Games hosted: %d (public %d, private %d)' % (pub + priv, pub, priv),
             'Games joined by a second player: %d' % played,
             'Different hosts: %d' % len(ips)]
    if games:
        lines.append('Games: ' + ', '.join('%s %d' % (g, n) for g, n in games.most_common(6)))
    lines.append('Debug reports: %d' % reports)
    st = {u: active(u) for u in ('vxrelay', 'vxreport')}
    lines.append('Meeting point %s, report receiver %s' % (st['vxrelay'], st['vxreport']))
    text = '\n'.join(lines)
    if os.environ.get('VXDAILY_PRINT'):
        print(text); return
    try:                                                # (/daily off in the bot - vxbot.py)
        if not json.load(open('/var/lib/vxbot/state.json')).get('daily', True):
            return
    except (OSError, ValueError):
        pass
    tok, chat = os.environ.get('TELEGRAM_TOKEN', ''), os.environ.get('TELEGRAM_CHAT', '')
    if tok and chat:
        data = urllib.parse.urlencode({'chat_id': chat, 'text': text}).encode()
        urllib.request.urlopen('https://api.telegram.org/bot%s/sendMessage' % tok, data=data, timeout=30).read()


if __name__ == '__main__':
    main()
