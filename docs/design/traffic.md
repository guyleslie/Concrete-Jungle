# Traffic

How traffic and police vehicles drive. Traffic follows its lane kinematically ("on rails") until something knocks it into physics; police cars are always physics-driven. The rationale is recorded in [ADR-0004](../adr/0004-kinematic-rail-traffic.md).

Source: `src/traffic.h`, `src/traffic.cpp`; population management in `Game::UpdateSpawning` and `Game::UpdatePolice` (`src/game.cpp`).

The user requested replacing timeout-based abandonment and relocation with persistent drivers, feasible manoeuvres and cause-based incidents on 2026-10-03. See the [CJ-016 proposal](traffic-behaviour-proposal.md). It is awaiting agreement; this document still describes the implemented system.

## Contents

- [Rail driving](#rail-driving)
- [Route planning](#route-planning)
- [Speed control](#speed-control)
- [Junction rules](#junction-rules)
- [Obstacles and impatience](#obstacles-and-impatience)
- [Knocked off the lane](#knocked-off-the-lane)
- [Police](#police)
- [Population](#population)

## Rail driving

A traffic car on its lane is not simulated by the physics solver. It keeps a path — a list of waypoints along the right-hand lane — and a distance *s* along it. Every frame the AI advances *s* by its speed and places the car on the path: the pose comes from two sample points at the front and rear axle (±32 % of the length), so long vehicles sweep realistically through turns. A lateral offset shifts the car sideways when it overtakes or mounts the kerb.

Rail cars cannot jitter or deadlock in physics. They keep their distance by looking ahead along their own path, and in the solver they are infinitely heavy moving bodies that ignore buildings and street furniture (see [Physics › Traffic on rails](physics.md#traffic-on-rails)).

## Route planning

The path is planned one junction ahead and always extends at least 520 px beyond the car.

- At each junction the driver picks straight on (55 %, 70 % for large vehicles), right (25 %) or left (20 %, 10 % for large vehicles), among the exits that exist. A dead end at the city edge forces a U-turn.
- Turns are smooth curves through the junction. Large vehicles swing wide on right turns.
- Each turn is preceded by a stop waypoint that carries the junction, the signal axis and the planned manoeuvre.

## Speed control

| Influence | Behaviour |
|---|---|
| Cruise speed | 215–265 px/s (≈ 48–60 km/h); large vehicles 170–205 px/s. Panicking drivers go 50 % faster. |
| Curves | Slows to 130 px/s (95 px/s for large vehicles) before and through turns. |
| Signals | Stops for red. Stops for yellow if it can do so comfortably. |
| Obstacles | A look-ahead of 40 px + 1.1 × speed + half its length along the path finds vehicles and, unless the driver is distracted, pedestrians on the road (found through the pedestrian grid). The allowed speed follows the gap; the car stops 14 px short of the obstacle. |
| Acceleration | 45 % of the class acceleration; comfortable braking at 670 px/s², hard braking at 900 px/s² for an obstacle right ahead. |

### Planned stop

Every frame a rail car records in `DriverAI::stopDist` how far its front can still travel before a stop it has planned: the stop line at a red light or a blocked junction, or 14 px short of a person, a stopped vehicle or a static obstacle ahead. Pedestrians read it, together with the planned path (`AIPathPose`), to predict whether the car will reach them (see [Pedestrians › Perception and dodging](pedestrians.md#perception-and-dodging)).

## Junction rules

A car only enters a junction box when:

- the box holds no crossing traffic (vehicles going the same way are followed through; straight-on and right turns may meet oncoming straight-on and right turns);
- a left turn has no oncoming traffic about to come through on green;
- its exit lane has room for it, so it never blocks the box.

A right-of-way tie-breaker ensures two cars waiting for each other cannot wait forever.

## Obstacles and impatience

| Situation | Reaction |
|---|---|
| Stopped behind a static vehicle for 1 s (scaled by the driver's temper) | Overtakes through the oncoming lane if it is clear; otherwise, except for large vehicles, mounts the kerb if the sidewalk is free of furniture and buildings. |
| Blocked for 6 s | Makes a U-turn in the middle of the block, if it is far enough from the junction and the opposite lane is clear (not for large vehicles). |
| Blocked, or someone standing in the road | Honks; impatient drivers honk sooner. |
| Distracted (random, about once every few minutes per driver) | Ignores pedestrians for 1–2.5 s — accidents happen. |
| Scared (gunfire, explosions, hit by the player) | Panics: drives faster and runs red lights. |

## Knocked off the lane

A hard hit, an explosion, or pushing on something for a moment turns a rail car into a normal physics car (see [Physics](physics.md#traffic-on-rails)). The driver then goes through these stages:

1. **Stop.** Brakes, then holds the car with the handbrake. This stage lasts at least 0.7 s and at most 3 s.
2. **Rejoin.** Every 0.25 s the driver checks whether the car is within 4 m of its lane, pointing within 54° of the lane direction, outside a junction, and whether the way onto the lane (half-way and final pose) is free of vehicles, solid objects and buildings. If so, the car blends back onto a fresh path over about 0.7 s and continues on rails.
3. **Recover.** Otherwise the driver steers towards a point on the lane two and a half car lengths ahead (or out of the junction), slowly. If that point is behind the car, it makes a three-point turn; if the car is wedged and not moving for 0.8 s, it switches between forward and reverse.
4. **Give up.** If the car drops below 35 % health or recovery takes longer than 12 s, the driver gives up. Off-screen, the car is quietly re-placed elsewhere; on-screen, the driver gets out and walks off (fleeing if the car is badly damaged).

## Police

Police cars are physics-driven at all times, like the player's car.

| Mode | Behaviour |
|---|---|
| Patrol / pursuit on the grid | Plans routes along the lanes, choosing turns that close in on the player when chasing. Ignores red lights; when not chasing it keeps its distance from traffic. |
| Direct pursuit | With line of sight within about 40 m, or when very close: steers straight at a point ahead of the player, rams, and handbrakes into sharp turns. |
| Stuck | Pressing on without moving for 1.8 s: reverses for 1 s. |

Arrests, the number of police cars and when they give up are game rules — see [Gameplay › Police response](gameplay.md#police-response).

## Population

- 50 traffic cars drive at all times. Cars more than about 210 m from the player and off-screen are recycled to a random lane 55–160 m away, off-screen, so the city around the player stays busy.
- Up to 40 parked cars stand on parking spots, including police cars at the police station and ambulances at the hospital.
- Abandoned vehicles are removed once they are far away and off-screen.
