"""CJ-011: side-view contact sheet and dimensions of Ultimate Gun Pack models (OBJ).

    blender -b --factory-startup --python tools/cj011/gun_sheet.py -- <OBJ dir> <out.png> <name> [<name> ...]

Prints each model's bounding box in its own units and renders the models side by side,
viewed from their right side, on a 0.1 m grid scaled by the assumed overall length
(GUN_LENGTH_M: a pistol 0.20 m, a rifle 0.90 m) for a first look at proportions.
"""
import bpy, math, os, sys
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
src, out, names = args[0], os.path.abspath(args[1]), args[2:]

for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)

x = 0.0
for name in names:
    before = set(bpy.data.objects)
    bpy.ops.wm.obj_import(filepath=os.path.join(src, name + ".obj"))
    objs = [o for o in bpy.data.objects if o not in before]
    pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    size = hi - lo
    print("GUN %-20s size x %.3f y %.3f z %.3f  min %s" % (name, size.x, size.y, size.z, tuple(round(v, 3) for v in lo)))
    # normalise: longest horizontal axis to 1 unit, laid out along X for the sheet
    longest = max(size.x, size.y)
    for o in objs:
        o.location -= (lo + hi) / 2
        if size.y > size.x:
            o.rotation_euler.z += math.pi / 2
    bpy.context.view_layer.update()
    for o in objs:
        o.scale = [s / longest for s in o.scale]
        o.location = o.location / longest
        o.location.x += x
        o.location.z += 0
    x += 1.2

cam_data = bpy.data.cameras.new("side")
cam_data.type = "ORTHO"
cam_data.ortho_scale = x
cam = bpy.data.objects.new("side", cam_data)
bpy.context.scene.collection.objects.link(cam)
cam.location = (x / 2 - 0.6, -10, 0)
cam.rotation_euler = (math.pi / 2, 0, 0)
bpy.context.scene.camera = cam
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 4
sun.rotation_euler = (math.radians(60), 0, math.radians(-30))
bpy.context.scene.collection.objects.link(sun)
world = bpy.data.worlds.new("w")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.8, 0.8, 0.8, 1)
bpy.context.scene.world = world
r = bpy.context.scene.render
r.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] else "CYCLES"
r.resolution_x, r.resolution_y = 300 * len(names), 220
r.filepath = out
bpy.ops.render.render(write_still=True)
print("SAVED", out)
