"""make_um2.py - builds movies.um2: the ARM build (arm-none-eabi-gcc in WSL), the .um2 header.
A .um2 file: "2CMU", version 1, 1, the load address (0x20000000), the image's length in 32-bit words, the image."""
import os, struct, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
wsl = '/mnt/' + HERE[0].lower() + HERE[2:].replace('\\', '/')
cmd = ('cd "%s" && arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -O2 -ffreestanding -nostdlib -fno-builtin '
       '-Wall -Wno-misleading-indentation -T link.ld -o movies.elf player.c -lgcc && '
       'arm-none-eabi-objcopy -O binary movies.elf movies.bin && arm-none-eabi-size movies.elf' % wsl)
r = subprocess.run(['wsl', '-e', 'bash', '-lc', cmd], capture_output=True, text=True)
print(r.stdout, r.stderr)
if r.returncode:
    sys.exit(1)
img = open(os.path.join(HERE, 'movies.bin'), 'rb').read()
img += b'\0' * (-len(img) % 4)
out = b'2CMU' + struct.pack('<4I', 1, 1, 0x20000000, len(img) // 4) + img
open(os.path.join(HERE, 'movies.um2'), 'wb').write(out)
print('movies.um2: %d bytes' % len(out))
