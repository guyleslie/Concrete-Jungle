# Changelog

All notable changes to Concrete Jungle are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). There are no public releases yet; version numbers mark development milestones.

## [Unreleased]

### Added

- CJ-002 specification draft and proposed ADR-0007: researched handling references, calibration targets for all 17 vehicle classes, collision acceptance and a before/after measurement plan. Awaiting approval; gameplay and test commands are unchanged.
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

- 300 pedestrians live within 110 m of the player instead of 220 spread over the island.
- Injuries from vehicles grow with the impact energy: about half of the people hit at 36 km/h die, nearly everyone above 45 km/h.
- Fleeing people choose a direction along the sidewalk, away from walls and out of the path of moving vehicles; panic spreads to bystanders, but only one step.
- Traffic records where it plans to stop, so pedestrians can predict it.
- Lying bodies are drawn at real size (they were almost three times too long); standing people are drawn 8 % larger.
- The game runs in borderless full screen only: no window frame, no resizing, no F11 toggle, and the Windows cursor is hidden. Alt+F4 quits. Test runs keep the fixed window.

### Fixed

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
