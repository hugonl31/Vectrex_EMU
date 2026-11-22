"""photo_pv.py: the Photo Test's sixteen pages stepped with button 4 (and back once with button 1) -> testcart/photo_test_pages.png"""
import sys; sys.path.insert(0, r'G:\AI\Vectrex_EMU\tools')
from lrhost import Host
import numpy as np, os
from PIL import Image
S = os.path.dirname(os.path.abspath(__file__)); tiles = []
h = Host(S + r'\cb\sl.dll', 'G:/AI/Vectrex_EMU/libretro/dist/pkg/system', S + r'\save',
         options={'vectrex_emu_deck': 'off', 'vectrex_emu_rotate': 'off', 'vectrex_emu_skip_intro': 'on', 'vectrex_emu_wobble': 'off', 'vectrex_emu_bending': 'off', 'vectrex_emu_overlay': 'off', 'vectrex_emu_look': 'vectrex'}, size=(1200, 1500))
h.load(r'G:\AI\Vectrex_EMU\testcart\photo_test.bin')
for i in range(60): h.run()
for p in range(16):
    for i in range(90): h.run()
    tiles.append(np.array(Image.fromarray(h.grab().astype(np.uint8)).resize((564, 720))))
    for i in range(3): h.run(b4=1)
for i in range(60): h.run()          # wrapped to page 1
for i in range(3): h.run(b1=1)       # back: page 16
for i in range(90): h.run()
back = np.array(Image.fromarray(h.grab().astype(np.uint8)).resize((564, 720))); h.close()
rows = [np.concatenate(tiles[r * 4:(r + 1) * 4], 1) for r in range(4)]
Image.fromarray(np.concatenate(rows, 0)).save(r'G:\AI\Vectrex_EMU\testcart\photo_test_pages.png')
Image.fromarray(np.concatenate([tiles[15], back], 1)).save(S + r'\sw\photo_back.png')
print('page 16 again after button 1 from page 1:', float(abs(tiles[15].astype(int) - back.astype(int)).mean()) < 2.0)
