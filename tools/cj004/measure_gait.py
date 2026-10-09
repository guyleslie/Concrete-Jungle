"""CJ-029: measure a civilian's gaits as the game plays them.

    blender -b <civilian-anim.blend> --python tools/cj004/measure_gait.py -- <out.json> <action>:<frames>:<cycle> ...

<frames> are the scene frames that become atlas frames (comma-separated, fractions allowed);
<cycle> is the distance in metres over which the game plays those frames once, or "auto" to
use the stride measured here. For each action, the JSON holds:
  - stride_m: how far the weight-bearing foot travels backwards per cycle (in an in-place loop,
    the distance the body covers), in steps of a quarter frame; only meaningful when the whole
    action is keyed;
  - thigh_forward_max_deg: the furthest the thighs swing forward from straight down;
  - slip_m: over the atlas frames, with the sprite advanced by cycle / frames per frame as the
    game does, the largest movement of the contact point the weight is on while it stays on it
    (0 = the foot stays put); contact_slip_m: the same for every planted heel and ball, which
    includes the back toe pushing off and the heel rolling over the ball;
  - cadence_steps_per_m: steps per metre walked (2 per cycle), to turn into steps per second;
  - locked_frames: as many frames as <frames>, from its first, that split the stride evenly:
    played by distance, they keep the weight-bearing foot in place (no_support_share: the
    share of the cycle without a planted foot, a flight phase; 0 for a walk);
  - toe_ahead_max_m, foot_behind_max_m: the furthest a toe reaches ahead of the pelvis and a
    foot (toe or heel) behind it in the atlas frames; spread_max_m: the largest distance from
    the front toe to the back of the other foot in one frame (the "splits");
  - upper body over every whole frame, as seen from above: hand_swing_m, the largest fore-aft
    travel of a hand relative to the pelvis; hand_out_m, how far a hand gets further out to
    the side than its shoulder joint; lean_deg, the mean forward lean of the line from the
    pelvis to the head; twist_deg, the range of the shoulder line's turn.
A planted foot rolls from the heel to the ball, so each of the two is a contact point of its
own: on the ground while it moves backwards relative to the body (in an in-place loop the
ground slides back under a planted foot) within 3 cm of the lowest it goes. Height alone is not
enough: scaling and turning the legs (retarget * and ~) leave the hips bobbing as much as in
the library, so a planted foot rises and sinks a few centimetres, which does not show from
above. The weight is on the lowest down point of the foot furthest ahead. The rig faces -Y.
"""
import bpy, json, math, sys

CONTACT = 0.03       # a contact point on the ground is within 3 cm of its lowest
POINTS = [(s, p) for s in ("l", "r") for p in ("heel", "ball")]

args = sys.argv[sys.argv.index("--") + 1:]
out, jobs = args[0], args[1:]
scene = bpy.context.scene
rig = bpy.data.objects["civilian_rig"]
rig.rotation_euler = (0.0, 0.0, 0.0)


def set_action(name):
    a = bpy.data.actions[name]
    rig.animation_data_create()
    rig.animation_data.action = a
    if a.slots:
        rig.animation_data.action_slot = a.slots[0]
    return a


def at(f):
    """Contact points {(side, 'heel'|'ball'): world position} and thigh swings at frame f."""
    scene.frame_set(int(math.floor(f)), subframe=f - math.floor(f))
    w = rig.matrix_world
    pb = rig.pose.bones
    feet = {}
    thighs = {}
    for s in ("l", "r"):
        feet[(s, "heel")] = w @ pb["foot_" + s].head
        feet[(s, "ball")] = w @ pb["ball_" + s].head
        d = (w @ pb["calf_" + s].head) - (w @ pb["thigh_" + s].head)
        thighs[s] = math.degrees(math.atan2(-d.y, -d.z))
    return feet, thighs


def down(before, now, after, pt, bottom):
    """The contact point is on the ground: moving backwards (+Y), near its lowest."""
    return after[pt].y > before[pt].y and now[pt].z < bottom[pt] + CONTACT


def bearing(before, now, after, bottom):
    """The contact point the weight is on: the lowest down point of the foot furthest ahead (in
    double support the weight moves to the front foot while the back toe pushes off)."""
    on = [pt for pt in POINTS if down(before, now, after, pt, bottom)]
    if not on:
        return None
    lead = min(("l", "r"), key=lambda side: min((now[pt].y for pt in on if pt[0] == side), default=math.inf))
    return min((pt for pt in on if pt[0] == lead), key=lambda pt: now[pt].z - bottom[pt])


def drift(track):
    """The largest movement, along or across, within a stretch of consecutive frames on the same
    contact point; track holds (point or None, y on the ground, x) per frame."""
    worst, run = 0.0, []
    for pt, y, x in track + [(None, 0.0, 0.0)]:
        if run and pt != run[0][0]:
            if len(run) >= 2:
                worst = max(worst, max(r[1] for r in run) - min(r[1] for r in run),
                            max(r[2] for r in run) - min(r[2] for r in run))
            run = []
        if pt is not None:
            run.append((pt, y, x))
    return worst


result = {}
for job in jobs:
    name, frame_list, cycle = job.split(":")
    a = set_action(name)
    f0, f1 = (int(x) for x in a.frame_range)
    # ---- stride and thigh swing over the whole action ----
    samples = []
    t = float(f0)
    while t < f1:
        samples.append((t,) + at(t))
        t += 0.25
    n_s = len(samples)
    bottom = {pt: min(sm[1][pt].z for sm in samples) for pt in POINTS}
    # The backward travel of the point the weight is on, over the cycle, is the stride; atlas
    # frames that split it evenly (locked_frames) keep that point in place in the game.
    step, gaps = [], 0
    for i in range(n_s):
        before, now, after = samples[i - 1][1], samples[i][1], samples[(i + 1) % n_s][1]
        pt = bearing(before, now, after, bottom)
        if pt is None:
            step.append(None)
            gaps += 1
        else:
            step.append(max(0.0, after[pt].y - now[pt].y))
    known = sorted(d for d in step if d is not None)
    fill = known[len(known) // 2] if known else 0.0          # a flight phase: the typical step
    step = [fill if d is None else d for d in step]
    ground = [0.0]
    for d in step:
        ground.append(ground[-1] + d)                        # at f0 + 0.25 i, i = 0 .. n_s
    stride = ground[-1]
    thigh_max = max(max(sm[2].values()) for sm in samples)
    # ---- upper body over every whole frame ----
    hands, out_side, leans, twists = {"l": [], "r": []}, 0.0, [], []
    for f in range(f0, f1):
        scene.frame_set(f)
        w = rig.matrix_world
        pb = rig.pose.bones
        pel = w @ pb["pelvis"].head
        head = w @ pb["head"].head
        leans.append(math.degrees(math.atan2(pel.y - head.y, head.z - pel.z)))       # forward is -Y
        sh = {side: w @ pb["upperarm_" + side].head for side in ("l", "r")}
        twists.append(math.degrees(math.atan2(sh["l"].y - sh["r"].y, sh["l"].x - sh["r"].x)))
        for side in ("l", "r"):
            hand = w @ pb["hand_" + side].head
            hands[side].append(hand.y - pel.y)
            out_side = max(out_side, abs(hand.x - pel.x) - abs(sh[side].x - pel.x))
    hand_swing = max(max(v) - min(v) for v in hands.values())
    lean = sum(leans) / len(leans)
    twist = max(twists) - min(twists)
    # ---- reach and spread over the atlas frames ----
    frames = [float(x) for x in frame_list.split(",")]
    cycle_m = stride if cycle == "auto" else float(cycle)
    n = len(frames)

    def ground_at(f):
        """Ground travelled at frame f (f0 <= f <= f1)."""
        x = (f - f0) / 0.25
        i = min(int(x), n_s - 1)
        return ground[i] + (ground[i + 1] - ground[i]) * (x - i)

    def frame_at(g):
        """The frame at which the ground travelled reaches g (0 <= g < stride)."""
        i = next(k for k in range(n_s) if ground[k + 1] >= g)
        d = ground[i + 1] - ground[i]
        return f0 + 0.25 * (i + ((g - ground[i]) / d if d > 0 else 0.0))

    g0 = ground_at(frames[0])
    locked = [round(frame_at((g0 + k * stride / n) % stride), 3) for k in range(n)] if stride > 0 else frames
    toe_ahead = behind = spread = 0.0
    for f in frames:
        at(f)
        w = rig.matrix_world
        pel = w @ rig.pose.bones["pelvis"].head
        ys = []
        for s in ("l", "r"):
            toe = w @ rig.pose.bones["ball_" + s].tail
            heel = w @ rig.pose.bones["foot_" + s].head
            toe_ahead = max(toe_ahead, pel.y - toe.y)
            behind = max(behind, toe.y - pel.y, heel.y - pel.y)
            ys.append((toe.y, max(toe.y, heel.y)))
        spread = max(spread, max(ys[0][1], ys[1][1]) - min(ys[0][0], ys[1][0]))
    # ---- slip over the atlas frames, advanced like the game: the sprite moves forward (-Y) by
    # cycle / n per frame; over two cycles, so that a stance across the loop's end shows whole ----
    posed = [(at(f - 0.25)[0], at(f)[0], at(f + 0.25)[0]) for f in frames]

    def on_ground(k, pt):
        if pt is None:
            return None, 0.0, 0.0
        now = posed[k % n][1][pt]
        return pt, now.y - k * cycle_m / n, now.x
    weight = [bearing(*posed[k], bottom) for k in range(n)]
    slip = drift([on_ground(k, weight[k % n]) for k in range(2 * n)])
    contact_slip = max(drift([on_ground(k, pt if down(*posed[k % n], pt, bottom) else None) for k in range(2 * n)])
                       for pt in POINTS)
    result[name] = {"stride_m": stride, "thigh_forward_max_deg": thigh_max, "slip_m": slip, "contact_slip_m": contact_slip,
                    "cycle_m": cycle_m, "frames": n, "cadence_steps_per_m": 2.0 / cycle_m if cycle_m else 0,
                    "toe_ahead_max_m": toe_ahead, "foot_behind_max_m": behind,
                    "spread_max_m": spread, "locked_frames": locked, "no_support_share": gaps / n_s,
                    "hand_swing_m": hand_swing, "hand_out_m": out_side, "lean_deg": lean, "twist_deg": twist}
    print("GAIT %-17s stride %.3f m, cycle %.3f m, thigh forward %.1f deg, slip %.3f m (any contact %.3f m), toe ahead %.2f m, "
          "behind %.2f m, spread %.2f m" % (name, stride, cycle_m, thigh_max, slip, contact_slip, toe_ahead, behind, spread))
    print("UPPER %-16s hand swing %.2f m, hand out %+.2f m, lean %.1f deg, shoulder twist %.1f deg" %
          (name, hand_swing, out_side, lean, twist))

json.dump(result, open(out, "w"), indent=1)
