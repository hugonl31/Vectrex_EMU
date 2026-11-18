"""render_ios_icon_blender.py - the iOS app's icon rendered from the finished VR model (Vectrex_EMU_EmuVR
vr_version_export/vectrex_vr_rounded.obj): black plastic, the screen a flat grey (the desktop icon's), the controller
plugs left out, seen from the front a little from above and from the right (the cartridge port side), no ground, on a
transparent film; then cropped and set on an off-white ground at 84 % of 1024 x 1024 -> ios_icon_1024.png.
  RES=2400 SAMPLES=256 KEY=12 OUT=final.png blender -b -P render_ios_icon_blender.py
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import bpy, math, os, sys
D = os.path.dirname(os.path.abspath(__file__))
AZ = float(os.environ.get('AZ', '28')); EL = float(os.environ.get('EL', '14')); RES = int(os.environ.get('RES', '1024'))
SAMPLES = int(os.environ.get('SAMPLES', '160')); KEY = float(os.environ.get('KEY', '12')); OUT = os.environ.get('OUT', 'render.png')
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
bpy.ops.wm.obj_import(filepath='G:/AI/Vectrex_EMU_EmuVR/vr_version_export/vectrex_vr_rounded.obj', global_scale=0.001,
                      forward_axis='NEGATIVE_Z', up_axis='Y', use_split_groups=True)


def mat(name, col, rough, spec=0.5, bump=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree; b = nt.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = col + (1,); b.inputs['Roughness'].default_value = rough
    b.inputs['Specular IOR Level'].default_value = spec
    if bump:   # (the plastic's fine grain)
        tc = nt.nodes.new('ShaderNodeTexCoord'); nz = nt.nodes.new('ShaderNodeTexNoise'); bp = nt.nodes.new('ShaderNodeBump')
        nz.inputs['Scale'].default_value = 900.0; nz.inputs['Detail'].default_value = 4.0
        bp.inputs['Strength'].default_value = bump; bp.inputs['Distance'].default_value = 0.0004
        nt.links.new(tc.outputs['Object'], nz.inputs['Vector']); nt.links.new(nz.outputs['Fac'], bp.inputs['Height'])
        nt.links.new(bp.outputs['Normal'], b.inputs['Normal'])
    return m


lin = lambda c: (c / 12.92) if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
plastic = mat('black plastic', (0.010, 0.010, 0.011), 0.42, 0.5, 0.0)
screen = mat('screen', (lin(150 / 255), lin(152 / 255), lin(148 / 255)), 0.10, 0.6)
_b = screen.node_tree.nodes['Principled BSDF']; _b.inputs['Base Color'].default_value = (0.02, 0.02, 0.02, 1)
_b.inputs['Emission Color'].default_value = (lin(150 / 255), lin(152 / 255), lin(148 / 255), 1); _b.inputs['Emission Strength'].default_value = 1.0
metal = mat('plug', (0.03, 0.03, 0.032), 0.35, 0.6)
for o in list(bpy.context.scene.objects):
    if o.type != 'MESH': continue
    if 'de9' in o.name.lower(): bpy.data.objects.remove(o); continue   # (the controller plugs out: the ports empty)
    print('part', o.name, len(o.data.polygons))
    o.data.materials.clear()
    n = o.name.lower()
    o.data.materials.append(screen if 'screen' in n else metal if 'de9' in n else plastic)
    for p in o.data.polygons: p.use_smooth = True; p.material_index = 0
    for sl in o.material_slots: sl.link = 'DATA'
# the ground: a shadow catcher
# (no ground: an icon's subject on its own)
# light: a big soft key above front-left, a fill from the right, a rim behind (the black case's edges against the ground)
def area(name, loc, energy, size, rot_to=(0, 0, 0.18)):
    L = bpy.data.lights.new(name, 'AREA'); L.energy = energy; L.size = size; o = bpy.data.objects.new(name, L); sc.collection.objects.link(o)
    o.location = loc; d = [rot_to[i] - loc[i] for i in range(3)]
    o.rotation_euler = (math.atan2(math.hypot(d[0], d[1]), -d[2]), 0, math.atan2(d[1], d[0]) - math.pi / 2)
area('key', (-0.9, -1.1, 1.2), KEY, 1.4)
area('fill', (1.2, -0.8, 0.5), KEY * 0.25, 1.2)
area('rim', (0.3, 1.0, 1.0), KEY * 1.6, 1.0)
area('top', (0.0, 0.0, 1.5), KEY * 0.2, 1.4)
w = bpy.data.worlds.new('w'); w.use_nodes = True; w.node_tree.nodes['Background'].inputs['Color'].default_value = (0.8, 0.8, 0.82, 1)
w.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.03; sc.world = w
# the camera: AZ degrees to the right of straight ahead, EL above
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); sc.collection.objects.link(cam); sc.camera = cam
cam.data.lens = 85; tgt = (0.0, 0.0, 0.178); r = 2.05
a, e = math.radians(AZ), math.radians(EL)
cam.location = (tgt[0] + r * math.sin(a) * math.cos(e), tgt[1] - r * math.cos(a) * math.cos(e), tgt[2] + r * math.sin(e))
d = [tgt[i] - cam.location[i] for i in range(3)]
cam.rotation_euler = (math.atan2(math.hypot(d[0], d[1]), -d[2]), 0, math.atan2(d[1], d[0]) - math.pi / 2)
sc.render.engine = 'CYCLES'; sc.cycles.samples = SAMPLES; sc.cycles.use_denoising = True
try:
    pr = bpy.context.preferences.addons['cycles'].preferences
    for t in ('OPTIX', 'CUDA', 'HIP', 'ONEAPI'):
        try:
            pr.compute_device_type = t; pr.get_devices(); dev = [x for x in pr.devices if x.type == t]
            if dev:
                for x in pr.devices: x.use = x.type == t
                sc.cycles.device = 'GPU'; print('GPU', t); break
        except Exception: pass
except Exception as ex: print('CPU', ex)
sc.render.film_transparent = True; sc.render.resolution_x = sc.render.resolution_y = RES
sc.view_settings.view_transform = 'Standard'
sc.render.image_settings.color_mode = 'RGBA'; sc.render.filepath = os.path.join(D, OUT)
bpy.ops.render.render(write_still=True)
