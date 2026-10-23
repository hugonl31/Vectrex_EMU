"""make_um2.py - builds demo.um2: the sine table, the ARM build (arm-none-eabi-gcc in WSL), the .um2 header.
A .um2 file: "2CMU", version 1, 1, the load address (0x20000000), the image's length in 32-bit words, the image."""
import math, os, struct, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(HERE, 'sintab.h'), 'w') as f:
    f.write('/* sintab.h - made by make_um2.py: sin of a/256 turns, x 16384 */\n')
    f.write('static const short sin_q14[256] = {\n')
    v = [round(16384 * math.sin(2 * math.pi * i / 256)) for i in range(256)]
    for i in range(0, 256, 16):
        f.write('\t' + ', '.join('%d' % x for x in v[i:i + 16]) + ',\n')
    f.write('};\n')
wsl = '/mnt/' + HERE[0].lower() + HERE[2:].replace('\\', '/')
cmd = ('cd "%s" && arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -O2 -ffreestanding -nostdlib -fno-builtin '
       '-Wall -Wno-misleading-indentation -T link.ld -o demo.elf demo.c -lgcc && '
       'arm-none-eabi-objcopy -O binary demo.elf demo.bin && arm-none-eabi-size demo.elf' % wsl)
r = subprocess.run(['wsl', '-e', 'bash', '-lc', cmd], capture_output=True, text=True)
print(r.stdout, r.stderr)
if r.returncode:
    sys.exit(1)
img = open(os.path.join(HERE, 'demo.bin'), 'rb').read()
img += b'\0' * (-len(img) % 4)
out = b'2CMU' + struct.pack('<4I', 1, 1, 0x20000000, len(img) // 4) + img
open(os.path.join(HERE, 'demo.um2'), 'wb').write(out)
print('demo.um2: %d bytes' % len(out))
