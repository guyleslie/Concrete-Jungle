"""CJ-004: render a civilian .blend as top-down game frames.

    blender -b <civilian.blend> --python tools/cj004/render_civilian.py -- <out dir> <pose> [frames...]

<pose> is "idle" (arms hang straight down, built here from the rest pose) or the name of an
action already on the rig; with an action, [frames...] lists the scene frames to render.

Camera: orthographic, straight down, 1.4 m x 1.4 m per frame (the civilian atlas frame,
src/pedestrian.cpp PED_DRAW before CHAR_SCALE), 384 px square, the character facing the
top of the image like the procedural atlas. Light: a sun from the upper left plus a soft
grey sky. For every frame it writes <out>/<pose>_<n>.png (RGBA) and <out>/<pose>_<n>_legs.png,
a mask in which legs and shoes are white, to measure what the camera sees of them.
Environment overrides for look development: CJ_SUN (sun strength, 5.0), CJ_SKY (sky
brightness, 0.6), CJ_EXPOSURE (0.0) and CJ_LEAN (forward lean of the idle per spine bone, 0 deg).
"""
import bpy, math, os, sys
from mathutils import Matrix, Vector

FRAME_M = 1.4          # metres covered by one frame
FRAME_PX = 384         # render size; the game atlas uses 96 px frames
LEG_BONES = ("thigh_", "calf_", "foot_", "ball_")

args = sys.argv[sys.argv.index("--") + 1:]
out_dir, pose = os.path.abspath(args[0]), args[1]
frames = [int(f) for f in args[2:]] or [1]
os.makedirs(out_dir, exist_ok=True)

scene = bpy.context.scene
rig = bpy.data.objects["civilian_rig"]
meshes = [o for o in bpy.data.objects if o.type == "MESH"]

# Smooth the low-poly meshes for the close camera.
for o in meshes:
    if not any(m.type == "SUBSURF" for m in o.modifiers):
        sub = o.modifiers.new("smooth", "SUBSURF")
        sub.levels = sub.render_levels = 1

# Face the top of the image: MakeHuman's front is -Y, the camera's image top is +Y.
rig.rotation_euler = (0.0, 0.0, math.pi)


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
    """Tilt a bone forward (towards MakeHuman's front, -Y) about the armature's X axis."""
    bpy.context.view_layer.update()
    pb = rig.pose.bones[name]
    aim_bone(name, Matrix.Rotation(math.radians(degrees), 3, "X") @ (pb.tail - pb.head).normalized())


LEAN = float(os.environ.get("CJ_LEAN", "0.0"))   # degrees per spine bone; the agreed idle stands upright

if pose == "idle":
    for bone in ("spine_01", "spine_02", "spine_03", "neck_01"):
        lean(bone, LEAN)
    for side, sx in (("l", 1.0), ("r", -1.0)):
        aim_bone("upperarm_" + side, (0.14 * sx, 0.02, -1.0))   # beside the torso, hanging
        aim_bone("lowerarm_" + side, (0.04 * sx, -0.06, -1.0))  # a slight natural bend forward
        aim_bone("hand_" + side, (0.02 * sx, -0.04, -1.0))
        # The rest pose stands with the feet apart; bring them under the hips, flat on the ground.
        aim_bone("thigh_" + side, (0.0, 0.0, -1.0))
        aim_bone("calf_" + side, (0.0, 0.0, -1.0))
        aim_bone("foot_" + side, rest_direction("foot_" + side))
        aim_bone("ball_" + side, rest_direction("ball_" + side))
else:
    rig.animation_data_create()
    action = bpy.data.actions[pose]
    rig.animation_data.action = action
    if action.slots:
        rig.animation_data.action_slot = action.slots[0]

# ---- camera and light ----
cam_data = bpy.data.cameras.new("top")
cam_data.type = "ORTHO"
cam_data.ortho_scale = FRAME_M
cam_data.clip_end = 20
cam = bpy.data.objects.new("top", cam_data)
cam.location = (0.0, 0.0, 6.0)
scene.collection.objects.link(cam)
scene.camera = cam

sun_data = bpy.data.lights.new("sun", "SUN")
sun_data.energy = float(os.environ.get("CJ_SUN", "5.0"))
sun_data.angle = math.radians(25)            # soft shadow edges
sun = bpy.data.objects.new("sun", sun_data)
travel = Vector((0.75, -0.75, -0.9)).normalized()   # from the upper left of the image, about 40 deg high
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
    scene.cycles.samples = 96
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


build_leg_attribute()
for f in frames:
    scene.frame_set(f)
    stem = os.path.join(out_dir, "%s_%02d" % (pose, f))
    render_colour(stem + ".png")
    render_mask(stem + "_legs.png")
    print("RENDERED", stem)
