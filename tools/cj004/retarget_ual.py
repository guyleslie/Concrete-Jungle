"""CJ-004: copy Quaternius Universal Animation Library actions onto an MPFB civilian.

    blender -b <civilian.blend> --python tools/cj004/retarget_ual.py -- <UAL .glb> <out.blend> <action>[=<frames>] ...

<frames> (comma-separated) limits the copy to the frames that will be rendered; without it
every frame of the action is copied. A suffix @<degrees> leans the upper body (spine_01 and
everything above it, arms included) back by that angle about the hips: the library's sprint
leans about 40 degrees, a real runner at 5 m/s about 15-20. A suffix ~<a>,<c> remaps the
forward swing of each thigh (degrees from straight down, forward positive) to a * angle + c by
turning the whole leg about the hip; the library's walks lift the thigh 50 degrees forward,
a casual walk about 30. A suffix *<k> (written last) scales every leg joint's motion to k of
the way from standing straight: shorter steps at both ends of the stride, hip, knee and ankle
alike.

Both rigs use Unreal-style bone names and face -Y, but the library's rest pose is a T-pose
and MPFB's an A-pose with bent elbows. The civilian's limbs are first posed so that every limb
bone points the way the library's bone points at rest ("matched rest"); the torso, upright in
both, keeps its own rest orientation. The constant rotation
between the two bones there (their different rolls) is the offset; every frame, a civilian
bone takes the library bone's world rotation times that offset. Rotation about the bone's
own axis (the hips and shoulders turning during a walk) carries over. Only the pelvis
moves, by the library's pelvis motion scaled to the civilian's hip height. The imported
library is removed again; the retargeted actions are kept on the civilian rig.
"""
import bpy, math, sys
from mathutils import Matrix, Quaternion, Vector

args = sys.argv[sys.argv.index("--") + 1:]
glb, out, wanted = args[0], args[1], args[2:]

target = bpy.data.objects["civilian_rig"]
before = set(bpy.data.objects)
actions_before = set(bpy.data.actions)
bpy.ops.import_scene.gltf(filepath=glb)
imported = [o for o in bpy.data.objects if o not in before]
source = next(o for o in imported if o.type == "ARMATURE")
library_actions = {a.name: a for a in bpy.data.actions if a not in actions_before}

# Bones present in both rigs (the library capitalises 'Head', and has extra leaf bones).
names = {}
for b in source.data.bones:
    t = target.data.bones.get(b.name) or target.data.bones.get(b.name.lower())
    if t is not None and b.name != "root":
        names[t.name] = b.name


def depth(bone):
    return len(bone.parent_recursive)


order = sorted(names, key=lambda n: depth(target.data.bones[n]))


def update():
    bpy.context.view_layer.update()


def world(rig, name):
    return rig.matrix_world @ rig.pose.bones[name].matrix


def set_world_rotation(name, rotation, head=None):
    """Give a target pose bone a world rotation, keeping its head (or moving it to 'head')."""
    pb = target.pose.bones[name]
    m = world(target, name)
    loc = head if head is not None else m.to_translation()
    pb.matrix = target.matrix_world.inverted() @ Matrix.LocRotScale(loc, rotation, (1, 1, 1))
    update()


def clear(rig):
    rig.animation_data_create()
    rig.animation_data.action = None
    for pb in rig.pose.bones:
        pb.rotation_mode = "QUATERNION"
        pb.location = (0, 0, 0)
        pb.rotation_quaternion = Quaternion()
        pb.scale = (1, 1, 1)
    update()


# ---- matched rest: aim every limb bone along the library bone at rest ----
# The torso stands upright in both rest poses, but its bones follow different conventions
# (MPFB's pelvis points up and back, the library's up and forward): aiming them would tip
# the civilian's hips and add about 10 degrees of forward lean. The torso keeps its own rest
# orientation; the offset then carries the library's rotation from rest over unchanged.
TORSO = {"pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "head"}
clear(source)
clear(target)
for name in order:
    if name in TORSO:
        continue
    src_dir = (world(source, names[name]).to_3x3() @ Vector((0, 1, 0))).normalized()
    m = world(target, name)
    cur_dir = (m.to_3x3() @ Vector((0, 1, 0))).normalized()
    set_world_rotation(name, cur_dir.rotation_difference(src_dir) @ m.to_quaternion())
offset = {n: world(source, names[n]).to_quaternion().inverted() @ world(target, n).to_quaternion() for n in order}
# Leg joints in the matched rest stand straight; *<k> scales their motion towards it.
LEGS = [n for n in order if n.startswith(("thigh_", "calf_", "foot_", "ball_"))]
straight = {n: target.pose.bones[n].rotation_quaternion.copy() for n in LEGS}
pelvis_rest_src = world(source, names["pelvis"]).to_translation()
pelvis_rest_tgt = world(target, "pelvis").to_translation()
hip_ratio = pelvis_rest_tgt.z / pelvis_rest_src.z

# ---- retarget each requested action frame by frame ----
created = []
upper = {"spine_01"} | {b.name for b in target.data.bones["spine_01"].children_recursive}

for request in wanted:
    request, _, amplitude = request.partition("*")
    request, _, upright = request.partition("@")
    request, _, swing = request.partition("~")
    leg_k = float(amplitude) if amplitude else None
    swing_a, swing_c = (float(x) for x in swing.split(",")) if swing else (None, None)
    action_name, _, frame_list = request.partition("=")
    back = Quaternion((1, 0, 0), -math.radians(float(upright))) if upright else None
    src_action = library_actions[action_name]
    source.animation_data.action = src_action
    if src_action.slots:
        source.animation_data.action_slot = src_action.slots[0]
    first, last = (int(f) for f in src_action.frame_range)
    act = bpy.data.actions.new(action_name + "_civilian")
    created.append((act, action_name))
    act.use_fake_user = True
    clear(target)
    target.animation_data.action = act
    for f in ([int(x) for x in frame_list.split(",")] if frame_list else range(first, last + 1)):
        bpy.context.scene.frame_set(f)
        for name in order:
            src = world(source, names[name])
            head = None
            if name == "pelvis":
                head = pelvis_rest_tgt + (src.to_translation() - pelvis_rest_src) * hip_ratio
            rot = src.to_quaternion() @ offset[name]
            if back is not None and name in upper:
                rot = back @ rot          # positions follow through the hierarchy from spine_01
            set_world_rotation(name, rot, head)
        if leg_k is not None:
            # every leg joint moves leg_k of the way from standing straight to the library's pose
            for name in LEGS:
                pb = target.pose.bones[name]
                pb.rotation_quaternion = straight[name].slerp(pb.rotation_quaternion, leg_k)
                update()
        if swing_a is not None:
            for side in ("l", "r"):
                hip = world(target, "thigh_" + side).to_translation()
                knee = world(target, "calf_" + side).to_translation()
                d = knee - hip
                angle = math.degrees(math.atan2(-d.y, -d.z))
                delta = swing_a * angle + swing_c - angle
                # a positive turn about +X swings a hanging leg backwards (+Y); the lower leg follows
                turn = Quaternion((1, 0, 0), -math.radians(delta))
                set_world_rotation("thigh_" + side, turn @ world(target, "thigh_" + side).to_quaternion())
        for name in order:
            pb = target.pose.bones[name]
            pb.keyframe_insert("rotation_quaternion", frame=f)
            if name == "pelvis":
                pb.keyframe_insert("location", frame=f)
    print("RETARGETED", action_name, first, last)

# ---- remove the library, keep the civilian and the new actions ----
for o in imported:
    bpy.data.objects.remove(o, do_unlink=True)
for a in library_actions.values():
    bpy.data.actions.remove(a)
for act, name in created:
    act.name = name
target.animation_data.action = None
bpy.ops.wm.save_as_mainfile(filepath=out)
print("SAVED", out, sorted(a.name for a in bpy.data.actions))
