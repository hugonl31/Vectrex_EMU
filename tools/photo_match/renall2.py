import sys; sys.path.insert(0, r'G:\AI\Vectrex_EMU\tools')
from lrhost import Host
import numpy as np, os
S = r'C:\Users\hugob\AppData\Local\Temp\claude\011542675e50d3c9\59f591e1-b007-4e38-b9be-876bc46dc5d2\scratchpad'
T = r'G:\AI\Vectrex_EMU\build\Vectrex_EMU\roms'
look = 'vectrex'; TAG = sys.argv[1]
JOBS = [  # name, rom, [(button, presses)], idle frames before the grab
    ('intensity', T + r'\Tests\Test Cartridge Rev. 4 (Proto).bin', [('b4', 6)], 150),
    ('axis', T + r'\Tests\Axis Test.bin', [], 120),
    ('beam_p1', T + r'\Tests\Beam Test.bin', [('b1', 1)], 120), ('beam_p2', T + r'\Tests\Beam Test.bin', [('b2', 1)], 120),
    ('beam_p3', T + r'\Tests\Beam Test.bin', [('b3', 1)], 120), ('beam_p4', T + r'\Tests\Beam Test.bin', [('b4', 1)], 120),
    ('corner', T + r'\Tests\Corner Test.bin', [], 120),
    ('dac_p1', T + r'\Tests\DAC Test.bin', [('b1', 1)], 120), ('dac_p2', T + r'\Tests\DAC Test.bin', [('b2', 1)], 120),
    ('gap_p1', T + r'\Tests\Gap Test.bin', [('b1', 1)], 120), ('gap_p2', T + r'\Tests\Gap Test.bin', [('b2', 1)], 120),
    ('gap_p3', T + r'\Tests\Gap Test.bin', [('b3', 1)], 120),
    ('lag', T + r'\Tests\Lag Test.bin', [], 120),
    ('vectorblade', T + r'\Vectorblade.bin', [], 400),
    ('dot_p3', T + r'\Tests\Dot Test.bin', [('b3', 1)], 120), ('dot_p4', T + r'\Tests\Dot Test.bin', [('b4', 1)], 120),
]
for name, rom, presses, idle in JOBS:
    h = Host(S + r'\cb\sl.dll', 'G:/AI/Vectrex_EMU/libretro/dist/pkg/system', S + r'\save',
             options={'vectrex_emu_deck': 'off', 'vectrex_emu_rotate': 'off', 'vectrex_emu_skip_intro': 'on', 'vectrex_emu_wobble': 'off',
                      'vectrex_emu_bending': 'off', 'vectrex_emu_overlay': 'off', 'vectrex_emu_look': look}, size=(1200, 1500))
    h.load(rom)
    for i in range(60): h.run()
    for b, n in presses:
        for k in range(n):
            for i in range(3): h.run(**{b: 1})
            for i in range(10 if n > 1 else 3): h.run()
    for i in range(idle): h.run()
    np.save(S + r'\sl_%s_%s.npy' % (name, TAG), h.grab().astype(np.uint8)); h.close(); print(name)
