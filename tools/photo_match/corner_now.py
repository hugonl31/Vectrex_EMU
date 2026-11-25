import sys, tempfile, numpy as np
sys.path.insert(0, r'G:\AI\Vectrex_EMU\tools'); from lrhost import Host
from PIL import Image
look = sys.argv[1]; out = sys.argv[2]
h = Host(r'G:\AI\Vectrex_EMU\libretro\vectrex_emu_libretro.dll', 'G:/AI/Vectrex_EMU/libretro/dist/pkg/system', tempfile.mkdtemp(),
         options={'vectrex_emu_deck': 'off', 'vectrex_emu_rotate': 'off', 'vectrex_emu_skip_intro': 'on', 'vectrex_emu_wobble': 'off',
                  'vectrex_emu_bending': 'off', 'vectrex_emu_overlay': 'off', 'vectrex_emu_look': look, 'vectrex_emu_case': 'off',
                  'vectrex_emu_glass_edge': 'off', 'vectrex_emu_room': 'off (dark room)'}, size=(1200, 1500))
h.load(r'G:\AI\Vectrex_EMU\build\Vectrex_EMU\roms\Tests\Corner Test.bin')
for i in range(200): h.run()
Image.fromarray(h.grab().astype(np.uint8)[..., :3]).save(out); h.close()
