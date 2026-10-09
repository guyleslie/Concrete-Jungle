"""CJ-029: choose each gait's leg settings for a look so that, seen from above, as much leg shows
ahead of the body as behind it.

    blender -b <civilian.blend> --python tools/cj004/tune_gait.py -- <UAL .glb> <out.json> [walk] [jog] [run]

For every hip-swing shift and upper-body correction a gait lists in gaits.py, the civilian is
posed on the gait's atlas frames (retarget_ual.Retargeter, nothing keyed) and a small legs mask
is rendered from straight above (192 px over 2.1 m). Measured per frame, and the largest over
the cycle:
  - front, back: how far the legs (and shoes) show beyond the body's outline ahead and behind;
  - spread: from the front toe to the back of the other foot, in 3D.
The best setting keeps the spread and the legs beyond the body within the gait's limits and
has front and back closest to equal. The JSON holds every setting tried and, per gait, "best" with its retarget request.
"""
import bpy, json, math, os, sys
import numpy as np
from mathutils import Matrix

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retarget_ual
from gaits import GAITS, request

FRAME_M, PX = 2.1, 192

args = sys.argv[sys.argv.index("--") + 1:]
glb, out = args[0], args[1]
wanted = args[2:] or list(GAITS)
rig = bpy.data.objects["civilian_rig"]
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
scene = bpy.context.scene

# ---- legs mask: white legs and shoes, dark body (as render_civilian.py) ----
LEG_BONES = ("thigh_", "calf_", "foot_", "ball_")
for o in meshes:
    me = o.data
    attr = me.color_attributes.get("legmask") or me.color_attributes.new("legmask", "FLOAT_COLOR", "POINT")
    groups = {g.index: g.name for g in o.vertex_groups}
    shoe = "shoe" in o.name.lower()
    for v in me.vertices:
        best, bw = "", 0.0
        for g in v.groups:
            n = groups.get(g.group, "")
            if g.weight > bw and not n.startswith(("helper", "joint", "body", "Mid", "Left", "Right")):
                best, bw = n, g.weight
        attr.data[v.index].color = (1, 1, 1, 1) if shoe or best.startswith(LEG_BONES) else (0, 0, 0, 1)
    me.color_attributes.active_color = attr

cam = bpy.data.objects.new("top", bpy.data.cameras.new("top"))
cam.data.type = "ORTHO"
cam.data.ortho_scale = FRAME_M
scene.collection.objects.link(cam)
scene.camera = cam
r = scene.render
r.engine = "BLENDER_WORKBENCH"
r.resolution_x = r.resolution_y = PX
r.film_transparent = True
r.image_settings.file_format = "PNG"
r.image_settings.color_mode = "RGBA"
scene.display.shading.light = "FLAT"
scene.display.shading.color_type = "VERTEX"
tmp = os.path.join(os.path.dirname(os.path.abspath(out)), "_tune_mask.png")
mpp = FRAME_M / PX


def measure_frame(final):
    """front/back beyond the outline (m) and the 3D spread for the posed rig (faces -Y)."""
    tw = rig.matrix_world
    pel = tw @ final["pelvis"].to_translation()
    cam.location = (pel.x, pel.y, 6.0)
    r.filepath = tmp
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(tmp, check_existing=False)
    px = np.array(img.pixels[:], dtype=np.float32).reshape(PX, PX, 4)    # row 0 at the bottom (-Y)
    bpy.data.images.remove(img)
    solid = px[..., 3] > 0.5
    legs = solid & (px[..., 0] > 0.5)
    body = solid & ~legs
    lr, br = np.where(legs.any(axis=1))[0], np.where(body.any(axis=1))[0]
    if len(lr) == 0 or len(br) == 0:
        return 0.0, 0.0, 0.0
    front = max(0, br.min() - lr.min()) * mpp      # forward is -Y: the lower rows
    back = max(0, lr.max() - br.max()) * mpp
    ys = []
    for s in ("l", "r"):
        toe = tw @ (final["ball_" + s] @ Matrix.Translation((0, rig.data.bones["ball_" + s].length, 0))).to_translation()
        heel = tw @ final["foot_" + s].to_translation()
        ys.append((toe.y, max(toe.y, heel.y)))
    spread = max(ys[0][1], ys[1][1]) - min(ys[0][0], ys[1][0])
    return front, back, spread


rt = retarget_ual.Retargeter(rig, glb)
rig.rotation_euler = (0, 0, 0)
results = {}
for gait in wanted:
    g = GAITS[gait]
    rt.set_action(g["action"])
    tried = []
    for c in g["swing"]:
        for u in g["upright"]:
            req = request(gait, c, u)
            mods = retarget_ual.parse(req)[-1]
            front = back = spread = 0.0
            for f in g["frames"]:
                rot, loc, final = rt.pose(f, mods)
                rt.apply(rot, loc)
                bpy.context.view_layer.update()
                a, b, s = measure_frame(final)
                front, back, spread = max(front, a), max(back, b), max(spread, s)
            ratio = front / back if back > 0 else float("inf")
            tried.append({"request": req, "swing_c": c, "upright": u, "front_m": front, "back_m": back,
                          "spread_m": spread, "ratio": ratio})
            print("TUNE %-4s c %+3d upright %2d: front %.2f m, back %.2f m, ratio %.2f, spread %.2f m" %
                  (gait, c, u, front, back, ratio, spread), flush=True)
    side = g["side_limit"] if g["side_limit"] is not None else float("inf")
    ok = [x for x in tried if x["spread_m"] <= g["spread_limit"] + 1e-6 and x["back_m"] > 0
          and max(x["front_m"], x["back_m"]) <= side + 1e-6] or tried
    best = min(ok, key=lambda x: abs(math.log(max(x["ratio"], 1e-3))))
    print("BEST %-4s %s" % (gait, best["request"]), flush=True)
    results[gait] = {"tried": tried, "best": best}
if os.path.exists(tmp):
    os.remove(tmp)
json.dump(results, open(out, "w"), indent=1)
