# Vehicles hitting people

How a vehicle meets a person, the player or a civilian: one impact per contact, never a push; throws, injuries and run-overs from published measurements; and the body that falls, lies and gets up. The model is [CJ-035](../backlog.md#cj-035-vehicles-hitting-people) ([ADR-0011](../adr/0011-vehicle-person-impact.md)); it lives in `src/impact.*`, and every value is in [`assets/data/impacts.cfg`](../../assets/data/impacts.cfg).

## Contents

- [One contact, one impact](#one-contact-one-impact)
- [The launch](#the-launch)
- [Injury](#injury)
- [On the ground](#on-the-ground)
- [Other falls](#other-falls)
- [Data](#data)
- [Measurements](#measurements)

## One contact, one impact

A person's legs cannot keep up with a vehicle. In real life a contact lasts a fraction of a second, and then the person staggers on their own feet, falls or is thrown; a vehicle does not carry anyone along. `Game::VehiclePedCollisions` therefore resolves each contact between a vehicle and a person once, after the physics step, by `VehicleMeetsPerson`, for civilians and the player alike:

| The person | The vehicle | What happens |
|---|---|---|
| Standing, walking into it | Not moving towards them | It stops them like a wall |
| Standing | Moving towards them, a speed change below their balance limit | **Bump:** they stagger and step out of the way, unhurt; another contact within the stagger time (0.8 s) knocks them down |
| Standing | A larger speed change, or while staggering | **Launch:** they fall or are thrown, and hurt ([below](#the-launch)) |
| In the air | Any | No contact: the body is above the vehicle and is drawn over it |
| Lying, alive or dead | Moving | **Run-over:** the vehicle drives over them once per pass; the living die, the body moves 0.2 m back under it, the vehicle loses 1 km/h (a jolt; the camera shakes in the player's vehicle) |

Momentum is conserved. The person takes at most the share *m*<sub>v</sub> / (*m*<sub>v</sub> + *m*<sub>p</sub>) of the closing speed along the contact normal (*m*<sub>p</sub> = 75 kg), and the vehicle loses what the person gains over the road: a 1.1 t taxi 6 % of the speed, a 0.3 t scooter 20 %, a 4.5 t bus under 2 %. The impulse acts along the contact normal, so a corner throws a person to the side of the vehicle's path.

A bumped civilian turns and steps back and aside at once, faster than the vehicle comes on (`StaggerPed`), so a slow vehicle that keeps going finds nobody in its way; the player is shoved back and steers out themselves. The balance limit is drawn for every civilian between 1.0 and 1.6 m/s (about 4–6 km/h); the player has the mean. Balance studies show that a step catches a push depending on its position and speed, and that older people step at smaller pushes, but give no single figure, so these are design values.

## The launch

A launched person flies as in Searle's model. The launch speed is the person's speed change times the **impact factor** of the vehicle's front, the share of the speed the body keeps; the launch angle is the front's too:

| Front | When | Launch angle | Impact factor |
|---|---|---|---|
| Wrap | The front edge (`FRONT` in `vehicles.cfg`) below the person's centre of mass (1.0 m): cars, motorbikes | 25° | 0.94 |
| Projection | The front edge at or above it: vans, ambulances, buses, trucks | 10° | 1.0 |

`MoveBody` then flies the body in a parabola, takes the friction of the landing off its speed over the road (μ times its vertical speed, μ = 0.7) and slides it to rest at μg. The values put the throw distance of a braking vehicle inside Happer's band and Searle's corridor at every speed from 15 to 100 km/h, for every class but the two-wheelers, for which no regression exists ([measurements](#measurements)).

The outcomes of a vehicle that does not brake follow from the same model, as in real life: at lower speeds the body lands in front of it and is driven over; from about 80 km/h it flies over a car (a roof vault) and lands behind it; a bus or a truck drives over the person it hit at any speed. A speed change below 25 km/h is a fall, above it a throw.

## Injury

One function for the player and civilians gives the damage of a launch from the person's speed change Δ*v*:

damage = 100 · e<sup>β (Δ*v* − Δ*v*<sub>50</sub>)</sup> · *u* / (1 − *u*), with *u* uniform on (0, 1).

Reaching 100 points, death, then has exactly the logistic probability 1 / (1 + e<sup>−β (Δ*v* − Δ*v*<sub>50</sub>)</sup>). Δ*v*<sub>50</sub> = 59.5 km/h and β = 0.089 per km/h reproduce Tefft's death curve over impact speed (half the people hit at 68 km/h die; all ages, cars and light trucks) for a 1.1 t car wrapping the person; heavier vehicles, and high fronts that keep the whole speed, hurt a little more. The share losing at least 25 points follows Tefft's severe-injury curve within about 4 km/h. Armour absorbs damage as it does for any blow.

## On the ground

People on the ground move by `MoveBody`: in the air, landing, sliding. A civilian knocked down lies 1.2–2.0 s after coming to rest from a fall and 2.7–3.5 s from a throw, then gets up and runs off; a dead body stays where it came to rest, with its blood pool.

The player falls, lies and gets up without control (`Game::UpdatePlayerDown`), drawn from frames rendered from the 3D player ([CJ-011 pipeline](../../assets/art/cj011/README.md#on-the-ground)): eight frames of the Universal Animation Library clip `Hit_Knockback` (thrown back off the feet onto the back, 0.5 s), its last frame lying, and twelve frames of `LayToIdle` (getting up, 1.4 s). Each frame is centred on the pelvis, the body's position in the game. The player is drawn from the standing look, larger than life, to real size lying, as the civilians are, and back while getting up; the weapon is not drawn on the ground. The lying times plus getting up give control back 2.6–3.4 s after a fall comes to rest and 4.1–4.9 s after a throw. A player who dies of any cause on foot falls the same way and lies there until the respawn.

## Other falls

- A motorbike rider thrown off (`Game::ThrowRider`) is launched like a person on a wrapping front, at the speed the bike lost (`ThrownOff`).
- A blow or a blast knocks a person down as far as before CJ-035: the old knock-down impulse (60 + 3 px/s per health point, at most 260 px/s) over the old drag of 4/s gives the distance, 0.9–4 m, and `SlideSpeed` the speed that slides that far under the new friction.

## Data

[`impacts.cfg`](../../assets/data/impacts.cfg) holds every value; its header describes the records, each required once ([format](../guides/adding-content.md#vehicles-hitting-people)):

| Record | Values |
|---|---|
| `PERSON` | Mass 75 kg, centre of mass 1.0 m |
| `BALANCE` | 1.0–1.6 m/s, stagger 0.8 s |
| `GROUND` | Friction 0.7 |
| `WRAP`, `PROJECTION` | 25°, 0.94; 10°, 1.0 |
| `INJURY` | Δ*v*<sub>50</sub> 59.5 km/h, slope 0.089 per km/h |
| `DOWN` | A fall below 25 km/h; lying 1.2–2.0 s after a fall, 2.7–3.5 s after a throw |
| `RUNOVER` | A body moves 0.2 m; the vehicle loses 1 km/h |

Every vehicle class declares its front edge with a [`FRONT`](../guides/adding-content.md#front--front-edge) record, so a new vehicle needs one line. The player's frames are declared by `DOWN`, `FALL`, `LIE` and `GETUP` in [`player.cfg`](../guides/adding-content.md#player).

## Measurements

The published data behind the values:

| Source | What it gives |
|---|---|
| Happer et al. (2000), 106 real collisions, via the [THUMS validation](https://dynalook.com/conferences/11th-european-ls-dyna-conference/pedestrian-safety-head-impact/validation-of-thums-human-model-throw-distance-in-pedestrian-accident-scenarios) | Impact speed from the throw distance *d*: cars V = 12.7 √d − 2.6 km/h (±9 km/h), SUVs and light trucks V = 11.4 √d − 0.4 km/h (±10.5 km/h) |
| Searle's model (same source) | A body launched at speed *v* comes to rest between v²/(2μg) and (1 + μ²) v²/(2μg) away; μ ≈ 0.7 on a dry road |
| [Tefft (2011), AAA Foundation](https://aaafoundation.org/impact-speed-pedestrians-risk-severe-injury-death/) | Death: 10, 25, 50, 75 and 90 % at 37, 51, 68, 80 and 93 km/h; severe injury at 26, 37, 50, 63 and 74 km/h |
| Run-over studies ([pedestrian deaths](https://tandf.figshare.com/articles/journal_contribution/Injury_patterns_in_motor_vehicle_collision-pediatric_pedestrian_deaths/21235749), [French national data](https://trid.trb.org/View/1488036)) | Being driven over happens mostly below 30 km/h, with severe head injuries; about half the people hit by trucks are driven over |

The [person-hit scenes](../testing.md#person-hit-scenes) measure the model against them for every vehicle class; the results before and after CJ-035 are in [Testing](../testing.md#person-hit-scenes).
