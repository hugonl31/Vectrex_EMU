import sys; sys.path.insert(0, r'G:\AI\Vectrex_EMU\tools')
from lrhost import Host
import numpy as np, shutil, os
S = r'C:\Users\hugob\AppData\Local\Temp\claude\011542675e50d3c9\59f591e1-b007-4e38-b9be-876bc46dc5d2\scratchpad'
dll, look = sys.argv[1], sys.argv[2]
h = Host(S + r'\cb\%s.dll' % dll, 'G:/AI/Vectrex_EMU/libretro/dist/pkg/system', S + r'\save',
         options={'vectrex_emu_deck': 'off', 'vectrex_emu_rotate': 'off', 'vectrex_emu_skip_intro': 'on', 'vectrex_emu_wobble': 'off', 'vectrex_emu_bending': 'off', 'vectrex_emu_look': look}, size=(1200, 1500))
h.load(r'G:\AI\Vectrex_EMU\testcart\dot_test.bin')
for i in range(300): h.run()
PG = [int(c) for c in (sys.argv[4] if len(sys.argv) > 4 else '1234')]
for p in [q - 1 for q in PG]:
    for i in range(4): h.run(**{'b%d' % (p + 1): 1})
    for i in range(60): h.run()
    np.save(S + r'\r_%s_%s_p%d.npy' % (sys.argv[3] if len(sys.argv) > 3 else dll, look, p + 1), h.grab().astype(np.uint8))
