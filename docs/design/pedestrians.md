# Pedestrians

How the people in the streets behave: walking their block, crossing at the lights, noticing vehicles and getting out of their way, fleeing, fighting back, and what happens when they are hit. The player on foot is covered in [Gameplay](gameplay.md).

Source: `src/pedestrian.h`, `src/pedestrian.cpp`; drivers on foot in `src/traffic_incidents.*`; population, injuries and diagnostics in `src/game.cpp`. The steering model is recorded in [ADR-0006](../adr/0006-pedestrian-steering.md).

## Contents

- [States](#states)
- [Walking the city](#walking-the-city)
- [Crossing the street](#crossing-the-street)
- [Perception and dodging](#perception-and-dodging)
- [Fleeing and panic](#fleeing-and-panic)
- [Fighting back](#fighting-back)
- [Drivers on foot](#drivers-on-foot)
- [Locomotion and avoidance](#locomotion-and-avoidance)
- [Injury and death](#injury-and-death)
- [Population](#population)
- [Appearance](#appearance)
- [Tuning reference](#tuning-reference)

## States

| State | Behaviour |
|---|---|
| `Walk` | Follows its personal line along its block's sidewalk ring, from corner to corner |
| `Wait` | Stands at the kerb (queueing behind others) until it is safe to cross |
| `Cross` | Crosses on the zebra crossing; hurries when the lights change |
| `Idle` | Stands still for 1–4 s, or stands startled after a near miss, facing the vehicle |
| `Wander` | Walks to a random spot inside a park or plaza |
| `Flee` | Runs from a threat along the sidewalk |
| `Rejoin` | Pauses, then returns to the nearest point of its sidewalk ring |
| `Dodge` | Jumps (or, for a slow vehicle, steps) sideways out of a vehicle's path |
| `Fight` | Hits the player back after being punched; a driver in an incident may fight the other driver instead |
| `Confront` | A driver out of their car walks to the other party and argues |
| `ToCar` | A driver walks back to the door of their own car |
| `Down` | Knocked down; gets up after 2.5–4 s |
| `Dead` | Lies where it came to rest; fades out after 24–27 s |

A person interrupted by a dodge returns to what it was doing (`Walk`, `Wait`, `Cross`, `Wander`, `Flee`) with its targets unchanged.

## Walking the city

- Each person walks on a personal line up to 0.56 m (9 px) either side of the sidewalk centre line, so people do not all walk single file. The steering aims at a point 2.5 m ahead on that line, which brings anyone pushed aside back onto it.
- At each corner a person picks the next leg: a short idle (6 %), in parks and plazas sometimes a wander (14 %), a street crossing to the neighbouring block (up to 39 %), otherwise the next corner of its own block.
- Walking speed is 1.2–1.7 m/s per person; crossing is 1.25 times faster.

## Crossing the street

1. The person steps up to the kerb. Waiting spots are spread along the kerb within the width of the zebra crossing and up to 0.5 m back; when a spot is taken the person queues behind it (up to three rows).
2. It crosses on green only if it can leave the road before the crossing traffic gets green. The crossing traffic gets green 2 s after its own phase ends (1.5 s yellow and 0.5 s all-red, see [City › Traffic signals](city.md#traffic-signals)), so it needs *green left + 2 s ≥ time to clear the road + 0.5 s*.
3. On green it also checks that no vehicle will reach it within the next 2.5 s (a car running the red light, a turning car). After waiting longer than its personal patience (18–45 s) it crosses on red, but only when no vehicle will come through while it is on the road (a jaywalker).
4. If the lights change while it is on the road, it hurries at 1.9 times its walking speed.

Traffic stops for people in the road (see [Traffic › Obstacles and impatience](traffic.md#obstacles-and-impatience)).

## Perception and dodging

Every 0.1 s (with a random offset per person) each person predicts the motion of the vehicles near it over the next 1.6 s:

| Vehicle | Prediction |
|---|---|
| Traffic on its lane | Follows its planned path at its current speed and stops where its driver plans to stop — at a red light, behind a queue, or in front of a person (`DriverAI::stopDist`, read through `AIPathPose`) |
| Player, police, knocked cars | Keeps its velocity and turn rate, turning at most a quarter turn |

The person is in danger when its predicted position falls inside the vehicle's body grown by 0.35 m of personal space (0.1 m for a vehicle that is about to stop), and not beside its rear half. Normal traffic passing a sidewalk therefore never alarms anyone.

A person in danger reacts after its personal reaction time of 0.18–0.45 s (1.5 times longer when the vehicle comes from behind). It then moves sideways out of the danger zone, to the nearer edge unless a wall, furniture or the city edge is in the way there: at 5.5 m/s for a vehicle faster than 20 km/h, otherwise at a calm 1.8 m/s. It keeps dodging until it is out of the vehicle's path (at most 2 s), and re-plans if another vehicle becomes the more urgent danger. After a close call (the vehicle was less than 0.9 s away) it panics and flees with a 50 % chance, sometimes screaming; otherwise it stands startled for 0.5–1.4 s and carries on.

## Fleeing and panic

| Trigger | Reaction |
|---|---|
| Gunfire (34 m), explosions (56 m), someone hurt nearby (16 m) | Flees for 4–7 s after its reaction time |
| A person fleeing past within 4 m | Flees for 2–3.5 s with a chance of 1.2 per second (panic spreads one step: second-hand fleers do not spread it further) |
| Knocked down and back on its feet | Flees for about 4 s from where the blow came from |
| A close call with a vehicle | See [Perception and dodging](#perception-and-dodging) |

A fleeing person runs at about 5.2 m/s. Every 0.3–0.5 s it chooses among 16 directions: away from the threat, keeping its current direction, but not into the road (unless it is already on the road, when it heads for the kerb), not into walls or furniture within 4 m, not out of the city, and not into the path of a vehicle moving faster than 3 m/s within 35 m.

## Fighting back

About 15 % of people are tough (courage above 0.85). Punched by the player on foot, a tough person does not run but fights: it walks up to the player and punches every 0.8–1.2 s for 5–9 damage. It gives up after 10–16 s, when the player gets more than 12 m away, and flees when it drops below 45 health or the player gets into a vehicle. Knocked down, it comes back for more once it is on its feet again, if the player is within 6 m. Fighters are not scared off by the commotion around them.

## Drivers on foot

A traffic driver who gets out after a collision is a pedestrian with an owner's handle to their car and an explicit opponent: another driver on foot, the player, or the other car's door. The [incident rules](traffic.md#driver-incidents) decide the transitions; the pedestrian AI walks and punches.

- `Confront`: walks at 1.3 times the walking speed to the opponent and faces them. From five reaches away the person shouts every 0.9–1.4 s (a [shout](audio.md#shouts) in their own voice) and, face to face or standing, raises a fist and shakes it for 0.75 s (two atlas frames, the arm lifted towards the camera above the head, the other hand held out). Only at a car still occupied does the fist come down on the door, with the punch frame and a thump.
- `Fight` against another driver: the same punches as against the player (every 0.8–1.2 s), for 6–10 damage with a 12 % chance to knock the other down; a driver who is hit while still arguing fights back.
- `ToCar`: walks to the driver's door, or the passenger door when the driver's side is blocked, facing it to start; within 16 px the person gets back in.

Both new states turn the body towards the target before walking: the gait cannot start walking backwards, and without a facing target a person who stood with their back to the goal never set off. Drivers on foot are not recycled by the population manager while they own a car, and second-hand panic does not affect them. Scared, injured or knocked down, they react like anyone else, then walk back to their car.

## Locomotion and avoidance

People walk where their body faces. The steering produces a desired velocity; the body turns towards it at a limited rate, and only the part of the desired velocity along the body is walked, plus a side step of at most 0.3 m/s. Someone who wants to go the other way turns on the spot first, stepping round as it turns. There is no sliding sideways.

| Gait | Acceleration | Turn rate | Avoidance limit |
|---|---|---|---|
| Walking, waiting, crossing | 3 m/s² | 4.5 rad/s | 4 m/s² |
| Fleeing | 6 m/s² | 8 rad/s | 8 m/s² |
| Dodging | 12 m/s² | 20 rad/s | 3 m/s² |
| Fighting | 6 m/s² | 8 rad/s | 4 m/s² |
| Confronting, returning to a car | 3 m/s² | 4.5 rad/s | 4 m/s² |

The desired velocity combines the goal (relaxation time 0.5 s) with anticipatory avoidance based on the time to collision, after Karamouzas, Skinner and Guy (2014), *Universal power law governing pedestrian interactions*. For every person within 5 m, the player on foot, and street furniture within 3 m, the interaction energy is *E(τ) = k / τ² · e^(−τ/τ₀)* with *k* = 1.5 m² and *τ₀* = 3 s, where *τ* is the time until the two discs would touch; collisions further than 4 s away are ignored. People lying on the ground are avoided like furniture. A box-shaped object acts as a post at its nearest point.

Contact rules keep the model honest:

- Bodies closer than two radii are separated half each; the player does not give way.
- Buildings and solid furniture are hard walls; bus shelters can be walked into.
- A person who wants to move but makes no progress for 1.2 s turns round or takes another route.

People are drawn in one of three gaits by their speed: a walk, a jog from 2.0 m/s (hurrying across a street) and a run from 4.0 m/s (fleeing and dodging); a faster gait holds until 0.3 m/s below its start, so that it does not flicker. The walk advances with the distance walked at the look's own stride, so that a planted foot stays put. The jog and run play at a cadence that rises with speed (2.5–2.8 and 2.8–3.3 steps/s), because their feet touch the ground for a frame at most. The gaits, their speeds and the strides come from [civilians.cfg](../guides/adding-content.md#civilians). The procedural fallback has only a walk; a fast person shows it at 0.7 of its rate, for longer strides.

## Injury and death

| Event | Effect |
|---|---|
| Hit by a vehicle faster than 21 km/h | Damage 100 · (*v* / 36 km/h)², ±25 %: about half die at 36 km/h, nearly everyone above 45 km/h. The body is thrown a couple of metres and stays there. Hit and run: traffic drivers panic 60 % of the time. |
| A vehicle faster than 9 km/h driving over someone lying | The wheels go over them — they are not shoved along. Blood splatter and a blood decal; someone knocked down takes 40 + 0.2 · *v* damage. At most once every 0.8 s. |
| Weapons | See [Gameplay](gameplay.md); heavy hits knock people down |
| Death | A small blood splash where it happened; a blood pool (1.9 m, 90 s) where the body comes to rest. The body fades out between 24 and 27 s after death; the blood fades during its last 10 s. |

Slower vehicles push people aside. People who are knocked down but alive get up after 2.5–4 s.

## Population

300 people live around the player, like in GTA: they are spawned on sidewalks within 100 m of the start, and anyone further than 110 m from the player and off-screen is moved to an off-screen sidewalk point 30–100 m away. Removed bodies are replaced, one newcomer per frame. Nobody new appears within 20 m of a death for 90 s. The constants are `PEDESTRIANS`, `PED_KEEP_RADIUS`, `PED_SPAWN_MIN` and `PED_SPAWN_MAX` in `src/config.h`.

A uniform grid with one cell per tile (`PedGrid`) answers "who is near this point" for the pedestrians themselves, for traffic looking for people in the road, and for vehicle–pedestrian collisions. It is rebuilt before vehicles and before pedestrians update. The whole pedestrian update costs about 0.4 ms per frame for 300 people (see [Testing › Metrics](../testing.md#metrics)).

## Appearance

The 28 looks are atlases rendered from 3D models: men and women of three age groups, 1.55–1.86 m tall, in ordinary clothes ([pipeline](../../assets/art/cj004/README.md#civilian-3d-pipeline)). Each atlas holds the walk, idle, lying, two punches, two raised-fist positions and a run cycle, seen straight from above. A standing civilian is drawn `CHAR_SCALE` times life size and then the `SCALE` of [civilians.cfg](../guides/adding-content.md#civilians) larger again (1.12), so that an average man reads as large as the player, whose combat stance and vest make him broad; men and women keep their real proportions to each other. A body on the ground shows at real size. Without the atlases the game falls back to procedural looks.

## Tuning reference

| Constant | Value | Where |
|---|---|---|
| `LOOK_AHEAD`, `SENSE_PERIOD` | 1.6 s, 0.1 s | `pedestrian.cpp` |
| `BODY_MARGIN` | 0.35 m | `pedestrian.cpp` |
| `SLOW_VEHICLE` | 20 km/h | `pedestrian.cpp` |
| `RUN_SPEED`, `DODGE_SPEED`, `STEP_SPEED` | 5.2, 5.5, 1.8 m/s | `pedestrian.cpp` |
| `TTC_K`, `TTC_TAU0`, `TTC_MAX` | 1.5 m², 3 s, 4 s | `pedestrian.cpp` |
| `NEIGHBOUR_R`, `GOAL_TIME` | 5 m, 0.5 s | `pedestrian.cpp` |
| `PED_BODY_FADE`, `PED_BODY_GONE` | 24 s, 27 s | `pedestrian.h` |
| `PEDESTRIANS`, `PED_KEEP_RADIUS`, `PED_SPAWN_MIN`, `PED_SPAWN_MAX` | 300, 110 m, 30 m, 100 m | `config.h` |
| Standing / lying sprite size | 2.1 m frame × `CHAR_SCALE` × `SCALE` (1.12); lying bodies at real size | `civilians.cfg`, `pedestrian.cpp` |
| Gaits | Jog from 2.0 m/s, run from 4.0 m/s, cadences and strides | `civilians.cfg` |
| `GAIT_HYSTERESIS` | 0.3 m/s | `pedestrian.cpp` |
