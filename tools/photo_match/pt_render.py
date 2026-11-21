"""pt_render.py <tag> <pages>: the Photo Test's pages as the libretro core draws them -> pt/emu_<tag>_p<n>.npy (settings from the environment, LROPT for unit options)"""
import sys, os; sys.path.insert(0, r'G:\AI\Vectrex_EMU\tools')
from lrhost import Host
import numpy as np
S = os.path.dirname(os.path.abspath(__file__)); tag = sys.argv[1]; pages = [int(x) for x in sys.argv[2].split(',')]
opt = {'vectrex_emu_deck': 'off', 'vectrex_emu_rotate': 'off', 'vectrex_emu_skip_intro': 'on', 'vectrex_emu_wobble': 'off', 'vectrex_emu_bending': 'off', 'vectrex_emu_overlay': 'off', 'vectrex_emu_look': 'vectrex'}
opt.update(dict(kv.split('=') for kv in os.environ.get('LROPT', '').split(',') if kv))
h = Host(S + r'\cb\sl.dll', 'G:/AI/Vectrex_EMU/libretro/dist/pkg/system', S + r'\save', options=opt, size=(1200, 1500))
h.load(r'G:\AI\Vectrex_EMU\testcart\photo_test.bin')
for i in range(60): h.run()
for p in range(1, 17):
    for i in range(80): h.run()
    if p in pages: np.save(S + r'\pt\emu_%s_p%d.npy' % (tag, p), h.grab().astype(np.uint8))
    for i in range(3): h.run(b4=1)
h.close()
