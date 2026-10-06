# CJ-016 third increment results

The third CJ-016 increment (2026-10-06) brings the driver decision CPU of every city scenario under the approved targets, resolves wait-for loops that the pair rule left standing (two knocked cars blocking each other, and junction gridlock), and finishes the incident presentation: shouting, a raised fist, and lane changes without sideways sliding. `chase` fell from 0.689 / 1.415 ms (average / 95th percentile) to 0.321 / 0.819 ms and `rampage` from 0.727 / 1.547 ms to 0.378 / 0.861 ms; `crash` now has a wide margin at 0.234 / 0.484 ms. Apart from one deliberate capacity change, the optimisations leave every decision bit for bit unchanged, and all four fixtures (182 isolated checks, 60 yielding and 80 incident cases) reproduce the second increment exactly. Two knocked cars and four cars locked in a junction box went from 0 to 20 of 20 resolved fixture cases each. Arguing drivers now shout and shake a raised fist, and rail cars change lane by steering: the rear-axle slip of a yielding car tucking back into its lane fell from 90° to 2°. The user playtest is pending.

## Contents

- [Evidence](#evidence)
- [Driver decision CPU](#driver-decision-cpu)
- [Where the time went](#where-the-time-went)
- [Changes](#changes)
- [Exactness](#exactness)
- [Reproducible on-foot runs](#reproducible-on-foot-runs)
- [Wait-for cycles](#wait-for-cycles)
- [Incident presentation](#incident-presentation)
- [Lane changes](#lane-changes)
- [Limits and remaining work](#limits-and-remaining-work)

## Evidence

| Run | Manifest | Build |
|---|---|---|
| City, before | `build/shots/cj002/after/cj016-s3-base-20261006/manifest-20261006T075116130257Z-city-47256.json` | Revision `95ea8ad` (`04093f9` in the manifests, before the commit e-mail rewrite of 2026-10-06; the code is unchanged) with the stage profiler |
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

## Wait-for cycles

The end-of-run `unresolved mutual pair` that the second increment reported in `crash` and `rampage` turned out to be a pair that had just formed: the new `TRAFFIC wait cycles` metric shows that no loop in the city runs lasted longer than 1.9 s on the CPU build (for example `crash` ended with a rail Taxi and a knocked Semi that had been waiting on each other for 0.3 s, before the 0.5 s detection time). Two knocked cars blocking each other, and loops of three or more drivers, therefore did not occur in the scripted city scenes; the new fixture situations stage them.

| Situation, rate | Before: resolved | After: resolved | After: resolved median / max (s) | After: completed max (s) | Roles |
|---|---|---|---|---|---|
| `knocked-pair` 60 Hz | 0 / 10 | 10 / 10 | 4.02 / 5.08 | 9.35 | 10 |
| `knocked-pair` 20 Hz | 0 / 10 | 10 / 10 | 3.65 / 4.85 | 9.35 | 10 |
| `junction-gridlock` 60 Hz | 0 / 10 | 10 / 10 | 3.06 / 3.10 | 7.37 | 10 |
| `junction-gridlock` 20 Hz | 0 / 10 | 10 / 10 | 3.05 / 3.10 | 7.30 | 10 |

The before phase keeps the second increment's pair rule (`YIELD cycles 0`). In `knocked-pair` each knocked car then held with `no_feasible_manoeuvre`, each waiting on the other, for the full 30 s: the rear car's full reversing manoeuvre does not fit in front of the wall behind it, and the front car can only reverse into it. After the change the rear car (it can move 70–85 px straight back; the front car, nose at the kerb, cannot move away at all) makes room with one short creep, the front car's hold replans as soon as its blocker moves, it reverses out and rejoins its lane, and then the rear car rejoins too. In `junction-gridlock` the four cars stood for 30 s; after the change the car with the most driven path behind it backs up 80 px (a Taxi) or 136 px (the Bus), the car waiting on it crosses, and the loop unwinds. In every case one role was taken, nobody flipped a role, no vehicle overlapped another or a wall and no rail car was knocked. The 60 earlier cases reproduce the second increment's results line by line.

### Evidence and city check

| Run | Manifest |
|---|---|
| Conflict fixture v2, before (`YIELD cycles 0`) | `build/shots/cj016-conflict/before/s3-cycles-20261006/manifest-20261006T102135131305Z-24064.json`: 60 / 100 cases |
| Conflict fixture v2, after | `build/shots/cj016-conflict/after/s3-cycles-20261006/manifest-20261006T102530160347Z-44656.json`: 100 / 100 cases |
| Recovery, clearance, incidents | `build/shots/cj016/after/s3-cycles-20261006/manifest-20261006T102753842290Z-56404.json`, `build/shots/cj016-clearance/after/s3-cycles-20261006/manifest-20261006T103227029224Z-4572.json`, `build/shots/cj016-incident/after/s3-cycles-20261006/manifest-20261006T103246755820Z-2676.json`: 156 / 156, 26 / 26 checks, 80 / 80 cases, every result line as in `final3` |
| City | `build/shots/cj002/after/cj016-s3-cycles-20261006/manifest-20261006T103733074407Z-city-4288.json` |

All used executable SHA-256 `b3899678c768dc155db7b449ecbd0ebad02455f0eae94c4522d4519903a59511` and `traffic.cfg` SHA-256 `d26c66b0b2737f2f1e53ee045eae58d715572f8172e571553afd8f32e7b1a82e`. In the six city scenarios no wait-for loop lasted over 10 s (the longest: 1.9 s in `rampage`), recovery give-ups stayed at zero, and driver decisions stayed within the targets (`chase` 0.347 / 0.839 ms, `rampage` 0.374 / 0.894 ms, `crash` 0.251 / 0.498 ms). `crash` still ends with the rail Taxi and the knocked Semi that met 0.3 s before the end.

## Incident presentation

Arguing drivers now shout and raise a fist; the punch frame is kept for blows. The `cj016-incident-v2` fixture measures it in its aggressive situations (10 seeds, 60 Hz and 20 Hz each). The before run is the same fixture on a build with the measurements but the second increment's behaviour.

| Situation | Before: shouts | After: shouts | Before: raised fist (s) | After: raised fist (s) | Before: punch frame while arguing face to face (s) | After |
|---|---|---|---|---|---|---|
| `aggressive-pair` | 0 | 8–10 | 0 | 3.45–4.43 | 1.00–1.50 | 0 |
| `aggressive-player` | 0 | 4–6 | 0 | 1.50–2.20 | 0.50–0.75 | 0 |

Before, 40 of 80 cases failed the new checks (every aggressive case); after, all 80 pass with every earlier check unchanged. The shouts are synthesised ([Audio › Shouts](audio.md#shouts)): three voices at a personal pitch. Their spectrograms show the intended formant glides; whether they sound convincing is for the playtest, and recorded voices remain part of [CJ-012](../backlog.md#cj-012-audio-overhaul). The raised fist is two new frames of the procedural civilian atlas in the existing style; the civilian art as a whole is still [CJ-004](../backlog.md#cj-004-replace-placeholder-art).

## Lane changes

`TRAFFIC slip` and the conflict fixture's `lane_change_slip_deg` measure the angle between a rail car's body and the motion of its rear axle point; a steered car's rear wheels do not slide sideways, so it should stay near 0°.

| Measurement | Before | After |
|---|---|---|
| `passing-head-on`, slip while changing lane (max of 20 cases) | 90.0° | 2.1° |
| `passing-head-on`, oncoming car through (median / max) | 5.18 / 5.72 s (60 Hz) | 5.23 / 5.80 s |
| `passing-head-on`, passing car past the parked car and back in lane (median / max) | 10.14 / 10.45 s (60 Hz) | 11.09 / 11.25 s |
| `chase`, slip while changing lane | 51.9 % of 3.0 car-s over 5°, max 90.0° | 0 % of 2.4 car-s, max 0.5° |
| Conflict fixture v2 | 80 / 100 cases (every `passing-head-on` case fails the slip check) | 100 / 100 |

Before, the yielding passing car slid straight sideways into its lane, 90° to its body. After, it reverses on an S-curve with its rear swinging in, holds while the oncoming car passes, then backs up about 40 px and pulls out round the parked car again; the whole manoeuvre takes about 0.9 s longer. In the other 80 conflict cases every metric is unchanged, as are the recovery (156 checks) and clearance (26 checks) fixtures line by line.

The measurement also exposed a separate problem: rail cars slide through turns. A right turn's curve has a radius of about 2 m and both axle samples stay on it, so in every city scenario 70–74 % of the turning time shows over 5° of rear-axle slip, up to 70–75°. This is not a lane change and is unchanged here; it is backlog item [CJ-020](../backlog.md#cj-020-turning-kinematics-of-traffic).

### Evidence

| Run | Manifest or log |
|---|---|
| Before (measurements on the wait-for cycle build) | `build/shots/s3pres/before-conflict/` (conflict, incident and six city logs, run directly with `build/ConcreteJungle-s3-metrics-before.exe`) |
| Conflict fixture v2, after | `build/shots/cj016-conflict/after/s3-present-20261006/manifest-20261006T115346878515Z-47340.json`: 100 / 100 |
| Incident fixture v2, after | `build/shots/cj016-incident/after/s3-present2-20261006/manifest-20261006T121326195536Z-40376.json`: 80 / 80 (the first run, `s3-present-20261006`, passed every case but its runner still expected the v1 fixture name) |
| Recovery, clearance | `build/shots/cj016/after/s3-present-20261006/manifest-20261006T120023300283Z-57576.json`, `build/shots/cj016-clearance/after/s3-present-20261006/manifest-20261006T120453782259Z-9368.json` |
| City | `build/shots/cj002/after/cj016-s3-present-20261006/manifest-20261006T120513462310Z-city-47052.json` |

All after runs used executable SHA-256 `18b92d616fadae520c3cd4ced1e37804eadefbffe40343bd598ddce2b24abe8f` and `traffic.cfg` SHA-256 `d26c66b0b2737f2f1e53ee045eae58d715572f8172e571553afd8f32e7b1a82e`. Driver decisions stayed within the targets in five scenarios of the city run (`chase` 0.331 / 0.826 ms). `rampage` measured 0.451 / 1.050 ms in that run; the before build, with the same decision code, measured 0.519 / 1.195 ms in the same time window, and two repeats of the after build right afterwards measured 0.393 / 0.884 and 0.391 / 0.892 ms (`build/shots/s3pres/cpu/`). The machine was evidently busier during that window; the `rampage` 95th percentile has about 10 % margin in a quiet one.

## Limits and remaining work

- The covered-check capacity change is the only behaviour change; its city effect is measured above but not isolated in a fixture.
- Single frames of 2–2.6 ms remain where a planning job and several immediate checks coincide; the planning budget (`planning_steps 600`) is unchanged.
- Wait-for loops are staged in fixtures; the scripted city scenes produced none that lasted. The fixtures cover one geometry each (a knocked pair at a kerb, a four-car box), with one seed family.
- The shouts are synthesised and the fist frames procedural, both stand-ins until CJ-012 and CJ-004; turning traffic still slides (CJ-020).
- The user playtest of queues, two-car conflicts, heavy-vehicle reversing, a full confrontation (now with shouting) and lane changes is pending.
