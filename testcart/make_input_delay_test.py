"""make_input_delay_test.py - the Input Delay Test cart's images (input_delay_test.asm is written by hand - nothing to
generate). --pack: input_delay_test_raw.bin (lwasm) -> input_delay_test.bin (32 KB) and input_delay_test_64k.bin
(twice, for a 64 KB EPROM). Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import os
here = os.path.dirname(os.path.abspath(__file__))
raw = os.path.join(here, 'input_delay_test_raw.bin')
if '--pack' in os.sys.argv and os.path.exists(raw):
    d = open(raw, 'rb').read(); assert len(d) <= 32768, len(d)
    d += b'\xff' * (32768 - len(d))
    open(os.path.join(here, 'input_delay_test.bin'), 'wb').write(d)
    open(os.path.join(here, 'input_delay_test_64k.bin'), 'wb').write(d + d)
    os.remove(raw); print('packed input_delay_test.bin (32 KB), input_delay_test_64k.bin')
