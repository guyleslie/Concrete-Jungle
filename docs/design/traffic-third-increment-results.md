# CJ-016 third increment results

The third CJ-016 increment (2026-10-06) brings the driver decision CPU of every city scenario under the approved targets. `chase` fell from 0.689 / 1.415 ms (average / 95th percentile) to 0.321 / 0.819 ms and `rampage` from 0.727 / 1.547 ms to 0.378 / 0.861 ms; `crash` now has a wide margin at 0.234 / 0.484 ms. Apart from one deliberate capacity change, the optimisations leave every decision bit for bit unchanged, and all four fixtures (182 isolated checks, 60 yielding and 80 incident cases) reproduce the second increment exactly. Roles for two knocked cars, junction gridlock and the incident presentation follow in this increment; the user playtest is pending.

## Contents

- [Evidence](#evidence)
- [Driver decision CPU](#driver-decision-cpu)
- [Where the time went](#where-the-time-went)
- [Changes](#changes)
- [Exactness](#exactness)
- [Reproducible on-foot runs](#reproducible-on-foot-runs)
- [Limits and remaining work](#limits-and-remaining-work)

## Evidence

| Run | Manifest | Build |
|---|---|---|
| City, before | `build/shots/cj002/after/cj016-s3-base-20261006/manifest-20261006T075116130257Z-city-47256.json` | Revision `04093f9` with the stage profiler |
| City, after | `build/shots/cj002/after/cj016-s3-cpu2-20261006/manifest-20261006T092919634939Z-city-18980.json` | This change, executable SHA-256 `c389094bdd58d05d2e53095ef520a672d49390864a0cc0b08d0feb5e4a593602` |
| Recovery fixture, after | `build/shots/cj016/after/s3-cpu-final-20261006/manifest-20261006T093735705608Z-41704.json` | The same executable: 156 / 156 checks |
| Clearance fixture, after | `build/shots/cj016-clearance/after/s3-cpu-final-20261006/manifest-20261006T094206360387Z-52304.json` | 26 / 26 checks |
| Yielding fixture, after | `build/shots/cj016-conflict/after/s3-cpu-final-20261006/manifest-20261006T094226159620Z-60940.json` | 60 / 60 cases |
| Incident fixture, after | `build/shots/cj016-incident/after/s3-cpu-final-20261006/manifest-20261006T094353808750Z-33288.json` | 80 / 80 cases |

Both city runs used `assets/data/traffic.cfg` SHA-256 `6dd93537925e99a1876b66404e3d1a816c112714c221a590d52b38c66db92e9d` and the runner's standard 1,500 frames (3,600 for `rampage`) on the development machine. Intermediate profiling runs are kept in `build/shots/s3prof/`, `build/shots/s3opt1/`, `build/shots/s3opt2/` and `build/shots/bisect/`.

## Driver decision CPU

Targets: driver decisions at most 0.5 ms average and 1.0 ms 95th percentile per frame, total vehicle update at most 1.0 ms average.

| Scenario | Decision avg (ms) | Decision p95 (ms) | Decision worst (ms) | Vehicle avg (ms) | Knocked / rejoined / gave up |
|---|---|---|---|---|---|
| `crash` | 0.390 → 0.234 | 0.865 → 0.484 | 1.721 → 2.593 | 0.562 → 0.394 | 4 / 0 / 0 → 4 / 0 / 0 |
| `derby` | 0.369 → 0.171 | 0.603 → 0.296 | 1.770 → 1.876 | 0.540 → 0.305 | 2 / 2 / 0 → 2 / 2 / 0 |
| `chase` | 0.689 → 0.321 | 1.415 → 0.819 | 2.316 → 2.227 | 0.871 → 0.498 | 10 / 2 / 0 → 11 / 2 / 0 |
| `foot` | 0.263 → 0.165 | 0.420 → 0.251 | 2.005 → 0.727 | 0.406 → 0.311 | 0 / 0 / 0 → 0 / 0 / 0 |
| `day` | 0.254 → 0.146 | 0.395 → 0.231 | 1.218 → 0.882 | 0.393 → 0.273 | 0 / 0 / 0 → 0 / 0 / 0 |
| `rampage` | 0.727 → 0.378 | 1.547 → 0.861 | 4.297 → 2.213 | 0.922 → 0.569 | 29 / 14 / 0 → 24 / 15 / 0 |

Every scenario meets both decision targets and the vehicle target. Single frames above 1 ms remain (the worst frame); the targets are about the average and the 95th percentile. Overlap stays at most 0.7 px with no deep frame, and recovery give-ups stay at zero. On this machine repeated runs of one build vary by about ±8 % in these timings; the margins above are larger than that.

## Where the time went

`DRIVER DECISION STAGES` (new) splits the decision span. Before the change, per frame:

| Stage (ms) | `foot` | `chase` | `rampage` |
|---|---|---|---|
| Rail traffic | 0.212 | 0.175 | 0.177 |
| Knocked traffic (recovery) | 0 | 0.437 | 0.475 |
| Snapshot | 0.020 | 0.040 | 0.037 |
| Pedestrian grid | 0.027 | 0.024 | 0.023 |

- **Rail traffic.** The path look-ahead (`ScanPath`) cost about 3 µs per car per frame: every path sample scanned the path from its start and computed a segment heading with `atan2f`, and every nearby car's box was rebuilt (with its sine and cosine) at every sample.
- **Recovery.** In `chase`, 1.43 full immediate checks ran per frame, because nine out of ten covered checks overflowed the 12 recorded actors near crowded sidewalks. Each force step then tested about 45 actors, and nine out of ten rail-car forecast boxes were built only to be culled by distance.

## Changes

| Change | Decisions | Effect |
|---|---|---|
| `ScanPath` samples its points first, continues along the path from the previous sample, computes each segment heading once, and tests only the cars and grid people that can reach the sampled area | Unchanged | Rail stage 0.18 → 0.09–0.13 ms |
| Recovery rollouts put distant actors to sleep until they could be within reach again ([Traffic › Planning budget](traffic.md#planning-budget)) | Unchanged | Actor tests per frame in `chase` 23,077 → 945 |
| A moving rail car's forecast box is built only when its route centre comes within the largest possible reach | Unchanged | Forecast boxes per frame in `rampage` 514 → 86 |
| `VehicleForces` and the rollout box share one sine/cosine evaluation per step; per-sub-step exponential factors are computed once per step length | Unchanged | Force step cost down by about a third |
| No neighbourhood for holds between planning jobs; no initial-contact test beyond both enclosing circles; forecast cache emptied by an epoch; pedestrian grid clears only its used cells | Unchanged | Gather, snapshot and grid stages smaller |
| Covered immediate checks record up to 32 actors instead of 12 | **Changed** in crowded city scenes | Full immediate checks in `chase` 1.43 → 0.56 per frame |

## Exactness

Each exact optimisation is a conservative cull, a cache of a pure function keyed on its exact input, or a reordering that keeps the test order: an actor that is skipped could not have overlapped, and the first rejecting actor (the recorded blocker) is the same. To check it, the city logs of every intermediate build were compared line by line (knocks, rejoins, plans, rejected candidates, holds, stopped reasons, jolts, pedestrian metrics) with the base build, and the final fixtures with the second increment's `final3` evidence:

| Fixture | Cases / checks | Result lines differing from `final3` (timings excluded) |
|---|---|---|
| Recovery (Taxi, Bus, BoxTruck) | 18 phases, 156 checks | 0 |
| Clearance | 8 cases, 26 checks | 0 |
| Cooperative yielding | 60 cases | 0 |
| Driver incidents | 80 cases | 0 |

The fixtures never had more than 12 relevant actors, so the larger covered-check capacity does not reach them. In the city it changes which frames run a full immediate check, and the scenes diverge from there: `rampage`, for example, ended with no unresolved mutual pair instead of one.

## Reproducible on-foot runs

`foot` and `day` gave different traffic and pedestrian counts on two runs of the same executable. On foot, the camera looks ahead towards the mouse cursor, and the camera decides what is on screen and therefore which cars and people are recycled; the runs depended on where the cursor happened to rest. Test runs now use the screen centre. With that fixed, `foot` from the base revision and from this change match line by line.

## Limits and remaining work

- The covered-check capacity change is the only behaviour change; its city effect is measured above but not isolated in a fixture.
- Single frames of 2–2.6 ms remain where a planning job and several immediate checks coincide; the planning budget (`planning_steps 600`) is unchanged.
- Roles for two knocked cars, junction gridlock, the shouting sound, the raised-fist gesture and lane changes without sideways sliding are the next parts of this increment. The user playtest is pending.
