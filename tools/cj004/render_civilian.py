"""CJ-004: render a civilian .blend as top-down game frames.

    blender -b <civilian.blend> --python tools/cj004/render_civilian.py -- <out dir> <job> [<job> ...]

A job is <pose>[:<frames>][:lying]:
  - <pose> is "idle" (upright, arms hanging, feet under the hips, built here from the rest
    pose), "fist" (the right fist raised above the head, the left hand held out; frames 0 and
    1 are the two shake positions) or the name of an action on the rig;
  - <frames> is a comma-separated list of scene frames (fractions are interpolated);
  - "lying" turns a body on the ground head up, centres it and renders it at real size.
Outputs are <out>/<pose>_<nn>.png (RGBA), numbered in the order of the frames,
<pose>_<nn>_legs.png, a mask in which legs and shoes are white, and <pose>_pelvis.json, the
pelvis position of every frame in atlas pixels from the frame centre (right, down).

Camera: orthographic, straight down, CJ_FRAME_M metres square per frame (2.1: the atlas frame
before CHAR_SCALE; a running stride and a punch need about 2 m), 384 px per 1.4 m, the
character facing the top of the image like the procedural atlas. A lying frame covers
CJ_FRAME_M x CHAR_SCALE metres, because the game draws standing people CHAR_SCALE times life
size but a body on the ground at real size. Light: a sun from the upper left plus a soft
grey sky. Look-development overrides: CJ_SUN (5.0), CJ_SKY (0.6), CJ_EXPOSURE (0.0),
CJ_LEAN (forward lean of the idle per spine bone, 0 deg), CJ_SAMPLES (48).
"""
import bpy, json, math, os, sys
from mathutils import Matrix, Vector

CHAR_SCALE = 1.35
FRAME_M = float(os.environ.get("CJ_FRAME_M", "2.1"))
FRAME_PX = round(384 * FRAME_M / 1.4)
LEG_BONES = ("thigh_", "calf_", "foot_", "ball_")
LEAN = float(os.environ.get("CJ_LEAN", "0.0"))

args = sys.argv[sys.argv.index("--") + 1:]
out_dir, jobs = os.path.abspath(args[0]), args[1:]
os.makedirs(out_dir, exist_ok=True)

scene = bpy.context.scene
rig = bpy.data.objects["civilian_rig"]
meshes = [o for o in bpy.data.objects if o.type == "MESH"]

# Smooth the low-poly meshes for the close camera.
for o in meshes:
    if not any(m.type == "SUBSURF" for m in o.modifiers):
        sub = o.modifiers.new("smooth", "SUBSURF")
        sub.levels = sub.render_levels = 1


# ---- posing helpers (armature space; MakeHuman's front is -Y) ----
def aim_bone(name, direction):
    """Rotate a pose bone about its head so that it points along 'direction' (armature space)."""
    bpy.context.view_layer.update()
    pb = rig.pose.bones[name]
    current = (pb.tail - pb.head).normalized()
    rot = current.rotation_difference(Vector(direction).normalized())
    head = pb.head.copy()
    pb.matrix = Matrix.Translation(head) @ rot.to_matrix().to_4x4() @ Matrix.Translation(-head) @ pb.matrix


def rest_direction(name):
    b = rig.data.bones[name]
    return (b.tail_local - b.head_local).normalized()


def lean(name, degrees):
    """Tilt a bone forward (towards -Y) about the armature's X axis."""
    bpy.context.view_layer.update()
    pb = rig.pose.bones[name]
    aim_bone(name, Matrix.Rotation(math.radians(degrees), 3, "X") @ (pb.tail - pb.head).normalized())


def curl_fingers(side, degrees):
    """Close a hand: bend every finger joint about its own X axis."""
    for finger in ("index", "middle", "ring", "pinky"):
        for j in ("01", "02", "03"):
            pb = rig.pose.bones.get("%s_%s_%s" % (finger, j, side))
            if pb is not None:
                pb.rotation_mode = "XYZ"
                pb.rotation_euler = (math.radians(degrees), 0.0, 0.0)
    bpy.context.view_layer.update()


# Standing stance. FEET_IN tilts the thighs inwards (feet closer together); FEET_BACK tilts the
# legs back (standing, the line of gravity passes in front of the ankles, so the hips are a few
# centimetres in front of them). Unless CJ_FEET_IN / CJ_FEET_BACK fix them, every standing job
# searches these ranges for the stance that shows the least of the legs and shoes from above:
# slim shoes hide best with the feet back, bulky shoes and boots with the feet under the hips.
FEET_IN = float(os.environ.get("CJ_FEET_IN", "0.06"))
FEET_BACK = float(os.environ.get("CJ_FEET_BACK", "0.06"))
STANCE_SEARCH = "CJ_FEET_IN" not in os.environ and "CJ_FEET_BACK" not in os.environ
STANCE_BACK = (0.0, 0.02, 0.04, 0.06, 0.08)
STANCE_IN = (0.03, 0.06, 0.09)


def stand_on_both_feet():
    # The rest pose stands with the feet apart; bring them under the hips, flat on the ground.
    for side, sx in (("l", 1.0), ("r", -1.0)):
        aim_bone("thigh_" + side, (-FEET_IN * sx, FEET_BACK, -1.0))
        aim_bone("calf_" + side, (0.0, FEET_BACK, -1.0))
        aim_bone("foot_" + side, rest_direction("foot_" + side))
        aim_bone("ball_" + side, rest_direction("ball_" + side))


def pose_idle():
    for bone in ("spine_01", "spine_02", "spine_03", "neck_01"):
        lean(bone, LEAN)
    for side, sx in (("l", 1.0), ("r", -1.0)):
        aim_bone("upperarm_" + side, (0.14 * sx, 0.02, -1.0))   # beside the torso, hanging
        aim_bone("lowerarm_" + side, (0.04 * sx, -0.06, -1.0))  # a slight natural bend forward
        aim_bone("hand_" + side, (0.02 * sx, -0.04, -1.0))
    stand_on_both_feet()


def pose_fist(shake):
    """Right fist raised above the head, left hand held out; 'shake' 0..1 tilts the fist forward."""
    for bone in ("spine_01", "spine_02", "spine_03", "neck_01"):
        lean(bone, 1.5)
    aim_bone("upperarm_r", (-0.25, -0.15 - 0.15 * shake, 1.0))
    aim_bone("lowerarm_r", (-0.05, -0.25 - 0.35 * shake, 1.0))
    aim_bone("hand_r", (-0.05, -0.25 - 0.35 * shake, 1.0))
    curl_fingers("r", 85.0)
    aim_bone("upperarm_l", (0.35, -0.75, -0.55))
    aim_bone("lowerarm_l", (0.15, -1.0, -0.15))
    aim_bone("hand_l", (0.1, -1.0, -0.1))
    stand_on_both_feet()


def reset():
    """Rest pose, no action, facing the top of the image, camera on the origin."""
    if rig.animation_data:
        rig.animation_data.action = None
    for pb in rig.pose.bones:
        pb.matrix_basis = Matrix()
    rig.rotation_euler = (0.0, 0.0, math.pi)      # MakeHuman's front (-Y) to the image top (+Y)
    cam.location = (0.0, 0.0, 6.0)
    bpy.context.view_layer.update()


def lay_head_up_and_centre():
    """Turn the rig so the head is above the pelvis in the image, and centre the camera on the body."""
    bpy.context.view_layer.update()
    head = rig.matrix_world @ rig.pose.bones["head"].head
    pelvis = rig.matrix_world @ rig.pose.bones["pelvis"].head
    d = head - pelvis
    rig.rotation_euler.z += math.atan2(d.x, d.y)         # rotate the pelvis-to-head direction onto +Y
    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()
    xs, ys = [], []
    for o in meshes:
        ev = o.evaluated_get(deps)
        me = ev.to_mesh()
        for v in me.vertices:
            w = o.matrix_world @ v.co
            xs.append(w.x); ys.append(w.y)
        ev.to_mesh_clear()
    cam.location.x, cam.location.y = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2


# ---- camera, light, output ----
cam_data = bpy.data.cameras.new("top")
cam_data.type = "ORTHO"
cam_data.clip_end = 20
cam = bpy.data.objects.new("top", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam

sun_data = bpy.data.lights.new("sun", "SUN")
sun_data.energy = float(os.environ.get("CJ_SUN", "5.0"))
sun_data.angle = math.radians(25)                       # soft shadow edges
sun = bpy.data.objects.new("sun", sun_data)
travel = Vector((0.75, -0.75, -0.9)).normalized()       # from the upper left of the image, about 40 deg high
sun.rotation_euler = travel.to_track_quat("-Z", "Y").to_euler()
scene.collection.objects.link(sun)

world = bpy.data.worlds.new("sky")
world.use_nodes = True
sky = float(os.environ.get("CJ_SKY", "0.6"))
world.node_tree.nodes["Background"].inputs[0].default_value = (sky * 0.95, sky * 0.98, sky, 1.0)
world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
scene.world = world

r = scene.render
r.resolution_x = r.resolution_y = FRAME_PX
r.resolution_percentage = 100
r.film_transparent = True
r.image_settings.file_format = "PNG"
r.image_settings.color_mode = "RGBA"
scene.view_settings.view_transform = "Standard"
scene.view_settings.exposure = float(os.environ.get("CJ_EXPOSURE", "0.0"))


def render_colour(path):
    r.engine = "CYCLES"
    scene.cycles.samples = int(os.environ.get("CJ_SAMPLES", "48"))
    scene.cycles.use_denoising = True
    r.filepath = path
    bpy.ops.render.render(write_still=True)


# ---- leg mask: a colour attribute, 1 where the mesh belongs to the legs or shoes ----
def build_leg_attribute():
    for o in meshes:
        me = o.data
        attr = me.color_attributes.get("legmask") or me.color_attributes.new("legmask", "FLOAT_COLOR", "POINT")
        groups = {g.index: g.name for g in o.vertex_groups}
        is_shoe = "shoe" in o.name.lower()
        for v in me.vertices:
            best, best_w = "", 0.0
            for g in v.groups:
                n = groups.get(g.group, "")
                if g.weight > best_w and not n.startswith(("helper", "joint", "body", "Mid", "Left", "Right")):
                    best, best_w = n, g.weight
            leg = is_shoe or best.startswith(LEG_BONES)
            attr.data[v.index].color = (1, 1, 1, 1) if leg else (0, 0, 0, 1)
        me.color_attributes.active_color = attr


def render_mask(path):
    r.engine = "BLENDER_WORKBENCH"
    shading = scene.display.shading
    shading.light = "FLAT"
    shading.color_type = "VERTEX"
    r.filepath = path
    bpy.ops.render.render(write_still=True)


ATLAS_PER_M = 96 / 1.4


def pelvis_offset():
    p = rig.matrix_world @ rig.pose.bones["pelvis"].head
    return [(p.x - cam.location.x) * ATLAS_PER_M, -(p.y - cam.location.y) * ATLAS_PER_M]


def visible_leg_pixels(tmp):
    """Render the legs mask small and count its white, opaque pixels."""
    keep = r.resolution_x, r.resolution_y
    r.resolution_x = r.resolution_y = 192
    render_mask(tmp)
    r.resolution_x, r.resolution_y = keep
    img = bpy.data.images.load(tmp, check_existing=False)
    px = img.pixels[:]
    bpy.data.images.remove(img)
    return sum(1 for i in range(0, len(px), 4) if px[i] > 0.5 and px[i + 3] > 0.5)


stance_chosen = False


def choose_stance(pose_fn):
    """Pick the stance that hides the legs best (once per run; idle and fist share it)."""
    global FEET_IN, FEET_BACK, stance_chosen
    if not STANCE_SEARCH or stance_chosen:
        return
    tmp = os.path.join(out_dir, "_stance.png")
    best = None
    for back in STANCE_BACK:
        for inward in STANCE_IN:
            FEET_IN, FEET_BACK = inward, back
            reset()
            pose_fn()
            n = visible_leg_pixels(tmp)
            # prefer fewer pixels, then the more natural stance (feet less far back, less inwards)
            key = (n, back, inward)
            if best is None or key < best:
                best = key
    os.remove(tmp)
    FEET_IN, FEET_BACK = best[2], best[1]
    stance_chosen = True
    with open(os.path.join(out_dir, "stance.json"), "w") as fh:
        json.dump({"feet_in": FEET_IN, "feet_back": FEET_BACK, "legs_px_192": best[0]}, fh)
    print("STANCE feet in %.2f, back %.2f: %d px of legs at 192 px" % (FEET_IN, FEET_BACK, best[0]))


build_leg_attribute()
for job in jobs:
    parts = job.split(":")
    pose = parts[0]
    frames = [float(f) for f in parts[1].split(",")] if len(parts) > 1 and parts[1] else [0.0]
    lying = "lying" in parts[2:]
    cam_data.ortho_scale = FRAME_M * (CHAR_SCALE if lying else 1.0)
    if pose in ("idle", "fist"):
        choose_stance(pose_idle)

    def apply(f):
        reset()
        if pose == "idle":
            pose_idle()
        elif pose == "fist":
            pose_fist(f)
        else:
            action = bpy.data.actions[pose]
            rig.animation_data_create()
            rig.animation_data.action = action
            if action.slots:
                rig.animation_data.action_slot = action.slots[0]
            scene.frame_set(int(f), subframe=f - int(f))

    # A standing job is centred on its mean pelvis position, so a cycle keeps the hips at the
    # frame centre on average and idle, walk and run share the same pivot.
    centre = Vector((0.0, 0.0))
    if not lying:
        for f in frames:
            apply(f)
            centre += (rig.matrix_world @ rig.pose.bones["pelvis"].head).xy
        centre /= len(frames)
    pelvis = []
    for i, f in enumerate(frames):
        apply(f)
        if lying:
            lay_head_up_and_centre()
        else:
            cam.location.x, cam.location.y = centre.x, centre.y
        stem = os.path.join(out_dir, "%s_%02d" % (pose, i))
        render_colour(stem + ".png")
        render_mask(stem + "_legs.png")
        pelvis.append(pelvis_offset())
        print("RENDERED", stem)
    with open(os.path.join(out_dir, pose + "_pelvis.json"), "w") as fh:
        json.dump(pelvis, fh)
