"""CJ-011: pose the player with a weapon, render top-down frames and measure the holds.

    blender -b <player.blend> --python tools/cj011/render_player.py -- <out dir> <pose> [<pose> ...]

Poses (standing; the legs stand as the civilians' idle):
  pistol_low   the pistol lowered beside the right thigh, pointing at the ground ahead
  pistol_aim   both hands on the pistol, arms extended, barrel level (isosceles)
  pistol_fire  pistol_aim with the recoil: muzzle up, the arms a little bent
  rifle_low    low ready: the butt at the shoulder, the barrel angled down and forward
  rifle_aim    shouldered, barrel level
  rifle_fire   rifle_aim with the recoil: muzzle up, the shoulder pushed back
  none         no weapon, the civilians' idle arms (a reference for the width)
  <weapon>_low, _aim, _fire0/1, _reload0-3, knife_stab0/1, flashlight_strike0/1: the same
               for every weapon of build_player.py (unarmed, knife, flashlight, pistol,
               rifle, shotgun; see pose(), reload_pose(), stab_knife(), strike_flashlight())
  clip:<weapon>:<action>:<frame>  a frame of a clip with the weapon in its right hand

Gait jobs, gait:<name>:<action>:<frames> (action "stand" for the standing frame): every frame
of the action, posed with each weapon lowered, is rendered as
  full_<name>_<nn>.png    the whole player with the pistol lowered, and its _legs mask;
  legs_<name>_<nn>.png    the legs layer: trousers, boots and the body below the waist;
  <weapon>_<name>_<nn>.png the upper-body layer per weapon (pistol, rifle): the jacket, the
                          head, the cap, the pack and the weapon held lowered.
Body jobs, body:<name>:<action>:<frames> (CJ-035): every frame of the action as the clip poses it,
the whole player without a weapon, each frame centred on its own pelvis (the game moves the body),
as body_<name>_<nn>.png; with CJ_SIDE also its side, front and back views.

The layers come from one render each, the other layer's surfaces made transparent (the
trousers by a texture mask, the body by its bones, see LAYER_KIND). While walking and running,
the pistol stays in the swinging right hand, muzzle down; the rifle is held at the low ready,
fixed to the chest, with both hands. The frames are centred on the mean pelvis position of the
job, so all layers share the pivot.

The weapon is placed in armature space (MakeHuman: front -Y, the character's left +X, up +Z)
and each hand is put on it by a two-bone inverse kinematics solved here: the palm on the
grip (the right hand) or the fore grip (the left), the hand turned to hold it, the elbow
towards a pole direction. Outputs per pose: <pose>.png (RGBA, the civilians' camera: 2.1 m,
576 px, facing the top), <pose>_legs.png (leg mask) and a line in measure.json:
  - pitch_deg: the barrel's angle below the horizontal (positive down);
  - reach_m: how far the weapon reaches ahead of the body front from above;
  - butt_shoulder_m: the butt's distance from the shoulder pocket;
  - grip_err_m, fore_err_m: the palm's distance from its grip point after the pose.
"""
import bpy, json, math, os, sys
from mathutils import Matrix, Quaternion, Vector

FRAME_M = float(os.environ.get("CJ_FRAME_M", "2.1"))      # metres a frame covers (CJ-035 lying frames: 2.6)
FRAME_PX = round(384 * FRAME_M / 1.4)
LEG_BONES = ("thigh_", "calf_", "foot_", "ball_")

args = sys.argv[sys.argv.index("--") + 1:]
out_dir, poses = os.path.abspath(args[0]), args[1:]
os.makedirs(out_dir, exist_ok=True)
scene = bpy.context.scene
rig = bpy.data.objects["civilian_rig"]
guns = {o.name[4:]: o for o in bpy.data.objects if o.name.startswith("gun_")}
mags = {o.name[4:]: o for o in bpy.data.objects if o.name.startswith("mag_")}


def make_shell():
    """A 12-gauge shell, 70 mm: a red ribbed hull and a brass head, its origin at the middle,
    +Z towards the crimp."""
    bpy.ops.mesh.primitive_cylinder_add(radius=0.0105, depth=0.056, location=(0, 0, 0.007), vertices=20)
    hull = bpy.context.object
    bpy.ops.mesh.primitive_cylinder_add(radius=0.0112, depth=0.014, location=(0, 0, -0.028), vertices=20)
    head = bpy.context.object
    red = bpy.data.materials.new("Shell_Hull")
    red.use_nodes = True
    red.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.45, 0.02, 0.02, 1)
    red.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.45
    brass = bpy.data.materials.new("Shell_Brass")
    brass.use_nodes = True
    brass.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.75, 0.55, 0.2, 1)
    brass.node_tree.nodes["Principled BSDF"].inputs["Metallic"].default_value = 0.9
    brass.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.3
    hull.data.materials.append(red)
    head.data.materials.append(brass)
    bpy.ops.object.select_all(action="DESELECT")
    hull.select_set(True)
    head.select_set(True)
    bpy.context.view_layer.objects.active = hull
    bpy.ops.object.join()
    shell = bpy.context.object
    shell.name = "mag_shell"
    shell.hide_render = True
    return shell


if "shotgun" in guns:
    mags["shell"] = make_shell()
LONG_GUNS = ("rifle", "shotgun")
HAND_HELD = ("pistol", "knife", "flashlight")      # carried lowered in the swinging right hand
TROUSERS_MASK = os.environ.get("CJ_TROUSERS_MASK", "")
meshes = [o for o in bpy.data.objects if o.type == "MESH" and not o.name.startswith("gun_")]
for o in meshes:
    if o.name.startswith("civilian") and not any(m.type == "SUBSURF" for m in o.modifiers):
        sub = o.modifiers.new("smooth", "SUBSURF")
        sub.levels = sub.render_levels = 1


def update():
    bpy.context.view_layer.update()


def bone_head(name):
    return rig.pose.bones[name].head.copy()


def aim_bone(name, direction):
    update()
    pb = rig.pose.bones[name]
    rot = (pb.tail - pb.head).normalized().rotation_difference(Vector(direction).normalized())
    h = pb.head.copy()
    pb.matrix = Matrix.Translation(h) @ rot.to_matrix().to_4x4() @ Matrix.Translation(-h) @ pb.matrix


def curl(side, degrees, thumb=None):
    for finger in ("index", "middle", "ring", "pinky"):
        for j in ("01", "02", "03"):
            pb = rig.pose.bones.get("%s_%s_%s" % (finger, j, side))
            if pb is not None:
                pb.rotation_mode = "XYZ"
                pb.rotation_euler = (math.radians(degrees), 0.0, 0.0)
    if thumb is not None:
        for j in ("02", "03"):
            pb = rig.pose.bones.get("thumb_%s_%s" % (j, side))
            if pb is not None:
                pb.rotation_mode = "XYZ"
                pb.rotation_euler = (math.radians(thumb), 0.0, 0.0)
    update()


def reset():
    if rig.animation_data:
        rig.animation_data.action = None
    for pb in rig.pose.bones:
        pb.rotation_mode = "QUATERNION"
        pb.matrix_basis = Matrix()
    rig.rotation_euler = (0.0, 0.0, math.pi)
    for g in list(guns.values()) + list(mags.values()):
        g.hide_render = True
    update()


def stand():
    for side, sx in (("l", 1.0), ("r", -1.0)):
        aim_bone("thigh_" + side, (-0.06 * sx, 0.04, -1.0))
        aim_bone("calf_" + side, (0.0, 0.04, -1.0))
        b = rig.data.bones["foot_" + side]
        aim_bone("foot_" + side, (b.tail_local - b.head_local))
        b = rig.data.bones["ball_" + side]
        aim_bone("ball_" + side, (b.tail_local - b.head_local))


def turn_bone(name, axis, degrees):
    """Rotate a pose bone about its head by an armature-space axis."""
    update()
    pb = rig.pose.bones[name]
    rot = Matrix.Rotation(math.radians(degrees), 4, axis)
    h = pb.head.copy()
    pb.matrix = Matrix.Translation(h) @ rot @ Matrix.Translation(-h) @ pb.matrix


def blade(yaw, lean):
    """A shooter's stance: the torso turned yaw degrees to the right and leaned forward, the
    head turned back to the front, the left shoulder pushed forward."""
    for b in ("spine_01", "spine_02", "spine_03"):
        turn_bone(b, "Z", -yaw / 3)
        turn_bone(b, "X", -lean / 3)                    # forward is -Y: a negative turn about X
    turn_bone("neck_01", "Z", yaw * 0.5)
    turn_bone("head", "Z", yaw * 0.5)
    turn_bone("neck_01", "X", lean * 0.6)
    update()
    c = rig.pose.bones["clavicle_l"]
    aim_bone("clavicle_l", (c.tail - c.head).normalized() + Vector((0, -0.6, 0.05)))


def pocket():
    """The right shoulder pocket: inside the shoulder joint, at the front of the chest."""
    update()
    pb = rig.pose.bones["spine_03"]
    q = pb.matrix.to_quaternion() @ rig.data.bones["spine_03"].matrix_local.to_quaternion().inverted()
    return bone_head("upperarm_r") + q @ Vector((0.075, -0.10, -0.05))


def arms_hanging(sides=("l", "r")):
    for side in sides:
        sx = 1.0 if side == "l" else -1.0
        aim_bone("upperarm_" + side, (0.14 * sx, 0.02, -1.0))
        aim_bone("lowerarm_" + side, (0.04 * sx, -0.06, -1.0))
        aim_bone("hand_" + side, (0.02 * sx, -0.04, -1.0))


# ---- the hands' rest frames: direction (wrist to knuckles), width (pinky to index), palm ----
def hand_rest(side):
    bones = rig.data.bones
    wrist = bones["hand_" + side].head_local
    knuckles = bones["middle_01_" + side].head_local
    d = (knuckles - wrist).normalized()
    w = bones["index_01_" + side].head_local - bones["pinky_01_" + side].head_local
    w = (w - w.dot(d) * d).normalized()
    n = d.cross(w)
    # the palm faces the thigh in the rest pose: towards the body's centre line
    sign = 1.0 if (n.x > 0) == (side == "r") else -1.0
    frame = Matrix((d, w, d.cross(w))).transposed()               # columns d, w, d x w
    palm = wrist + 0.55 * (knuckles - wrist) + sign * n * 0.022     # the palm's centre, on its surface
    q_hand = bones["hand_" + side].matrix_local.to_quaternion()
    lower = (bones["lowerarm_" + side].tail_local - bones["lowerarm_" + side].head_local).normalized()
    return {"frame": frame.to_quaternion(), "sign": sign, "rest_bend": math.degrees(d.angle(lower)), "palm_local": q_hand.inverted() @ (palm - wrist),
            "q_hand": q_hand, "q_lower": bones["lowerarm_" + side].matrix_local.to_quaternion()}


HAND = {s: hand_rest(s) for s in ("l", "r")}


def palm_point(side):
    update()
    pb = rig.pose.bones["hand_" + side]
    return pb.head + pb.matrix.to_quaternion() @ HAND[side]["palm_local"]


def hand_target(side, d, w):
    """Armature-space rotation of the hand bone for a wanted direction d and width w; the palm
    faces d x w times the hand's sign (towards what it holds)."""
    d = Vector(d).normalized()
    w = Vector(w)
    w = (w - w.dot(d) * d).normalized()
    want = Matrix((d, w, d.cross(w))).transposed().to_quaternion()
    h = HAND[side]
    # rest: frame = q_hand_rest * f_local  ->  f_local = q_hand_rest^-1 frame
    f_local = h["q_hand"].inverted() @ h["frame"]
    return want @ f_local.inverted()


MAX_BEND = 25.0       # degrees the wrist may bend beyond its rest angle


def solve_arm(side, palm, q_hand, pole, max_bend=MAX_BEND):
    """Put the palm at 'palm' with the hand turned as near q_hand as the wrist allows (at most
    max_bend degrees beyond its rest bend: a hand bent back too far does not hold a weapon), the
    elbow towards 'pole'. The hand turns about the palm, so the palm stays on its point.
    Returns the reach (wrist distance over arm length) and the wrist bend."""
    h = HAND[side]
    d_local = h["q_hand"].inverted() @ (h["frame"] @ Vector((1, 0, 0)))
    for _ in range(8):
        reach, elbow, wrist = _solve_arm(side, palm, q_hand, pole)
        fore = (wrist - elbow).normalized()
        bend = math.degrees((q_hand @ d_local).angle(fore)) - h["rest_bend"]
        if bend <= max_bend + 0.5:
            break
        axis = (q_hand @ d_local).cross(fore).normalized()
        q_hand = Quaternion(axis, math.radians(bend - max_bend)) @ q_hand
    return reach, bend


def _solve_arm(side, palm, q_hand, pole):
    h = HAND[side]
    wrist = palm - q_hand @ h["palm_local"]
    update()
    s = bone_head("upperarm_" + side)
    l1 = (rig.data.bones["lowerarm_" + side].head_local - rig.data.bones["upperarm_" + side].head_local).length
    l2 = (rig.data.bones["hand_" + side].head_local - rig.data.bones["lowerarm_" + side].head_local).length
    to = wrist - s
    dist = min(to.length, (l1 + l2) * 0.999)
    u = to.normalized()
    a = (l1 * l1 - l2 * l2 + dist * dist) / (2 * dist)
    hgt = math.sqrt(max(0.0, l1 * l1 - a * a))
    p = Vector(pole) - Vector(pole).dot(u) * u
    p.normalize()
    # the elbow on its circle as near the pole as it can be without entering the torso
    for k in range(19):
        for sgn in ((1,) if k == 0 else (1, -1)):
            pk = Quaternion(u, math.radians(sgn * 10 * k)) @ p
            elbow = s + u * a + pk * hgt
            if not inside_torso(elbow) and not inside_torso((s + elbow) / 2):
                break
        else:
            continue
        break
    p = pk
    wrist = s + u * dist
    # The elbow is a hinge: the upper arm turns so that the arm bends in its rest plane (the
    # plane of shoulder, elbow and wrist), and the forearm follows in the same plane. The
    # forearm's twist (pronation, supination) is shared between the elbow and the wrist.
    bones = rig.data.bones
    up_rest = bones["upperarm_" + side].matrix_local.to_quaternion()
    lo_rest = h["q_lower"]
    up_dir_rest = (bones["upperarm_" + side].tail_local - bones["upperarm_" + side].head_local).normalized()
    lo_dir_rest = (bones["lowerarm_" + side].tail_local - bones["lowerarm_" + side].head_local).normalized()
    n_rest = up_dir_rest.cross(lo_dir_rest).normalized()
    up_dir, lo_dir = (elbow - s).normalized(), (wrist - elbow).normalized()
    n = up_dir.cross(lo_dir)
    n = n.normalized() if n.length > 1e-4 else up_dir.cross(p).normalized()

    def align(a_rest, b_rest, a_now, b_now):
        """Rotation taking a_rest to a_now and, about it, b_rest as near b_now as possible."""
        r1 = a_rest.rotation_difference(a_now)
        b1 = r1 @ b_rest
        b1 = (b1 - b1.dot(a_now) * a_now).normalized()
        b2 = (b_now - b_now.dot(a_now) * a_now).normalized()
        ang = math.atan2(b1.cross(b2).dot(a_now), b1.dot(b2))
        return Quaternion(a_now, ang) @ r1

    q_u = align(up_dir_rest, n_rest, up_dir, n) @ up_rest
    q_l = align(lo_dir_rest, n_rest, lo_dir, n) @ lo_rest
    # the hand's twist relative to the forearm, about the forearm's axis (local Y); half of it
    # goes to the forearm
    rel = (q_l @ lo_rest.inverted() @ h["q_hand"]).inverted() @ q_hand
    twist = 2 * math.atan2(rel.y, rel.w)
    twist = (twist + math.pi) % (2 * math.pi) - math.pi
    q_l = Quaternion(lo_dir, twist / 2) @ q_l
    update()
    pb = rig.pose.bones["upperarm_" + side]
    pb.matrix = Matrix.Translation(pb.head) @ q_u.to_matrix().to_4x4()
    update()
    pb = rig.pose.bones["lowerarm_" + side]
    pb.matrix = Matrix.Translation(pb.head) @ q_l.to_matrix().to_4x4()
    update()
    pb = rig.pose.bones["hand_" + side]
    pb.matrix = Matrix.Translation(pb.head) @ q_hand.to_matrix().to_4x4()
    update()
    return to.length / (l1 + l2), elbow, wrist


TORSO_VERTS = {}


def torso_front():
    """The front (smallest y, armature space) of the jacket's and the body's vertices that
    follow the spine most (not the arms, the head or the legs), as posed."""
    if not TORSO_VERTS:
        for o in meshes:
            if not (o.name == "civilian" or "casualsuit" in o.name):
                continue
            groups = {g.index: g.name for g in o.vertex_groups}
            keep = []
            for v in o.data.vertices:
                best, bw = "", 0.0
                for g in v.groups:
                    n = groups.get(g.group, "")
                    if g.weight > bw and not n.startswith(("helper", "joint", "body", "Mid", "Left", "Right")):
                        best, bw = n, g.weight
                if best.startswith("spine_"):
                    keep.append(v.index)
            TORSO_VERTS[o.name] = keep
    subs = [m for name in TORSO_VERTS for m in bpy.data.objects[name].modifiers if m.type == "SUBSURF" and m.show_viewport]
    for m in subs:                                      # the original vertices, deformed only
        m.show_viewport = False
    update()
    deps = bpy.context.evaluated_depsgraph_get()
    inv = rig.matrix_world.inverted()
    front = None
    for name, idx in TORSO_VERTS.items():
        o = bpy.data.objects[name]
        ev = o.evaluated_get(deps)
        me = ev.to_mesh()
        if len(me.vertices) == len(o.data.vertices):       # the same vertices as the original mesh
            for i in idx:
                y = (inv @ (o.matrix_world @ me.vertices[i].co)).y
                front = y if front is None else min(front, y)
        ev.to_mesh_clear()
    for m in subs:
        m.show_viewport = True
    update()
    return front


# ---- body landmarks (armature space, rest) ----
def body_points(test):
    deps = bpy.context.evaluated_depsgraph_get()
    inv = rig.matrix_world.inverted()
    pts = []
    for o in meshes:
        ev = o.evaluated_get(deps)
        me = ev.to_mesh()
        for v in me.vertices:
            w = inv @ (o.matrix_world @ v.co)
            if test(w):
                pts.append(w)
        ev.to_mesh_clear()
    return pts


reset()
stand()
arms_hanging()
update()
torso = body_points(lambda w: 1.05 < w.z < 1.5 and abs(w.x) < 0.17)
CHEST_Y = min(p.y for p in torso)                         # the body's front from above
# The torso as an elliptic cylinder the elbow must stay out of (2 cm of the arm's flesh added):
# its front the jacket's, its back the jacket's back (not the pack), from the hips to below the
# shoulders (an arm may pass in front of the chest, touching it, as a real one does).
TORSO_BACK = max(p.y for p in body_points(lambda w: 1.05 < w.z < 1.45 and abs(w.x) < 0.14 and w.y < 0.16))
TORSO_RX = 0.18 + 0.02
TORSO_RY = (TORSO_BACK - CHEST_Y) / 2 + 0.02
TORSO_Y = (TORSO_BACK + CHEST_Y) / 2


def inside_torso(pt):
    return 0.90 < pt.z < 1.30 and (pt.x / TORSO_RX) ** 2 + ((pt.y - TORSO_Y) / TORSO_RY) ** 2 < 1.0
SH_R = bone_head("upperarm_r")
SH_L = bone_head("upperarm_l")
SH_Z = SH_R.z
print("BODY chest front y %.3f, shoulders z %.3f" % (CHEST_Y, SH_Z))


def gun_frame(fwd, up):
    """Rotation taking the weapon's X (bore) to fwd and Z (top) towards up."""
    f = Vector(fwd).normalized()
    u = Vector(up)
    u = (u - u.dot(f) * f).normalized()
    return Matrix((f, u.cross(f), u)).transposed()        # columns X=f, Y=u x f (left), Z=u


def place_gun(key, rot, origin):
    g = guns[key]
    m = Matrix.Translation(origin) @ rot.to_4x4()
    g.matrix_world = rig.matrix_world @ m
    g.hide_render = False
    return m


def gp(key, m, name):
    return m @ Vector(guns[key][name])


def pitch_dir(pitch_deg, yaw_deg=0.0):
    """Forward (-Y), pitched down by pitch_deg, turned yaw_deg towards the character's left."""
    p, y = math.radians(pitch_deg), math.radians(yaw_deg)
    return Vector((math.sin(y) * math.cos(p), -math.cos(y) * math.cos(p), -math.sin(p)))


def hold_grip(key, m, side="r", pole=(-0.6, 0.3, -1.0), thumb=30.0):
    """The right hand on the grip: the width runs up the grip, the fingers wrap forward;
    thumb: the thumb's bend (a pistol held with both hands: along the frame, about 5)."""
    rot = m.to_3x3()
    down = rot @ Vector(guns[key]["grip_down"])
    # the knuckles point along the barrel, or for a hammer grip (knife, flashlight) where given
    fwd = rot @ Vector(guns[key]["knuckles"] if len(guns[key]["knuckles"]) else (1, 0, 0))
    d = (fwd - fwd.dot(down) * down).normalized()
    left = rot @ Vector((0, 1, 0))
    d = (d * 0.75 + left * 0.35 - down * 0.15).normalized()            # knuckles on the gun's left
    q = hand_target(side, d, -down)
    WANTED[side] = q
    palm = m @ Vector(GRIP_PALM.get(key, GRIP_PALM["default"]))
    fingers, wrap = GRIP_CURL.get(key, (62.0, None))
    curl(side, fingers, thumb if wrap is None else wrap)
    return solve_arm(side, palm, q, pole, 30.0)


# Where the palm's centre lies in the weapon's frame (the grip at the origin): on the grip's
# right panel for a gun; for a hammer grip, beside the handle by its radius and the palm's
# flesh, a little towards the wrist (+Z, the knuckles point to -Z), so that the curled fingers
# close round the handle. And how far the fingers and the thumb bend.
GRIP_PALM = {"default": (0.0, -0.012, 0.0), "knife": (0.0, -0.024, 0.010), "flashlight": (0.0, -0.030, 0.014)}
GRIP_CURL = {"knife": (72.0, 35.0), "flashlight": (80.0, 45.0)}


WANTED = {}


def support_pistol(key, m):
    """The isosceles support hand: the palm fills the grip's left side, its heel against the
    right hand's, the fingers wrapped over the right hand's fingers under the trigger guard,
    the thumb forward along the frame, the wrist canted down about 30 degrees."""
    rot = m.to_3x3()
    down = rot @ Vector(guns[key]["grip_down"])
    fwd = rot @ Vector((1, 0, 0))
    right = rot @ Vector((0, -1, 0))
    w = (fwd * 0.5 - down * 0.85).normalized()                   # pinky to index: up the grip, canted forward
    d = (right * 0.8 + down * 0.35 + fwd * 0.2).normalized()       # the fingers round the front of the right hand
    q = hand_target("l", d, w)
    palm = m @ Vector((-0.004, 0.028, -0.012))
    curl("l", 58.0, 5.0)
    return solve_arm("l", palm, q, (0.25, 0.0, -1.0), 30.0)


def isosceles(lean):
    """The isosceles stance's upper body: leaned forward over the hips, both shoulders rolled
    forward, the head kept upright."""
    for b in ("spine_01", "spine_02", "spine_03"):
        turn_bone(b, "X", -lean / 3)
    turn_bone("neck_01", "X", lean * 0.8)
    update()
    for side in ("l", "r"):
        c = rig.pose.bones["clavicle_" + side]
        aim_bone("clavicle_" + side, (c.tail - c.head).normalized() + Vector((0, -0.15, 0.0)))


def eye_point():
    """The right (the aiming) eye: half the distance between the eyes right of the centre."""
    update()
    pb = rig.pose.bones["head"]
    q = pb.matrix.to_quaternion() @ rig.data.bones["head"].matrix_local.to_quaternion().inverted()
    return pb.head + q @ Vector((-0.035, -0.085, 0.075))


ISO_REACH = 0.90          # the elbows soft, not locked: wrist distance over arm length
ISO_SIGHT = 0.035         # the sights above the bore
ISO_DROP = 0.03           # the sights a little below the eye: the head tilts a little to them


FORE_RADIUS = 0.027 + 0.014       # the handguard's half height, plus the palm's flesh


def fore_palm(key, m, slide=0.0):
    """The support palm: under the handguard, a little to its left; 'slide' metres back along
    the bore (a shotgun's pump racked)."""
    rot = m.to_3x3()
    return gp(key, m, "fore") + rot @ (Vector((-slide, 0, 0)) + Vector((0, 0.25, -1.0)).normalized() * FORE_RADIUS)


def support_fore(key, m, slide=0.0):
    """The left hand supports the handguard from below and holds it: the open palm under it,
    the fingers bent loosely up round its right side, angled forward as a real support hand
    is, the thumb along its left side. Not a clenched fist."""
    rot = m.to_3x3()
    fwd = rot @ Vector((1, 0, 0))
    right = rot @ Vector((0, -1, 0))
    up = rot @ Vector((0, 0, 1))
    d = (fwd * 0.45 + right * 0.85 + up * 0.15).normalized()
    q = hand_target("l", d, fwd)
    curl("l", 34.0, 0.0)
    return solve_arm("l", fore_palm(key, m, slide), q, (0.8, 0.2, -1.0), 35.0)


def guard_fists():
    """Unarmed, ready: a boxer's guard, the left fist leading at chin height, the right by the
    chin, elbows down, the fists' knuckles forward and up."""
    isosceles(5.0)
    eye = eye_point()
    fwd, up = Vector((0, -1, 0)), Vector((0, 0, 1))
    info = {}
    for side, at, pole in (("l", Vector((0.08, -0.20, -0.12)), (0.4, 0.2, -1.0)),
                           ("r", Vector((-0.09, -0.10, -0.14)), (-0.4, 0.2, -1.0))):
        palm = Vector((0.0, eye.y, eye.z)) + at
        # a vertical fist: the knuckles forward, the index on top, the palm towards the face
        q = hand_target(side, fwd * 0.9 + up * 0.3, up)
        curl(side, 90.0, 40.0)
        info["reach_" + side], info["bend_" + side] = solve_arm(side, palm, q, pole, 35.0)
    return info


DIAGONAL = 55.0       # degrees between a hammer-gripped handle and the hand's width (index side)


def diagonal_hold(key, palm, pitch, yaw, up, pole, span=(-0.40, 0.10)):
    """diagonal_hold_once pointed pitch degrees down (yaw to the left). Beyond the wrist's
    limit the weapon follows the forearm, so the hand's height is searched (within 'span' of
    'palm', metres) until the weapon points so, within half a degree: to point a light level
    with the arm out, the arm slopes a little down, as a real person's does."""
    lo, hi = span
    for _ in range(16):
        dz = (lo + hi) / 2
        m, reach, bend = diagonal_hold_once(key, palm + Vector((0, 0, dz)), pitch_dir(pitch, yaw), up, pole)
        got = math.degrees(math.asin(-(m.to_3x3() @ Vector((1, 0, 0))).normalized().z))
        if abs(got - pitch) < 0.5:
            break
        lo, hi = (lo, dz) if got < pitch else (dz, hi)      # lower hand -> the weapon tips down
    return m, reach, bend


def diagonal_hold_once(key, palm, aim, up, pole):
    """A knife or flashlight pointed along 'aim': the handle lies diagonally across the palm,
    from the heel of the hand to between the thumb and the index, so that the blade or the
    beam continues the forearm's line. The hand is placed first (the wrist allowed its usual
    bend), then the weapon follows the hand as it ends up. 'up' is the weapon's top (the knife's
    spine, away from the edge)."""
    a = math.radians(DIAGONAL)
    A = Vector(aim).normalized()
    U = Vector(up) - Vector(up).dot(A) * A
    U.normalize()
    w = A * math.cos(a) + U * math.sin(a)          # pinky to index
    d = A * math.sin(a) - U * math.cos(a)          # wrist to knuckles
    q = hand_target("r", d, w)
    fingers, wrap = GRIP_CURL.get(key, (62.0, 30.0))
    curl("r", fingers, wrap)
    reach, bend = solve_arm("r", palm, q, pole, 30.0)
    # the hand as posed: its direction and width in armature space
    update()
    h = HAND["r"]
    q_now = rig.pose.bones["hand_r"].matrix.to_quaternion() @ h["q_hand"].inverted() @ h["frame"]
    d1, w1 = q_now @ Vector((1, 0, 0)), q_now @ Vector((0, 1, 0))
    n1 = d1.cross(w1) * h["sign"]                  # the palm's normal, towards what it holds
    X = (w1 * math.cos(a) + d1 * math.sin(a)).normalized()
    Z = (w1 * math.sin(a) - d1 * math.cos(a)).normalized()
    rot = Matrix((X, Z.cross(X), Z)).transposed()
    radius = 0.0175 if key == "flashlight" else 0.013
    origin = palm_point("r") + n1 * (radius + 0.010)
    m = place_gun(key, rot, origin)
    return m, reach, bend


def ready_knife(key):
    """Knife ready: held low beside the right hip and a little forward, the blade forward and
    15 degrees up, as about to stab at the belly, the edge down; the left hand half closed in
    front of the chest, guarding."""
    isosceles(6.0)
    chest, hip = bone_head("spine_03"), bone_head("thigh_r")
    info = {"diagonal": True}
    m, info["reach_r"], info["bend_r"] = diagonal_hold(key, Vector((hip.x - 0.08, hip.y - 0.22, hip.z + 0.08)),
                                                        -15.0, 4.0, (0, 0, 1), (-0.6, 0.4, -1.0), span=(-0.10, 0.12))
    # the left hand half closed in front of the chest, the knuckles forward
    q = hand_target("l", (0.0, -0.9, 0.4), (0.0, 0.0, 1.0))
    curl("l", 50.0, 20.0)
    info["reach_l"], info["bend_l"] = solve_arm("l", Vector((0.10, chest.y - 0.26, chest.z + 0.02)), q, (0.6, 0.2, -1.0), 35.0)
    return (key, m), info


STAB_CLIP = "Stabbing"
STAB_FRAMES = (29.0, 13.0)        # drawn back; the thrust
STAB_AT = (0.34, 1.02, -0.06)     # the thrust's hand: ahead of the chest, height, to the side (m)


def stab_knife(key, i):
    """A stab at the belly, two frames from the motion-captured stab (its step, lean and left
    arm): 0 the ready stance (ready_knife) with the knife drawn back beside the right hip, the
    blade forward, the left hand guarding in front; 1 the thrust, from the clip, the right hand
    put lower and closer than the clip's (at the belly of someone in front, STAB_AT), the
    blade forward and a little up."""
    if i == 0:          # the accepted ready stance with the knife drawn back beside the right hip,
                        # the blade forward; the left hand guarding
        reset()
        stand()
        isosceles(6.0)
        info = {}
        hip = bone_head("thigh_r")
        info["diagonal"] = True
        m, info["reach_r"], info["bend_r"] = diagonal_hold(key, Vector((hip.x - 0.09, hip.y - 0.04, hip.z + 0.10)),
                                                            -10.0, 4.0, (0, 0, 1), (-0.6, 0.6, -1.0), span=(-0.06, 0.10))
        chest = bone_head("spine_03")
        info["reach_l"], info["bend_l"] = left_relaxed(Vector((0.10, chest.y - 0.30, chest.z - 0.04)),
                                                       Vector((0.0, -0.2, 1.0)), 40.0, pole=(0.6, 0.2, -1.0), lead=(0, -0.7, 0.5))
        return (key, m), info
    (_, m), info = clip_pose(key, STAB_CLIP, STAB_FRAMES[i])
    if i == 1:
        chest = bone_head("spine_03")
        palm = Vector((chest.x + STAB_AT[2], chest.y - STAB_AT[0], STAB_AT[1]))
        info["diagonal"] = True
        m, info["reach_r"], info["bend_r"] = diagonal_hold(key, palm, -12.0, 4.0, (0, 0, 1), (-0.5, 0.4, -1.0),
                                                            span=(-0.06, 0.08))
    return (key, m), info


def strike_flashlight(key, i):
    """A hammer strike with the flashlight, two frames: 0 raised beside the right ear, the head
    of the light up and back, the right shoulder drawn back, the left hand guarding; 1 the blow,
    the arm swung forward and down, the fist at chest height in front, the head of the light
    forward and down, the right shoulder forward and the body leaned in."""
    info = {"diagonal": True}
    if i == 0:
        isosceles(4.0)
        for b in ("spine_01", "spine_02", "spine_03"):
            turn_bone(b, "Z", -5.0)                    # the right shoulder back
        turn_bone("head", "Z", 12.0)
        sh = bone_head("upperarm_r")
        m, info["reach_r"], info["bend_r"] = diagonal_hold(key, Vector((sh.x - 0.02, sh.y + 0.02, sh.z + 0.16)),
                                                            -115.0, 0.0, (0, 1, 0), (-0.6, 0.4, -0.3), span=(-0.10, 0.10))
    else:
        isosceles(10.0)
        for b in ("spine_01", "spine_02", "spine_03"):
            turn_bone(b, "Z", 6.0)                     # the right shoulder forward
        turn_bone("head", "Z", -15.0)
        sh = bone_head("upperarm_r")
        m, info["reach_r"], info["bend_r"] = diagonal_hold(key, Vector((sh.x + 0.07, sh.y - 0.42, sh.z - 0.26)),
                                                            30.0, 4.0, (0, -1, 1), (-0.6, 0.3, -1.0), span=(-0.10, 0.14))
    chest = bone_head("spine_03")
    info["reach_l"], info["bend_l"] = left_relaxed(Vector((0.12, chest.y - 0.28, chest.z + 0.02)),
                                                   Vector((0.0, -0.2, 1.0)), 50.0, pole=(0.6, 0.2, -1.0), lead=(0, -0.6, 0.6))
    return (key, m), info


def aim_flashlight(key):
    """Flashlight aimed: the right arm forward, the beam level at shoulder height; the left arm
    hangs."""
    isosceles(4.0)
    sh = bone_head("upperarm_r")
    info = {}
    info["diagonal"] = True
    m, info["reach_r"], info["bend_r"] = diagonal_hold(key, Vector((sh.x + 0.06, sh.y - 0.50, sh.z - 0.06)),
                                                        0.0, 4.0, (0, 0, 1), (-0.4, 0.2, -1.0))
    arms_hanging(("l",))
    return (key, m), info


def pose(name):
    """A standing pose: <weapon>_low (lowered, as the standing frame of the gaits), <weapon>_aim,
    <weapon>_fire0 / _fire1 (FIRE) or <weapon>_reload0 .. _reload3 (reload_pose); 'none' is the
    civilians' idle."""
    reset()
    stand()
    if name == "none":
        arms_hanging()
        return None, {}
    if name.startswith("clip:"):                 # clip:<weapon>:<action>:<frame>
        _, key, action, f = name.split(":")
        return clip_pose(key, action, float(f))
    key, kind = name.rsplit("_", 1)
    info = {}
    if kind.startswith("reload"):
        return reload_pose(key, int(kind[6:]))
    if kind.startswith("stab"):
        return stab_knife(key, int(kind[4:]))
    if kind.startswith("strike"):
        return strike_flashlight(key, int(kind[6:]))
    shot = FIRE[key][int(kind[4:] or 0)] if kind.startswith("fire") else (0.0, 0.0, 0.0, 0.0)
    recoil, kick, give, pump = shot
    if kind == "low":
        gait_pose("stand", 0)
        GAIT_MEAN.clear()
        for g in guns.values():
            g.hide_render = True
        return carry(key if key != "unarmed" else "none")
    if key == "unarmed":
        return None, guard_fists()
    if key == "knife":
        return ready_knife(key)
    if key == "flashlight":
        return aim_flashlight(key)
    if key == "pistol":
        # isosceles: the sights at the eye, the gun on the eye's line, the arms nearly
        # straight; firing, the muzzle flips up and the elbows give a little
        isosceles(7.0)
        target = ISO_REACH - give
        rot = gun_frame(pitch_dir(-recoil), (0, 0, 1))
        eye = eye_point()
        bore_z = guns[key]["muzzle"][2]
        lo, hi = 0.15, 0.80
        for _ in range(20):                 # the distance ahead of the eye that gives the reach
            ahead = (lo + hi) / 2
            origin = Vector((eye.x, eye.y - ahead, eye.z - ISO_DROP - ISO_SIGHT - bore_z))
            m = place_gun(key, rot, origin)
            reach, _ = hold_grip(key, m, pole=(-0.25, 0.0, -1.0), thumb=5.0)
            lo, hi = (ahead, hi) if reach < target else (lo, ahead)
        info["reach_r"], info["bend_r"] = hold_grip(key, m, pole=(-0.25, 0.0, -1.0), thumb=5.0)
        info["reach_l"], info["bend_l"] = support_pistol(key, m)
        info["sight_below_eye_m"] = eye.z - (m @ Vector((0, 0, bore_z))).z - ISO_SIGHT
        info["right_of_centre_m"] = -(m @ Vector((0, 0, 0))).x
        return (key, m), info
    # long guns, shouldered
    butt = Vector(guns[key]["butt"])
    blade(32.0, 8.0)
    info["pocket"] = pocket()
    rot = gun_frame(pitch_dir(-recoil), (0, 0, 1))
    at = info["pocket"] + Vector((0, kick, 0))
    origin = at - rot @ butt
    m = place_gun(key, rot, origin)
    info["reach_r"], info["bend_r"] = hold_grip(key, m)
    info["reach_l"], info["bend_l"] = support_fore(key, m, pump)
    if pump:
        info["pump"] = True                      # the fore grip error is measured at the racked pump
    return (key, m), info


# Firing, two frames per gun: (muzzle flip in degrees, the butt pushed back in metres, how much
# less the pistol's arms reach, the shotgun's pump racked back in metres). The first frame is
# the recoil's peak, the second the recovery (the shotgun pumping).
FIRE = {"pistol": [(12.0, 0.0, 0.05, 0.0), (5.0, 0.0, 0.02, 0.0)],
        "rifle": [(6.0, 0.025, 0.0, 0.0), (2.5, 0.010, 0.0, 0.0)],
        "shotgun": [(9.0, 0.035, 0.0, 0.0), (2.0, 0.010, 0.0, 0.07)]}


def facing(side, d, palm_dir):
    """The hand's width for fingers along d and the palm facing palm_dir (hand_target's palm
    is d x w times the hand's sign)."""
    d = Vector(d).normalized()
    p = Vector(palm_dir)
    p = (p - p.dot(d) * d).normalized()
    return p.cross(d) * HAND[side]["sign"]


def left_relaxed(palm, palm_dir, curl_deg=35.0, pole=(0.3, 0.4, -1.0), lead=None):
    """The left hand at 'palm', continuing the forearm's line (a relaxed wrist), the palm
    towards palm_dir: solved twice, the second time with the fingers along the forearm the
    first solve gave ('lead' tips them that way, a unit vector)."""
    d = Vector(lead) if lead is not None else Vector((0, -0.3, -1.0))
    for _ in range(2):
        r = left_hand(palm, d, facing("l", d, palm_dir), curl_deg, pole)
        update()
        fore = (bone_head("hand_l") - bone_head("lowerarm_l")).normalized()
        d = (fore + (Vector(lead) * 0.3 if lead is not None else Vector())).normalized()
    return r


def left_hand(palm, d, w, curl_deg=40.0, pole=(0.7, 0.2, -1.0)):
    q = hand_target("l", d, w)
    curl("l", curl_deg, 20.0)
    return solve_arm("l", palm, q, pole, 35.0)


GRIP_REL = {}


def grip_relation(key):
    """The weapon's matrix relative to the right hand bone, as hold_grip asks for it: taken
    from the wanted hand (before the wrist's limit), so it is the grip itself."""
    if key not in GRIP_REL:
        rot = gun_frame(pitch_dir(30, 0), (0, 0, 1))
        m = place_gun(key, rot, Vector((-0.2, -0.3, 1.1)))
        hold_grip(key, m)
        q = WANTED["r"]
        palm = m @ Vector(GRIP_PALM.get(key, GRIP_PALM["default"]))
        wrist = palm - q @ HAND["r"]["palm_local"]
        GRIP_REL[key] = Matrix.LocRotScale(wrist, q, None).inverted() @ m
    return GRIP_REL[key]


def follow_hand(key):
    """Put the weapon in the right hand as the hand is posed (a clip's reload)."""
    update()
    pb = rig.pose.bones["hand_r"]
    m = Matrix.LocRotScale(pb.head, pb.matrix.to_quaternion(), None) @ grip_relation(key)
    curl("r", *GRIP_CURL.get(key, (62.0, 30.0)))
    return place_gun(key, m.to_3x3().normalized(), m.to_translation())


def clip_pose(key, action, f):
    """A frame of a clip (a reload) with the weapon in the right hand as the clip moves it."""
    reset()
    if key not in guns:                           # a punch: fists, the clip's torso turn kept
        gait_pose(action, f)
        for b in ("spine_01", "spine_02", "spine_03"):
            turn_bone(b, "X", -PUNCH_UPRIGHT / 3)       # less bent over than the clip
        turn_bone("neck_01", "X", -8.0)                # the eyes on the target, not the ground
        curl("l", 90.0, 40.0)
        curl("r", 90.0, 40.0)
        # the whole body turned so that the punching fist (the one furthest from the chest)
        # strikes straight ahead, along the aim
        update()
        chest = bone_head("spine_03")
        fist = max((bone_head("hand_l"), bone_head("hand_r")), key=lambda h: (h - chest).xy.length)
        side = "l" if (fist - bone_head("hand_l")).length < 1e-6 else "r"
        sh = bone_head("upperarm_" + side)
        d = fist - sh
        want_z = bone_head("head").z + 0.03                 # the face: chin to mouth
        axis = d.cross(Vector((0, 0, 1))).normalized()
        pb = rig.pose.bones["upperarm_" + side]
        step = 0.0
        for _ in range(12):                                 # turn about the shoulder until there
            update()
            err = bone_head("hand_" + side).z - want_z
            if abs(err) < 0.005:
                break
            d = bone_head("hand_" + side) - sh
            ang = math.asin(max(-1.0, min(1.0, err / d.length)))
            rot = Matrix.Rotation(ang, 4, axis)
            pb.matrix = Matrix.Translation(sh) @ rot @ Matrix.Translation(-sh) @ pb.matrix
            update()
            if abs(bone_head("hand_" + side).z - want_z) > abs(err):    # the wrong way: undo, flip
                pb.matrix = Matrix.Translation(sh) @ rot.inverted() @ Matrix.Translation(-sh) @ pb.matrix
                axis = -axis
        update()
        fist = bone_head("hand_" + side)
        v = fist - chest
        rig.rotation_euler.z -= math.atan2(v.x, -v.y)
        update()
        return None, {"turn_deg": math.degrees(math.atan2(v.x, -v.y)), "fist_below_head_m": want_z - fist.z}
    grip_relation(key)
    gait_pose(action, f)
    face_front()                                  # the clips turn the whole body; the layer faces the aim
    for g in guns.values():
        g.hide_render = True
    m = follow_hand(key)
    if key in mags:
        place_mag(key, m @ seat(key))
    info = {}
    if key == "rifle":
        info["handle_m"] = (palm_point("l") - m @ Vector((-0.04, 0.012, 0.11))).length
    return (key, m), info


PUNCH_UPRIGHT = 14.0     # degrees taken off the punch clips' forward lean

# The rifle's charging frame: the Mixamo reload's frame where the left hand pulls the handle.
CHARGE_CLIP = ("Reloading_2_handed_gun", float(os.environ.get("CJ_CHARGE_FRAME", "68")))


def seat(key):
    """The magazine's frame in the weapon's frame, seated in the well."""
    v = guns[key]["mag_seat"]
    return Matrix([v[0:4], v[4:8], v[8:12], v[12:16]])


def place_mag(key, m):
    o = mags[key]
    o.matrix_world = rig.matrix_world @ m
    o.hide_render = False
    return m


def mag_in_left_hand(key):
    """The magazine as the left hand holds it after the pose: upright along the hand's width
    (the index at the feed lips), its front towards the fingers, its body in the palm."""
    update()
    h = HAND["l"]
    q_now = rig.pose.bones["hand_l"].matrix.to_quaternion() @ h["q_hand"].inverted() @ h["frame"]
    d1, w1 = q_now @ Vector((1, 0, 0)), q_now @ Vector((0, 1, 0))
    n1 = d1.cross(w1) * h["sign"]
    z = w1.normalized()
    x = (d1 - d1.dot(z) * z).normalized()
    half = 0.012 if key == "pistol" else 0.016
    centre = palm_point("l") + n1 * (half + 0.006) - z * 0.01
    return place_mag(key, Matrix.Translation(centre) @ Matrix((x, z.cross(x), z)).transposed().to_4x4())


def left_on_mag(key, mm, under=False):
    """The left hand on a magazine (matrix mm): round its body, the index at the top; or with
    'under', the palm pushing on its base."""
    R = mm.to_3x3()
    mx, my, mz = R @ Vector((1, 0, 0)), R @ Vector((0, 1, 0)), R @ Vector((0, 0, 1))
    half = 0.058 if key == "pistol" else 0.11
    if under:
        palm = mm @ Vector((0, 0, -half - 0.012))
        return left_hand(palm, mx, my, 55.0, (0.3, 0.3, -1.0))   # the palm under the base, the fingers round it
    palm = mm @ Vector((0, 0.03, -0.01))                 # on the magazine's left side
    return left_hand(palm, mx * 0.6 - mz * 0.2, mz, 70.0, (0.3, 0.3, -1.0))


SHOTGUN_PORT = Vector((0.11, 0.0, -0.035))      # the loading port under the receiver, gun frame


def shell_in_left_hand():
    """The shell held in the left hand's fingertips, across the fingers."""
    update()
    h = HAND["l"]
    q_now = rig.pose.bones["hand_l"].matrix.to_quaternion() @ h["q_hand"].inverted() @ h["frame"]
    d1, w1 = q_now @ Vector((1, 0, 0)), q_now @ Vector((0, 1, 0))
    n1 = d1.cross(w1) * h["sign"]
    centre = palm_point("l") + d1 * 0.075 + n1 * 0.03
    z = w1.normalized()
    x = (d1 - d1.dot(z) * z).normalized()
    return place_mag("shell", Matrix.Translation(centre) @ Matrix((x, z.cross(x), z)).transposed().to_4x4())


def shotgun_reload(key, i, info):
    """Loading the pump shotgun shell by shell, four frames: 0 the left hand at the left upper
    thigh taking a shell; 1 the shell at the loading port, its head still out; 2 the thumb's
    heel pushing it home, the hand under the receiver; 3 the gun forward and down, the left
    hand racking the pump back. In 0-2 the gun is held across the chest, rolled so that the
    loading port faces the left hand."""
    butt = Vector(guns[key]["butt"])
    thigh = bone_head("thigh_l")
    if i == 3:
        info["pocket"] = pocket()                     # held as when backpedalling (STRAIGHT)
        rot = gun_frame(pitch_dir(STRAIGHT_PITCH, STRAIGHT_YAW), (0, 0, 1))
        m = place_gun(key, rot, info["pocket"] - rot @ butt)
        info["reach_r"], info["bend_r"] = hold_grip(key, m)
        info["reach_l"], info["bend_l"] = support_fore(key, m, FIRE[key][1][3])
        info.pop("pocket")
        return (key, m), info
    rot = gun_frame(pitch_dir(-50, 35), (-0.55, 0.0, 0.83))     # the top to the right: the port faces left
    m = place_gun(key, rot, pocket() + Vector((0.05, 0.08, -0.22)) - rot @ butt)
    info["reach_r"], info["bend_r"] = hold_grip(key, m)
    R = m.to_3x3()
    fwd, top = R @ Vector((1, 0, 0)), R @ Vector((0, 0, 1))
    port = m @ SHOTGUN_PORT
    if i == 0:
        info["reach_l"], info["bend_l"] = left_relaxed(Vector((thigh.x + 0.06, thigh.y - 0.10, thigh.z - 0.10)),
                                                       Vector((-0.3, 1.0, 0.0)), 55.0, lead=(0, 0, -1))
        shell_in_left_hand()
    elif i == 1:        # the shell at the port, pointing forward into the tube, its head out
        tilt = (fwd - top * 0.6).normalized()             # its nose at the port, from below and behind
        place_mag("shell", frame_along(port - top * 0.03 - fwd * 0.03, tilt, top))
        palm = port - top * 0.075 - fwd * 0.075
        info["reach_l"], info["bend_l"] = left_hand(palm, fwd, facing("l", fwd, top), 45.0, (0.4, 0.3, -1.0))
    else:               # pushed home: the heel of the thumb under the port
        palm = port - top * 0.035 - fwd * 0.01
        info["reach_l"], info["bend_l"] = left_hand(palm, fwd, facing("l", fwd, top), 35.0, (0.4, 0.3, -1.0))
    return (key, m), info


def frame_along(origin, z, up):
    """A matrix at origin with +Z along z and +X towards up."""
    z = z.normalized()
    x = (up - up.dot(z) * z).normalized()
    return Matrix.Translation(origin) @ Matrix((x, z.cross(x), z)).transposed().to_4x4()


def reload_pose(key, i):
    """Reloading, four frames that together show the whole magazine change; the gun is
    brought in to the chest and tilted, its well towards the left hand (a long gun's butt
    under the armpit), the right hand on the grip:
      pistol: 0 the empty magazine dropping out, the left hand on its way to the pocket;
              1 the left hand at the left upper thigh, taking out the new magazine; 2 the magazine in the well,
              the palm pushing it home; 3 the left hand over the slide, racking it;
      rifle:  0 the left hand pulling the empty magazine out; 1 at the pocket with the new one;
              2 gripping it and pushing it into the well; 3 the left hand on the charging handle, pulling it;
      shotgun: shells from the pocket, 0 at the pocket, 1 at the loading port, 2 and 3 again."""
    isosceles(5.0)
    info = {}
    chest = bone_head("spine_03")
    # the spare magazines in the jacket's left front pocket, at the waist (a belt pouch would be
    # under the jacket)
    pouch = bone_head("thigh_l") + Vector((0.0, -0.17, 0.06))
    POUCH_POLE = (0.15, 0.6, -1.0)                                   # the elbow back and down, not out
    if key == "pistol":
        rot = gun_frame(pitch_dir(-30, 0), (0.5, 0.0, 0.85))           # muzzle up, the top to the left
        m = place_gun(key, rot, chest + Vector((-0.01, -0.32, -0.04)))
        info["reach_r"], info["bend_r"] = hold_grip(key, m, pole=(-0.6, 0.3, -1.0))
        S = m @ seat(key)
        R = m.to_3x3()
        mx, my, mz = R @ Vector((1, 0, 0)), R @ Vector((0, 1, 0)), R @ Vector((0, 0, 1))
        front = torso_front()
        # the spare magazine on the left upper thigh (a trouser pocket or a thigh pouch), below
        # the jacket's hem
        thigh = bone_head("thigh_l")
        pocket_pt = Vector((thigh.x + 0.06, thigh.y - 0.10, thigh.z - 0.10))
        if i == 0:      # the empty magazine falls; the hand on its way down to the pocket
            # falling, at the waist below the gun, tipping as it drops
            fall = S @ Matrix.Rotation(0.45, 4, "Y")
            at = fall.to_translation()
            place_mag(key, Matrix.Translation((at.x, at.y - 0.02, bone_head("thigh_l").z + 0.06)) @ fall.to_3x3().to_4x4())
            info["reach_l"], info["bend_l"] = left_relaxed(Vector((0.17, front - 0.04, chest.z - 0.16)),
                                                           Vector((-0.8, 0.5, 0.0)), 30.0, lead=(0, -0.2, -1))
        elif i == 1:    # at the upper thigh, taking out the new magazine
            info["reach_l"], info["bend_l"] = left_relaxed(pocket_pt, Vector((-0.3, 1.0, 0.0)), 70.0, lead=(0, 0, -1))
            mag_in_left_hand(key)
        elif i == 2:    # the magazine in the well, the palm under its base pushing it home
            mm = place_mag(key, S @ Matrix.Translation((0, 0, -0.035)))
            # the forearm comes up from the left side, the elbow down beside the body, the hand
            # in its line, the palm up under the base
            # the palm up and in (towards the body's middle), not turned fully up: the forearm
            # hardly rolls; the heel of the hand on the base
            base = mm @ Vector((0, 0, -0.058 - 0.012))
            up = mm.to_3x3() @ Vector((0, 0, 1))
            info["reach_l"], info["bend_l"] = left_relaxed(base, (up * 0.55 + Vector((-0.8, 0.0, 0.0))).normalized(), 45.0,
                                                           pole=(0.4, 0.1, -1.0), lead=(-0.35, -0.6, 0.6))
        else:           # overhand on the slide's rear: palm on top, fingers down its right side
            place_mag(key, S)
            palm = m @ Vector((-0.04, 0.0, 0.085 + 0.018))
            info["reach_l"], info["bend_l"] = left_hand(palm, -my, facing("l", -my, -mz), 75.0, (0.5, 0.3, -1.0))
        return (key, m), info
    butt = Vector(guns[key]["butt"])
    # pulled in: the butt under the right armpit, the muzzle up and across the chest to the
    # left, so that the well is a hand's span in front of the chest and the left elbow bends
    rot = gun_frame(pitch_dir(-35, 40), (0.45, 0.0, 0.89))
    at = pocket() + Vector((0.04, 0.08, -0.18))
    m = place_gun(key, rot, at - rot @ butt)
    info["reach_r"], info["bend_r"] = hold_grip(key, m)
    R = m.to_3x3()
    fx, fz = R @ Vector((1, 0, 0)), R @ Vector((0, 0, 1))
    if key == "rifle":
        S = m @ seat(key)
        thigh = bone_head("thigh_l")
        thigh_pt = Vector((thigh.x + 0.06, thigh.y - 0.10, thigh.z - 0.10))

        def grip_mag(mm):
            """The left hand round the magazine's left side, the palm towards it, the hand in
            the forearm's line."""
            my = mm.to_3x3() @ Vector((0, 1, 0))
            return left_relaxed(mm @ Vector((0, 0.03, -0.01)), -my, 70.0, pole=(0.4, 0.3, -1.0), lead=(0, -0.3, 0.8))

        if i == 0:      # the empty magazine pulled out and let go: falling at the waist, the open
                        # left hand just above it
            fall = S @ Matrix.Rotation(0.5, 4, "Y")
            at = fall.to_translation()
            low = Vector((at.x + 0.02, at.y + 0.04, thigh.z + 0.08))
            place_mag(key, Matrix.Translation(low) @ fall.to_3x3().to_4x4())
            info["reach_l"], info["bend_l"] = left_relaxed(low + Vector((0.0, 0.0, 0.17)), Vector((-0.3, 0.2, -1.0)), 25.0,
                                                           pole=(0.4, 0.3, -1.0), lead=(0, -0.3, -0.6))
        elif i == 1:    # at the left upper thigh, taking out the new magazine
            info["reach_l"], info["bend_l"] = left_relaxed(thigh_pt, Vector((-0.3, 1.0, 0.0)), 70.0, lead=(0, 0, -1))
            mag_in_left_hand(key)
        elif i == 2:    # the new magazine at the well, pushed in
            mm = place_mag(key, S @ Matrix.Translation((0, 0, -0.04)))
            info["reach_l"], info["bend_l"] = grip_mag(mm)
        else:           # charging, from the motion-captured reload (see CHARGE_CLIP)
            return clip_pose(key, *CHARGE_CLIP)
        return (key, m), info
    return shotgun_reload(key, i, info)


def measure(held, info):
    if held is None:
        return info
    key, m = held
    g = guns[key]
    muzzle = gp(key, m, "muzzle")
    bore = (m.to_3x3() @ Vector((1, 0, 0))).normalized()
    info["pitch_deg"] = math.degrees(math.asin(-bore.z))
    update()
    deps = bpy.context.evaluated_depsgraph_get()
    ev = g.evaluated_get(deps)
    inv = rig.matrix_world.inverted()
    ys = [(inv @ (g.matrix_world @ v.co)).y for v in ev.data.vertices]
    # ahead of the body as posed: the front of the torso (the jacket over the spine, not the arms)
    info["reach_m"] = torso_front() - min(ys)
    if key == "flashlight":                    # the lens: right of and ahead of the pelvis, its height (m)
        lens = m @ Vector(g["muzzle"])
        pel = rig.pose.bones["pelvis"].head
        info["lens_right_m"], info["lens_ahead_m"], info["lens_z_m"] = pel.x - lens.x, pel.y - lens.y, lens.z
        # and as the frame shows it: from the frame centre (the camera), the character facing +Y
        # (the image top), and where the beam points: degrees to the right and below the horizontal
        lw = rig.matrix_world @ lens
        dw = (rig.matrix_world.to_3x3() @ m.to_3x3() @ Vector((1, 0, 0))).normalized()
        info["lamp"] = [lw.x - cam.location.x, lw.y - cam.location.y, lw.z,
                        math.degrees(math.atan2(dw.x, dw.y)), math.degrees(math.asin(max(-1.0, min(1.0, -dw.z))))]
    if not info.pop("diagonal", False):
        info["grip_err_m"] = (palm_point("r") - (m @ Vector(GRIP_PALM.get(key, GRIP_PALM["default"])))).length
    if g["butt"] and info.pop("across", False):
        info["fore_err_m"] = (palm_point("l") - fore_palm(key, m)).length
    if g["butt"] and "pocket" in info:          # held at the shoulder (not reloading)
        info["butt_shoulder_m"] = (gp(key, m, "butt") - info.pop("pocket")).length
        slide = FIRE[key][1][3] if info.pop("pump", False) else 0.0
        info["fore_err_m"] = (palm_point("l") - fore_palm(key, m, slide)).length
    return {k: ([round(x, 4) for x in v] if isinstance(v, list) else round(v, 3)) for k, v in info.items()}


# ---- camera, light, render (as tools/cj004/render_civilian.py) ----
cam_data = bpy.data.cameras.new("top")
cam_data.type = "ORTHO"
cam_data.ortho_scale = FRAME_M
cam_data.clip_end = 20
cam = bpy.data.objects.new("top", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
cam.location = (0.0, 0.0, 6.0)
sun_data = bpy.data.lights.new("sun", "SUN")
sun_data.energy = 5.0
sun_data.angle = math.radians(25)
sun = bpy.data.objects.new("sun", sun_data)
sun.rotation_euler = Vector((0.75, -0.75, -0.9)).normalized().to_track_quat("-Z", "Y").to_euler()
scene.collection.objects.link(sun)
world = bpy.data.worlds.new("sky")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.57, 0.588, 0.6, 1.0)
scene.world = world
r = scene.render
r.resolution_x = r.resolution_y = FRAME_PX
r.film_transparent = True
r.image_settings.file_format = "PNG"
r.image_settings.color_mode = "RGBA"
scene.view_settings.view_transform = "Standard"


def render(path):
    r.engine = "CYCLES"
    scene.cycles.samples = int(os.environ.get("CJ_SAMPLES", "48"))
    scene.cycles.use_denoising = True
    scene.cycles.transparent_max_bounces = 64     # a layer looks through many transparent surfaces
    r.filepath = path
    bpy.ops.render.render(write_still=True)


# ---- layers: each surface belongs to the legs or to the upper body ----
def build_leg_attribute():
    """legmask: 1 where a mesh belongs to the legs or shoes (by its strongest bone)."""
    for o in [o for o in bpy.data.objects if o.type == "MESH"]:
        me = o.data
        attr = me.color_attributes.get("legmask") or me.color_attributes.new("legmask", "FLOAT_COLOR", "POINT")
        groups = {g.index: g.name for g in o.vertex_groups}
        shoe = "boots" in o.name.lower() or "shoe" in o.name.lower()
        for v in me.vertices:
            best, best_w = "", 0.0
            for g in v.groups:
                n = groups.get(g.group, "")
                if g.weight > best_w and not n.startswith(("helper", "joint", "body", "Mid", "Left", "Right")):
                    best, best_w = n, g.weight
            leg = shoe or best.startswith(LEG_BONES)
            attr.data[v.index].color = (1, 1, 1, 1) if leg else (0, 0, 0, 1)
        me.color_attributes.active_color = attr


def layer_kind(o):
    """How a surface splits between the layers: 'legs', 'upper', 'trousers' (the suit, by its
    texture mask) or 'bones' (the body, by its legmask attribute)."""
    n = o.name.lower()
    if "boots" in n or "shoe" in n:
        return "legs"
    if "casualsuit" in n:
        return "trousers"
    if o.name == "civilian":
        return "bones"
    return "upper"


LAYER_NODES = []


def setup_layers():
    """Put a transparency mix in front of every material's output, driven by two values."""
    mask_img = bpy.data.images.load(os.path.abspath(TROUSERS_MASK)) if TROUSERS_MASK else None
    for o in bpy.data.objects:
        if o.type not in ("MESH", "CURVE"):
            continue
        kind = layer_kind(o)
        for slot in o.material_slots:
            if slot.material is None:
                continue
            mat = slot.material.copy()
            slot.material = mat
            mat.use_nodes = True
            nt = mat.node_tree
            out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL" and n.is_active_output)
            if not out.inputs["Surface"].is_linked:
                continue
            src = out.inputs["Surface"].links[0].from_socket
            mix = nt.nodes.new("ShaderNodeMixShader")
            clear = nt.nodes.new("ShaderNodeBsdfTransparent")
            if kind == "trousers":
                if mask_img is None:
                    raise RuntimeError("CJ_TROUSERS_MASK is needed for the layers")
                tex = nt.nodes.new("ShaderNodeTexImage")
                tex.image = mask_img
                tex.image.colorspace_settings.name = "Non-Color"
                legs = tex.outputs["Color"]
            elif kind == "bones":
                attr = nt.nodes.new("ShaderNodeAttribute")
                attr.attribute_name = "legmask"
                legs = attr.outputs["Fac"]
            else:
                val = nt.nodes.new("ShaderNodeValue")
                val.outputs[0].default_value = 1.0 if kind == "legs" else 0.0
                legs = val.outputs[0]
            sel_l, sel_u = nt.nodes.new("ShaderNodeValue"), nt.nodes.new("ShaderNodeValue")
            inv = nt.nodes.new("ShaderNodeMath")
            inv.operation = "SUBTRACT"
            inv.inputs[0].default_value = 1.0
            nt.links.new(legs, inv.inputs[1])
            a = nt.nodes.new("ShaderNodeMath")
            a.operation = "MULTIPLY"
            nt.links.new(legs, a.inputs[0])
            nt.links.new(sel_l.outputs[0], a.inputs[1])
            b = nt.nodes.new("ShaderNodeMath")
            b.operation = "MULTIPLY"
            nt.links.new(inv.outputs[0], b.inputs[0])
            nt.links.new(sel_u.outputs[0], b.inputs[1])
            alpha = nt.nodes.new("ShaderNodeMath")
            alpha.operation = "ADD"
            alpha.use_clamp = True
            nt.links.new(a.outputs[0], alpha.inputs[0])
            nt.links.new(b.outputs[0], alpha.inputs[1])
            nt.links.new(alpha.outputs[0], mix.inputs["Fac"])
            nt.links.new(clear.outputs[0], mix.inputs[1])
            nt.links.new(src, mix.inputs[2])
            nt.links.new(mix.outputs[0], out.inputs["Surface"])
            LAYER_NODES.append((sel_l, sel_u))


def show_layer(legs, upper):
    for sel_l, sel_u in LAYER_NODES:
        sel_l.outputs[0].default_value = 1.0 if legs else 0.0
        sel_u.outputs[0].default_value = 1.0 if upper else 0.0


def render_mask(path):
    """Workbench render of the legmask attribute; curves (seams, zips) hidden, guns black."""
    curves = [o for o in bpy.data.objects if o.type == "CURVE" and not o.hide_render]
    for o in curves:
        o.hide_render = True
    keep = r.engine
    r.engine = "BLENDER_WORKBENCH"
    sh = scene.display.shading
    sh.light = "FLAT"
    sh.color_type = "VERTEX"
    r.filepath = path
    bpy.ops.render.render(write_still=True)
    r.engine = keep
    for o in curves:
        o.hide_render = False


# ---- gaits ----
def gait_pose(action, f):
    """The rig posed on frame f of an action, then freed from the action (the pose stays)."""
    reset()
    if action == "stand":
        stand()
        arms_hanging()
        return
    rig.animation_data_create()
    act = bpy.data.actions[action]
    rig.animation_data.action = act
    if act.slots:
        rig.animation_data.action_slot = act.slots[0]
    scene.frame_set(int(f), subframe=f - int(f))
    basis = {pb.name: pb.matrix_basis.copy() for pb in rig.pose.bones}
    rig.animation_data.action = None
    for pb in rig.pose.bones:
        pb.matrix_basis = basis[pb.name]
    update()


def chest_rotation():
    """spine_03's posed rotation relative to its rest, armature space."""
    update()
    pb = rig.pose.bones["spine_03"]
    return pb.matrix.to_quaternion() @ rig.data.bones["spine_03"].matrix_local.to_quaternion().inverted()


def face_front():
    """Turn the chest back to the front about the spine's base: in a strafe or a backpedal the
    upper-body layer faces the aim, and the hips' turn stays in the legs layer."""
    update()
    ls, rs = bone_head("upperarm_l"), bone_head("upperarm_r")
    yaw = math.atan2((ls - rs).y, (ls - rs).x)          # 0 when the shoulder line runs along +X
    turn_bone("spine_01", "Z", -math.degrees(yaw))


# A long gun carried across the body (strafing): the grip by the right hip, the barrel along
# the shoulder line towards the left and down, in front of the left thigh; in the chest's frame
# (the rest: +X the left, -Y the front, +Z up).
ACROSS_GRIP = Vector((-0.12, -0.2, -0.06))         # from spine_03's head
ACROSS_DIR = Vector((0.7, -0.2, -0.7))           # the bore
ACROSS_UP = Vector((0.0, -0.75, 0.65))              # the gun's top, away from the body


def carry_across(key, info):
    R = chest_rotation()
    rot = gun_frame(R @ ACROSS_DIR, R @ ACROSS_UP)
    origin = bone_head("spine_03") + R @ ACROSS_GRIP
    m = place_gun(key, rot, origin)
    info["reach_r"], info["bend_r"] = hold_grip(key, m, pole=(-0.6, 0.6, -1.0))
    info["reach_l"], info["bend_l"] = support_fore(key, m)
    info["across"] = True
    return (key, m), info


def carry(key):
    """Hold a weapon lowered on the current gait pose; returns (key, matrix), info."""
    info = {}
    if key == "none":
        return None, info                       # the gait's own arms
    if key in HAND_HELD:
        if GAIT_MEAN.get("face_front"):
            face_front()
        # the pistol in the right hand where the gait swings it, muzzle down and forward; the
        # hand swings PISTOL_SWING of the gait's swing about its mean, so a running arm keeps
        # the muzzle down
        update()
        palm = palm_point("r")
        mean = GAIT_MEAN.get("palm_r")
        if mean is not None:
            # the hand swings about its mean; running, the mean is where it hangs standing
            # (beside the thigh, relative to the pelvis), not where a runner pumps it
            centre = mean
            if GAIT_MEAN.get("hang") is not None:
                if GAIT_MEAN.get("face_front"):      # the chest faces front: hang as from the standing chest
                    centre = bone_head("spine_03") + chest_rotation() @ GAIT_MEAN["hang_chest"]
                else:
                    centre = bone_head("pelvis") + GAIT_MEAN["hang"]
            palm = centre + (palm - mean) * GAIT_MEAN["swing"]
        elbow = bone_head("lowerarm_r")
        shoulder = bone_head("upperarm_r")
        rot = gun_frame(pitch_dir(GAIT_MEAN.get("pistol_pitch", 65) if key == "pistol" else CARRY_PITCH[key], 4),
                        Vector((-0.4, -0.2, 0.3)) if key == "pistol" else Vector((0.0, 0.0, 1.0)))
        origin = palm - rot @ Vector(GRIP_PALM.get(key, GRIP_PALM["default"]))
        m = place_gun(key, rot, origin)
        pole = elbow - (shoulder + palm) / 2
        info["reach_r"], info["bend_r"] = hold_grip(key, m, pole=tuple(pole))
        # the hand may turn less than wanted (the wrist's limit): the gun follows the hand
        update()
        q_now = rig.pose.bones["hand_r"].matrix.to_quaternion()
        turn = (q_now @ WANTED["r"].inverted()).to_matrix()
        rot = turn @ rot
        origin = palm_point("r") - rot @ Vector(GRIP_PALM.get(key, GRIP_PALM["default"]))
        m = place_gun(key, rot, origin)
    else:
        butt = Vector(guns[key]["butt"])
        # a rifle held in both hands keeps the torso from twisting: the spine and the
        # shoulders move RIFLE_TWIST of their motion about the cycle's mean
        for b in RIFLE_STILL:
            mean = GAIT_MEAN.get(b)
            if mean is not None:
                pb = rig.pose.bones[b]
                q = pb.matrix_basis.to_quaternion()
                pb.matrix_basis = Matrix.LocRotScale(pb.matrix_basis.to_translation(),
                                                     mean.slerp(q if q.dot(mean) >= 0 else -q, RIFLE_TWIST), None)
        # ... and the hips' turn under it does not carry the chest: spine_01 takes it out
        update()
        mean_pelvis = GAIT_MEAN.get("pelvis_arm")
        if mean_pelvis is not None:
            pb = rig.pose.bones["pelvis"]
            q = pb.matrix.to_quaternion()
            dev = q @ mean_pelvis.inverted() if q.dot(mean_pelvis) >= 0 else (-q) @ mean_pelvis.inverted()
            sp = rig.pose.bones["spine_01"]
            h = sp.head.copy()
            sp.matrix = Matrix.Translation(h) @ dev.inverted().to_matrix().to_4x4() @ Matrix.Translation(-h) @ sp.matrix
            update()
        if GAIT_MEAN.get("straight"):
            # the upper body as the legs carry it (no turn back to the front, no blade), the
            # gun turned a little across in the hands
            info["pocket"] = pocket() + STRAIGHT_BUTT
            rot = gun_frame(pitch_dir(STRAIGHT_PITCH, STRAIGHT_YAW), (0, 0, 1))
            m = place_gun(key, rot, info["pocket"] - rot @ butt)
            info["reach_r"], info["bend_r"] = hold_grip(key, m)
            info["reach_l"], info["bend_l"] = support_fore(key, m)
            return (key, m), info
        if GAIT_MEAN.get("face_front"):
            face_front()
        if GAIT_MEAN.get("across"):
            return carry_across(key, info)
        blade(32.0, 8.0)
        info["pocket"] = pocket()
        rot = gun_frame(pitch_dir(min(60.0, LOW_PITCH[key] + GAIT_MEAN.get("low_extra", 0.0)), 8), (0, 0, 1))
        origin = info["pocket"] - rot @ butt
        m = place_gun(key, rot, origin)
        info["reach_r"], info["bend_r"] = hold_grip(key, m)
        info["reach_l"], info["bend_l"] = support_fore(key, m)
    return (key, m), info


GAIT_WEAPONS = os.environ.get("CJ_WEAPONS", "pistol,rifle").split(",")    # the first also gives the full render
GAIT_MEAN = {}          # per job: the right palm's and the spine bones' mean over the frames
FACE_FRONT = {"strafel", "strafer", "back", "jogl", "jogr", "runback"}
ACROSS = {"strafel", "strafer", "jogl", "jogr"}
STRAIGHT = {"back"}                                 # long guns: the upper body as the legs carry it
STRAIGHT_YAW = 55.0
STRAIGHT_PITCH = 40.0                               # degrees down
STRAIGHT_BUTT = Vector((0.03, 0.0, 0.00))          # the butt lower, against the right of the chest                                 # the gun turned this far to the left, degrees           # gaits whose long gun is carried across the body   # gaits whose hips turn away
PISTOL_SWING = {"walk": 1.0, "run": 0.2, "strafel": 0.3, "strafer": 0.3, "back": 0.3, "jogl": 0.2,
                "jogr": 0.2, "runback": 0.2}      # of the gait's hand swing
PISTOL_PITCH = {"run": 72.0}                  # degrees down; 65 otherwise
PISTOL_HANG = {"run", "strafel", "strafer", "back", "jogl", "jogr", "runback"}                         # the hand swings about where it hangs standing
RIFLE_PITCH = {"run": 52.0, "back": 48.0}                   # degrees down; 45 otherwise
RIFLE_TWIST = 0.0
CARRY_PITCH = {"knife": 25.0, "flashlight": 15.0}
LOW_PITCH = {"rifle": 45.0, "shotgun": 55.0}          # the long guns' low ready; the 1.05 m shotgun steeper     # the blade and the beam a little down
RIFLE_STILL = ("spine_01", "spine_02", "spine_03", "clavicle_l", "clavicle_r")


def gait_job(spec):
    _, name, action, frames = spec.split(":")
    frames = [float(x) for x in frames.split(",")]
    centre = Vector((0.0, 0.0))
    GAIT_MEAN.clear()
    palms, spine, pelvis_q = Vector(), {b: [] for b in RIFLE_STILL}, []
    for f in frames:
        gait_pose(action, f)
        centre += (rig.matrix_world @ rig.pose.bones["pelvis"].head).xy
        palms += palm_point("r")
        for b in spine:
            spine[b].append(rig.pose.bones[b].matrix_basis.to_quaternion())
        pelvis_q.append(rig.pose.bones["pelvis"].matrix.to_quaternion())
    centre /= len(frames)
    GAIT_MEAN["palm_r"] = palms / len(frames)
    spine["pelvis_arm"] = pelvis_q
    for b, qs in spine.items():
        m = qs[0].copy()
        acc = Quaternion((0, 0, 0, 0))
        for q in qs:
            acc = Quaternion([a + (c if q.dot(m) >= 0 else -c) for a, c in zip(acc, q)])
        GAIT_MEAN[b] = acc.normalized()
    GAIT_MEAN["swing"] = PISTOL_SWING.get(name, 1.0)
    GAIT_MEAN["face_front"] = name in FACE_FRONT
    GAIT_MEAN["across"] = name in ACROSS
    GAIT_MEAN["straight"] = name in STRAIGHT
    GAIT_MEAN["hang"] = None
    if name in PISTOL_HANG:
        gait_pose("stand", 0)
        carry("pistol")
        GAIT_MEAN["hang"] = palm_point("r") - bone_head("pelvis")
        GAIT_MEAN["hang_chest"] = chest_rotation().inverted() @ (palm_point("r") - bone_head("spine_03"))
    GAIT_MEAN["pistol_pitch"] = PISTOL_PITCH.get(name, 65.0)
    GAIT_MEAN["low_extra"] = RIFLE_PITCH.get(name, 45.0) - 45.0    # a gait's steeper low ready
    cam.location.x, cam.location.y = centre.x, centre.y
    for i, f in enumerate(frames):
        for key in GAIT_WEAPONS:
            gait_pose(action, f)
            for g in guns.values():
                g.hide_render = True
            held, info = carry(key)
            results["%s_%s_%02d" % (key, name, i)] = measure(held, info)
            if os.environ.get("CJ_MEASURE_ONLY"):
                print("MEASURED", name, i, key, results["%s_%s_%02d" % (key, name, i)], flush=True)
                continue
            if key == GAIT_WEAPONS[0]:
                show_layer(True, True)
                render(os.path.join(out_dir, "full_%s_%02d.png" % (name, i)))
                render_mask(os.path.join(out_dir, "full_%s_%02d_legs.png" % (name, i)))
                show_layer(True, False)
                render(os.path.join(out_dir, "legs_%s_%02d.png" % (name, i)))
            show_layer(False, True)
            render(os.path.join(out_dir, "%s_%s_%02d.png" % (key, name, i)))
            print("RENDERED", name, i, key, results["%s_%s_%02d" % (key, name, i)], flush=True)
    show_layer(True, True)


def views(fname, p):
    """Side, front and back views of the current pose about the point p, for checking it."""
    keep = cam.location.copy(), cam.rotation_euler.copy()
    for suffix, sx in (("_side", 1.0), ("_left", -1.0)):
        cam.location = (p.x + 3.0 * sx, p.y, 1.1)
        cam.rotation_euler = (math.pi / 2, 0, math.pi / 2 * sx)
        render(os.path.join(out_dir, fname + suffix + ".png"))
    cam.location = (p.x, p.y - 3.0, 1.1)               # from behind (the character faces +Y)
    cam.rotation_euler = (math.pi / 2, 0, 0)
    render(os.path.join(out_dir, fname + "_back.png"))
    cam.location = (p.x, p.y + 3.0, 1.1)               # from the front
    cam.rotation_euler = (math.pi / 2, 0, math.pi)
    render(os.path.join(out_dir, fname + "_front.png"))
    cam.location, cam.rotation_euler = keep


def body_job(spec):
    """The whole player on frames of a clip, no weapon, each frame centred on its pelvis."""
    _, name, action, frames = spec.split(":")
    for g in guns.values():
        g.hide_render = True
    for i, f in enumerate(float(x) for x in frames.split(",")):
        gait_pose(action, f)
        p = rig.matrix_world @ rig.pose.bones["pelvis"].head
        cam.location.x, cam.location.y = p.x, p.y
        fname = "body_%s_%02d" % (name, i)
        results[fname] = {"frame": f, "pelvis": [p.x, p.y, p.z]}
        render(os.path.join(out_dir, fname + ".png"))
        if os.environ.get("CJ_SIDE"):
            views(fname, p)
        print("RENDERED", fname, flush=True)


results = {}
mpath = os.path.join(out_dir, "measure.json")
if os.path.exists(mpath):
    results = json.load(open(mpath))
UPPER_ONLY = bool(os.environ.get("CJ_UPPER"))      # poses: the upper-body layer only, for the atlases
if any(j.startswith("gait:") for j in poses) or UPPER_ONLY:
    build_leg_attribute()
    setup_layers()
    show_layer(True, True)
STAND_CENTRE = None
if UPPER_ONLY:                  # the poses' pivot: the standing pelvis, as the stand legs' frame
    gait_pose("stand", 0)
    STAND_CENTRE = (rig.matrix_world @ rig.pose.bones["pelvis"].head).xy
for name in poses:
    if name.startswith("gait:") or name.startswith("body:"):
        (gait_job if name.startswith("gait:") else body_job)(name)
        json.dump(results, open(mpath, "w"), indent=1)
        continue
    held, info = pose(name)
    # the pelvis at the frame centre, as the civilians' standing frames; for the atlases, the
    # standing pelvis, so that the upper body sits on the stand legs
    p = rig.matrix_world @ rig.pose.bones["pelvis"].head
    cam.location.x, cam.location.y = (STAND_CENTRE.x, STAND_CENTRE.y) if UPPER_ONLY else (p.x, p.y)
    if UPPER_ONLY:
        show_layer(False, True)
    results[name] = measure(held, info)
    print("MEASURE", name, results[name])
    fname = name.replace(":", "_").replace(".", "_")        # clip:<weapon>:<action>:<frame>
    if os.environ.get("CJ_NO_RENDER"):                       # measure only
        continue
    render(os.path.join(out_dir, fname + ".png"))
    if os.environ.get("CJ_SIDE"):                      # side views for checking the hold
        views(fname, p)
json.dump(results, open(mpath, "w"), indent=1)
