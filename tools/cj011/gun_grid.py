"""CJ-011: one gun from the right side with its model coordinates, to read off grip points.

    blender -b --factory-startup --python tools/cj011/gun_grid.py -- <model.obj> <out.png>

Writes <out.png> (the render) and <out.json> (the mapping from pixels to model X and Z: the
model's X axis runs along the barrel, muzzle at +X, Z up, as the OBJ import gives them).
"""
import bpy, json, math, os, sys
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
src, out = args[0], os.path.abspath(args[1])
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)
bpy.ops.wm.obj_import(filepath=src)
objs = [o for o in bpy.data.objects if o.type == "MESH"]
pts = [o.matrix_world @ Vector(c) for o in objs for c in o.bound_box]
lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
pad = 0.1 * (hi.x - lo.x)
w, h = hi.x - lo.x + 2 * pad, hi.z - lo.z + 2 * pad
cx, cz = (lo.x + hi.x) / 2, (lo.z + hi.z) / 2
PX = 1600
cam_data = bpy.data.cameras.new("side")
cam_data.type = "ORTHO"
cam_data.ortho_scale = max(w, h)
cam = bpy.data.objects.new("side", cam_data)
bpy.context.scene.collection.objects.link(cam)
cam.location = (cx, -20, cz)
cam.rotation_euler = (math.pi / 2, 0, 0)
cam_data.clip_end = 100
bpy.context.scene.camera = cam
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 4
sun.rotation_euler = (math.radians(60), 0, math.radians(-30))
bpy.context.scene.collection.objects.link(sun)
world = bpy.data.worlds.new("w")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.9, 0.9, 0.9, 1)
bpy.context.scene.world = world
r = bpy.context.scene.render
r.engine = "BLENDER_WORKBENCH"
r.resolution_x = PX
r.resolution_y = round(PX * h / w) if w >= h else PX
r.filepath = out
bpy.ops.render.render(write_still=True)
scale = max(w, h) / max(r.resolution_x, r.resolution_y)
json.dump({"x0": cx - r.resolution_x * scale / 2, "z0": cz + r.resolution_y * scale / 2, "units_per_px": scale,
           "lo": list(lo), "hi": list(hi)}, open(os.path.splitext(out)[0] + ".json", "w"))
print("SAVED", out)
