"""paged_pv.py <stem> <npages>: a paged cart's pages (button 4) -> testcart/<stem>_pages.png; and each page's drawing span (headless)"""
import sys, os; sys.path.insert(0, r'G:\AI\Vectrex_EMU\tools')
from lrhost import Host
import numpy as np
from PIL import Image
S = os.path.dirname(os.path.abspath(__file__)); stem = sys.argv[1]; n = int(sys.argv[2]); tiles = []
h = Host(S + r'\cb\sl.dll', 'G:/AI/Vectrex_EMU/libretro/dist/pkg/system', S + r'\save', options={'vectrex_emu_deck': 'off', 'vectrex_emu_rotate': 'off', 'vectrex_emu_skip_intro': 'on', 'vectrex_emu_wobble': 'off', 'vectrex_emu_bending': 'off', 'vectrex_emu_overlay': 'off', 'vectrex_emu_look': 'vectrex'}, size=(1200, 1500))
h.load(r'G:\AI\Vectrex_EMU\testcart\%s.bin' % stem)
for i in range(60): h.run()
for p in range(n):
    for i in range(90): h.run()
    tiles.append(np.array(Image.fromarray(h.grab().astype(np.uint8)).resize((564, 720))))
    for i in range(3): h.run(b4=1)
h.close()
Image.fromarray(np.concatenate(tiles, 1)).save(r'G:\AI\Vectrex_EMU\testcart\%s_pages.png' % stem)
sys.path.insert(0, r'G:\AI\Vectrex_EMU\src\frontend'); os.chdir(r'G:\AI\Vectrex_EMU')
import vectrex_emu as V
c = V.Core(r'G:\AI\Vectrex_EMU\testcart\%s.bin' % stem, False, 'auto', 'off', None, None, None, False)
def fr(b=0): return list(c.frame(0, 0, b, 0)[0])
for i in range(900): fr()
for p in range(n):
    for i in range(40): L = fr()
    print('page %d: drawing spans %.0f cycles' % (p + 1, max(l[6] + l[5] / 8.0 for l in L) - min(l[6] for l in L)))
    for i in range(3): fr(8)
