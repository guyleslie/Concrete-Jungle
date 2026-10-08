# Changelog

All notable changes to Concrete Jungle are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Version 0.3.0 is the first public release, published on [GitHub](https://github.com/guyleslie/Concrete-Jungle/releases); earlier version numbers mark development milestones.

## [Unreleased]

### Added

- `civilians.cfg` records `SWAY`, `ANIM` (the atlas layout), `GAIT` (jog and run speeds and cadences) and a walk stride per `CIVILIAN` line, replacing `RUN`; `tools/cj004/measure_gait.py`, which measures stride, foot slip, toe reach and leg spread as the game plays the frames (CJ-029).
- `WIDTH <class> <width m>` and `BIKE <class> <empty file> <ridden file>` records in `vehicles.cfg`, the `bikes` test scenario, and a `VEHICLE` start-up log line with each class's collision and drawn width (CJ-004).
- `assets/data/civilians.cfg` (`FRAME`, `SCALE`, `WALK`, `RUN`, `CIVILIAN`): pedestrian atlases with run frames, the `civilians` test scenario and `tools/cj004/make_civilians.py` with `looks.json`, which builds, animates, renders and checks every look (CJ-004).
- CJ-004 civilian 3D pipeline (`tools/cj004/`): Blender scripts that build a civilian from MakeHuman CC0 assets, retarget Quaternius Universal Animation Library actions onto it, render top-down frames, measure them and reduce them to stylized 96 px atlas frames. The proof of concept, one civilian with an idle and an eight-frame walk, is accepted; the game is unchanged.
- CJ-004 art review: motorcycle designs are accepted; civilian sheets and earlier idle masters are rejected experiments. The [v4 idle candidate](assets/art/cj004/civilian-idle-master-v4.png) awaits review before animation groups: fully hidden idle legs/shoes, vertical arms, compact shoulders, ordinary clothing and the player's moderate detail. [Source art and historical prompts](assets/art/cj004/README.md) and the [smaller-group workflow](assets/art/cj004/civilian-prompts-v2.md) are recorded. Vehicle exports bake position and scale into PNGs using a shared facing, centre and size convention. For the civilians, animation validation, appearance variants and playtest remain open.

### Changed

- Civilians walk, jog and run with compact, natural steps (CJ-029): the legs no longer reach out like the splits (0.67 m at most from the front toe to the back foot, 0.87 m before), the walk advances with each look's own measured stride so planted feet stay put (at most 1.7 cm), people hurrying across a street jog (from 2.0 m/s) and fleeing people run (from 4.0 m/s), with a cadence that rises with speed. The walk has 16 frames instead of 8, and the game no longer turns the sprite with the steps (the rendered walk sways by itself). Civilian atlases take about a third of their former video memory: every frame is cut to its visible part at load. Awaits playtest.
- Civilians are rendered from 3D models (CJ-004): 28 looks, men and women of three age groups and real heights (1.55–1.86 m) in ordinary clothes, with a relaxed walk, a run for fleeing and dodging, two punches, the raised fist and a body on the ground at real size, replacing the procedural looks (which remain the fallback). Standing civilians are drawn 12 % larger than life on top of the usual character scale, so that an average man reads as large as the player.
- CJ-004 (replace placeholder art) is done: the user playtested and accepted the civilians on 2026-10-08, after the motorbikes earlier the same day.
- Motorbikes use the new CJ-004 art instead of the procedural placeholders: a sportbike, a chopper and a scooter, each with and without a rider. The art keeps its proportions at the class length, so its mirrors and handlebars (0.93–1.24 m) reach beyond the collision box, which keeps the handlebar width of real bikes: Sportbike 0.75 m, Chopper 0.95 m, Scooter 0.70 m (0.77, 0.95 and 0.69 m before). One look per class for now, instead of 11 colour and outfit combinations.
- CJ-004 motorbikes are done: the user playtested and accepted them on 2026-10-08. CJ-004 stays open for the civilians.

## [0.4.0] - 2026-10-07

Traffic turns like real cars (CJ-020), and the human-like traffic and driver incidents of CJ-016 are accepted after their playtests. The specification for roomier streets and real-world vehicle widths (CJ-023) is agreed; the city itself is unchanged in this release. Pedestrian behaviour (CJ-010) and full screen (CJ-013) still await playtest acceptance.

### Added

- CJ-020 measurements: the `traffic-turns` fixture (every traffic class turning right, left and straight through an empty junction at 60 Hz and 20 Hz: slip, rear-axle radius, kerb intrusion, encroachment, lateral acceleration, trace captures), the `TRAFFIC rail overlaps` line in the test log and `tools/run_cj020_turns.py`.
- `TURN <class> <turning circle m>` records in `vehicles.cfg`; `TURN lateral_accel`, `easement`, `left_radius` and `max_swing` in `traffic.cfg` (CJ-020).
- `TRAFFIC turns planned` and `RAIL-SLIP` lines in the test log; the turning fixture (`cj020-turns-v2`) also checks the wheels against the kerbs and logs a strict encroachment.
- [Turning results](docs/design/traffic-turning-results.md): turning fixture 32 → 96 of 96 cases, city turning slip 70–75 % of the time over 5° → none, at most 1.9°.
- Backlog items CJ-022 (three-point turns), CJ-023 (vehicle widths and street geometry, from the feedback that the streets feel cramped) and CJ-024 (recovery cost in dense traffic).
- [Street geometry proposal](docs/design/street-geometry-proposal.md): the agreed CJ-023 specification — real-world vehicle widths from a `WIDTH` record, data-driven street profiles (12 m main streets, 10 m side streets), sidewalks with a furniture zone and a clear walking zone, building setbacks, rounded kerb corners, and the measurements that will judge them. Backlog items CJ-027 (multi-lane roads) and CJ-028 (rare rule-breaking drivers), from the user's direction for the city.

### Changed

- Traffic turns like real cars (CJ-020): the rear axle follows the path and the body points along it, so the front swings out and nothing slides sideways through a turn. Each class turns on its own path through a junction, never tighter than its turning circle allows, with gradual steering in and out; the turning speed keeps the lateral acceleration at the rear axle within 3.5 m/s² (about 11–15 km/h through a right turn, 15–19 km/h through a left turn). A class too wide for a clean turn takes a wide turn and has the junction box to itself; a turn that does not fit at all (the Bus, the Semi, and the right turns of the large trucks and the Limo, between square corners) is avoided unless there is no other way. Paths are planned while the game loads. Police are unchanged.
- U-turns follow a 2 m semicircle between the lanes, tangent to both, at a limited speed, instead of a curve that started with a kink. A car rejoining its lane after a knock joins it along a smooth curve from its actual pose. At a junction a car counts as occupying the box once its nose is in it or it has passed its stop line, so a slow turning car and a car coming straight through no longer meet in the box.
- CJ-020 (turning kinematics) is done: the user playtested and accepted the turns on 2026-10-07. The feedback that roads, junctions and the space beside them are too narrow joins CJ-023; parks and parking lots join CJ-014; new items CJ-025 (metro track, stations and passengers) and CJ-026 (bus stops and bus bays).
- CJ-016 (human-like traffic and incidents) is done: the user playtested and accepted it on 2026-10-07. ADR-0008 stays Proposed until visible drivers control the physical integrator.
- Backlog: CJ-018 becomes *Police driving and reactions* (High) with the playtest feedback that pursuing police run down pedestrians, ram other cars and cause crashes, and the open police reaction to a fight. New item CJ-021 for visible towing.

### Fixed

- The README's download link points at the releases page: GitHub's latest-release address skips pre-releases such as 0.3.0.

## [0.3.0] - 2026-10-06

First public release: the source on GitHub and a playable Windows build. Pedestrian behaviour (CJ-010), full screen (CJ-013) and the third traffic increment (CJ-016) still await playtest acceptance.

### Added

- MIT licence for the project's own code (`LICENSE`, CJ-009); third-party assets keep the licences listed in CREDITS. A screenshot at the top of the README, ready for publishing on GitHub.
- `tools/package_release.py` packages a playable Windows build as a zip; the [build guide](docs/guides/building.md#packaging-a-release) describes the release steps.
- A CJ-008 note: the `drive` autopilot now locks up with a traffic taxi in the first junction.
- Angry drivers shout: three synthesised voices ("hey!", "oi!", "hah!") at a personal pitch, every second or so from the moment they walk up; `shout1.wav`–`shout3.wav` in `assets/sounds/` replace them. Arguing, they raise and shake a fist (two new civilian atlas frames); the punch frame is now only for blows.
- `TRAFFIC slip` in the test log: the rear-axle slip of rail cars during lane changes, turns and straight driving. New backlog item CJ-020 for the turning slip it found.
- Wait-for cycles: two knocked cars blocking each other, and three or more drivers waiting on each other in a loop (junction gridlock), are found once per frame; one driver gives way to the driver waiting on it. A rail car backs up along its path; a knocked car makes room with short checked creeps away from the other car. A car held at a stop line by a car in the junction box now waits on that car. `YIELD cycles` and `YIELD min_room` in `traffic.cfg`; `TRAFFIC wait cycles` in the test log.
- `knocked-pair` and `junction-gridlock` situations in the conflict fixture (`cj016-conflict-v2`, 100 cases): both went from 0 to 20 of 20 resolved cases; the 60 earlier cases reproduce their results.
- Cooperative yielding: two traffic drivers stopped behind each other get stable roles; the one giving way backs up along its own driven path (still on rails), tucks back into its lane if it was passing, and a car queued close behind backs up too. `YIELD` tuning in `assets/data/traffic.cfg`.
- Driver incidents: every traffic driver is calm, normal or (about 10 %) aggressive. After a collision with another car or the player's car, an aggressive driver may stop, get out on a safe side, confront and argue, fight the other driver or the player, and then drive the same car on. A lost car or a dead driver ends the incident with a logged reason; nobody is replaced or moved. `INCIDENT` tuning in `traffic.cfg`.
- `traffic-conflict` (60 cases) and `traffic-incident` (80 cases) fixtures with before/after evidence runners. Yielding: 6 → 60 resolved cases; incidents: 20 → 80 accepted cases, with no ownership violation or duplicate driver. See the [yielding and incident report](docs/design/traffic-yielding-incident-results.md).
- `RECOVERY WORK`, `RECOVERY WORST`, `TRAFFIC yielding` and `INCIDENTS` lines in the test log; `--traffic-config <path>` for measurement runs.
- Approved CJ-016 specification for human-like traffic: persistent recovery, cooperative manoeuvres, driver identities and incidents, with test and CPU targets. Proposed ADR-0008 records the replacement constraints for rail traffic; ordinary traffic remains on rails in the first recovery increment.
- Isolated `traffic-recovery --vehicle Taxi|Bus|BoxTruck` fixtures: enclosed holding, open-road recovery and reverse garage escape at 60 Hz and 20 Hz, with 52 checks per class, rendered checkpoints and separate decision/physics timings. The sequential CJ-016 runner retains before/after manifests and failed baseline evidence. The [result report](docs/design/traffic-recovery-results.md) records 14 failed checks per class before the change and all 156 checks passing on a physical recovery build, with exact hashes and preserved intermediate attempts. Its pre-fix city suite exceeded CPU targets; the corrected build passed all 156 recovery checks and 26 separate clearance checks. Six city regressions completed with observed rejoins and zero recovery give-ups, while their remaining CPU failures stay open.
- Terminal `LONG-REJOIN` diagnostics for persistent recovery, including alignment/velocity/block-position guards, the most recent rejoin forecast status and `RejoinCause` for actual contact, clearance-only contact, unsafe sweep or incomplete stop.
- Separate `traffic-clearance` regression and evidence runner: nearby separated building/parked-car boxes must allow an API check and real rejoin, while actual overlap, clearance-only contact and forward blockage remain rejected. Its pre-fix run completed 26 checks across eight cases with four failures, exactly clear API/rejoin at both rates; all obstacle guards passed. The corrected build passed all 26 checks; both clear cases rejoined at 0.700 s and all obstacle guards remained rejected. The original recovery fixture remains frozen.
- Validated recovery tuning in `assets/data/traffic.cfg`.
- Approved CJ-002 specification and proposed ADR-0007: researched handling references, calibration targets for all 17 vehicle classes, collision acceptance and a before/after measurement plan.
- Isolated `handling --vehicle <Class>` and `crash-handling` measurements, sequential evidence runner and a baseline report covering all 17 classes and 122 collision phases. Named runs preserve earlier evidence; the report distinguishes failed acceptance checks from incomplete execution and documents unavailable arcade tyre telemetry. Production handling is unchanged.
- Follow-up playtest notes for CJ-010/CJ-013, including the request to match GTA 1/2's approach to the crosshair.

- Pedestrians predict the motion of vehicles and jump out of their way; they react after a personal reaction time and panic after a close call ([ADR-0006](docs/adr/0006-pedestrian-steering.md)).
- Anticipatory time-to-collision avoidance between people, the player and street furniture; people walk where they face and turn at a realistic rate.
- Queueing at the kerb and safe crossing: people only cross on green when they can leave the road in time, check for vehicles, hurry when the lights change, and jaywalk only on a gap.
- Tough people (about 15 %) hit back when the player punches them.
- Vehicles drive over people lying on the ground instead of shoving them; blood splatter, blood pools where bodies come to rest, bodies that fade out after about 25 s.
- Pedestrian grid for neighbour queries, used by pedestrians, traffic and vehicle–pedestrian collisions.
- `rampage` and `brawl` test scenarios; `PEDS` and `TIMING` metrics in the test log.
- Backlog items CJ-010 to CJ-019 from the playtest feedback, and a recommended order for the next sessions.
- A crosshair on foot, drawn by the game.
- `AGENTS.md`, pointing AI coding agents other than Claude to the working notes in `CLAUDE.md`.

### Changed

- `build.sh` reads `RAYLIB_DIR` like `build.bat`, so it builds with raylib installed anywhere. Both build scripts report a missing raylib installation and end with `BUILD FAILED` on any error; `build.sh` no longer fails silently when a compiler error does not contain the word "error".
- The README quick start is a step-by-step guide: installing raylib, getting the source, building from the Command Prompt, PowerShell or Git Bash, and starting the game. Tested on a fresh clone with a clean `PATH`.
- Rail cars change lane by steering instead of sliding sideways: the lateral offset follows an S-curve along the path, the rear axle traces it and the body points along it. Overtaking and mounting the kerb check the whole swept curve first, backing up a little when the car stands too close behind the obstacle; a driver giving way reverses back into its lane the same way.
- Driver decisions now meet the CJ-016 CPU targets in every city scenario (`chase` 0.69 / 1.41 → 0.32 / 0.82 ms, `rampage` 0.73 / 1.55 → 0.38 / 0.86 ms average / 95th percentile): the rail look-ahead tests only cars and people that can reach its sampled path, recovery rollouts let distant actors sleep and skip rail forecast boxes out of reach, and force steps share their trigonometry. These leave every decision unchanged; covered immediate checks record up to 32 actors instead of 12. `DRIVER DECISION STAGES` in the test log splits the decision time. See the [third increment report](docs/design/traffic-third-increment-results.md).
- On-foot test runs use the screen centre instead of the mouse cursor for the camera look-ahead, so `foot`, `day` and `brawl` no longer depend on where the cursor rests.
- Recovery planning no longer runs simultaneous full searches in one frame: jobs are sliced under a shared step budget (`RECOVERY planning_steps`), holds replan only when a blocking actor moves, and immediate checks are reused while every nearby actor moves as forecast. A follower behind a recovering car is trusted to keep its distance and rail cars are forecast to stop where they plan to, which ends the start-stop oscillation of cars moving off a queue. The worst city decision frame fell from 11.6 ms to 2.5 ms; `crash` meets the decision CPU targets, `chase` and `rampage` do not yet.
- A recovering car aligned behind a stopped car joins the queue instead of reversing away, creeping forward first if it was hit from behind; a car pressed against a wall or another car can now move off.
- Knocked traffic plans bounded physical forward, reverse or hold moves from a shared actor snapshot, checking swept vehicle footprints and stopping room. Safe lane feedback avoids unnecessary escape searches; full planning evaluates at most 20 candidates. No safe local candidate retains the driver and car; elapsed recovery time and low health no longer trigger abandonment or relocation. Lane rejoin requires physical alignment without a pose blend. All 182 isolated checks pass and six city regressions are recorded; city CPU acceptance and the user recovery playtest remain pending. Mutual yielding/reservations and on-foot incidents follow in later CJ-016 increments.
- Recovery geometry caches actor radius, speed and initial oriented boxes, uses conservative travel/rotation bounds before detailed forecasts, and omits hold prediction when it cannot change the selected controls. Controller, tuning and candidate tie order are preserved; complete corrected measurements are recorded in the result report, with simultaneous-recovery CPU failures retained.
- 300 pedestrians live within 110 m of the player instead of 220 spread over the island.
- Injuries from vehicles grow with the impact energy: about half of the people hit at 36 km/h die, nearly everyone above 45 km/h.
- Fleeing people choose a direction along the sidewalk, away from walls and out of the path of moving vehicles; panic spreads to bystanders, but only one step.
- Traffic records where it plans to stop, so pedestrians can predict it.
- Lying bodies are drawn at real size (they were almost three times too long); standing people are drawn 8 % larger.
- The game runs in borderless full screen only: no window frame, no resizing, no F11 toggle, and the Windows cursor is hidden. Alt+F4 quits. Test runs keep the fixed window.

### Fixed

- A traffic car whose mid-block U-turn was refused lost its route and jumped to the map origin. A refused U-turn keeps the route, and the swept turn must now miss other vehicles.
- A person standing with their back to their target could not start walking in the new driver states; they now turn towards it first.
- Traffic path scanning no longer ignores an occupied vehicle because both drivers block each other; junction right-of-way rules still apply.
- Recovery forecasts retain legacy rail pose blends and use the actual observed body at time zero. Distance culls include the `sqrt(2)` corner-radius growth of inflated oriented boxes.
- Recovery records initial penetration only when the overlap query returns true. Previously a positive output left by a false box-overlap result made separated nearby boxes veto rejoin. The regression reproduced this before the fix; all 182 isolated checks pass after it, and matching city measurements are preserved.
- Pedestrians fled from normal passing traffic and ran into the road, where they were hit.
- Knocked-down pedestrians never got up.
- Pedestrians slid sideways and did not turn their bodies properly.
- Pedestrians pushed off their walking line kept walking along the edge of the road.

## [0.2.0] - 2026-09-27

### Added

- Vehicle contact solver (`src/physics.*`) in the style of Box2D v3: contact manifolds with up to two points, speculative contacts, 240 Hz sub-steps, soft push-out, Coulomb friction and speed-dependent restitution ([ADR-0005](docs/adr/0005-vehicle-contact-solver.md)).
- Breakaway street furniture: lamp and signal posts fall over, hydrants burst, bins and cones fly, phone booths and bus shelters shatter; steel bollards, planters, trees and buildings stop vehicles. Shrubs slow vehicles down and get flattened.
- Box-shaped collision for benches, picnic tables, bus shelters, dumpsters and planters (benches, picnic tables and bus shelters could previously be driven through).
- Crash consequences based on delta-V: vehicle damage, driver injury in very hard crashes, motorbike riders thrown off.
- Sparks and grinding sounds when scraping along walls and other vehicles.
- Recovery for traffic knocked off its lane: the driver waits until the lane is clear, drives or reverses back to it, and only gives up when the car is badly damaged or recovery takes too long.
- `crash` and `derby` test scenarios, the `--every` screenshot option, and physics metrics in the test log.
- Git repository, `.gitattributes`, `CLAUDE.md`.
- Documentation set: documentation index, architecture overview, design documents for every subsystem, building and content guides, testing guide, architecture decision records, prioritised backlog, this changelog and a contributing guide.
- `tools/check_docs.py` to validate links in the documentation.

### Changed

- Tyre model runs per physics sub-step: limited yaw authority, saturated friction for sliding cars, static friction, locked wheels for parked cars and wrecks.
- Vehicle damage now follows delta-V instead of closing speed, so mass matters; ramming parked cars does less damage than before, wall hits slightly more.
- Traffic cars are knocked into physics after pushing on something for 0.35 s, not only by hard hits.
- `README.md` restructured into an overview with links to the documentation.

### Fixed

- Vehicles getting stuck after hitting street furniture, with the sprite jumping in place: vehicle-versus-object contacts used an inverted normal and pulled vehicles into the object.
- Vehicles wedged between obstacles being pushed back and forth by one-at-a-time position corrections.
- Traffic cars pushing the player's vehicle into walls.
- Steering flipping direction when the forward speed crossed zero.
- Knocked traffic cars rocking back and forth because the brake engaged reverse at crawling speed.

## [0.1.0] - 2026-09-26

### Added

- First playable version.
- Procedurally generated island city with streets, traffic lights, buildings, parks, plazas, parking lots, a police station, a hospital, gate buildings, skybridges and an elevated metro.
- Real 3D top-down renderer with sun shadows, day/night cycle, dynamic lights and bloom ([ADR-0001](docs/adr/0001-real-3d-top-down-renderer.md)).
- Lane-following traffic with junction rules ([ADR-0004](docs/adr/0004-kinematic-rail-traffic.md)), police pursuit, pedestrians.
- Player on foot with six weapons, carjacking, wanted level, arrests, missions and pickups.
- Data-driven vehicles, characters, weapons and foliage ([ADR-0002](docs/adr/0002-data-driven-content.md)).
- Procedural audio and procedural placeholder sprites.
- `--shot` automated test mode.
