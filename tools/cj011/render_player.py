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

Gait jobs, gait:<name>:<action>:<frames> (action "stand" for the standing frame): every frame
of the action, posed with each weapon lowered, is rendered as
  full_<name>_<nn>.png    the whole player with the pistol lowered, and its _legs mask;
  legs_<name>_<nn>.png    the legs layer: trousers, boots and the body below the waist;
  <weapon>_<name>_<nn>.png the upper-body layer per weapon (pistol, rifle): the jacket, the
                          head, the cap, the pack and the weapon held lowered.
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

FRAME_M = 2.1
FRAME_PX = round(384 * FRAME_M / 1.4)
LEG_BONES = ("thigh_", "calf_", "foot_", "ball_")

args = sys.argv[sys.argv.index("--") + 1:]
out_dir, poses = os.path.abspath(args[0]), args[1:]
os.makedirs(out_dir, exist_ok=True)
scene = bpy.context.scene
rig = bpy.data.objects["civilian_rig"]
guns = {k: bpy.data.objects["gun_" + k] for k in ("pistol", "rifle")}
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
    for g in guns.values():
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
    elbow = s + u * a + p.normalized() * hgt
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
    fwd = rot @ Vector((1, 0, 0))
    d = (fwd - fwd.dot(down) * down).normalized()
    left = rot @ Vector((0, 1, 0))
    d = (d * 0.75 + left * 0.35 - down * 0.15).normalized()            # knuckles on the gun's left
    q = hand_target(side, d, -down)
    WANTED[side] = q
    palm = m @ Vector((0, -0.012, 0))                                 # on the grip's right panel
    curl(side, 62.0, thumb)
    return solve_arm(side, palm, q, pole, 30.0)


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


def fore_palm(key, m):
    """The support palm: under the handguard, a little to its left."""
    rot = m.to_3x3()
    return gp(key, m, "fore") + rot @ (Vector((0, 0.25, -1.0)).normalized() * FORE_RADIUS)


def support_fore(key, m):
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
    return solve_arm("l", fore_palm(key, m), q, (0.8, 0.2, -1.0), 35.0)


def pose(name):
    reset()
    stand()
    if name == "none":
        arms_hanging()
        return None, {}
    key = name.split("_")[0]
    kind = name.split("_")[1]
    info = {}
    if key == "pistol":
        if kind == "low":
            rot = gun_frame(pitch_dir(65, 4), Vector((-0.4, -0.2, 0.3)))     # top towards the body side and up
            origin = Vector((SH_R.x - 0.03, -0.05, SH_Z - 0.53))
            m = place_gun(key, rot, origin)
            info["reach_r"], info["bend_r"] = hold_grip(key, m, pole=(-0.3, 0.6, -1.0))
            arms_hanging(("l",))
        else:
            # isosceles: the sights at the eye, the gun on the eye's line, the arms nearly
            # straight; firing, the muzzle flips up and the elbows give a little
            isosceles(7.0)
            recoil = 12.0 if kind == "fire" else 0.0
            target = ISO_REACH if kind == "aim" else ISO_REACH - 0.05
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
    else:
        butt = Vector(guns[key]["butt"])
        blade(32.0, 8.0)
        info["pocket"] = pocket()
        if kind == "low":
            rot = gun_frame(pitch_dir(45, 8), (0, 0, 1))
            at = info["pocket"]
        else:
            recoil = 6.0 if kind == "fire" else 0.0
            rot = gun_frame(pitch_dir(-recoil), (0, 0, 1))
            at = info["pocket"] + Vector((0, 0.025 if kind == "fire" else 0.0, 0))
        origin = at - rot @ butt
        m = place_gun(key, rot, origin)
        info["reach_r"], info["bend_r"] = hold_grip(key, m)
        info["reach_l"], info["bend_l"] = support_fore(key, m)
    return (key, m), info


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
    info["grip_err_m"] = (palm_point("r") - (m @ Vector((0, -0.012, 0)))).length
    if g["butt"]:
        info["butt_shoulder_m"] = (gp(key, m, "butt") - info.pop("pocket")).length
        info["fore_err_m"] = (palm_point("l") - fore_palm(key, m)).length
    return {k: round(v, 3) for k, v in info.items()}


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


def face_front():
    """Turn the chest back to the front about the spine's base: in a strafe or a backpedal the
    upper-body layer faces the aim, and the hips' turn stays in the legs layer."""
    update()
    ls, rs = bone_head("upperarm_l"), bone_head("upperarm_r")
    yaw = math.atan2((ls - rs).y, (ls - rs).x)          # 0 when the shoulder line runs along +X
    turn_bone("spine_01", "Z", -math.degrees(yaw))


def carry(key):
    """Hold a weapon lowered on the current gait pose; returns (key, matrix), info."""
    info = {}
    if key == "pistol":
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
                centre = bone_head("pelvis") + GAIT_MEAN["hang"]
            palm = centre + (palm - mean) * GAIT_MEAN["swing"]
        elbow = bone_head("lowerarm_r")
        shoulder = bone_head("upperarm_r")
        rot = gun_frame(pitch_dir(GAIT_MEAN.get("pistol_pitch", 65), 4), Vector((-0.4, -0.2, 0.3)))
        origin = palm - rot @ Vector((0, -0.012, 0))
        m = place_gun(key, rot, origin)
        pole = elbow - (shoulder + palm) / 2
        info["reach_r"], info["bend_r"] = hold_grip(key, m, pole=tuple(pole))
        # the hand may turn less than wanted (the wrist's limit): the gun follows the hand
        update()
        q_now = rig.pose.bones["hand_r"].matrix.to_quaternion()
        turn = (q_now @ WANTED["r"].inverted()).to_matrix()
        rot = turn @ rot
        origin = palm_point("r") - rot @ Vector((0, -0.012, 0))
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
        if GAIT_MEAN.get("face_front"):
            face_front()
        blade(32.0, 8.0)
        info["pocket"] = pocket()
        rot = gun_frame(pitch_dir(GAIT_MEAN.get("rifle_pitch", 45), 8), (0, 0, 1))
        origin = info["pocket"] - rot @ butt
        m = place_gun(key, rot, origin)
        info["reach_r"], info["bend_r"] = hold_grip(key, m)
        info["reach_l"], info["bend_l"] = support_fore(key, m)
    return (key, m), info


GAIT_MEAN = {}          # per job: the right palm's and the spine bones' mean over the frames
FACE_FRONT = {"strafel", "strafer", "back", "fstrafel", "fstrafer", "runback"}   # gaits whose hips turn away
PISTOL_SWING = {"walk": 1.0, "run": 0.2, "strafel": 0.3, "strafer": 0.3, "back": 0.3, "fstrafel": 0.2,
                "fstrafer": 0.2, "runback": 0.2}      # of the gait's hand swing
PISTOL_PITCH = {"run": 72.0}                  # degrees down; 65 otherwise
PISTOL_HANG = {"run", "strafel", "strafer", "back", "fstrafel", "fstrafer", "runback"}                         # the hand swings about where it hangs standing
RIFLE_PITCH = {"run": 52.0, "back": 48.0}                   # degrees down; 45 otherwise
RIFLE_TWIST = 0.0
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
    GAIT_MEAN["hang"] = None
    if name in PISTOL_HANG:
        gait_pose("stand", 0)
        carry("pistol")
        GAIT_MEAN["hang"] = palm_point("r") - bone_head("pelvis")
    GAIT_MEAN["pistol_pitch"] = PISTOL_PITCH.get(name, 65.0)
    GAIT_MEAN["rifle_pitch"] = RIFLE_PITCH.get(name, 45.0)
    cam.location.x, cam.location.y = centre.x, centre.y
    for i, f in enumerate(frames):
        for key in ("pistol", "rifle"):
            gait_pose(action, f)
            held, info = carry(key)
            other = guns["rifle" if key == "pistol" else "pistol"]
            other.hide_render = True
            results["%s_%s_%02d" % (key, name, i)] = measure(held, info)
            if os.environ.get("CJ_MEASURE_ONLY"):
                print("MEASURED", name, i, key, results["%s_%s_%02d" % (key, name, i)], flush=True)
                continue
            if key == "pistol":
                show_layer(True, True)
                render(os.path.join(out_dir, "full_%s_%02d.png" % (name, i)))
                render_mask(os.path.join(out_dir, "full_%s_%02d_legs.png" % (name, i)))
                show_layer(True, False)
                render(os.path.join(out_dir, "legs_%s_%02d.png" % (name, i)))
            show_layer(False, True)
            render(os.path.join(out_dir, "%s_%s_%02d.png" % (key, name, i)))
            print("RENDERED", name, i, key, results["%s_%s_%02d" % (key, name, i)], flush=True)
    show_layer(True, True)


results = {}
mpath = os.path.join(out_dir, "measure.json")
if os.path.exists(mpath):
    results = json.load(open(mpath))
if any(j.startswith("gait:") for j in poses):
    build_leg_attribute()
    setup_layers()
    show_layer(True, True)
for name in poses:
    if name.startswith("gait:"):
        gait_job(name)
        json.dump(results, open(mpath, "w"), indent=1)
        continue
    held, info = pose(name)
    # the pelvis at the frame centre, as the civilians' standing frames
    p = rig.matrix_world @ rig.pose.bones["pelvis"].head
    cam.location.x, cam.location.y = p.x, p.y
    results[name] = measure(held, info)
    print("MEASURE", name, results[name])
    render(os.path.join(out_dir, name + ".png"))
    side = os.environ.get("CJ_SIDE")
    if side:                                           # side views for checking the hold
        keep = cam.location.copy(), cam.rotation_euler.copy()
        for suffix, sx in (("_side", 1.0), ("_left", -1.0)):
            cam.location = (p.x + 3.0 * sx, p.y, 1.1)
            cam.rotation_euler = (math.pi / 2, 0, math.pi / 2 * sx)
            render(os.path.join(out_dir, name + suffix + ".png"))
        cam.location = (p.x, p.y - 3.0, 1.1)               # from behind (the character faces +Y)
        cam.rotation_euler = (math.pi / 2, 0, 0)
        render(os.path.join(out_dir, name + "_back.png"))
        cam.location, cam.rotation_euler = keep
json.dump(results, open(mpath, "w"), indent=1)
