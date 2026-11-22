import sys; sys.path.insert(0, r'G:\AI\Vectrex_EMU\tools')
from lrhost import Host
import numpy as np, os
from PIL import Image
S = os.path.dirname(os.path.abspath(__file__)); tiles = []
for p in (1, 2, 3, 4):
    h = Host(S + r'\cb\sl.dll', 'G:/AI/Vectrex_EMU/libretro/dist/pkg/system', S + r'\save',
             options={'vectrex_emu_deck': 'off', 'vectrex_emu_rotate': 'off', 'vectrex_emu_skip_intro': 'on', 'vectrex_emu_wobble': 'off', 'vectrex_emu_bending': 'off', 'vectrex_emu_overlay': 'off', 'vectrex_emu_look': 'vectrex'}, size=(1200, 1500))
    h.load(r'G:\AI\Vectrex_EMU\testcart\short_test.bin')
    for i in range(60): h.run()
    for i in range(3): h.run(**{'b%d' % p: 1})
    for i in range(120): h.run()
    a = h.grab().astype(np.uint8); np.save(S + r'\sl_short_p%d.npy' % p, a); tiles.append(a); h.close()
Image.fromarray(np.concatenate([np.concatenate(tiles[:2], 1), np.concatenate(tiles[2:], 1)], 0)).save(r'G:\AI\Vectrex_EMU\testcart\short_test_pages.png')
