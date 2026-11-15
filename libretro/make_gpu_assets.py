"""The GPU path's data from the desktop Vectrex_EMU's own models (run with the desktop's Python: numpy):
  gpu_tab.h                     the glass's and the case's outlines (GLASS_LUT, casemodel.CASE_LUT) and the light's
                                place (roomlight.place, casemodel.light_pos) - compiled into the core
  gpu/glass_light.bin           roomlight.maps at the default Light position: R, G as float32, MAP_W x MAP_H, top row first
  gpu/case_scratch.png          casemodel.scratch_map: R the tube's glint, G the room's, top row first
The core loads the two files from <system>/vectrex_emu/gpu (package_android.sh copies them there).
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import os, sys, struct, zlib, math
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'src', 'frontend'))
import vecrender, roomlight, casemodel

LX, LY = 0.0, 0.273                     # (vecrender: light_x, light_y - the default Light position)
os.makedirs(os.path.join(HERE, 'gpu'), exist_ok=True)

def png(path, w, h, rows):              # (RGBA8, top row first)
    raw = b''.join(b'\0' + r for r in rows)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

# the light map
R, G = roomlight.maps(LX, LY, vecrender.GLASS_REF_AW, vecrender.GLASS_N)
with open(os.path.join(HERE, 'gpu', 'glass_light.bin'), 'wb') as f:
    f.write(struct.pack('<%df' % len(R), *R)); f.write(struct.pack('<%df' % len(G), *G))
# the case's scratches (the map comes bottom row first: turned back)
w, h, data, bounds = casemodel.scratch_map(vecrender.GLASS_LUT)
rows = [data[y * w * 4:(y + 1) * w * 4] for y in range(h)][::-1]
png(os.path.join(HERE, 'gpu', 'case_scratch.png'), w, h, rows)

(gx, gy), _ = roomlight.place(LX, LY)
lc = casemodel.light_pos(gx, gy)
fl = lambda v: ('%.6ff' % v)
with open(os.path.join(HERE, 'gpu_tab.h'), 'w', newline='\n') as f:
    f.write('/* gpu_tab.h - made by make_gpu_assets.py from the desktop Vectrex_EMU (vecrender, roomlight, casemodel): do not edit */\n')
    f.write('#define GPU_GLASS_LUT "%s"\n' % ', '.join('%.5f' % v for v in vecrender.GLASS_LUT))
    f.write('#define GPU_CASE_LUT "%s"\n' % ', '.join('%.5f' % v for v in casemodel.CASE_LUT))
    f.write('static const float GPU_SCR_BOUNDS[4] = { %s };   /* x0, x1, y0, y1 (units, y down) */\n' % ', '.join(fl(v) for v in bounds))
    f.write('static const int GPU_LMAP_W = %d, GPU_LMAP_H = %d;\n' % (roomlight.MAP_W, roomlight.MAP_H))
    f.write('static const float GPU_LMR[4] = { %s };\n' % ', '.join(fl(v) for v in (roomlight.MAP_U[0], roomlight.MAP_U[1], roomlight.MAP_V[0], roomlight.MAP_V[1])))
    f.write('static const float GPU_LPOS[3] = { %s, %s, %s };   /* the fixture\'s point for the glass, its size */\n' % (fl(gx), fl(gy), fl(roomlight.GLASS_SIZE)))
    f.write('static const float GPU_LC[3] = { %s, %s, %s };   /* the ceiling light, cm */\n' % tuple(fl(v) for v in lc))
    f.write('static const float GPU_LANG = %s;\n' % fl(roomlight.ANG))
    f.write('static const float GPU_GSC[2] = { %s, %s };\n' % (fl(vecrender.GLASS_AW / vecrender.GLASS_REF_AW), fl(vecrender.GLASS_AH / vecrender.GLASS_REF_AH)))
    f.write('static const float GPU_EDGE = %s, GPU_GLASS_GREY = %s, GPU_ART_AW = %s, GPU_ART_AH = %s, GPU_ROOM_MAX = %s;\n'
            % (fl(vecrender.GLASS_EDGE), fl(vecrender.GLASS_GREY), fl(vecrender.ART_AW), fl(vecrender.ART_TUBE_K), fl(vecrender.ROOM_MAX)))
print('gpu assets:', w, 'x', h, 'scratches;', roomlight.MAP_W, 'x', roomlight.MAP_H, 'light map; light', gx, gy, lc)
