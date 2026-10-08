"""CJ-004: copy Quaternius Universal Animation Library actions onto an MPFB civilian.

    blender -b <civilian.blend> --python tools/cj004/retarget_ual.py -- <UAL .glb> <out.blend> <action> [<action> ...]

Both rigs use Unreal-style bone names and face -Y, but the library's rest pose is a T-pose
and MPFB's an A-pose with bent elbows. The civilian is first posed so that every bone
points the way the library's bone points at rest ("matched rest"). The constant rotation
between the two bones there (their different rolls) is the offset; every frame, a civilian
bone takes the library bone's world rotation times that offset. Rotation about the bone's
own axis (the hips and shoulders turning during a walk) carries over. Only the pelvis
moves, by the library's pelvis motion scaled to the civilian's hip height. The imported
library is removed again; the retargeted actions are kept on the civilian rig.
"""
import bpy, sys
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


# ---- matched rest: aim every civilian bone along the library bone at rest ----
clear(source)
clear(target)
for name in order:
    src_dir = (world(source, names[name]).to_3x3() @ Vector((0, 1, 0))).normalized()
    m = world(target, name)
    cur_dir = (m.to_3x3() @ Vector((0, 1, 0))).normalized()
    set_world_rotation(name, cur_dir.rotation_difference(src_dir) @ m.to_quaternion())
offset = {n: world(source, names[n]).to_quaternion().inverted() @ world(target, n).to_quaternion() for n in order}
pelvis_rest_src = world(source, names["pelvis"]).to_translation()
pelvis_rest_tgt = world(target, "pelvis").to_translation()
hip_ratio = pelvis_rest_tgt.z / pelvis_rest_src.z

# ---- retarget each requested action frame by frame ----
created = []
for action_name in wanted:
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
    for f in range(first, last + 1):
        bpy.context.scene.frame_set(f)
        for name in order:
            src = world(source, names[name])
            head = None
            if name == "pelvis":
                head = pelvis_rest_tgt + (src.to_translation() - pelvis_rest_src) * hip_ratio
            set_world_rotation(name, src.to_quaternion() @ offset[name], head)
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
