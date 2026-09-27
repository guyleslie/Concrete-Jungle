# Testing

The game has an automated test mode that runs a scripted scenario for a fixed number of frames, saves screenshots and logs metrics. Use it to verify every change to the simulation before and after, and compare the numbers.

## Contents

- [Running a scenario](#running-a-scenario)
- [Scenarios](#scenarios)
- [Metrics](#metrics)
- [Baseline](#baseline)
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

## Baseline

Results on 2026-09-27 after the collision rewrite (1,500 frames each):

| Scenario | Position flips | Heading flips | Max penetration | Deep frames | Traffic knocked / re-joined / gave up |
|---|---|---|---|---|---|
| `crash` | 7 | 2 | 0.3 px | 0 | 2 / 1 / 0 |
| `derby` | 1 | 9 | 0.3 px | 0 | 3 / 3 / 0 |
| `chase` | 0 | 5 | 1.7 px | 0 | 4 / 3 / 1 |

For comparison, before the rewrite the `crash` scenario produced 965 position flips, a maximum penetration of 33 px and 1,849 deep frames.

## Workflow

1. Run the relevant scenarios on the current code and keep the log.
2. Make the change.
3. Run the same scenarios again and compare the metrics.
4. Look at a screenshot series (`--every`) for anything the numbers cannot show.
5. Record new baselines here when a change intentionally moves them.
