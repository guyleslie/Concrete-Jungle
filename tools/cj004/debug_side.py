"""CJ-004 debug: side and top views of the retargeted civilian next to the library mannequin.

    blender -b <civilian-anim.blend> --python tools/cj004/debug_side.py -- <UAL .glb> <out dir> <action> <frame> [<frame> ...]

Writes <out>/side_<frame>.png: the civilian (left) and the library mannequin playing the
library's own action (right), seen from the side (left) and from above (right half).
"""
import bpy, math, os, sys
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
glb, out, action_name, frames = args[0], os.path.abspath(args[1]), args[2], [int(f) for f in args[3:]]
os.makedirs(out, exist_ok=True)
scene = bpy.context.scene

civ = bpy.data.objects["civilian_rig"]
act = bpy.data.actions[action_name]
civ.animation_data_create()
civ.animation_data.action = act
if act.slots:
    civ.animation_data.action_slot = act.slots[0]

before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=glb)
lib = next(o for o in bpy.data.objects if o not in before and o.type == "ARMATURE")
lib.location.x = 1.2
lib_act = next(a for a in bpy.data.actions if a.name.startswith(action_name) and a is not act)
lib.animation_data.action = lib_act
if lib_act.slots:
    lib.animation_data.action_slot = lib_act.slots[0]

cam_data = bpy.data.cameras.new("dbg")
cam_data.type = "ORTHO"
cam = bpy.data.objects.new("dbg", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 4
sun.rotation_euler = (math.radians(40), 0, math.radians(30))
scene.collection.objects.link(sun)
r = scene.render
r.engine = "BLENDER_WORKBENCH"
r.resolution_x, r.resolution_y = 768, 512
r.film_transparent = False
r.image_settings.file_format = "PNG"

views = {"side": ((-4.0, 0.6, 0.95), (math.radians(90), 0, math.radians(-90)), 2.6),
         "top": ((0.6, 0.0, 6.0), (0, 0, 0), 2.8)}
for f in frames:
    scene.frame_set(f)
    for name, (loc, rot, scale) in views.items():
        cam.location, cam.rotation_euler, cam_data.ortho_scale = loc, rot, scale
        r.filepath = os.path.join(out, "%s_%02d.png" % (name, f))
        bpy.ops.render.render(write_still=True)
    print("DEBUG", f)
