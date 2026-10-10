"""CJ-011: how far an in-place loop travels per cycle, in any direction (strafes, backpedals).

    blender -b <player-anim.blend> --python tools/cj011/measure_travel.py -- <out.json> <action> [<action> ...]

In an in-place loop the ground slides under the planted foot at the body's speed. Every
quarter frame, the lowest contact point (either foot's heel or ball, the point that bears the
weight) is followed relative to the pelvis; its slide summed over the cycle is the travel per
cycle, and its direction the direction of motion (the rig faces -Y; the reported angle is
measured from the facing, positive to the character's right). The cycle is the action's
frame range minus one frame (the last frame repeats the first).
"""
import bpy, json, math, sys
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
out, actions = args[0], args[1:]
scene = bpy.context.scene
rig = bpy.data.objects["civilian_rig"]
rig.rotation_euler = (0, 0, 0)
res = {}
for name in actions:
    act = bpy.data.actions[name]
    rig.animation_data_create()
    rig.animation_data.action = act
    if act.slots:
        rig.animation_data.action_slot = act.slots[0]
    f0, f1 = act.frame_range
    n = int(round((f1 - f0) * 4))

    def sample(t):
        f = math.floor(t)
        scene.frame_set(int(f), subframe=t - f)
        pel = rig.matrix_world @ rig.pose.bones["pelvis"].head
        pts = []
        for s in ("l", "r"):
            foot, ball = rig.pose.bones["foot_" + s], rig.pose.bones["ball_" + s]
            pts.append(rig.matrix_world @ foot.head)                      # the heel (the ankle above it)
            pts.append(rig.matrix_world @ ball.head)                      # the ball of the foot
        return pel, pts

    total = Vector((0.0, 0.0))
    prev = sample(f0)
    for i in range(1, n + 1):
        t = f0 + i * (f1 - f0) / n
        cur = sample(t)
        low = min(range(4), key=lambda k: prev[1][k].z)
        rel0 = (prev[1][low] - prev[0]).xy
        rel1 = (cur[1][low] - cur[0]).xy
        total += rel1 - rel0
        prev = cur
    travel = -total                       # the body moves opposite to the planted foot's slide
    angle = math.degrees(math.atan2(-travel.x, -travel.y))   # 0 = forward (-Y), + = right (-X)
    res[name] = {"cycle_frames": f1 - f0, "travel_m": travel.length, "direction_deg": angle,
                 "speed_at_30fps": travel.length / ((f1 - f0) / 30.0)}
    print("TRAVEL %-20s cycle %.0f frames, %.2f m towards %+.0f deg, %.2f m/s at 30 fps" % (
        name, f1 - f0, travel.length, angle, res[name]["speed_at_30fps"]))
json.dump(res, open(out, "w"), indent=1)
