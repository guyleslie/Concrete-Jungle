# Testing

The game has an automated test mode that runs a scripted scenario for a fixed number of frames, saves screenshots and logs metrics. Use it to verify every change to the simulation before and after, and compare the numbers.

## Contents

- [Running a scenario](#running-a-scenario)
- [Scenarios](#scenarios)
- [Metrics](#metrics)
- [Baseline](#baseline)
- [CJ-002 measurements](#cj-002-measurements)
- [Workflow](#workflow)

## Running a scenario

```bash
./ConcreteJungle.exe --shot build/shots/crash.png --frames 1500 --every 30 --scenario crash > build/shots/crash.log 2>&1
```

| Option | Meaning |
|---|---|
| `--shot <file.png>` | Enables test mode and saves a screenshot after the last frame |
| `--frames <n>` | Number of frames to run (default 180); the frame time is fixed at 1/60 s |
| `--every <n>` | Also saves `<file>_<frame>.png` every *n* frames |
| `--scenario <name>` | The scenario to set up (default `foot`) |
| `--vehicle <Class>` | Selects a class for the isolated `handling` scenario (default `Taxi`) |
| `--uncapped` | Disables vsync in screenshot mode; simulation still advances exactly 1/60 s per rendered frame |

Screenshot paths must be **relative** to the working directory: raylib prefixes the working directory to the file name. The window opens while the test runs. Keep test output in `build/`, which is not version-controlled.

## Scenarios

| Scenario | Setup |
|---|---|
| `title` | The title screen with the city simulating behind it |
| `foot` | The player on foot at the central square, at dusk |
| `day`, `night` | On foot at 13:00 or 22:30 |
| `drive`, `nightdrive` | The player in the starter car with a simple autopilot (steady throttle, gentle weaving) |
| `chase` | As `drive`, with three wanted stars |
| `overview` | A zoomed-out view of the traffic around the player, each car ringed by the reason it is stopped |
| `crash` | Scripted crash course: grinding along a wall at 35°, reversing, a full-speed head-on hit, a building corner at 45°, a lamp post at about 60 km/h, a hydrant, a steel bollard, and shoving three parked cars into a wall. Every impact is logged. |
| `derby` | Full throttle through traffic with random steering; reverses when stuck |
| `rampage` | At 13:00: straight 4 s runs at 50 km/h along the sidewalk walking lines of the blocks around the start, each from a fixed start point, so people have to get out of the way. Run it for 3,600 frames. |
| `brawl` | At 13:00, on foot with fists: runs up to the nearest person standing (preferring anyone fighting back, not chasing runners) and punches them |
| `handling` | Isolated acceleration, braking, top-speed, skidpad, rear-brake, reverse and surface measurements for the selected class; 14,400 frames |
| `crash-handling` | Prescribed-speed contacts with isolated forces and production consequences measured separately, at 1/60 s and 1/20 s physics intervals; 14,400 frames |

## Metrics

The run ends by logging:

| Line | Metric | Healthy value |
|---|---|---|
| `SHOT` | Average frames per second | ≈ 75 (vsync-limited on the development machine) |
| `TRAFFIC` | Number of traffic cars, average speed, stopped and blocked cars, AI-to-AI contacts per second | Few blocked cars; AI contacts close to 0 in `drive` |
| `TRAFFIC jolts` | Sudden velocity jumps per second, split into rail, knocked and police cars | Rail jolts close to 0 |
| `TRAFFIC stopped because` | Why stopped cars are stopped: red light, queue, person, yield, static obstacle | Mostly red lights and queues |
| `PHYS` jitter | Frames where a physics body's position or heading reverses direction frame after frame | Close to 0 per body-second |
| `PHYS` penetration | Deepest overlap with buildings or solid furniture, and frames deeper than 3 px | Under 3 px; no deep frames |
| `PHYS` stuck | Times the player pressed on without moving for 2.5 s | Expected in `crash` (pushing into walls on purpose) |
| `PHYS` traffic | Knocked off the lane / re-joined / drivers gave up; any car knocked for more than 12 s is listed as `LONG-KNOCKED` | Most knocked cars re-join; no long-knocked cars |
| `IMPACT` (`crash`) | Contact kind, object, closing speed, delta-V of both bodies, whether an object broke | Plausible delta-V; breakaway objects break |
| `DEEP` | A body deeper than 3 px in the static world (logged while it happens) | None |
| `PEDS` fleeing, dodging | Average number of people in the `Flee` and `Dodge` states | Below 1 in `foot` and `day` (nothing is happening) |
| `PEDS` on the road off a crossing | Average number of people standing or walking on a road tile outside a zebra crossing (not counting people on the ground) | Below 1 in `foot` and `day` |
| `PEDS` visible | Average number of people on screen, not counting bodies | At least 4 in `foot` and `day` |
| `PEDS` overlaps | Pairs of people closer than 1.6 body radii, per second | Low; a few in crowded scenes |
| `PEDS` crossing | Person-seconds spent on a crossing's road part while the crossing traffic has green, split into people who started on green and jaywalkers | Close to 0 in `foot` and `day` |
| `PEDS` down too long | Person-seconds spent knocked down for more than 1 s beyond their get-up time | 0 |
| `PEDS` sliding | Share of moving time in which a person's body faces more than 35° away from its motion | Close to 0 % |
| `PEDS` hits | People hit by traffic and by the player; in `rampage`, people in the car's straight path within 2 s and how many of them were hit | Traffic hits 0 in `foot` and `day`; at least 70 % escape in `rampage` |
| `PEDS at the end` | People per state and within 30, 60 and 110 m of the player | Most people walking; everyone within 110 m |
| `PEDS` fights | People who fought back and punches they landed (`brawl`) | Some in `brawl` |
| `TIMING` | CPU time per frame of the vehicle update, the pedestrian update and the world drawing (CPU side only) | Pedestrians at most 0.5 ms |

## Baseline

Results on 2026-09-27 after the collision rewrite (1,500 frames each):

| Scenario | Position flips | Heading flips | Max penetration | Deep frames | Traffic knocked / re-joined / gave up |
|---|---|---|---|---|---|
| `crash` | 7 | 2 | 0.3 px | 0 | 2 / 1 / 0 |
| `derby` | 1 | 9 | 0.3 px | 0 | 3 / 3 / 0 |
| `chase` | 0 | 5 | 1.7 px | 0 | 4 / 3 / 1 |

For comparison, before the rewrite the `crash` scenario produced 965 position flips, a maximum penetration of 33 px and 1,849 deep frames.

After the pedestrian rework ([CJ-010](backlog.md#cj-010-pedestrian-behaviour), 2026-09-27) the traffic situations in these scenarios differ, because the larger population draws a different random sequence; the physics code itself did not change. New reference values (1,500 frames each):

| Scenario | Position flips | Heading flips | Max penetration | Deep frames | Traffic knocked / re-joined / gave up |
|---|---|---|---|---|---|
| `crash` | 11 | 30 | 0.3 px | 0 | 11 / 9 / 1 |
| `derby` | 3 | 3 | 0.1 px | 0 | 1 / 1 / 0 |
| `chase` | 1 | 10 | 0.1 px | 0 | 5 / 5 / 0 |

In `drive` the simple autopilot now ends up pressed against a building corner (9 stuck events); it cannot reverse ([CJ-008](backlog.md#cj-008-test-autopilot-improvements)).

### Pedestrians

Before and after CJ-010. `foot` ran 1,500 frames and `rampage` 3,600 frames. The old AI ran 220 people spread over the island; the new one runs 300 around the player.

| Metric | Before (`foot`) | After (`foot`) | Before (`rampage`) | After (`rampage`) |
|---|---|---|---|---|
| Fleeing, average | 29.9 | 0.0 | 44.3 | 28.0 |
| On the road off a crossing, average | 11.7 | 0.0 | 12.1 | 2.5 |
| Visible, average | 1.3 | 5.9 | 22.7 | 58.0 |
| Hit by traffic | 7 | 0 | 26 | 23 |
| In the car's path / escaped | — | — | 55 / 67 % | 136 / 74 % |
| Down too long | 71 person-s | 0 | 1,015 person-s | 0 |
| Sliding | 4.0 % | 0.1 % | 4.0 % | 0.1 % |
| Overlaps per second | 0.04 | 1.6 | 0.8 | 13.8 |
| Pedestrian CPU time | 0.28 ms | 0.37 ms | 0.26 ms | 0.41 ms |

The overlap count rose with the local density (about five times as many people near the player) and is highest in `rampage`, where crowds run from the car; people still never stay inside each other. Across runs the `rampage` escape rate varied between 72 % and 81 %. In `brawl`, 1–4 tough people fought back per run and landed up to 10 punches.

## CJ-002 measurements

The [approved handling specification](design/vehicle-handling-proposal.md#measurement-plan) defines class-by-class acceleration, braking, skidpad, rear-brake and collision tests. The measurement-only harness is implemented before changing production handling or collision response. The [recorded arcade baseline](design/vehicle-handling-baseline.md) contains all 17 class results and 122 collision phases. No after measurement or handling playtest has been performed yet.

Use `--run-name review-20261003` to save a new runner evidence set in a subdirectory of the selected phase without replacing previous results. Names allow letters, digits, underscores and hyphens only. Run `python tools/summarize_cj002.py --output docs/design/vehicle-handling-baseline.md` to rebuild the baseline report from complete before manifests. The summarizer requires all classes, matching isolated input hashes, fixture summaries and final screenshots; failed target checks remain visible. An unbracketed skidpad sweep is a bound, not a measured grip maximum; missing baseline tyre telemetry is distinct from a handling defect.

Run `python tools/run_cj002.py --phase before --suite all` after building, then repeat with `--phase after`. The runner opens one visible window at a time and records exact arguments, revision, dirty status, executable/configuration/fixture SHA-256 hashes, exit status and screenshot presence in timestamped manifests under `build/shots/cj002/`. Existing evidence requires explicit `--replace`; `--dry-run` previews the schedule without changing files. `--suite handling --vehicle Taxi` and `--suite city --scenario crash` select individual cases. Isolated fixtures use `--uncapped`; city runs preserve the existing timing options.

Fixture `cj002-v1` reports each scheduled phase and fails missing measurements, incomplete execution, non-finite state or penetration above 3 px, including vehicle pairs. A skidpad trial needs exactly 180 measurement samples, radius error at most 5 %, speed error at most 2 % and measured lateral acceleration within 0.03 g of the requested value. The maximum is bracketed by both successful and unsuccessful trials; slowing down cannot count as meeting a higher-speed target. Axle-slip telemetry is unavailable on the baseline arcade model and is reported explicitly. Rear-brake yaw travel uses the first 3 s, and recovery requires 0.25 s continuously below the specified lateral/yaw limits.

Keep the existing `crash`, `derby` and `chase` scripts; their run-up speeds depend on the handling, so prescribed-speed collision fixtures are also required. Contact-only momentum/energy checks exclude tyre-ground and damage effects. Inspect screenshot series and phase-labelled captures as well as numeric results. See the specification for per-class bands and collision acceptance criteria.

## Workflow

1. Run the relevant scenarios on the current code and keep the log.
2. Make the change.
3. Run the same scenarios again and compare the metrics.
4. Look at a screenshot series (`--every`) for anything the numbers cannot show.
5. Record new baselines here when a change intentionally moves them.
