# Backlog

Planned work for Concrete Jungle, in priority order. Each item has a stable ID (`CJ-NNN`) that commits and documents can refer to.

## How to use this backlog

- Work on one item per session. Before implementing, agree a short specification: the target behaviour and measurable acceptance criteria.
- Add a [test scenario](testing.md) that measures the criteria whenever the item changes the simulation.
- When an item is finished, move it to [Done](#done) with the date, and add an entry to the [changelog](../CHANGELOG.md).
- New findings from playtesting become new items (or notes on an existing one) with the next free ID.

| Priority | Meaning |
|---|---|
| High | Directly hurts the game today; next in line |
| Medium | Clear improvement; schedule after the high items |
| Low | Polish or housekeeping |

## Next session

Recommended order, updated after the CJ-023 specification was agreed on 2026-10-07, CJ-032 to CJ-037 were added on 2026-10-11, the CJ-035 specification was agreed on 2026-10-11, CJ-039 was added on 2026-10-12 and CJ-040 was implemented on 2026-10-12:

1. **[CJ-035](#cj-035-vehicles-hitting-people) Vehicles hitting people** — implemented on 2026-10-11, awaiting playtest: every contact is one impact, never a push; injuries from published measurements, throws an eighth as far as real ones for play (second playtest, 2026-10-12); vehicles drive over people lying on the road; the player falls, lies and gets up. Its isolated scene test passes every check.
2. **[CJ-038](#cj-038-pedestrian-attention-and-evasion) Pedestrian attention and evasion** — test of 2026-10-11: nearly everyone escapes a car driven into a crowd; not everyone notices it, or can get away, in real life.
3. **[CJ-032](#cj-032-isolated-test-arena) Isolated test arena** — approach agreed on 2026-10-11; the measurable criteria are confirmed and the before values measured at the start of the session. Later items that change the player, animation or lighting are tested in it.
4. **[CJ-023](#cj-023-vehicle-widths-and-street-geometry) Vehicle widths and street geometry** — specification agreed; continue with step 1 (measurements only and the before run) of the [street geometry proposal](design/street-geometry-proposal.md#delivery).
5. **[CJ-018](#cj-018-police-driving-and-reactions) Police driving and reactions** — police in pursuit run down pedestrians, ram other cars and cause crashes; they do not react to a fight between drivers. Needs its own session and specification.
6. **[CJ-002](#cj-002-vehicle-handling-model) Vehicle handling model** — retain its approved specification and recorded baseline; implement the controller/physical capabilities that [ADR-0008](adr/0008-human-like-traffic.md) still needs deliberately rather than changing traffic behaviour incidentally.
7. **[CJ-003](#cj-003-vehicle-damage-model) Vehicle damage model**, which builds on the same physics.
8. **[CJ-012](#cj-012-audio-overhaul) Audio overhaul** — positional sound, sirens, horns and effects.
9. ~~[CJ-011](#cj-011-relaxed-player-posture) Relaxed player posture~~ — done on 2026-10-11: the user playtested and accepted the 3D player.
10. **[CJ-031](#cj-031-pedestrian-blocking-and-contact-realism) Pedestrian blocking and contact realism** — playtest of 2026-10-09: blocked people step on the spot, and people bumping into each other slide; specification agreed, next the before run.
11. **[CJ-039](#cj-039-varied-falls-and-lying-poses) Varied falls and lying poses** — the user's request of 2026-10-12: falls, being hit and rolling on the ground from Mixamo, and people coming to rest in different poses.
12. **[CJ-040](#cj-040-car-radio) Car radio** — implemented on 2026-10-12, awaiting playtest: the first station, Asphalt Rock FM, live; passing cars' music muffled by the car body. Its radio scenes pass every check; the station still needs two songs and four talk clips.

CJ-010 (pedestrians), CJ-013 (full screen) and CJ-033 (code review fixes) await user playtest acceptance.

Before each item, search for open-source code, assets and references that would help (the licence must allow redistribution; see [Adding content](guides/adding-content.md#art-and-licence-requirements)). Examples worth evaluating: Box2D or Jolt Physics as a reference for CJ-002, recorded CC0 sound libraries for CJ-012, OpenGameArt and Kenney-style texture packs of the right quality for CJ-014.

## Summary

| ID | Title | Priority | Status |
|---|---|---|---|
| [CJ-010](#cj-010-pedestrian-behaviour) | Pedestrian behaviour | High | Implemented, awaiting playtest |
| [CJ-013](#cj-013-full-screen-only) | Full screen only | High | Implemented, awaiting playtest |
| [CJ-002](#cj-002-vehicle-handling-model) | Vehicle handling model | High | In progress |
| [CJ-003](#cj-003-vehicle-damage-model) | Vehicle damage model | High | Open |
| [CJ-012](#cj-012-audio-overhaul) | Audio overhaul | High | Open |
| [CJ-011](#cj-011-relaxed-player-posture) | Relaxed player posture | High | Done |
| [CJ-031](#cj-031-pedestrian-blocking-and-contact-realism) | Pedestrian blocking and contact realism | High | Specification agreed |
| [CJ-032](#cj-032-isolated-test-arena) | Isolated test arena | High | Approach agreed |
| [CJ-018](#cj-018-police-driving-and-reactions) | Police driving and reactions | High | Open |
| [CJ-023](#cj-023-vehicle-widths-and-street-geometry) | Vehicle widths and street geometry | High | Specification agreed |
| [CJ-033](#cj-033-code-review-fixes) | Code review fixes | High | Implemented, awaiting playtest |
| [CJ-035](#cj-035-vehicles-hitting-people) | Vehicles hitting people | High | Implemented, awaiting playtest |
| [CJ-038](#cj-038-pedestrian-attention-and-evasion) | Pedestrian attention and evasion | High | Open |
| [CJ-014](#cj-014-city-art-and-layout) | City art and layout | Medium | Open |
| [CJ-015](#cj-015-street-lighting) | Street lighting | Medium | Open |
| [CJ-017](#cj-017-pedestrian-life) | Pedestrian life | Medium | Open |
| [CJ-019](#cj-019-performance-telemetry) | Performance telemetry | Medium | Open |
| [CJ-024](#cj-024-recovery-cost-in-dense-traffic) | Recovery cost in dense traffic | Medium | Open |
| [CJ-025](#cj-025-metro-track-stations-and-passengers) | Metro track, stations and passengers | Medium | Open |
| [CJ-026](#cj-026-bus-stops-and-bus-bays) | Bus stops and bus bays | Medium | Open |
| [CJ-027](#cj-027-multi-lane-roads) | Multi-lane roads | Medium | Open |
| [CJ-028](#cj-028-rule-breaking-drivers) | Rule-breaking drivers | Medium | Open |
| [CJ-005](#cj-005-data-driven-street-furniture) | Data-driven street furniture | Medium | Open |
| [CJ-037](#cj-037-audio-randomness-apart-from-gameplay) | Audio randomness apart from gameplay | Medium | Open |
| [CJ-039](#cj-039-varied-falls-and-lying-poses) | Varied falls and lying poses | Medium | Open |
| [CJ-040](#cj-040-car-radio) | Car radio | High | Implemented, awaiting playtest |
| [CJ-006](#cj-006-collision-polish) | Collision polish | Low | Open |
| [CJ-007](#cj-007-large-vehicle-recovery) | Large-vehicle recovery after a crash | Low | Open |
| [CJ-008](#cj-008-test-autopilot-improvements) | Test autopilot improvements | Low | Open |
| [CJ-021](#cj-021-visible-towing) | Visible towing | Low | Open |
| [CJ-022](#cj-022-three-point-turns) | Three-point turns | Low | Open |
| [CJ-036](#cj-036-dead-code-and-simplification) | Dead code and simplification | Low | Open |

## Open items

### CJ-010 Pedestrian behaviour

- **Priority:** High
- **Status:** Implemented on 2026-09-27, awaiting playtest

**Problem.** Pedestrians panicked at normal passing traffic and ran into the road, where traffic hit them; knocked-down people never got up; people slid sideways; the city looked empty on foot.

**Done.** Predictive perception of vehicles, dodging, sidewalk-aware fleeing with one-step panic spreading, safe crossing at the lights with queueing at the kerb, heading-constrained locomotion with time-to-collision avoidance, tough people who fight back, energy-based injuries, bodies that stay where they fall and fade out, blood pools, a population of 300 around the player, a pedestrian grid. See [Pedestrians](design/pedestrians.md) and [ADR-0006](adr/0006-pedestrian-steering.md).

**Acceptance criteria.**

| Criterion (`foot`/`day`, 1,500 frames) | Before | Target | Result |
|---|---|---|---|
| Fleeing without cause, average | 29.9 | below 1 | 0.0 |
| On the road off a crossing, average | 11.7 | below 1 | 0.0 |
| Hit by traffic | 7 | 0 | 0 |
| Knocked-down people who never get up | about 3 | 0 | 0 |
| Visible on foot, average | 1.3 | at least 4 | 5.9 |
| `rampage`: people in the car's path who escape | 67 % | at least 70 % | 72–81 % |
| Pedestrian CPU time | 0.28 ms | at most 0.5 ms | 0.37–0.41 ms |
| Sliding (added after playtest feedback) | 4.0 % | close to 0 | 0.1 % |
| `crash`, `derby`, `chase` physics | see [Testing](testing.md#baseline) | no worse | no deep frames, penetration under 3 px |

Remaining: the playtest.

**Feedback request (2026-09-27, repeated 2026-09-28).** Follow-up feedback requested on walking, crossings, dodging and getting up. No new acceptance or defect report received yet; keep this item awaiting playtest.

**Playtest feedback (2026-10-09).** The user finds the walking good. Blocked people stepping on the spot, people who slow down without slowing their steps while sliding past each other, and sideways sliding are not realistic; recorded as [CJ-031](#cj-031-pedestrian-blocking-and-contact-realism). This item stays awaiting playtest for crossings, dodging and getting up.

### CJ-013 Full screen only

- **Priority:** High
- **Status:** Implemented on 2026-09-27, awaiting playtest

**Problem.** The game opened in a resizable window that could be minimised, resized or switched with F11, and the Windows mouse cursor was visible.

**Done.** The game starts in borderless full screen at the monitor's resolution and cannot be resized; the Windows cursor is hidden and the game draws its own crosshair on foot; Alt+F4 closes the game. The `--shot` test mode keeps the 1,600 × 900 window so screenshots stay comparable.

**Acceptance criteria.** Playtested: full screen after start, no window controls, no Windows cursor, crosshair visible on foot, Alt+F4 quits. Verified on 2026-09-27: the start-up log reports a 1,920 × 1,080 borderless window on a 1,920 × 1,080 monitor, and Alt+F4 ends the process.

**Playtest feedback (2026-09-27).** The user questioned whether a crosshair is needed and requested an approach consistent with GTA 1 and GTA 2. Their original PC manuals describe character rotation and attack controls rather than mouse aiming with a persistent reticle ([GTA 1 manual](https://www.bestoldgames.net/download/games/grand-theft-auto/grand-theft-auto-manual.pdf), [GTA 2 manual](https://mocagh.org/miscgame/gtaset-gta2-manual.pdf)). Follow up on removing the persistent crosshair and verifying that shot direction remains readable with Concrete Jungle's mouse aiming; coordinate with [CJ-011](#cj-011-relaxed-player-posture). This revisits the crosshair acceptance criterion above; no HUD or control change is implemented in the CJ-002 session. Full-screen operation, cursor hiding and Alt+F4 have not yet received new user acceptance.

### CJ-002 Vehicle handling model

- **Priority:** High
- **Status:** In progress; specification approved, isolated baseline recorded

**Specification approved (2026-09-28).** [Vehicle handling proposal](design/vehicle-handling-proposal.md), with source/licence research, numeric targets for all 17 shipped classes, rear-brake profiles, prescribed-speed collision cases and a baseline-first measurement plan. [ADR-0007](adr/0007-dynamic-vehicle-handling.md) stays Proposed until implemented. Work stays directly on `main` for this session, as requested.

**Baseline review (2026-10-03).** All 17 classes completed their isolated handling measurements; the collision fixture completed 122 phases with 18 failed acceptance checks. The recorded executable, configuration and fixture hashes match the current build. See the [baseline report](design/vehicle-handling-baseline.md) for class results, collision failures and limitations of the old model's slip/skidpad telemetry. A fresh Taxi run reproduced all 97 metric records, including its missing stop and 23 failed checks. All six city reference runs completed on the matching build; `crash`, `derby` and `chase` reproduced the documented physics reference, with no deep frames. Production handling remains unchanged. Next: implement the approved axle model and compare against this evidence; keep CJ-002 open for the driving playtest.

**Problem.** Playtesting shows the vehicles are not controllable enough; the handling maths is not right. The physics is far from perfect: collisions need much closer attention, the vehicle types must handle clearly differently, and the game needs a reasonably realistic physics engine so that driving and every other vehicle action feel real.

**Playtest (2026-10-12).** The user finds the cars hard to turn and hard to control. Driving forwards feels right, but reversing seems inverted in some situations. GTA 1 and 2 felt much better to drive, so how they handled steering, reversing included, is to be studied before the model is changed. One likely cause of the inversion: the steering sense follows the car's actual motion along its axis (`dir`, `clamp(vF / 30, −1, 1)` in `VehicleForces`), not the key held. A car still rolling forwards while the player holds reverse steers as if driving forwards; a car pushed backwards while the player accelerates steers as if reversing; and near a standstill the steering fades out. The sense changes at a moment the player cannot see.

**City collision finding (2026-10-03).** The matching `rampage` baseline recorded 4 deep-overlap frames and a maximum penetration of 5.4 px. Keep this visible alongside the isolated collision failures; a zero process exit status is not proof of collision acceptance.

**Current state.** An arcade model (`VehicleForces` in `src/vehicle.cpp`, described in [Vehicles › Handling model](design/vehicles.md#handling-model)): steering sets a target yaw rate, and sideways velocity decays exponentially. The class values in `vehicles.cfg` are 4–9 times real-world figures (for example 90 m/s² braking). The contact solver ([ADR-0005](adr/0005-vehicle-contact-solver.md)) is sound but was tuned for the arcade model.

**Proposed approach.** A bicycle model: slip angles at the front and rear axle, a tyre force curve that saturates, load transfer under braking and acceleration, drivetrain (front, rear, four-wheel drive), mass and moment of inertia from the class. All parameters in `vehicles.cfg`. Keep the physics integration contract: the model only changes velocities. Revisit the impact response (restitution, friction, spin) with the new model. Study open-source references first (for example Box2D's top-down car sample, Jolt Physics vehicle constraints, Marco Monster's car physics notes).

**Acceptance criteria.**

- A written target for the feel, agreed before implementation, with reference numbers per vehicle type (braking distance from 100 km/h, 0–100 km/h time, maximum cornering acceleration, handbrake-turn behaviour, top speed).
- A handling test scenario that logs those numbers; every shipped class meets its targets, and the classes differ measurably.
- Impacts: a documented table of expected outcomes (glancing blow, T-bone, head-on, car versus truck) that a `crash` variant checks.
- The `crash` and `derby` metrics are no worse than the [baseline](testing.md#baseline).
- Playtested and accepted.
- [Vehicles](design/vehicles.md) and [Physics](design/physics.md) updated; a new ADR records the model change.

### CJ-003 Vehicle damage model

- **Priority:** High
- **Status:** Open

**Problem.** When and how a vehicle breaks down is not convincing yet.

**Current state.** A single health value per vehicle. Crash damage follows the impact's delta-V (`DV_*` constants in `src/game.cpp`); the engine smokes below 50 % and 25 % health, catches fire at zero and explodes 3–5 s later. There is no visible deformation, and the delta-V balance has not been playtested. See [Vehicles › Damage, fire and explosions](design/vehicles.md#damage-fire-and-explosions).

**Playtest (2026-10-12).** The user found that vehicles explode too soon. Each crash costs 0.13 health per px/s of delta-V above 90 px/s (20 km/h): a delta-V of 60 km/h, a head-on hit on a wall at about that speed, costs a Stinger 23 of its 100 points, and 100 km/h costs 46. Four such hits at 60 km/h, or two at 100 km/h, set it on fire, and every vehicle on fire explodes 3–5 s later. The rules table of this item is to say how much a vehicle takes before it is disabled, and when, if ever, it catches fire and explodes, from real crash data and for play.

**Ideas.** Separate subsystems (engine, body, tyres, lights) with their own effects — power loss, pulling to one side, flat tyres, broken headlights; visible damage on the sprite; clear rules for fire and explosion; driver injury tuning.

**Acceptance criteria.**

- A rules table, agreed before implementation, stating what each kind of hit does and when a vehicle is disabled, catches fire or explodes.
- Visible damage states for cars.
- Test scenario output that shows the damage progression for a fixed series of impacts.
- Playtested and accepted; [Vehicles](design/vehicles.md) and [Physics](design/physics.md#crash-consequences) updated.

### CJ-012 Audio overhaul

- **Priority:** High
- **Status:** Open

**Problem.** Sounds do not tell where things are, and they are monotonous. Every horn is the same sound at a different pitch; the siren is one loop; deaths, collisions and brakes sound thin. Nearly every important effect needs a better, more varied and realistic sound.

**Current state.** All sounds are synthesised at start-up (`src/audio.cpp`, [Audio](design/audio.md)). One-shots fade with the square of *1 − distance / 100 m* from the camera and are panned by the horizontal offset only. The siren is a single loop whose volume follows the nearest active siren (linear to 94 m), with no direction. The horn is one synthesised tone; traffic varies only its pitch.

**Scope.**

- **Positional sound:** a realistic distance law (inverse distance with a reference distance and a roll-off), panning for every loop including sirens and engines of other vehicles, optionally a slight Doppler shift for passing sirens.
- **Horns:** several realistic horn types chosen per vehicle class in `vehicles.cfg` (single-tone and dual-tone car horns, truck air horn, bus, scooter), and horn patterns chosen by the driver's mood: a short tap, a double tap, a long lean on the horn.
- **Sirens:** police, ambulance and fire sirens with wail, yelp and phaser modes, switching between slow and fast, and stopping and starting like real ones.
- **Effects:** varied recorded sounds for deaths and screams, punches, impacts by severity and material, tyre screech and braking, glass, explosions, footsteps on different surfaces.
- Recorded sounds from sources that allow redistribution (for example CC0 libraries) instead of synthesis wherever synthesis sounds artificial; several variants per sound, picked at random.

**Playtest (2026-10-12, during [CJ-040](#cj-040-car-radio)).** Sitting in a car with the radio on, the user hears every sound outside, crashes, shots, horns, as loud as on foot. In a closed car while driving, the sounds outside should come through the body: quieter and duller, roughly simulated. The radio already does this for a radio outside (the `BODY` of `assets/data/radio.cfg`: 25 dB and a 300 Hz low-pass per body, [Radio](design/radio.md#how-a-passing-car-sounds)); the same body is to be applied to every sound outside while the player sits in a car, the player's own engine, horn and radio excepted.

**Acceptance criteria.** A sound list with sources and licences agreed before implementation; at least three variants for each frequent sound; horn and siren types configurable in the data files; a test scenario that logs which sounds played at which volume and pan; playtested and accepted; [Audio](design/audio.md) and [CREDITS](../CREDITS.md) updated.

### CJ-011 Relaxed player posture

- **Priority:** High
- **Status:** Done (2026-10-11)

**Problem.** The player always looks as if in action. Unarmed, the character holds its fists up as if about to fight; armed, it constantly aims. In GTA the character walked normally and only raised the weapon to aim or fire.

**Current state.** The player uses the "Animated Top Down Survivor Player" sprites ([CREDITS](../CREDITS.md)), which only contain combat poses; the `unarmed` set was derived from the knife frames.

**Scope.** A character sheet with the arms down (idle, walk, run), a weapon carried lowered or holstered while not aiming, and a short raise transition when the player aims or fires. Needs new art (see CJ-004 for sources).

**Approach (agreed with the user, 2026-10-09).** The player becomes a 3D character built and rendered with the civilian pipeline of [CJ-004](#cj-004-replace-placeholder-art), replacing the Survivor sprites. The weapons come from Quaternius's Ultimate Gun Pack ([OpenGameArt](https://opengameart.org/content/low-poly-guns-pack), CC0): 40 guns (pistols, revolvers, shotguns, assault rifles, submachine guns, sniper rifles) and accessories such as a weapon light, a bipod and a tripod, but no knife or rocket launcher; it is downloaded to `build/art-sources/downloads/`. A weapon is fixed to the right hand bone with a grip offset per weapon, and Blender's inverse kinematics put the other hand on a two-handed weapon's fore grip. The legs take the accepted walk, jog and run of [CJ-029](#cj-029-civilian-gait-realism); the upper body takes a carry pose with the weapon lowered, or the aim pose, plus a recoil frame or two for firing. The animation library already has pistol clips (`Pistol_Idle_Loop`, `Pistol_Aim_Neutral`, `Pistol_Shoot`, `Pistol_Reload`); no free CC0 library found has rifle clips, so Mixamo is the fallback if the static holds look stiff. The specification (the player's look, the poses per weapon, frame counts and measurable criteria) is agreed at the start of the CJ-011 session.

**Requirements from the user (2026-10-09).**

- The player walks and runs. Today `Left Shift` runs at 6.5 m/s and the normal pace is 4.0 m/s, a jog's speed (civilians jog from 2.0 m/s). The user's choice: the player walks by default at a real walking pace of about 1.5 m/s with the walk, and `Left Shift` runs at 6.5 m/s with the run; the specification fixes the exact speeds.
- A weapon is raised and aimed only while the player aims, as the game decides it today (kept as it is, the user's choice): while either mouse button is held and for 0.8 s after a shot. Otherwise it is held lowered in the hand. GTA 1 worked the same way: its people stand, walk and run without a weapon pose, and have firing animations per weapon type for standing, walking and running ([Carnage3D](https://github.com/codenamecpp/carnage3d), a reimplementation that uses the original graphics); no source was found for GTA 2.
- The torso keeps facing the aim while the legs walk, strafe or backpedal along the motion, as now: the renders need the legs and the upper body as separate layers.
- The player looks like today's Survivor: a dark baseball cap, an olive green jacket, a light camouflage backpack, dark trousers and boots, so the player stays recognisable and stands out from the civilians. No CC0 baseball cap or backpack exists in the MakeHuman packs, so both are modelled in Blender by script (crown with seams and a peak; a fabric pack with a flap and straps) and fixed to the head and upper spine bones.
- Weapons are held as real people carry them: a pistol lowered beside the thigh, pointing at the ground, and aimed with both hands (the library's `Pistol_Aim_Neutral`); a long gun in the low ready, the stock at the shoulder and the barrel angled down and forward, the other hand on the fore grip, and shouldered to aim. The knife comes from the MakeHuman Equipment 01 pack's dagger ([CC0](https://static.makehumancommunity.org/assets/assetpacks/equipment01.html)); on 2026-10-10 the user chose the Ultimate Gun Pack's `Bayonet_2` instead.
- The shotgun reloads shell by shell, as a pump shotgun is loaded (the user's choice of 2026-10-11): it loads only the missing shells, each with the three loading frames (taking a shell, the shell at the loading port, pushed home), then racks the pump once. The reload time therefore grows with the number of missing shells instead of the fixed `reload s` of [weapons.cfg](../assets/data/weapons.cfg); the time per shell and for the pump, and the frame order, belong in the data files.
- Every shotgun shot is followed by racking the pump (the user's requirement of 2026-10-11): the recoil frame, then the pump drawn back, then the aim pose with the pump forward again.

**Specification (agreed with the user, 2026-10-09).** The legs and the upper body are separate layers; the frames are 144 px for 2.1 m, as the civilians'.

| Layer | Animation | Frames |
|---|---|---|
| Legs, shared by every weapon | Stand, walk, run | 1, 16, 16 |
| Upper body, per weapon | Stand, walk and run with the weapon lowered | 1, 16, 16, in step with the legs |
| | Aim, fire (recoil) | 1, 2 |
| | Reload (firearms) | 4 |
| | Punch or stab (fists, knife, flashlight) | 2–4 |

The player has no jog: the walk shows below 4 m/s and the run from 4 m/s. The walk advances with the distance walked at the player's measured stride and the run by cadence, as for the civilians. The run has 16 frames instead of the civilians' 8, because the player, always on screen, runs a lot: at 6.5 m/s and 3.3 steps/s, 8 frames would show only about 13 a second.

**Acceptance criteria.**

1. Weapon lowered: the barrel points down, a pistol at least 45° and a long gun 30–60° below the horizontal, and from above a pistol reaches at most 0.25 m and a long gun at most 0.45 m ahead of the body. With the butt at the shoulder, a carbine angled 45° down reaches about 0.4 m ahead, as a real low ready does; the user raised the long guns' limit from 0.25 m on 2026-10-10.
2. Aiming: the barrel is level within ±5°; a long gun's stock is within 5 cm of the shoulder.
3. Hands on the weapon in every frame: the grip within 1 cm of the right hand, a two-handed weapon's fore grip within 3 cm of the other hand.
4. Legs by the [CJ-029](#cj-029-civilian-gait-realism) measures: the planted foot moves at most 2 cm in the walk; as much leg shows ahead of the body as behind it (0.75–1.33).
5. Layers: with the legs and the upper body facing the same way, the two layers match a single render; at ±90° between them, the waist shows no gap. The backpack and the cap do not cut into the body in any frame.
6. Size: the player's width within 10 % of today's player.
7. In the game: aiming or firing raises the weapon within 0.2 s, and it is lowered 0.8 s after the last shot or aim; the walk is 1.5 m/s and the run 6.5 m/s.
8. A new `player` scenario: with every weapon, the player stands, walks, runs, strafes, backpedals, aims, fires and reloads, and the log records the raise and lower times. In the existing scenarios the pedestrian metrics are unchanged, FPS medians of alternating runs stay within ±3 %, and video memory grows by at most 15 MB.
9. Playtested by the user.

**Progress (2026-10-10).** The [CJ-011 player art](../assets/art/cj011/README.md) records the work. The user chose the Ultimate Gun Pack's `Pistol_1` and `AssaultRifle2_1` and accepted, after corrections, the look (plain dark shirt, a fabric daypack with flat shoulder and lower straps in a darker camouflage, a darker cap), the support hand under the middle of the handguard, the pistol aimed in the isosceles stance before the right eye, and the layered walk, run, strafe and backpedal. Neither animation library has a strafe or a backpedal; at the user's choice they come from Mixamo, whose files the user downloaded. Measured on every frame, criteria 1–5 are met; criterion 6 is not decided: standing, the player is 0.58 m wide (−11 %), and aiming the pistol 0.50 m. Next: aim, fire and reload in layers, the other weapons, the atlases and the game's two-layer player.

**Progress (2026-10-11).** The user accepted the shotgun, knife, flashlight and fist holds, the reloads (four frames each, the shotgun shell by shell), the punches at head height, the stab at the belly, the flashlight strike, the jogging strafes and the long guns carried across the body when strafing and with the upper body when backpedalling. Next: the atlases and the game's two-layer player.

**In the game (2026-10-11).** The player is drawn from the legs atlas and the weapon set's upper-body atlas ([player.cfg](../assets/data/player.cfg)); the Survivor sprites remained the fallback, unloaded, until they were removed later the same day ([ADR-0009](adr/0009-required-character-atlases.md)). The new `player` scenario drives every weapon through every gait and action. Criteria 6–8 are met: drawn as wide as before (`SCALE 1.12`), the weapon raised in 0.12 s and lowered 0.97 s after the last shot, walk 1.50 m/s and run 6.50 m/s, the pedestrian metrics of `brawl` unchanged, FPS medians −2.5 % and −2.8 %, video memory about 15 MB less ([measurements](../assets/art/cj011/README.md#measurements)).

**Playtest (2026-10-11).** The user played with every weapon (`--scenario armed`, by day and at night) and asked for one change: the flashlight's beam must move with the lamp. It now starts at the drawn lens and points as the lamp does in every frame (`LAMP` records measured on the renders). The user accepted the player; CJ-011 is done (criterion 9).

### CJ-018 Police driving and reactions

- **Priority:** High
- **Status:** Open

**Problem.** Police cars in pursuit drive at up to 150 km/h through crowds and on sidewalks and do not react to people at all; in the `brawl` scenario they caused most of the pedestrian deaths.

**Playtest feedback (2026-10-07).** During a pursuit the police are far too aggressive: they run down many pedestrians, drive their cars badly, ram other cars and cause crashes. They also do not react to a fight between drivers (left open by [CJ-016](#cj-016-road-rage-and-traffic-incidents)). The user wants police behaviour discussed again in a separate session before any change.

**Acceptance criteria.** To be agreed in that session. Starting points: police cars brake for and steer around people when it does not cost them the pursuit, mount the sidewalk only when necessary and slowly; in `chase` and `brawl`, pedestrians hit by police drop by at least 80 %; collisions between police and uninvolved traffic are measured and reduced; police respond to a fight between drivers.

### CJ-014 City art and layout

- **Priority:** Medium
- **Status:** Open

**Problem.** The city relies on one downloaded texture set and looks repetitive. Parks need to be redesigned, and the city lacks fences and many small details.

**Scope.**

- More ground, wall and roof textures of the same quality, with variation per district.
- Parks rethought: paths, lawns, fences and hedges, gates, playgrounds, ponds.
- Fences, walls and gates around lots, courtyards and parking lots.
- The gaps between buildings (service alleys) furnished: bins, back doors, fire escapes, puddles.
- Parking lots: markings, barriers, ticket machines, lamps.
- The elevated metro where it runs above the streets: pillars, the underside of the deck, shadows and lights.
- Small details everywhere: road wear, manholes, signs, awnings, shop fronts.
- Evaluate open-source city generators and asset packs before building everything by hand.

**Playtest feedback (2026-10-07).** Parking lots need a well-thought-out entrance and exit, cars parked inside, a fence round them, and variety: they all look the same. The parks are ugly: no fence, all alike, and the fountain in the middle looks bad.

**Acceptance criteria.** A before/after screenshot set of the same views agreed with the user; new content defined in data files where possible; playtested.

### CJ-015 Street lighting

- **Priority:** Medium
- **Status:** Open

**Problem.** The light from the street lamps does not fall where it should, the lamp posts themselves could look better, and the lighting of the whole game at night could be much better, with a cooler colour and without the current bugs.

**Playtest (2026-10-12).** The user found two faults:

- The head of a lamp does not always sit on the end of its arm. Likely cause: `CityMap` draws the arm as a box along the x or the y axis only, while the head is drawn at the arm's real end; the lamps at the corners of a plaza point diagonally at its centre, so their heads float beside the arm. Every kind of lamp (street, plaza, park, parking) is to be checked.
- Junctions are too dark. Street lamps stand on every fourth tile along the kerbs, so a junction gets light only from lamps that happen to be near it. In real life junctions are lit more brightly than the roads that meet there, from lamps at their corners or on the signal masts, because they are where vehicles and people cross; the lighting standards and how real junctions are lit are to be researched when the item is specified.

**Acceptance criteria.** A list of the lighting bugs with screenshots; lamp light pools aligned with the lamp heads; a night palette agreed with the user; before/after screenshots at dusk and at night; see [Rendering](design/rendering.md).

### CJ-017 Pedestrian life

- **Priority:** Medium
- **Status:** Open

**Idea.** Follow-ups to CJ-010 that make people look smarter and the city more alive:

- witnesses report crimes, and the wanted level depends on being seen;
- couples and groups walking together, people stopping to chat;
- using phone booths, waiting at bus stops and boarding buses, sitting on benches;
- reacting to a drawn gun (hands up, running), to the player's car honking, to sirens;
- drivers who get out of cars and people who get into parked cars;
- bloody tyre tracks after driving through blood.

**Acceptance criteria.** One behaviour at a time, each with its own measurable criteria and scenario; the `foot` metrics of CJ-010 stay met.

### CJ-019 Performance telemetry

- **Priority:** Medium
- **Status:** Open

**Idea.** Measure how fast the game runs, when and why it slows down. The `TIMING` log line (CPU time of the vehicle update, the pedestrian update and the world drawing) is a start.

**Acceptance criteria.** A frame-time log over the run (average, 95th and 99th percentile, worst frame, and the frames above 20 ms with what was happening), frames that save screenshots excluded; per-system CPU times including physics, traffic, particles and each render pass; an optional on-screen graph (F3 debug view); documented in [Testing](testing.md).

### CJ-005 Data-driven street furniture

- **Priority:** Medium
- **Status:** Open

**Problem.** Street furniture sizes, collision shapes and breakaway materials are defined in code (`Dim` and `ApplyMaterial` in `src/city_map.cpp`), unlike every other kind of content ([ADR-0002](adr/0002-data-driven-content.md)).

**Playtest (2026-10-12).** The user asked for street furniture that is placed and breaks as in real life:

- **Placement.** There are too many fire hydrants on the sidewalks. Today each free kerb tile gets one at random (6 %), about one or two per block, wherever they fall. Every kind of furniture is to be placed as in a real city: where it stands, and how far apart, from real street design; the figures are to be researched when the item is specified.
- **Breaking by speed.** A vehicle that runs into a hydrant slowly must not break it; one that drives over it faster shears it off, and water sprays from it. Today it breaks at the impulse of a 1 t car at about 20 km/h (`strength` 90), and a broken hydrant gushes water already. The thresholds of every breakable prop are to be checked against real breakaway designs and measured in a test.
- **Remains.** Shrubs and other props that can be run down need a second, broken graphic: today a broken prop or shrub disappears, leaving only debris that fades (only knocked-over poles stay). See [CJ-006](#cj-006-collision-polish).

**Acceptance criteria.** A `props.cfg` (or similar) defines each prop's size, collision shape, bullet breakability, strength and loose mass, its placement rules and its broken graphic; the city generator reads it; [Adding content](guides/adding-content.md#street-furniture) documents it. To be agreed: placement densities from real references; break thresholds measured at set speeds.

### CJ-006 Collision polish

- **Priority:** Low
- **Status:** Open

Smaller improvements left after the collision rewrite:

- a continuous grinding sound while scraping along walls (now sparks and a short crash sound);
- wheel-spin smoke when a vehicle presses against an obstacle under full throttle;
- knocked-over poles are decoration only; they could become low obstacles;
- dumpsters are rigid; they could be pushable bodies;
- a shrub driven over must stay as a flattened, broken shrub, with leaves and twigs flying, instead of disappearing; destructible scenery in general should leave realistic remains, a second, broken graphic for every prop that can be run down (the user's request of 2026-10-12, with [CJ-005](#cj-005-data-driven-street-furniture)).

### CJ-007 Large-vehicle recovery

- **Priority:** Low
- **Status:** Open

**Problem.** Before CJ-016, a 12 m bus knocked off its lane often failed to manoeuvre back and gave up after 12 s. CJ-016's first increment covers Bus/BoxTruck recovery fixtures; broader heavy-vehicle city recovery and playtest acceptance remain open.

**Acceptance criteria.** Buses and trucks recover to their lane in most knock-offs in the `chase` and `derby` scenarios, without teleporting on-screen.

### CJ-008 Test autopilot improvements

- **Priority:** Low
- **Status:** Open

The `drive` autopilot never reverses, so it stays stuck once it drives into something (since 2026-09-27 it ends up against a building corner). It should back out like the `derby` autopilot. Handling scenarios for [CJ-002](#cj-002-vehicle-handling-model) will need scripted manoeuvres as well.

**Observed on 2026-10-06** (`drive`, 1,800 frames, build `a3da34f`): the run now stops in the first junction. At t = 1.65 s a traffic taxi (#44, normal driver) hits the Stinger at an approach speed of 221 px/s; 0.05 s later the Stinger hits two pieces of street furniture. From then on the car stays at 0 km/h until the end of the run (11 player stuck events, health 61/100). The taxi stays knocked for more than 12 s (`LONG-REJOIN reason=no_feasible_manoeuvre`), so neither car frees the other, and every screenshot after the first two seconds shows the same junction.

### CJ-021 Visible towing

- **Priority:** Low
- **Status:** Open

**Idea.** A car that cannot get free, such as one boxed in by buildings and other cars, stays in place and stays observable under [CJ-016](#cj-016-road-rage-and-traffic-incidents). A tow truck should come and remove it in view, as the approved [traffic proposal](design/traffic-behaviour-proposal.md) plans after the core. Nothing may vanish or be relocated invisibly in the meantime.

**Acceptance criteria.** To be agreed: the tow truck drives there on the road network, hooks up the car and tows it away; a test scenario logs the call, the arrival time and the removal; playtested.

### CJ-024 Recovery cost in dense traffic

- **Priority:** Medium
- **Status:** Open (found by CJ-020 on 2026-10-07)

**Problem.** Since CJ-020 traffic takes turns at a realistic 11–19 km/h, so traffic is denser round the junctions. In `rampage`, where the player knocks many cars off their lanes, about 7 cars recover at a time instead of 4.5, with more moving drivers to forecast round each, and the driver decision CPU (0.60 ms average, 1.17 ms 95th percentile on 2026-10-07) exceeds the CJ-016 targets of 0.5 and 1.0 ms. The recovery decisions themselves are unchanged; the other city scenarios stay within the targets. The user accepted this for CJ-020 on 2026-10-07.

**Acceptance criteria.** `rampage` within 0.5 ms average and 1.0 ms 95th percentile decision CPU, with the CJ-016 fixtures passing unchanged.

### CJ-025 Metro track, stations and passengers

- **Priority:** Medium
- **Status:** Open (playtest feedback on 2026-10-07)

**Problem.** The elevated metro train does not run along the middle of its track, no rails are visible on the deck, and there are no stations where passengers get on and off.

**Acceptance criteria.** To be agreed: the train centred on its track, with visible rails and sleepers; stations with platforms and access from the street; the train stops at them, and people walk to a station, wait, board and alight; screenshots of the deck and a station; playtested. The deck's underside and pillars belong to [CJ-014](#cj-014-city-art-and-layout).

### CJ-026 Bus stops and bus bays

- **Priority:** Medium
- **Status:** Open (playtest feedback on 2026-10-07)

**Problem.** Bus stops are only shelters on the sidewalk. A bus should be able to pull into a bay beside the lane, so that it does not hold up the traffic while people get on and off.

**Playtest (2026-10-12).** The shelters themselves look wrong:

- The two posts holding up the roof always show, even where the roof should hide them from the camera. Likely cause: `CityMap` draws the posts as solid boxes reaching the roof's height, and the roof afterwards as a flat, translucent sprite that writes no depth, so the posts show through it from every angle.
- The roof is poorly drawn: a procedural sprite (`sprite_gen.cpp`) of a glass pane with three bars and an advertising panel, which does not reach the [art requirements](guides/adding-content.md#art-and-licence-requirements) of the rest of the city.

**Acceptance criteria.** To be agreed: a shelter that looks right from every angle, the posts hidden where the roof covers them, and art of the city's quality; bus bays at the stops (geometry and art, with [CJ-023](#cj-023-vehicle-widths-and-street-geometry)); buses on routes that stop in the bay and pull out into the traffic; people waiting at the stop board and alight ([CJ-017](#cj-017-pedestrian-life)); traffic measured passing a stopped bus; playtested.

### CJ-022 Three-point turns

- **Priority:** Low
- **Status:** Open (found by CJ-020 on 2026-10-07)

**Problem.** A traffic car blocked for 6 s turns round in the middle of a block, and a dead end at the city edge forces a U-turn in the junction. On an 8 m street the widest one-move turn between the lanes has a 2 m radius, while a car's rear axle needs at least 3.3 m (a bus 7 m): since CJ-020 the rear axle follows that semicircle without sliding, but no real car could make the turn.

**Acceptance criteria.** To be agreed: a three-point turn (forward into the far lane, reverse, forward) within each class's turning radius, checked against vehicles and people like a pull-out; large vehicles keep avoiding U-turns; playtested.

### CJ-023 Vehicle widths and street geometry

- **Priority:** High
- **Status:** Specification agreed on 2026-10-07; implementation not started (found by CJ-020 on 2026-10-07)

**Problem.** Playtest feedback (2026-10-07): the streets feel cramped; cars and pedestrians seem too close together. The measurements of CJ-020 point to the vehicles and the junctions rather than the road width: lanes are 4 m (real ones 3–3.5 m) and the 4 m sidewalks keep pedestrians 1.4–2.6 m from the kerb, but a vehicle's width follows its sprite's aspect ratio, which makes most of them 30–40 % wider than the real vehicle (Taxi 2.46 m against about 1.8 m, Pickup 2.88 m, Ambulance 2.96 m, Bus 2.92 m against 2.55 m, Semi 3.63 m against 2.55 m), and junction corners are square, without the 3–6 m kerb radius of a real street. As a result the Pickup, Van, Limo and Ambulance need wide turns, and the Bus, the Semi and the right turns of the BoxTruck, FireTruck and Garbage truck do not fit at all, so traffic avoids them ([turning results](design/traffic-turning-results.md)).

**CJ-020 playtest feedback (2026-10-07).** The turns are good, with minor glitches that, in the user's view, only wider roads and junctions and a rethought pedestrian zone can fix. The space between the road and the buildings is also too small for good play: a car leaving the road runs straight into a tree or another piece of street furniture, because everything beside the road is packed into a narrow strip. It should be somewhat wider.

**Scope.** Wider roads and junctions; rounded kerb corners; a rethought sidewalk (a furniture zone along the kerb and a clear walking zone); more room between the road and the buildings; real-world vehicle widths.

**User direction (2026-10-07).** Besides realism, the city must play well on foot and in a vehicle. Streets may differ in width, and multi-lane roads come later ([CJ-027](#cj-027-multi-lane-roads)). Traffic that obeys the rules must never collide while turning; a few drivers may break the rules, but never exaggerated ([CJ-028](#cj-028-rule-breaking-drivers)).

**Specification agreed (2026-10-07).** [Street geometry proposal](design/street-geometry-proposal.md). Collision boxes take each class's real body width from a new `WIDTH` record in `vehicles.cfg` (the sprites' extra width is mostly their side mirrors), with the sprite drawn to it. A new `city.cfg` assigns a street profile to every grid line: main streets 12 m between kerbs (two 4 m lanes and 2 m kerbside strips, kept empty behind an edge line) with 6 m sidewalks and a 2 m setback, side streets 10 m with 5 m sidewalks and a 1.5 m setback, both one lane each way. The sidewalk splits into a furniture zone along the kerb and a clear walking zone; kerb corners are rounded (6 m and 5 m); block interiors stay 48 m, so the island grows from 584 m to about 636 m. Surfaces are computed from the line geometry instead of 4 m tiles; turn paths are built per junction type; the signal timing moves to `city.cfg` with a longer green. The user chose the two profiles, the empty strip and the 48 m interiors.

**Acceptance criteria.** See the [proposal](design/street-geometry-proposal.md#acceptance-criteria): collision widths equal the `WIDTH` records; for every junction type, clean right and left turns for every car class and fitting turns for every large class; zero overlaps and contacts between law-abiding rail cars in junctions; the free run-off from the kerb at least doubled; people walking within 2 m of moving traffic cut to a quarter; pedestrian, traffic, physics, CJ-016 and CPU measurements no worse; before/after screenshots; the CJ-002 baseline re-recorded; playtested.

### CJ-027 Multi-lane roads

- **Priority:** Medium
- **Status:** Open (user direction on 2026-10-07)

**Idea.** Roads have one lane each way. The user wants multi-lane roads later, with streets of different widths. CJ-023's street profiles carry a lane count from the start, so main streets can grow into avenues with two or more lanes each way.

**Acceptance criteria.** To be agreed: avenues with several lanes each way in `city.cfg`; traffic keeps to a lane, changes lanes by signalling and checking the gap, uses turn lanes or turns from the correct lane at junctions; pedestrians cross wider roads safely (a refuge or a longer green); the CJ-020 turning and CJ-016 fixtures extended to multi-lane junctions; playtested.

### CJ-028 Rule-breaking drivers

- **Priority:** Medium
- **Status:** Open (user direction on 2026-10-07)

**Idea.** Traffic that obeys the rules never collides while turning ([CJ-023](#cj-023-vehicle-widths-and-street-geometry)). A few drivers should break the rules, as in a real city: run a red light, mount the sidewalk, misjudge a gap and hit another car; never exaggerated. Today traffic never runs a red light, and mounts the sidewalk only to get round an obstacle when impatient.

**Acceptance criteria.** To be agreed: which offences occur and how often (per driver mood, in `traffic.cfg`), measured in the city scenarios (offences per minute, crashes they cause, people hit); law-abiding drivers keep zero contacts in junctions; the police notice offences in view ([CJ-018](#cj-018-police-driving-and-reactions)); playtested.

### CJ-031 Pedestrian blocking and contact realism

- **Priority:** High
- **Status:** Specification agreed on 2026-10-09

**Problem (playtest, 2026-10-09).** People walk well, but when an obstacle stops them they keep stepping, jogging or running on the spot; when they bump into each other and slide past, they slow down while stepping as fast as before; and they slide sideways. The user wants this much more realistic: sliding cut to a minimum, and people turning away when they must.

**Causes in the code (`src/pedestrian.cpp`).**

- The drawn gait and its pace come from the steering velocity (`p.vel`), not from the actual displacement. The contact pass, the player's push and the walls (`Collide`) move the position afterwards without changing the velocity, so a blocked person animates at full pace.
- A person counts as stuck only after 1.2 s of progress below 0.25 m/s, and then turns round; slow progress along a wall never counts.
- Touching bodies are pushed apart half each, sideways to their heading, and the player pushes people aside; the body neither turns nor slows.
- The `PEDS` sliding metric compares the heading with the steering velocity, so it does not see these pushes (0.1 % in [CJ-010](#cj-010-pedestrian-behaviour)).

**Approach (agreed with the user, 2026-10-09).** Within [ADR-0006](adr/0006-pedestrian-steering.md):

1. The steps follow the actual displacement along the body's heading, smoothed over about 0.15 s: a blocked person stands and a slowed one steps slower. The gait (walk, jog, run) is chosen from the same speed.
2. Wall and contact corrections also take the blocked part out of the velocity, so the steering sees the obstacle.
3. A person who makes too little progress for 0.3–0.5 s stops pressing on: waits briefly for a person in the way, then turns and goes round; turns away from a wall or street furniture.
4. Fewer contacts: earlier anticipation for calm walkers, passing on the same side, and yielding (whoever stands or is slower waits, the other goes round). Bodies that still touch slow down and turn; a sideways push stays within a few centimetres.
5. Measurements from the actual displacement: stepping on the spot (step speed against ground speed), sideways slip, contacts, and the time from being blocked to reacting; the sliding metric moves to the actual displacement. A scenario with people walking into a wall and a bollard, pairs meeting head-on in a narrow passage, a crowd at a crossing, and the player pushing through a crowd.

**Acceptance criteria.** Measured in `foot` and `day` (1,500 frames) and the new scenario; the before values come from a before run at the start of the work.

| Criterion | Target |
|---|---|
| Stepping on the spot: walking, jogging or running drawn while the actual speed is under 40 % of the drawn pace | Below 1 % of the moving time |
| Sideways slip: displacement across the body's heading | At most a fifth of the before value |
| Bodies touching, per second | At most half of the before value |
| From being blocked to reacting (waiting, turning or going round) | At most 0.5 s |
| [CJ-010](#cj-010-pedestrian-behaviour) metrics in `foot`, `day` and `rampage` | Still met |
| Pedestrian CPU time | At most 0.5 ms |

Then the user's playtest.

### CJ-032 Isolated test arena

- **Priority:** High
- **Status:** Approach agreed on 2026-10-11; criteria to be confirmed at the start of the session

**Problem (2026-10-11).** The [CJ-011](#cj-011-relaxed-player-posture) player was tested in the real city: traffic, pedestrians, buildings, the police and pay-phone missions got in the way, a run took longer than the test needed, and runs were hard to compare. The user wants features tested in an isolated, consistent and parameterised environment, as precisely as the traffic fixtures test traffic, and not on the real map unless that is the question; how a feature behaves in the real city is mostly the user's playtest.

**Approach (agreed with the user, 2026-10-11; point 3 as the user specified it).**

1. **An arena mode in the game executable**, in its own source files and independent of the traffic and vehicle fixture code, so that the real renderer, lighting and atlases are tested. The traffic and vehicle fixtures stay as they are.
2. **An empty, generic test floor**: no city, traffic, pedestrians, police, missions or HUD messages. A one-metre grid and a scale bar can be switched on, and off for clean captures. The time of day is set by the test and the clock stands still; a street lamp can be placed on request.
3. **Faster start**: the arena skips building the city and planning the turn paths (about 1.6 s of the 6 s start of 2026-10-11); all assets still load as in the game.
4. **Tests are data files, kept apart from the code** (`tests/arena/*.cfg`, not shipped in the release): the subject, the time of day, the weapon, timed steps (walk, run, strafe, aim, fire, reload, melee and so on), named captures at a simulated time, and expected ranges (`EXPECT <metric> <min> <max>`). A new test needs no rebuild.
5. **Reproducible runs**: a fixed time step and seed, independent of the frame rate; captures at simulated times; the run may go faster than real time, since only the captures have to be drawn.
6. **Output**: one `ARENA metric … result=PASS/FAIL` line per expectation and a summary line; a failed expectation or an invalid test file gives a non-zero exit code and a clear message; only the named captures are saved, and a small tool lays them out on one contact sheet.
7. **A manual arena** for the user's own checks: the same floor with keys to step the time of day, change weapons, switch the grid, and pause or slow down time.
8. **Retired scenarios**: `player`, `player:<weapon>` and `armed:night` are removed once the arena covers them; `armed` stays, to start the real game with every weapon.

**Acceptance criteria (proposed; confirmed and measured before the change at the start of the session).**

| Criterion | Target |
|---|---|
| Arena contents | The log shows 0 vehicles, 0 buildings and only the people a test asks for |
| Reproducibility | Two runs of the same test give identical metric lines |
| Start to the first arena frame | At least 1.5 s faster than `foot` |
| A single-weapon player test (pistol) | At most a third of the wall time of today's `player:Pistol` run |
| The CJ-011 values reproduced in the arena | Walk 1.45–1.55 m/s, run 6.45–6.55 m/s, raise at most 0.2 s, lowered 0.8–1.0 s after the last shot, gait match at least 87 % |
| Time of day | Changing the hour changes the light only, not the metric lines |
| Failure reporting | A failed expectation exits with a non-zero code; an invalid test file names the file, the line and the problem |
| Captures | Only the named captures are saved; the contact sheet shows every one of them, labelled |
| Release | `tests/` is not in the release zip |

The testing guide gets an arena section with the test file format and the rule: features are tested in the arena; the real city is for integration and FPS runs and the user's playtest.

**The user's requests of 2026-10-12 (watching the [CJ-040](#cj-040-car-radio) radio scenes).**

- The test floor is to show only what the test needs: the empty asphalt showed the city's lane markings, the HUD's map and other things the test did not use. The radio scenes now draw the world without the HUD and with their own panel, and the empty test floor draws no markings for every fixture; the arena is to make this the rule for every test.
- The checks must be understandable while they run: what the scene tests, what is expected, and the measured value beside it, so that the test situations can be reviewed quickly and well.
- After a change, only the tests the change can affect are run, enough to verify its quality, correctness and implementation; scenarios the change cannot have touched are not run again.

### CJ-033 Code review fixes

- **Priority:** High
- **Status:** Implemented on 2026-10-11, awaiting playtest

**Background.** On 2026-10-11 the user asked for a review of the whole code for bugs, optimisation opportunities and dead code, and chose to fix the bugs first. The dead code and simplifications are [CJ-036](#cj-036-dead-code-and-simplification); the audio's use of the gameplay random numbers is [CJ-037](#cj-037-audio-randomness-apart-from-gameplay). CPU time is no bottleneck (about 1.7 ms a frame on foot, 2.8 ms in `derby`), so no optimisation was made. Two bugs the user reported during the session are included: the police after a respawn, and the old startup frame (moved to [CJ-034](#cj-034-no-fallback-content), which removes every fallback).

**Fixes.**

| Bug | Cause | Fix |
|---|---|---|
| After WASTED or BUSTED the police came back with sirens (user, 2026-10-11) | A car the player set on fire burns for 3–5 s of game time, but the four-second slow motion lasts only 1.4 s of it: the car exploded after the respawn and counted as the player's crime (+0.8 heat, +$150) | The respawn clears every vehicle's `hitByPlayer` |
| The population grew beyond 300 for good | A carjacked driver, a driver out of a burning car or a thrown rider is a new person; the population manager moved far-away people back near the player instead of removing the extras | Extras are removed when they would be moved back |
| Changing weapons during a reload | The reload stopped but its animation played on with the new weapon; the number key of the weapon in hand cancelled the reload | `Game::SelectWeapon`: another weapon stops the reload and its animation, the weapon in hand does nothing; getting into a vehicle stops it the same way |
| `TIMING` in `title` showed 1,088 ms | The world drawing was summed over the drawn frames but divided by the frames of play (none on the title screen) | Divided by the drawn frames |
| An incident's end reason | Both branches of a condition gave `car_lost` | One reason |

**Verification (2026-10-11).** Temporary measurement builds (not committed) against the code before and after:

| Measurement | Before | After |
|---|---|---|
| 50 extra people placed 115–140 m away: people after 2 and 600 frames | 350, 350 | 300, 300 |
| Switching from a pistol reload to the rifle: frames still showing the reload | 54 of 54 | 0 |
| Pressing the pistol's key during its reload: clip afterwards | 2 of 12 (cancelled) | 12 of 12 |
| `chase`, a police car set on fire by the player 10 frames before WASTED: heat after the respawn | 0 → 0.8, police back with sirens | 0, no sirens |
| `title` 300 frames: `TIMING` world drawing | 1,088 ms | 3.3 ms |

`foot`, `day`, `derby` (1,500 frames), `player:Shotgun` (1,200) and `title` (300) produce identical metric lines and startup digest before and after, apart from CPU times. Then the user's playtest: changing weapons during a reload, and dying or being arrested in a chase after setting a car on fire.

### CJ-035 Vehicles hitting people

- **Priority:** High
- **Status:** Implemented on 2026-10-11, awaiting playtest

**Problem (playtest, 2026-10-11).** The player can hardly be run over: cars push the player along, even at speed. A person hit by a vehicle should, depending on its speed and size, be pushed aside, knocked backwards or thrown some way, and fall. A car driving over someone lying on the road, alive or dead, should go over them, not push them along. The user wants this realistic and well thought out.

**Causes in the code (`src/game.cpp`).**

- The player on foot (`UpdatePlayerOnFoot`) is only pushed out of an overlapping vehicle's box. Above a closing speed of 110 px/s (about 25 km/h) every frame of contact costs (speed − 80) × 0.12 health and adds a 120 px/s shove, which the walking control removes within a fraction of a second. The player never falls or lies down, the car loses no speed, and below 25 km/h, as with traffic braking for the player or police in pursuit, the player is simply pushed along.
- A civilian (`VehiclePedCollisions`) hit above 95 px/s (about 21 km/h) is knocked down with 45 % of the car's velocity plus 50 px/s and slides on the ground: about 4 m ahead of a car at 100 km/h, so the car seems to push the body. A person lying down but alive who is driven over is knocked down again with the car's direction at up to 260 px/s and slides on until they die.
- The player has no fall, lying or getting-up frames; civilians have one lying frame.

**Measured data.** The throw distance runs from the point of impact to where the body comes to rest.

| Source | What it gives |
|---|---|
| Happer et al. (2000), 106 real collisions, via the [THUMS validation](https://dynalook.com/conferences/11th-european-ls-dyna-conference/pedestrian-safety-head-impact/validation-of-thums-human-model-throw-distance-in-pedestrian-accident-scenarios) | Impact speed from the throw distance *d*: cars V = 12.7 √d − 2.6 km/h (±9 km/h), SUVs and light trucks V = 11.4 √d − 0.4 km/h (±10.5 km/h) |
| Searle's model (same source) | A body launched at speed *v* comes to rest between v²/(2μg) and (1 + μ²) v²/(2μg) away, depending on the launch angle; μ ≈ 0.7 on a dry road |
| [Tefft (2011), AAA Foundation](https://aaafoundation.org/impact-speed-pedestrians-risk-severe-injury-death/): US crashes, all ages, cars and light trucks | Death: 10, 25, 50, 75 and 90 % at 37, 51, 68, 80 and 93 km/h; severe injury: the same shares at 26, 37, 50, 63 and 74 km/h |
| [Rosén, Stigson and Sander (2011)](https://swov.nl/en/publicatie/literature-review-pedestrian-fatality-risk-function-car-impact-speed): German crashes, cars, adults | Half die at 77 km/h; less lethal than Tefft, who includes light trucks and older people |
| Run-over studies ([pedestrian deaths](https://tandf.figshare.com/articles/journal_contribution/Injury_patterns_in_motor_vehicle_collision-pediatric_pedestrian_deaths/21235749), [French national data](https://trid.trb.org/View/1488036), [NHTSA](https://www-nrd.nhtsa.dot.gov/pdf/ESV/Proceedings/20/07-0440-W.pdf)) | Being driven over is 2–10 % of pedestrian casualties, mostly below 30 km/h, with severe head injuries; in collisions with trucks about half the victims are driven over |
| Balance recovery ([threshold study](https://media.isbweb.org/images/conf/2005/abstracts/0430.pdf), [older adults](https://cris.bgu.ac.il/en/publications/characteristics-of-first-recovery-step-response-following-unexpec/)) | Whether a push is caught by stepping depends on its position and speed; older people step at smaller pushes. No single figure, so the balance limit below is a design value |

**Specification (agreed with the user, 2026-10-11).**

1. **A contact is one impact, never a push.** A person's legs cannot keep up with a vehicle: in real life a contact lasts a fraction of a second, and then the person staggers on their own feet, falls or is thrown. The game resolves every contact between a vehicle moving towards a person and that person once, as an impact; afterwards both move on their own. A person who walks into a vehicle that is not moving towards them is stopped by it, as by a wall.
2. **Standing people.** Momentum is conserved: the person takes up to the share m<sub>v</sub>/(m<sub>v</sub> + m<sub>p</sub>) of the closing speed (m<sub>p</sub> = 75 kg), and the vehicle loses what the person gains.
   - Below the person's balance limit (1.0–1.6 m/s, about 4–6 km/h) they stagger and step aside, unhurt: a civilian turns and steps back and aside at once, faster than the vehicle comes on. Another contact while they still stagger (0.8 s) knocks them down.
   - Above it they are launched as in Searle's model: flight, a friction impulse on landing, then sliding at μ = 0.7. Since the playtest of 2026-10-12 a body travels an eighth as far as in real collisions, for play ([ADR-0012](adr/0012-shorter-throws-for-play.md)); the rest of this point describes the real model the eighth is taken of. A front edge below the person's centre of mass (1.0 m) wraps them up onto the vehicle: a 25° launch with an impact factor of 0.94 (the share of the speed the body keeps after the wrap). A higher, flat front projects them forward: 10° and 1.0. These values put the throw inside both Happer's and Searle's bands at every speed. They also give the real-life outcomes when the vehicle does not brake: the body lands in front of it and is driven over at lower speeds, while from about 80 km/h it flies over a car, as in a roof vault. The impulse acts along the contact normal, so a corner hit throws the person to the side of the vehicle's path. A bus or truck drives over the person it hit at any speed. A person's speed change below 25 km/h is a fall, above it a throw.
   - A body in the air touches no vehicle.
3. **People on the ground.** A vehicle drives over anyone lying on the road: it neither pushes them nor stops. A living person dies. The body moves 0.2 m back under the vehicle, and the vehicle loses 1 km/h (a jolt; in the player's vehicle the camera shakes).
4. **Injury.** One function for the player and civilians: damage = 100 · e<sup>β(Δv − Δv<sub>50</sub>)</sup> · u / (1 − u), with *u* uniform on (0, 1) and Δv the person's speed change. The chance of reaching 100 points, death, is then exactly a logistic curve; Δv<sub>50</sub> = 59.5 km/h and β = 0.089 per km/h reproduce Tefft's death curve over impact speed (68 km/h for half the deaths) for a 1.1 t car, and heavier vehicles hurt a little more. The share losing at least 25 points then follows Tefft's severe-injury curve within 4 km/h. Armour absorbs damage as it does today.
5. **Getting up.** After the body comes to rest, a person lies 1.2–2.0 s after a fall and 2.7–3.5 s after a throw, then gets up. The player's getting up is an animation of 1.4 s, after which the player is controlled again; civilians use the same lying times.
6. **Player art.** Fall, lying and getting-up frames made with the [CJ-011](#cj-011-relaxed-player-posture) pipeline from the Universal Animation Library clips `Hit_Knockback` and `LayToIdle` (CC0, already downloaded). Lying, the player is drawn at real size, as the civilians are; the weapon is not drawn while on the ground. A player who dies of any cause falls the same way.
7. **Data.** Every value is in a new `assets/data/impacts.cfg`. Every vehicle class declares the height of its front edge with a `FRONT <class> <m>` record in `vehicles.cfg`; a class without one stops loading. People knocked down by blows and explosions slide as far as they do today under the new friction. A rider thrown off a motorbike is launched the same way.

**Acceptance criteria (agreed with the user, 2026-10-11).**

| # | Criterion | Target |
|---|---|---|
| 1 | A bump at 2 km/h | The person staggers, unhurt, and recovers their balance within 1 s |
| 2 | 10 km/h | They fall |
| 3 | Throw distance, a vehicle braking after the impact, 15–100 km/h | Within Happer's band (low fronts the car regression ±9 km/h, high fronts the light-truck regression ±10.5 km/h) and within Searle's corridor for the person's launch speed (μ = 0.7); since 2026-10-12 an eighth of Happer's band ([ADR-0012](adr/0012-shorter-throws-for-play.md)) |
| 4 | Momentum | The vehicle's loss of momentum equals the person's gain within 10 % |
| 5 | Injury | Death share per impact speed, 4,000 draws each from 10 to 100 km/h, within 3 percentage points of Tefft's curve; one function for the player and civilians |
| 6 | No pushing | One contact lasts at most 0.1 s, and a person moves at most 0.3 m with a vehicle in contact, also with vehicles creeping at 2–5 km/h and vehicles that do not brake |
| 7 | No passing through | A living person and a vehicle overlap by at most 3 px for at most 2 consecutive frames, a body in the air excepted |
| 8 | Driving over a living person | They die; the body moves at most 0.3 m; the vehicle does not get stuck and loses at most 2 km/h |
| 9 | Driving over a body | It moves at most 0.3 m; the vehicle loses at most 1 km/h |
| 10 | Player | Falls at 10 km/h and more, lies, gets up (or dies), and is not controlled meanwhile; controlled again 2.5–3.5 s after coming to rest from a fall, 4–5 s after a throw |
| 11 | City | The [CJ-010](#cj-010-pedestrian-behaviour) metrics in `foot`, `day` and `rampage` still met; pedestrian CPU time at most 0.5 ms |
| 12 | Art | Falling, lying and getting up checked from above, the front and the side, in natural poses |
| 13 | Data | Every value in a data file; a new vehicle class is covered by the test without changing it |

Then the user's playtest.

**Test scenario (decided with the user, 2026-10-11).** CJ-035 gets its own isolated test instead of waiting for the [CJ-032](#cj-032-isolated-test-arena) arena, and the user asked for a reusable one: vehicles and people, one or many, set up by data. `--scenario person-hits` runs the scenes of a test file (`tests/person-hits.cfg`, not shipped) on the empty test road, with no traffic, other people, police or missions, a fixed time step and seed, by day. A scene places vehicles (a class, or `all` for one run per class in `vehicles.cfg`, at a speed, braking or keeping on after the first contact) and people (standing, lying, dead or the player, singly or in rows, or a random crowd), and names its checks; a new scene needs no rebuild. The CJ-035 scenes cover every class at 15–100 km/h braking, bumps at 2 and 10 km/h, creeping and non-braking vehicles, people lying alive and dead, the player and an injury sweep. [CJ-038](#cj-038-pedestrian-attention-and-evasion) adds crowds. The before values come from a run of the test on the unchanged model.

**Changes during the work (2026-10-11).** Each followed from a measurement or from the user's request:

- The stagger: bumps that added up until the person recovered let a creeping vehicle stay in touch for up to 1.7 s, so the agreed rule stands (another contact within 0.8 s knocks them down). The user asked that a person pushed by a vehicle step aside or else fall beside it; a civilian therefore turns and steps back and aside at once, faster than the vehicle comes on.
- A body driven over moved 0.2 m forward, and a vehicle creeping at 2 km/h caught it again and again, pushing it along; it now moves back, under the vehicle, which passes over it once.
- The injury curve is fitted for a 1.1 t car, the test's taxi, instead of a 1.5 t one: within 1.5 instead of 2.9 percentage points of Tefft.
- The player's getting up, `LayToIdle`, takes 1.4 s, so the lying times are 0.4 s shorter, keeping the agreed times until the player is controlled again.
- The contact step skipped people at the front and the corners of long vehicles for a frame or two (its bounding radius was half the length plus 4 px); it is now the half diagonal plus a body.

**Playtest (2026-10-12).** The user saw people hit at speed lie down and die at once, then fly and slide about ten metres. The distance is real: Searle's model and staged tests (Becker and Reade) give a quarter of the throw along the road after the landing, about 10 m at 80 km/h. How it looked was not: civilians had no falling frames, so they showed the lying frame from the moment of impact and glided along the road in it. The user chose to keep the real distances and fix the look. Every civilian look now falls as the player does (`Hit_Knockback`, eight frames, whose last pose is the new lying frame), the fall plays over the whole flight for civilians and the player, and a body on the road tumbles, turning 40° per metre, until it comes to rest. Two checks were added to the [person-hit scenes](testing.md#person-hit-scenes): nobody is drawn lying in the air, and a sliding body turns at least 20° per metre; all 2,170 checks pass.

**Playtest (2026-10-12, second).** The user found the throws far too long: in their view a person hit at 80 km/h flies about 4 m and slides about 1 m. The game's metres were checked (the speedometer and the world share 16 px per metre) and the measurements shown: real throws at 80 km/h are about 40 m (Happer; the Forensic Science Service's rule of thumb; staged impacts at 61 and 65 km/h threw a dummy 24.7 and 26.0 m). The user chose shorter throws for play: an eighth of a real throw (`THROW 0.125`, [ADR-0012](adr/0012-shorter-throws-for-play.md)), with the real injury. The constant 25° launch had also flown fast bodies unrealistically high (5.7 m at 100 km/h, 22 m at 200 km/h); at an eighth they stay below 1 m up to 100 km/h. Bodies slower than the vehicle that hit them needed two more rules: only the wheels drive over someone lying, and someone lying under a vehicle waits for it or crawls out. All 2,077 checks pass.

**Result (2026-10-11).** All 1,823 checks of the [person-hit scenes](testing.md#person-hit-scenes) pass, against 762 of 1,813 failing before: throws within the published bands for every class and speed, the injury within 1.5 percentage points of Tefft, no contact longer than 0.05 s and no carry further than 0.06 m, everyone driven over dead, the player falling and up again on time. The CJ-010 metrics hold in `foot`, `day` and `rampage` (77 % escaped). The design is in [Vehicles hitting people](design/impacts.md) and [ADR-0011](adr/0011-vehicle-person-impact.md); the art in [CJ-011 player art](../assets/art/cj011/README.md#on-the-ground). Then the user's playtest.

### CJ-036 Dead code and simplification

- **Priority:** Low
- **Status:** Open

**Findings of the 2026-10-11 review ([CJ-033](#cj-033-code-review-fixes)).** None of these changes the game; each change should leave every scenario's metric lines identical.

- Functions nothing calls: `CityMap::GroundHeight`, `CityMap::RailOverRoad`, `CityMap::RandomRoadPoint`, `OBBContactPoint`, `Game::ViewRect`, `startup::OutcomeName`.
- Unused constants and fields: `cfg::H_CAR`, `cfg::H_DECAL`; `PlayerState::triggerHeld`, `enterTimer`, `enterTarget`; `Mission::wantedClass`; `DriverAI::creep`; `DriverAI::jammed` (always 0, only logged); `RecoveryState::reverseDistance` and `lastProgress` and `Pedestrian::hitByPlayer` (written, never read); `Game::statAbandons` (never counted, so the `PHYS` line's "drivers gave up" is always 0).
- Dead branches: drawing of the `DECAL_OIL` and `DECAL_BULLET` decals (nothing creates them); the signal branch of `CityMap::DrawLights`; an empty planter block in `DrawSprites`; a `t * 0.0f`; the composite shader's unused `time` uniform.
- Parameters silenced with `(void)`: `DamageVehicle`'s `at`, `Crime`'s `where`, `StartMission`'s `phone`, `UpdateSpawning`'s `dt`, `VehiclePhysics::Solve`'s `h`, `CityMap::DrawLights`' `t`, and a stale `(void)dt`.
- Simplifications: the six measurement fixtures share one interface, but `main.cpp` dispatches them in seven `if`/`else` chains; `Finite`, `SkinOf`, `ConfigHash` and `Penetration` are copied between fixture files; `PED_RADIUS` repeats 16 and 1.35 by hand ("keep in sync") instead of using `cfg::M` and `cfg::CHAR_SCALE`; `Rotate` exists twice; six local variables shadow others (`-Wshadow`).

### CJ-037 Audio randomness apart from gameplay

- **Priority:** Medium
- **Status:** Open

**Problem (review, 2026-10-11).** `AudioSystem::Play` varies each sound's pitch with the gameplay random number generator (`GRng`), but only when an audio device is open and the sound is within hearing. A scenario or fixture therefore draws a different random sequence on a machine without sound, so runs are not reproducible across machines ([CJ-032](#cj-032-isolated-test-arena) asks for identical runs).

**Approach.** A separate generator for audio variation. Every scenario's random sequence moves once, so the recorded baselines and fixture results are measured again in the same change.

### CJ-038 Pedestrian attention and evasion

- **Priority:** High
- **Status:** Open; after [CJ-035](#cj-035-vehicles-hitting-people)

**Problem (test runs, 2026-10-11).** In `rampage` the car drives into people at 50 km/h and nearly everyone gets away: 85 % of the 141 people in its path escaped on 2026-10-11. In real life not everyone notices a car, and not everyone who does can get out of its way. Cause: everyone looks at the traffic in every direction ten times a second, reacts within 0.18–0.45 s and jumps sideways at 5.5 m/s from a standstill (`src/pedestrian.cpp`).

**Measured data.**

- About 30 % of pedestrians crossing at junctions are distracted: music 11 %, texting 7 %, a phone call 6 %, or talking to someone; people texting are 3.9 times as likely to cross unsafely, for example without looking ([Thompson et al. 2013](https://injuryprevention.bmj.com/content/early/2012/12/06/injuryprev-2012-040601.abstract)).
- In a virtual-reality study, 70 % of attentive participants avoided an approaching vehicle; those who were hit decided later (0.41 s against 0.17–0.24 s), and carrying out an avoidance took 1.5–1.8 s ([Scientific Reports 2021](https://doaj.org/article/075b455608484b35865b855acb9f3542)).
- Older people start avoiding later ([Université Laval](https://cirris.ulaval.ca/en/seniors-at-higher-risk-of-pedestrian-collisions)).

**Approach (proposed, 2026-10-11).** Personal traits drawn when a person appears, from data in `civilians.cfg`: attention (about 30 % distracted, noticing vehicles late; people listening to music do not hear a vehicle behind them), a reaction-time spread, an evasive step limited by acceleration instead of an instant jump, mobility by look (older looks slower), and a small share who freeze for a moment. The [CJ-035](#cj-035-vehicles-hitting-people) balance limit becomes one of these traits.

**Target (proposed).** In `rampage` 50–65 % of the people in the car's path escape; this replaces the [CJ-010](#cj-010-pedestrian-behaviour) target of at least 70 %. Traffic still hits nobody in `foot` and `day`. Measured with crowd scenes in the CJ-035 test file and in `rampage`, then the user's playtest.

### CJ-039 Varied falls and lying poses

- **Priority:** Medium
- **Status:** Open; after [CJ-035](#cj-035-vehicles-hitting-people)

**Problem (user request, 2026-10-12).** Since [CJ-035](#cj-035-vehicles-hitting-people) everyone knocked down falls the same way, as the Universal Animation Library clip `Hit_Knockback` does, tumbles along the road by turning the whole sprite, and comes to rest in its last pose: on the back, the head away from the vehicle. The dead always lie in that one pose. The user would like falls and hits by a vehicle, and rolling on the ground, from Mixamo animations, and people coming to rest in different poses: on the back, face down, on the side.

**Approach (to agree).** Mixamo clips for falling, being hit by a vehicle and rolling on the ground, downloaded by the user (In Place, 30 fps, without skin) to `build/art-sources/mixamo/`, as for the [CJ-011](#cj-011-relaxed-player-posture) strafes, and retargeted by `tools/cj004/retarget_ual.py`. The civilian and player atlases get several falls and lying poses; the game picks one by the impact (a fall, a throw, from the front, the side or behind) and at random, and a body on the ground rolls with frames instead of turning the sprite. The clips, frame counts, the atlas layout, the data records and measurable criteria are agreed at the start of the session; the poses are checked from the top, the front and the side, as for every pose.

### CJ-040 Car radio

- **Priority:** High
- **Status:** Implemented on 2026-10-12, awaiting playtest

**Request (2026-10-12).** The user made the first radio station, 1980s American rock, with Suno. Getting into a car, the radio plays; later the player can switch between stations of different kinds (rock, pop, house, rap and so on). Between the songs comes the DJ's talk ("habla babla"), varied, with little repetition: never the same song twice in a row, never talk after talk. A car passing close by plays its music as from a real closed car: louder and quieter with the distance, with a completely different tone. The files are to be renamed sensibly.

**Specification (agreed with the user, 2026-10-12).** The user chose the name Asphalt Rock FM, a live broadcast (the radio joins where the station is), the radio stopping when the player gets out, and a separate worktree while another session works on CJ-038.

1. The radio plays when the player gets into a car and shows the station's name for 3 s; Q or the mouse wheel steps through the stations and off; the pause menu pauses the broadcast.
2. Stations broadcast live; every car on a station plays the same moment.
3. Programme: at most one talk clip between two songs, never two in a row; a song comes back only after 5 others, a clip after 3; a clip after about 65 % of the songs; its own random generator.
4. Every clip at the same loudness by a gain in the data file, without re-encoding.
5. About 40 % of traffic plays a radio, some loud, never police, ambulances, fire engines or motorbikes; heard through the car body (the bass through, 1 kHz and up at least 20 dB duller), 6 dB less per doubling of the distance, out to about 11 m (normal) and 45 m (loud), from its side, with the travel time and so the Doppler shift; through two bodies from inside a car; the four nearest per station; no occlusion by buildings.
6. Clips in `assets/radio/<station>/`, renamed by kind and number; every value in `assets/data/radio.cfg`.
7. The pan of every sound fixed for raylib 6.0 (found during the work: every sound leaned right, and left and right were swapped).

**Acceptance criteria and results (2026-10-12).** Measured by the [radio scenes](testing.md#radio-scenes).

| # | Criterion | Target | Result |
|---|---|---|---|
| 1 | Programme over 24 h, ten seeds | No breach of its rules; a clip after 55–75 % of the songs; every clip played | 7,951 clips: no two clips in a row, songs back after 5 others at least, clips after 3, 60.5–65.5 %, every clip played |
| 2 | Loudness | Every clip −16 ±1 LUFS | Files −16 ±0.05 after their gains (`tools/radio_gain.py --check`); talk clips in the mix −16.0 |
| 3 | Change of clip | At most 50 ms of silence | 0–16 ms |
| 4 | Passing cars | Level within 1.5 dB of the model; 1–4 kHz at least 20 dB duller than the player's radio, at least 8 dB more from a car; Doppler within 0.5 %; silent at 11 ±1 m and 45 ±3 m; the right side; no clicks | Level 0.35–0.56 dB (95 %); 26–32 dB duller, 25 dB more from a car; Doppler within 0.24 %; silent at 10.8–11.2 m and 42.1–42.7 m against 10.5 and 42.0 m where the model falls below the measured −60 dB; the right side always; no clicks |
| 5 | Gameplay unchanged | The metric lines of `foot`, `drive`, `crash`, `rampage` identical | Identical |
| 6 | Main-thread cost in `drive` | At most 0.2 ms per frame | 0.075 ms; 0.07–0.09 % of a core on the audio thread |
| 7 | The user's listening and playtest | Accepted | Open |

The design is in [Radio](design/radio.md) and [ADR-0014](adr/0014-live-radio-stations.md); the clips and their provenance in the [station's README](../assets/radio/asphalt_rock/README.md).

**Content of a station (the user's requirement, 2026-10-12).** Every station needs ten songs and ten talk clips that, mixed and ordered well, sound like a real station of its kind. The songs are instrumental, without vocals; the talk between them is not meaningful speech but American-sounding gibberish. Asphalt Rock FM has eight songs and six talk clips (three DJ, three commercials): two songs and four talk clips are still to come. With ten of each, its `PROGRAMME` gaps can be raised to about 6 and 5. The clips were made on a paid Suno plan, so the author owns them ([CREDITS](../CREDITS.md#cj-040-asphalt-rock-fm)).

**Playtest (2026-10-12).** The user did not hear the radio in a car. Two causes: the game in the main folder was another branch's build, without the radio (the game reads `assets/` from its working directory), and in the radio's own build the engine drowned it: recorded in `drive` (`CJ_RADIO_MIX`), the radio played at −27.8 LUFS against −17.5 LUFS of everything else. The user chose the balance of a real car: `INCAR 1.0` and the engine 10 dB quieter, heard from the cabin. In steady driving the radio now plays at −21.7 LUFS against −27.7 LUFS of the engine and the city, 6 dB above them; its peak stays at 0.5 before the master volume.

**Playtest (2026-10-13).** The user did not hear the talk between the songs. With whole songs of 2:20–3:19 and a clip after 65 % of them, every game opened with three minutes of music (the median) and ten minutes of radio held about two clips; the 3.5-minute recording of a drive ended before the first song did. The user chose a talk clip after every song, a broadcast joined at a random point when a game starts, and songs aired as a different 90 s part every time ([ADR-0015](adr/0015-radio-song-excerpts.md)). Criterion 1 is now a clip after every song; three were added and measured in the [radio scenes](testing.md#radio-scenes):

| # | Criterion | Target | Result |
|---|---|---|---|
| 1 | Programme over 24 h, ten seeds | A clip after every song, never two in a row; every song aired as its slice | 17,672 clips: a clip after 100 % of the songs, none in a row, every slice right, starting 0–100 % into its song; talk 7.9 % of the airtime |
| 8 | A slot's airtime | Within 50 ms | Exact to the millisecond for three slices and two talk clips |
| 9 | Fades | Flat within 0.5 dB between them, within 1 dB of their curves | 0.00 dB; 0.02–0.04 dB |
| 10 | Main thread at a change of clip | At most 5 ms in a frame | 4.1 ms (a seek on the main thread: 0.3–0.7 s) |

**Found during the work.** Saving a screenshot stalls the main thread for up to half a second, and a clip change that falls into it waits as long (491 ms in a run with `--every`, 0–16 ms without). The radio scenes are run without screenshots.

## Done

| ID | Title | Completed |
|---|---|---|
| CJ-001 | Documentation overhaul: documentation structure, design documents, guides, testing guide, ADRs, changelog, contributing guide | 2026-09-27 |
| — | Collision physics rewrite: vehicles no longer stick to obstacles or jitter; breakaway street furniture; traffic recovery after crashes ([ADR-0005](adr/0005-vehicle-contact-solver.md)) | 2026-09-27 |
| CJ-009 | Licence for the code: MIT licence for the project's own code (`LICENSE`); third-party assets keep the licences in [CREDITS](../CREDITS.md) | 2026-10-06 |
| CJ-016 | Human-like traffic and incidents: physical recovery without timeouts, cooperative yielding, wait-for cycles, persistent drivers and road-rage incidents ([details](#cj-016-road-rage-and-traffic-incidents)) | 2026-10-07 |
| CJ-020 | Turning kinematics of traffic: the rear axle traces the path, a turn path per class from its turning circle, turning speed from lateral acceleration ([details](#cj-020-turning-kinematics-of-traffic)) | 2026-10-07 |
| CJ-004 | Replace placeholder art: motorbikes from generated top-down art, 28 civilians rendered from 3D models ([details](#cj-004-replace-placeholder-art)) | 2026-10-08 |
| CJ-030 | Startup loading screen: approved city art, fixed status rows and confirmed progress that adapts to actual work ([details](#cj-030-startup-loading-screen)) | 2026-10-09 |
| CJ-034 | No fallback content: every data file and every file it names is required; the old startup frame, placeholder vehicles and trees, noise textures and the default font are gone ([details](#cj-034-no-fallback-content)) | 2026-10-11 |
| CJ-029 | Civilian gait realism: a real walker's stride with calm arms, as much leg ahead of the body as behind it, a jog and a run that reach further, the walk locked to the distance walked ([details](#cj-029-civilian-gait-realism)) | 2026-10-09 |

### CJ-034 No fallback content

- **Priority:** High
- **Status:** Done on 2026-10-11

**Problem (user, 2026-10-11).** Every start showed, for about 0.4 s, an old-looking loading screen: the title and status in raylib's pixel font on a dark background, drawn before the loading screen's art and fonts were loaded. The user does not want old fallbacks in the code, and decided that a missing file stops the game with a message instead.

**Result.** [ADR-0010](adr/0010-no-fallback-content.md): every data file in `assets/data/` and every file a record names is required; a missing or unreadable one, or a record naming an unknown vehicle class or colour, fails loading with a message naming it (exit code 2). The first frame is plain dark until the loading screen's art and fonts are loaded, and these are required too. Removed: the built-in copies of the data files, the procedural placeholder vehicles and trees with the `GEN` record, the noise textures, the fallback vehicle, trees, bushes and weapon, raylib's default font as the interface font, the loading screen's text-only presentation, and `tools/sprite_dump.cpp`, which only drew the removed placeholders.

**Verification (2026-10-11).** `foot`, `day`, `derby`, `player:Shotgun` and `title` give the same startup digest and metric lines as before; the only log changes are `vehicles.cfg` being read once instead of twice and one mipmap line moving. `--loading-test` passes (20 captures, zero errors). From a copy of `assets/` with one file removed at a time (`weapons.cfg`, `traffic.cfg`, `loading.cfg`, `grass.png`, a tree image, `Rajdhani-SemiBold.ttf`) and with a missing loading background, startup exits with code 2 and names the file; the complete copy starts. See [CJ-030 loading checks](testing/cj030-loading.md#required-files-since-2026-10-11).

### CJ-029 Civilian gait realism

- **Priority:** High
- **Status:** Done on 2026-10-09: the user playtested and accepted the revised gaits

**Problem (playtest, 2026-10-08).** Walking civilians reach too far forward with their legs, which does not look natural. The user also wants the movement and the animation frames to match. The library walk lifts the thigh 50° forward (a casual walk about 30°), so the toes land 0.46 m ahead of the hips on average. One eight-frame walk loop at 1.3 m per cycle serves every look and every walking speed, so the feet of short and tall people slip, and a hurried walk at 2–3 m/s needs four to five steps a second. The game also turns the sprite by up to 0.05 rad with the steps, on top of the sway the rendered walk already has.

**Agreed specification.**

| Gait | Speed | Frames | Source |
|---|---|---|---|
| Walk | Up to 2.0 m/s | 16 | `Walk_Formal_Loop`, thigh forward swing remapped to about 30° |
| Jog | 2.0–4.0 m/s | 8 | `Jog_Fwd_Loop`, upper body upright like the run |
| Run | From 4.0 m/s | 8 | `Sprint_Loop`, as now |

- Each look's stride per gait is measured from its own animation (the travel of the planted foot per cycle) and written on its `CIVILIAN` line in `civilians.cfg`; the frames advance with the distance walked.
- `civilians.cfg` declares the atlas layout (which frames hold which animation), so the longer walk and the jog need no code constants; the procedural atlas keeps its layout.
- The game's own step sway is switched off for the rendered atlases (a `civilians.cfg` value).

**Acceptance criteria.** Per look: walk thigh forward swing at most 30°; a planted foot moves at most 2 cm while it is on the ground, simulated with the frames advanced by the distance as in the game; cycles closed, pelvis at the frame centre. Cadence within realistic bands at the speeds where each gait shows (walk 1.6–2.8 steps/s at 1.2–2.0 m/s, jog 2.4–3.4 at 2.0–4.0 m/s, run 2.8–3.8 at 4.0–5.7 m/s). The idle, punches, fist and lying frames and every other CJ-004 criterion unchanged. Before/after: the `civilians`, `day`, `rampage` and `brawl` scenarios (pedestrian metrics unchanged, FPS medians of alternating runs); then the user's playtest.

**Refinements during the work (agreed with the user, 2026-10-08).** Turning only the thighs shortened the forward reach but left the trailing leg 0.5 m behind: from above, a walking person's feet spread like the splits (0.87 m from the front toe to the back foot in the library walk, 1.4 m in the run). The user chose, from three variants shown in motion, a compact stride: every leg joint moves 0.65 (walk), 0.55 (jog) and 0.5 (run) of the library's range from standing straight, the walking hips a little further back. A shorter stride needs a quicker step to keep the feet planted, so the walk's cadence follows from each look's stride (about 2.9 steps/s for a man at 1.5 m/s, as the user accepted) instead of the agreed bands; the thigh limit became the measure of the actual complaint: toe at most 0.30 m ahead of the hips and at most 0.70 m from front toe to back foot. The jog and run play by cadence as agreed.

**Result (2026-10-08).** All 28 looks pass: walk stride 0.92–1.10 m for men and 0.82–0.91 m for women, measured per look and written to `civilians.cfg`; a planted foot moves at most 1.7 cm; toe at most 0.29 m ahead, spread at most 0.67 m (was 0.87 m); jog and run feet touch the ground for one frame at most, so nothing slips. Atlases hold 38 frames (walk 16, jog 8, run 8) in the layout that `civilians.cfg` declares; at load every frame is cut to its visible part and packed, 25.9 MB of video memory instead of 84.2 MB before mipmaps, and the procedural fallback draws exactly as before. The `traffic-incident` fixture, which now asks for the drawn pose instead of a frame number, passes 80 of 80 cases. FPS medians of alternating runs against the previous build: `day` −1.6 %, `brawl` +2.1 %, `rampage` +1.8 %; the `PEDS` lines are identical. The user's playtest of 2026-10-09 asked for the revision below.

**Playtest and revision (2026-10-09).** The user's playtest found the walking legs reaching far behind and hardly ahead (measured on the frames: 0.00–0.05 m ahead of the body and 0.26–0.37 m behind; running, none ahead and 0.38–0.51 m behind), the walk's rhythm nervous, many short steps for little progress, the walking arms swinging too much, and the run looking more like a walk than the walk itself. Agreed with the user from variants shown in motion:

- A real walker's stride: 1.8–2.2 steps/s at 1.3 m/s for a 1.78 m person (shorter people step shorter and so more often); the knee lifted at most 30°; a planted foot still moves at most 2 cm.
- Walking arms swing half as much as the library's and hang about as close as standing.
- In every gait as much leg shows ahead of the body as behind it (0.75–1.33); walking, at most 0.35 m for a 1.78 m person, in proportion to height.
- The jog keeps 0.85 and the run all of the library's leg motion, so the limbs reach further the faster the gait; the runner leans about 23° and its hands travel at least twice as far as the walker's.

The compact stride of 2026-10-08 is dropped. The stride is now measured on the foot that carries the weight over the whole cycle; the earlier measure followed only the flat foot and overstated the stride (civilian-01: 1.03 m against 0.92 m), so the feet slid a few centimetres a step. The settings and tools are described under [civilian gaits](../assets/art/cj004/README.md#gaits-cj-029).

**Result (2026-10-09).** All 28 looks pass. Walk stride 1.19–1.38 m for men and 1.11–1.26 m for women (1.89–2.18 and 2.07–2.35 steps/s at 1.3 m/s), the planted foot moving at most 1.0 cm; the knee lifted at most 28.8°; the legs showing 0.25–0.35 m ahead and 0.26–0.35 m behind walking, 0.27–0.35 m both ways jogging and 0.28–0.35 m running (ahead to behind 0.92–1.09); the runner leaning 20–24° with hands travelling 8.8–9.6 times as far as the walker's; the walker as wide as standing (±1 cm), the average man drawn 0.61 m wide walking (−6 % of the player). Since CJ-004 the right hand's fingers had stayed spread in every animated frame; they now relax. In the game nothing else changes: the `PEDS` lines of `day`, `brawl` and `rampage` are identical, the FPS medians of alternating runs move by −0.3 to +2.7 %, and the atlases take 24.8 MB of video memory (25.9 MB before). The user playtested and accepted it on 2026-10-09.

### CJ-030 Startup loading screen

- **Priority:** Medium
- **Status:** Done on 2026-10-09; implemented, reviewed and accepted by the user

**Problem.** Startup displayed three static messages on a plain dark background, without overall progress or current-content detail.

**Accepted result.** The final night-city background, upper-left ivory/amber wordmark and one overall amber bar accompany a fixed-size four-row status block: current task, useful detail with a qualified count, overall percentage and one recent completion. The static bar retains confirmed fill through stage changes. Subsystems publish real work into a frozen weighted plan; metadata and optional successful-startup timing history adapt budgets, while actual local plans supply counts and actual completion controls screen duration. See the [loading screen specification](design/loading-screen-proposal.md) and [art brief](design/loading-screen-art-brief.md).

**Safety and compatibility.** Original initialization order and RNG are preserved. Failure or cancellation prevents title handoff and unwinds partial resources. Held Enter/Space rearm independently on release. The approved [background source](images/cj030-loading-background.png) and runtime copy have identical bytes; the [asset record](design/loading-background.md) preserves its dimensions, checksum and provenance.

**Verification and acceptance.** [Loading measurements](testing/cj030-loading.md) record 49 passing model cases, 20 presentation captures, six partial stop checks, fallback runs and five alternating startup pairs with exact initialized state and title pixels. Day/drive captures also match. On 2026-10-09 the user accepted the implemented result and requested closure, cleanup and integration into `main`. Device-specific unmeasured cases remain documented limitations, rather than pending user acceptance.

### CJ-016 Road rage and traffic incidents

- **Priority:** High
- **Status:** Done on 2026-10-07; specification approved on 2026-10-05; three increments (2026-10-05, 2026-10-06); playtested and accepted on 2026-10-07

**User direction (2026-10-03).** Drivers should behave like people: follow, yield, avoid, reverse, honk, sometimes misjudge and collide, and occasionally get out to confront or fight another driver. A blocked situation must not be solved by deleting a driver, forcing them to walk away after a timer, or relocating an involved vehicle. This is the current priority; CJ-002 supplies the necessary vehicle capabilities.

**Pre-change problem.** `UpdateKnocked` gave up after 12 s or below 35 % vehicle health; it spawned an unrelated pedestrian on-screen or relocated the vehicle off-screen. Recovery alternated controls towards one target without validating a full escape path. Pedestrian fighting still targets the player, so a driver-versus-driver incident also needs persistent ownership and explicit actor targeting.

**Approved specification.** [Human-like traffic and incident proposal](design/traffic-behaviour-proposal.md): predicted occupancy, feasible swept-footprint manoeuvres, stable yielding roles, impossible-blockage handling, persistent drivers, cause-based incidents, reusable spatial queries and measured CPU budgets. [ADR-0008](adr/0008-human-like-traffic.md) is Proposed; the accepted rail decision remains in force until a tested replacement addresses its original jitter/deadlock concerns.

**First increment (2026-10-05).** Frozen `traffic-recovery` fixtures measure Taxi, Bus and BoxTruck in enclosed, free and reverse-escape geometry at 60 Hz and 20 Hz: 52 checks, 14,400 rendered frames and 240 s per class. The complete baseline had 14 failed checks per class; the corrected build passed all 156 recovery checks. A separate 26-check clearance regression reproduced four pre-fix failures caused by separated nearby boxes being falsely classified as initial contact, then passed every check after the fix. The [result report](design/traffic-recovery-results.md) preserves exact manifests/hashes, timings and failed intermediate attempts. Corrected garage recovery took 10.833/10.600 s for Taxi, 20.967/21.100 s for Bus and 13.767/13.900 s for BoxTruck at 60/20 Hz; enclosed cars stayed with their drivers for 60 s with no movement, overlap, blend or ownership loss. All six matching city runs completed and recorded zero recovery give-ups; crash/chase each recorded one rejoin and rampage recorded eleven. The crash/rampage CPU failures remain open in the report. The controller uses a shared snapshot and at most 20 physical forward/reverse/hold candidates, with safe progressing lane feedback skipping escape search. Path scanning no longer ignores a mutually blocking vehicle; ordinary rail passing and junction right-of-way rules remain. Full-population CPU acceptance, city-edge/moving/multi-seed fixtures and user playtest remain pending. Cooperative conflict roles/reservations, persistent on-foot ownership and fights remain to be implemented; CJ-002 remains an explicit handling dependency.

**Second increment (2026-10-06).** Recovery planning is sliced across frames under a shared deterministic step budget; holds replan only when a blocker moves; followers behind a car keep their own distance (the rear-end rule) and rail cars are forecast no further than their planned stop; immediate checks validate a few extra frames and are reused while everything moves as forecast. The worst city decision frame fell from 11.6 ms to 2.5 ms; `crash` meets both decision targets (0.434 ms average, 0.933 ms p95), while `chase` (0.739 / 1.455 ms) and `rampage` (0.799 / 1.617 ms) still exceed them. Cooperative yielding gives two drivers stopped behind each other stable roles; the yielder retraces its own path on rails and tucks back into its lane, with a chain behind it. The new `traffic-conflict` fixture (60 cases) went from 6 to 60 resolved cases. Driver moods (10 % aggressive) and collision incidents let an aggressive driver stop, get out on a safe side, confront the other driver or the player, fight, and drive the same car on; interruptions end with a logged reason. The new `traffic-incident` fixture (80 cases) went from 20 to 80 accepted cases with no ownership violation or duplicate driver. Queue-aware rejoining, escape from an existing contact and a fixed U-turn route loss came out of the new fixtures. The frozen recovery and clearance fixtures still pass all 182 checks with unchanged rejoin times. Evidence: [yielding and incident report](design/traffic-yielding-incident-results.md).

**Third increment (2026-10-06), CPU.** Every city scenario now meets the decision CPU targets: `chase` 0.689 / 1.415 → 0.321 / 0.819 ms, `rampage` 0.727 / 1.547 → 0.378 / 0.861 ms, `crash` 0.390 / 0.865 → 0.234 / 0.484 ms (average / 95th percentile). The rail look-ahead tests only what can reach its sampled path, recovery rollouts let distant actors sleep and skip rail forecast boxes out of reach, and force steps share their trigonometry; these leave every decision unchanged, and all four fixtures reproduce the second increment line by line. Covered immediate checks now record up to 32 actors instead of 12, which changes city decisions. On-foot test runs no longer depend on the mouse cursor. Evidence: [third increment report](design/traffic-third-increment-results.md).

**Third increment (2026-10-06), wait-for cycles.** Two knocked cars blocking each other and loops of three or more drivers (junction gridlock) are found once per frame; one driver gives way to the driver waiting on it, a rail car by backing up, a knocked car by short checked creeps that make room. A car held at a stop line waits on the car in the box. The conflict fixture (v2, 100 cases) adds `knocked-pair` and `junction-gridlock`: 0 → 20 of 20 each, with the 60 earlier cases unchanged. The city runs had no loop lasting over 10 s; the "unresolved pair" at the end of `crash` is one that formed 0.3 s before the end.

**Third increment (2026-10-06), presentation.** Arguing drivers shout (three synthesised voices at a personal pitch, every second or so) and shake a raised fist (two new civilian atlas frames); the punch frame is kept for blows. Rail cars change lane by steering: the lateral offset follows an S-curve along the path, the rear axle traces it and the body points along it; a car standing too close behind an obstacle backs up before pulling out, and a yielding car reverses back into its lane the same way. Incident fixture v2: 40 → 80 of 80 (shouts 0 → 4–10 per case, raised fist 0 → 1.5–4.4 s, punch frame while arguing face to face 0.5–1.5 → 0 s); conflict fixture: lane-change slip 90° → 2.1°, 80 → 100 of 100. The slip measurement also found turning traffic sliding (CJ-020).

**Playtest (2026-10-07).** The user tested queues, two-car conflicts, heavy-vehicle reversing, a full confrontation and lane changes, and accepted them. Moved on: the police reaction to a fight to [CJ-018](#cj-018-police-driving-and-reactions), visible towing to [CJ-021](#cj-021-visible-towing). [ADR-0008](adr/0008-human-like-traffic.md) stays Proposed until visible drivers control the physical integrator.

**Acceptance criteria.** Follow the approved proposal's scenario table. Feasible blockages resolve physically; impossible ones remain stable and observable; timeout-based disappearance/relocation is zero; incident participants retain identity and car ownership; exiting, approaching, fighting and returning are visible actions with interruption rules. Validate with fixed seeds, 60 Hz/20 Hz scenarios, class-specific recovery tests, regression runs, separate traffic CPU timings and a user playtest. First deliver reliable manoeuvring, then incidents and combat; visible towing follows the accepted core.

### CJ-020 Turning kinematics of traffic

- **Priority:** Medium
- **Status:** Done on 2026-10-07; specification approved and implemented on 2026-10-07; playtested and accepted on 2026-10-07

**Problem.** Rail cars turn on a quadratic curve whose control point sits at the two lane offsets, so a right turn has a radius of about 2 m; real cars need about 5.5 m. Both axle samples stay on that curve, so the body follows the chord and the rear axle point slides sideways through the bend. `TRAFFIC slip` measured in `chase` and `day` on 2026-10-06: over 5° of rear-axle slip in 72–74 % of the turning time, up to 70–75°. Lane changes no longer slide (CJ-016); turns still do.

**Approved specification (2026-10-07).**

- **Pose rule.** In turns too, the rear axle follows the path and the body points along the path's tangent, so the front swings out, as in lane changes. Straight driving is unchanged.
- **Turn geometry per class.** Each class gets a kerb-to-kerb turning circle in `vehicles.cfg` (`TURN` records: cars about 11 m, the Bus 23 m); the tightest rear-axle radius follows from it. Turns are arcs with gradual steering in and out (clothoid transitions). A right turn uses the largest radius that keeps the inner kerb corner clear; a class that cannot turn that tightly (large vehicles) first moves towards the road centre and then takes the junction box alone. A left turn uses an arc of about 6 m; large vehicles start turning before the box.
- **Turning speed.** Limited so that the lateral acceleration at the rear axle stays within about 3.5 m/s² (`TURN lateral_accel` in `traffic.cfg`): about 14 km/h through a right turn and 17 km/h through a left turn, instead of about 29 km/h on a 2 m arc. A change to the feel for the playtest.
- **Out of scope.** U-turns in the middle of a block: a car cannot turn round in one move on an 8 m street. They take the new pose rule; a realistic three-point turn becomes a new backlog item.
- **Measurement.** An isolated `traffic-turns` fixture drives every traffic class right, left and straight through an empty junction at 60 Hz and 20 Hz and records slip, rear-axle radius, kerb intrusion, encroachment and lateral acceleration, with trace captures. A new city line counts rail cars overlapping each other.

**Acceptance criteria.**

- Every city scenario: rear-axle slip while turning at most 8°, at most 1 % of the turning time over 5°.
- Fixture, every class: slip at most 3°; rear-axle radius at least the class minimum; lateral acceleration within the limit; cars (all but `large`) at most 4 px (0.25 m) over the kerb and no encroachment into the oncoming half of a road; large vehicles at most 1.5 m of encroachment, only in right turns.
- No regression: straight and lane-change slip, the CJ-016 fixtures (recovery and clearance checks, 100/100 conflict cases, 80/80 incident cases), AI contacts, cars blocked over 3 s, wait cycles, rail overlaps, and the decision CPU targets.
- Before/after captures of turning traffic; playtested.

**Result (2026-10-07).** The rear axle traces the path in turns too, and every traffic class turns on its own [turn path](design/traffic.md#turn-paths). Turning fixture: 32 → 96 of 96 cases; slip 13–70° → at most 1.9°; a Taxi's right turn 1.8 → 3.6 m (class minimum 3.3 m) at 2.7 m/s² instead of 37 m/s². City: turning slip 70–75 % of the time over 5°, up to 76° → 0.0 %, at most 1.9° in all six scenarios; rail overlaps 1.65 → 0.33 pair-s. The CJ-016 fixtures pass. The user agreed these refinements during the work: the stop-line zone does not count as encroachment; wheels stay off the kerbs while large vehicles' overhangs may sweep a corner; over-wide cars take wide turns; turns that do not fit at all (Bus, Semi, the large trucks' right turns) are avoided; the `rampage` CPU stays above target as [CJ-024](#cj-024-recovery-cost-in-dense-traffic). Out of the work came [CJ-022](#cj-022-three-point-turns) and [CJ-023](#cj-023-vehicle-widths-and-street-geometry). Evidence: [turning results](design/traffic-turning-results.md).

**Playtest (2026-10-07).** The user found the turns good, with minor glitches that in their view only wider roads and junctions and a rethought sidewalk can fix; recorded in [CJ-023](#cj-023-vehicle-widths-and-street-geometry).

### CJ-004 Replace placeholder art

- **Priority:** High
- **Status:** Done on 2026-10-08; motorbikes and civilians playtested and accepted on 2026-10-08

**Art session (2026-10-07).** The user rejected all civilian sheets and earlier idle masters: idle must hide legs and shoes completely, with vertical hanging arms, compact integrated shoulders and ordinary clothing. V3 was rejected for enormous shoulder/upper-arm blobs; the v4 idle candidate awaits review, with no animations generated from it. The revised workflow uses the player's moderate detail and separate walk, run and action groups after master acceptance. The sportbike, chopper and scooter designs with and without riders are accepted, subject to correct size. Keep the [source art and historical prompts](../assets/art/cj004/README.md). Six vehicle PNG exports follow a shared facing, centre and size convention, with position and scale baked into the pixels and no per-image runtime correction configuration. Remaining for the civilians: approve and animate them, register and validate loops and posture, preserve appearance variety, add data-driven loading, measure before and after, and playtest. CJ-011 player posture is not implemented by these images.

**Motorbikes integrated (2026-10-07).** The size review found the lengths, the pair registration (at most 1 px, 0.5 cm, between the empty and ridden image) and the riders' scale (helmet about 0.30 m) correct, but the drawn mirror and handlebar spans 24–35 % wider than real bikes: 0.96 m, 1.24 m and 0.93 m. The user chose real collision widths: `WIDTH` records give the Sportbike 0.75 m, the Chopper 0.95 m and the Scooter 0.70 m (the placeholders had 0.77, 0.95 and 0.69 m), while the art keeps its proportions and the mirrors overhang the box. `BIKE` records load the six images from `assets/vehicles/`, one look per class. Automatic paint variants were rejected because they also recolour the lights and cannot recolour the black chopper; more colours come as more image pairs. Results: the `VEHICLE` start-up log shows the bike widths above and every car unchanged; the [`bikes` scenario](testing.md#scenarios) shows the art at game scale beside the player and a car; the `traffic-turns` fixture passes 96 of 96 cases with results identical to the CJ-020 run; `drive` runs at 141.9 FPS uncapped (144.2 FPS in the CJ-020 run).

**Motorbike playtest (2026-10-08).** The user tested the motorbikes and accepted them. The motorbike part of the acceptance criteria is met; CJ-004 stays open for the civilians.

**Civilians from 3D (2026-10-08).** Image generation could not keep 22 frames consistent and correctly projected (the camera rarely occurs in its training data, a reference image overrode the prompt, the target moved between iterations), so the civilians are now built with MakeHuman (MPFB, CC0 assets) in Blender, animated with the Quaternius Universal Animation Library (CC0) and rendered straight from above ([pipeline](../assets/art/cj004/README.md#civilian-3d-pipeline)). The user's decisions: an upright idle in which legs and shoes are at most 4 % of the silhouette, and a stylized look with a dark contour and firmer colours. The proof of concept, one civilian with the idle and an eight-frame walk, met the idle, width and loop criteria; its side-to-side pelvis sway of 3.1 px is the natural sway of a walk. The user accepted it and asked for the full set: run, punch, lying, the raised-fist gestures and appearance variants, then integration, a before/after measurement and the playtest.

**Agreed civilian specification (2026-10-08).**

| Frames (96 px, facing up) | Content | Source |
|---|---|---|
| 0–7 | Walk | `Walk_Formal_Loop` |
| 8 | Idle | Upright pose built by the render script |
| 9 | Lying (knocked down or dead), real size | Last frame of `Death01` |
| 10–11 | Right and left punch | Peak of `Punch_Cross` / `Punch_Jab` |
| 12–13 | Raised fist, two shake positions | Pose built by the render script |
| 14–21 | Run (new) | `Jog_Fwd_Loop` or `Sprint_Loop`, whichever looks more natural |

- **Variants:** 28, as today: men and women, three age groups, three builds, several skin tones, at least eight outfits and eight hairstyles from the CC0 packs with recoloured clothes; no two with the same outfit, colour and hair.
- **Integration, data-driven:** atlases in `assets/characters/civilians/`, listed in a new `assets/data/civilians.cfg`; a missing file keeps the procedural civilian. Run frames show only at running speed; an atlas without them keeps the walk frames. No other behaviour changes.
- **Criteria per variant:** idle legs and shoes at most 4 % of the silhouette; width within ±10 % of the player's (0.59–0.72 m); walk and run cycles closed; pelvis cycle average within 1 px of the frame centre.
- **Criteria in the game:** a new `civilians` scenario lines up every variant beside the player (screenshots before and after); `day`, `rampage` and `brawl` change FPS by at most 5 % and leave the pedestrian metrics unchanged; then the user's playtest.

**Civilians integrated (2026-10-08).** 28 looks: 14 men and 14 women of three age groups, 1.55–1.86 m tall, in ordinary clothes from the CC0 packs, several recoloured ([full set](../assets/art/cj004/README.md#full-set-2026-10-08)). Width per look against the player could not be met: women and slim men are narrower than the player, whose combat stance and vest make him broad. The user chose to draw standing civilians 12 % larger (`SCALE 1.12` in `civilians.cfg`) and to apply the width criterion to the average man: −7 % standing, +7 % walking. Every look meets the other per-look criteria (idle legs and shoes 0.3–3.9 %, closed cycles, pelvis at the frame centre). The atlases load from [civilians.cfg](guides/adding-content.md#civilians) (144 px frames covering 2.1 m, a 3.2 m run cycle above 3.6 m/s); the procedural looks remained the fallback until 2026-10-11 ([ADR-0009](adr/0009-required-character-atlases.md)). In the game: the `civilians` scenario shows all 28 beside the player; single runs of the same build vary by up to 15 %, so FPS is compared as the median of four alternating runs: `day` 326.2 â†’ 314.9 FPS (âˆ’3.5 %), `brawl` 256.6 â†’ 271.9 FPS (+6.0 %); one `rampage` run 161.7 â†’ 155.9 FPS (âˆ’3.6 %), within that spread; the `PEDS` lines of all three scenarios are identical before and after.

**Civilian playtest (2026-10-08).** The user tested the civilians and accepted them; CJ-004 is done. The relaxed player posture remains [CJ-011](#cj-011-relaxed-player-posture).

**Problem.** Motorbikes and civilian pedestrians use procedural placeholder sprites that do not match the quality of the rest of the art. Playtest feedback (2026-09-27): the civilians look much cheaper than the player, their walk animation does not look real, and standing civilians look smaller than the player, because the player sprite holds its arms forward while the civilians are drawn thin from the top.

**Notes.** No free, high-resolution top-down motorbike or civilian art has been found yet. itch.io listings show a bot check to automated browsers, so they must be searched manually. The player's Survivor sprites are a soldier in a combat stance, so recolouring them into civilians (GTA 2 style remaps) was considered and rejected. Any new art must meet the [art requirements](guides/adding-content.md#art-and-licence-requirements); consider rendering sprites from open-source 3D character models (for example CC0 models rendered top-down) if no 2D set exists.

**Acceptance criteria.** Motorbikes (sport, chopper, scooter, with and without rider) and civilians replaced by art of consistent quality, with walk, run, idle, punch and lying frames, added through the data files, credited in [CREDITS.md](../CREDITS.md). Civilians and the player read as the same size.
