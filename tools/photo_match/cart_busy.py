"""cart_busy.py <stem>: the longest frame (cycles) of each page - the core's stats, one process a page"""
import sys, subprocess, re, os
S = os.path.dirname(os.path.abspath(__file__))
if len(sys.argv) > 2:
    sys.path.insert(0, r'G:\AI\Vectrex_EMU\tools'); from lrhost import Host
    p = int(sys.argv[2])
    for k in range(2):
        h = Host(S + r'\cb\sl.dll', 'G:/AI/Vectrex_EMU/libretro/dist/pkg/system', S + r'\save', options={'vectrex_emu_skip_intro': 'on'}, size=(300, 400))
        h.load(r'G:\AI\Vectrex_EMU\testcart\%s.bin' % sys.argv[1])
        if k == 0:
            for i in range(60): h.run()
            for i in range(3): h.run(**{'b%d' % p: 1})
            for i in range(100): h.run()
        h.close()
else:
    for p in (1, 2, 3, 4):
        o = subprocess.run([sys.executable, __file__, sys.argv[1], str(p)], capture_output=True, text=True); t = o.stdout + o.stderr
        m = re.findall(r'maxbusy=(\d+) over30000=(\d+)', t); print('page', p, 'longest frame', m[0][0] if m else '?', 'cycles; frames over 30000:', m[0][1] if m else '?')
