"""cart_pv.py <stem>: the four pages of testcart/<stem>.bin as the libretro core draws them -> testcart/<stem>_pages.png, and the frame's cycles"""
import sys; sys.path.insert(0, r'G:\AI\Vectrex_EMU\tools')
from lrhost import Host
import numpy as np, os
from PIL import Image
S = os.path.dirname(os.path.abspath(__file__)); stem = sys.argv[1]; tiles = []
for p in (1, 2, 3, 4):
    h = Host(S + r'\cb\sl.dll', 'G:/AI/Vectrex_EMU/libretro/dist/pkg/system', S + r'\save',
             options={'vectrex_emu_deck': 'off', 'vectrex_emu_rotate': 'off', 'vectrex_emu_skip_intro': 'on', 'vectrex_emu_wobble': 'off', 'vectrex_emu_bending': 'off', 'vectrex_emu_overlay': 'off', 'vectrex_emu_look': 'vectrex'}, size=(1200, 1500))
    h.load(r'G:\AI\Vectrex_EMU\testcart\%s.bin' % stem)
    for i in range(60): h.run()
    for i in range(3): h.run(**{'b%d' % p: 1})
    for i in range(120): h.run()
    tiles.append(h.grab().astype(np.uint8)); h.close()
Image.fromarray(np.concatenate([np.concatenate(tiles[:2], 1), np.concatenate(tiles[2:], 1)], 0)).save(r'G:\AI\Vectrex_EMU\testcart\%s_pages.png' % stem)
