# CJ-016 recovery results

The corrected recovery foundation passed all 182 isolated checks on 2026-10-05 and completed six city regressions. Drivers remain present during blocked recovery, feasible escape uses actual physical controls, and nearby separated boxes no longer falsely veto rejoin. City CPU acceptance, broader conflict resolution and the user playtest remain open; this is a measured first increment, not completion of CJ-016. The report preserves exact current evidence and earlier failed or incomplete attempts.

## Contents

- [Corrected build verification](#corrected-build-verification)
- [Evidence](#evidence)
- [Phase results](#phase-results)
- [Decision cost](#decision-cost)
- [First city run](#first-city-run)
- [Nearby-box diagnosis](#nearby-box-diagnosis)
- [Preserved attempts](#preserved-attempts)
- [Limits and remaining work](#limits-and-remaining-work)

## Corrected build verification

The session's current build completed all **156 recovery checks and 26 clearance checks** with no failures. It also completed all six city processes; process completion is separate from acceptance. Its executable SHA-256 is `d7d219a73a9b0ead9fc8f61a277d31baa8236222cb57f7d391ea058ad108d8ff`, identical in all three after manifests and in the current executable:

- Recovery: `build/shots/cj016/after/final-fixed-recovery/manifest-20261005T170949084892Z-32580.json`.
- Clearance: `build/shots/cj016-clearance/after/depth-contract/manifest-20261005T170916382681Z-48360.json`.
- City: `build/shots/cj002/after/cj016-final-fixed-20261005/manifest-20261005T212244877519Z-city-23408.json`.

The recovery and clearance runners verified complete schedules, counts, labels and final captures. The frozen recovery source and vehicle configuration hashes remain the same as the baseline below. The clearance source hash is `1ea244a311cb74276db401f449f1348d312257f177280a94afac6892bba6907d`; its before/after source hashes match. Traffic configuration SHA-256 remains `1516621ca36e7ed05fc900c9abc066fa038b473b30aee9c9b24dc18ef8e58b06`. City manifests fingerprint the executable and vehicle configuration; matching the executable to the richer fixture manifests associates the exact compiled recovery implementation and configuration with these city measurements.

All 18 recovery phases had zero penetration, ownership-loss samples, discontinuous pose jumps, non-finite state and rejoin blending. Enclosed cars held their actual pose with the driver present for 60 s at both rates. Free and garage rejoin times for this corrected build are:

| Class | Free 60 Hz (s) | Free 20 Hz (s) | Garage 60 Hz (s) | Garage 20 Hz (s) |
|---|---|---|---|---|
| Taxi | 7.100 | 7.000 | 10.833 | 10.600 |
| Bus | 11.367 | 11.500 | 20.967 | 21.100 |
| BoxTruck | 7.633 | 7.600 | 13.767 | 13.900 |

Reverse distances remain equal to the pre-fix physical results below. The bool-return fix permits an earlier safe handoff after physical alignment beside the garage walls; no alignment threshold, clearance, horizon, control or vehicle handling parameter was relaxed. The slowest Bus case now has 8.9 s of margin against this fixture's 30 s target. These figures still cover only the prescribed poses and one seed.

| Class | Decision average range (ms/physics step) | Decision p95 range (ms/physics step) |
|---|---|---|
| Taxi | 0.030514–0.069677 | 0.099300–0.318600 |
| Bus | 0.040123–0.104957 | 0.111100–0.503300 |
| BoxTruck | 0.030085–0.083914 | 0.104300–0.413000 |

These are per-phase minimum/maximum ranges, not pooled percentiles. Screenshots at reverse, turn and aligned/rejoined checkpoints were inspected for all three classes: the full body clears the garage before the outside turn; no sampled wall crossing or pose jump was seen. Stable enclosed captures and the nearby-box before/after series were also inspected. Screenshot sampling cannot establish continuous safety on its own; the per-physics-step metrics and predicted footprints provide the additional checks.

### Corrected city runs

The six runs use the same flags and durations as the city reference below. Every process exited 0 and produced its complete screenshot series: 50 screenshots per ordinary case and 30 for rampage. Full driver timing includes police, snapshot preparation, the pedestrian grid and fire/cleanup, so it conservatively bounds traffic decisions.

| Scenario | Knocked / rejoined / gave up | Max overlap (px) / deep frames | Driver avg / p95 / worst (ms/frame) | Vehicle avg (ms/frame) | Pedestrian avg (ms/frame) | CPU bands |
|---|---|---|---|---|---|---|
| `crash` | 10 / 1 / 0 | 0.2 / 0 | 0.6838 / 2.7563 / 5.5204 | 0.899 | 0.413 | FAIL: driver average, driver p95 |
| `derby` | 1 / 0 / 0 | 0.1 / 0 | 0.3076 / 0.5024 / 1.2781 | 0.473 | 0.444 | Within measured bands |
| `chase` | 5 / 1 / 0 | 0.2 / 0 | 0.3130 / 0.6252 / 4.9447 | 0.479 | 0.413 | Within measured bands |
| `foot` | 0 / 0 / 0 | 0.0 / 0 | 0.2714 / 0.4565 / 1.1422 | 0.430 | 0.419 | Within measured bands |
| `day` | 0 / 0 / 0 | 0.0 / 0 | 0.2937 / 0.4921 / 1.2495 | 0.460 | 0.437 | Within measured bands |
| `rampage` | 23 / 11 / 0 | 0.2 / 0 | 1.1487 / 3.9039 / 11.5995 | 1.362 | 0.425 | FAIL: driver average, driver p95, vehicle average |

Successful city rejoins now occur, and every case records zero recovery give-ups. This does not guarantee recovery of every knocked car: real contacts, moving obstructions, alignment, late impacts and the bounded planner can still prevent completion. End-of-run abandoned vehicles include fire or other causes and are not timeout abandonment counts. The lower overlap in these changed city trajectories does not close the recorded baseline contact-solver defect.

CPU acceptance remains open wherever the table reports a failed band. Cached geometry and conditional hold prediction reduce redundant work, but simultaneous urgent forecasts and full planning jobs still need measured scheduling/budget work before full-population acceptance. CJ-016 remains In progress: mutual yielding/reservations, ten-seed/moving-actor/edge fixtures, persistent driver incidents, exit/fight/re-entry and the user playtest remain next work. This increment establishes the measured recovery foundation and retains the stress-test failures.

## Evidence

Fixture `cj016-recovery-v1`, seed `0x000c0016`, runs Taxi, Bus and BoxTruck through the same six phases: enclosed 60 s, free 30 s and reverse-garage 30 s, repeated at 60 Hz and 20 Hz physics intervals. Each class has 52 checks, 14,400 rendered frames, 9,600 physics steps and 240 s. See [Testing](../testing.md#cj-016-recovery-measurements) for the geometry, tolerances and runner commands.

| Evidence | Manifest | Completion |
|---|---|---|
| Before | `build/shots/cj016/before/complete-timing/manifest-20261005T094031217478Z-31016.json` | All 18 phases complete; 14 failed checks per class |
| Pre-fix passing physical after | `build/shots/cj016/after/final-recovery/manifest-20261005T160936155397Z-52148.json` | All 18 phases complete; all 156 isolated checks passed; all three fixture cases accepted; precedes the separated-box contact fix and does not establish city acceptance |

Both manifests record dirty revision `1021abecdc18cb1b2e03cc2bda9917b56ce857a0`; the exact input fingerprints distinguish their measurement builds from the committed production code. The fixture implementation and vehicle configuration hashes match before/after. Logs and labelled screenshots remain beside each manifest in `build/`. Each pre-fix after class completed 52 checks with zero failures, no invalid/incomplete flag, exit status 0 and its final screenshot present.

| Input SHA-256 | Before | Pre-fix passing physical after |
|---|---|---|
| `ConcreteJungle.exe` | `b94600da6d296ab74a329eb5044843873b6f2bcc847cb9d0552af378d4251850` | `0fcab6511870ad394d46467bdce694485c30a54898634a77f2cb01eb814ac114` |
| `assets/data/vehicles.cfg` | `ee13108fa9692b3725bb4fb8641a9b4acfad57b5529355903efa012447e767d8` | `ee13108fa9692b3725bb4fb8641a9b4acfad57b5529355903efa012447e767d8` |
| `assets/data/traffic.cfg` | Absent on the legacy build | `1516621ca36e7ed05fc900c9abc066fa038b473b30aee9c9b24dc18ef8e58b06` |
| `src/traffic_tests.cpp` | `cd10f7a24b77228012e6ba0df4ed22e154488c4b7b5d713d04c7213c85349b4d` | `cd10f7a24b77228012e6ba0df4ed22e154488c4b7b5d713d04c7213c85349b4d` |

## Phase results

Every cell shows **before → pre-fix passing physical after**. A rejoin value of −1 s means no completed rejoin; this is expected in the enclosed phase. Baseline free cases rejoined using the forbidden pose blend, so their short times still fail acceptance. `ownership_losses` counts violating physics-step samples after a loss, not unique drivers or separate abandonment events. Reverse distance is accumulated backwards travel; it is not net displacement.

| Class | Phase | Failed checks | Rejoin (s) | Reverse (px) | Max overlap (px) | Ownership-loss samples |
|---|---|---|---|---|---|---|
| Taxi | `enclosed-60hz` | 4 → 0 | −1.000 → −1.000 | 42.221 → 0.000 | 0.002 → 0.000 | 2838 → 0 |
| Taxi | `free-60hz` | 1 → 0 | 2.483 → 7.100 | 0.000 → 0.000 | 0.000 → 0.000 | 0 → 0 |
| Taxi | `garage-60hz` | 2 → 0 | −1.000 → 15.900 | 358.505 → 284.955 | 0.317 → 0.000 | 1038 → 0 |
| Taxi | `enclosed-20hz` | 4 → 0 | −1.000 → −1.000 | 50.139 → 0.000 | 0.004 → 0.000 | 947 → 0 |
| Taxi | `free-20hz` | 1 → 0 | 2.650 → 7.000 | 0.000 → 0.000 | 0.000 → 0.000 | 0 → 0 |
| Taxi | `garage-20hz` | 2 → 0 | −1.000 → 15.700 | 344.648 → 284.543 | 0.390 → 0.000 | 347 → 0 |
| Bus | `enclosed-60hz` | 3 → 0 | −1.000 → −1.000 | 43.581 → 0.000 | 0.109 → 0.000 | 2838 → 0 |
| Bus | `free-60hz` | 1 → 0 | 3.283 → 11.367 | 0.000 → 0.000 | 0.000 → 0.000 | 0 → 0 |
| Bus | `garage-60hz` | 3 → 0 | −1.000 → 28.433 | 117.821 → 467.642 | 0.178 → 0.000 | 1038 → 0 |
| Bus | `enclosed-20hz` | 3 → 0 | −1.000 → −1.000 | 38.950 → 0.000 | 0.133 → 0.000 | 947 → 0 |
| Bus | `free-20hz` | 1 → 0 | 3.250 → 11.500 | 0.000 → 0.000 | 0.000 → 0.000 | 0 → 0 |
| Bus | `garage-20hz` | 3 → 0 | −1.000 → 28.900 | 121.196 → 467.541 | 0.220 → 0.000 | 347 → 0 |
| BoxTruck | `enclosed-60hz` | 4 → 0 | −1.000 → −1.000 | 36.796 → 0.000 | 0.002 → 0.000 | 2838 → 0 |
| BoxTruck | `free-60hz` | 1 → 0 | 2.750 → 7.633 | 0.000 → 0.000 | 0.000 → 0.000 | 0 → 0 |
| BoxTruck | `garage-60hz` | 2 → 0 | −1.000 → 19.900 | 199.365 → 372.676 | 0.164 → 0.000 | 1038 → 0 |
| BoxTruck | `enclosed-20hz` | 4 → 0 | −1.000 → −1.000 | 36.223 → 0.000 | 0.129 → 0.000 | 947 → 0 |
| BoxTruck | `free-20hz` | 1 → 0 | 2.950 → 7.600 | 0.000 → 0.000 | 0.000 → 0.000 | 0 → 0 |
| BoxTruck | `garage-20hz` | 2 → 0 | −1.000 → 19.900 | 195.788 → 372.816 | 0.163 → 0.000 | 347 → 0 |

All pre-fix physical phases had zero overlap, ownership-loss samples, discontinuous pose jumps, non-finite state and rejoin blending. Enclosed cars stayed at their initial pose without continuing throttle into walls. The 20 Hz Bus garage recovery took 28.9 s, leaving only 1.1 s of margin against this fixture's target; broader heavy-vehicle acceptance still needs more poses and seeds.

The pre-fix physical screenshot series was reviewed for all three classes: the enclosed car remains stable, the full body reverses beyond the garage side walls before turning outside them, and lane rejoin is continuous. This automated visual review does not replace the user playtest.

## Decision cost

These are the minimum and maximum phase averages and phase 95th percentiles across each class's six pre-fix physical cases. They are not pooled percentiles across the suite. Isolated timing covers the observation snapshot and one driver; physics and drawing are outside it. City `DRIVER DECISION CPU` additionally includes vehicle preparation, fire/explosions/cleanup, pedestrian grid construction and police AI, conservatively bounding all traffic decision work.

| Class | Decision average range (ms/physics step) | Decision p95 range (ms/physics step) |
|---|---|---|
| Taxi | 0.042067–0.106854 | 0.121500–0.557500 |
| Bus | 0.048974–0.139388 | 0.131300–0.733900 |
| BoxTruck | 0.037206–0.112203 | 0.117600–0.627300 |

Before CPU costs are artificially low after a driver abandons the car. They do not represent equal completed work and are not used to claim a speedup. The preserved initial passing Taxi garage-20hz attempt measured 0.503846 ms average and 4.124800 ms p95, versus the pre-fix physical after run's 0.088194 ms average and 0.215000 ms p95. This documents the optimization outcome alongside changed manoeuvre decisions; it is not a controlled microbenchmark. Current costs are recorded in the corrected build verification above.

The first city `crash` attempt exceeded acceptance: full driver decisions averaged 0.8689 ms/frame with 2.9536 ms p95 and 12.2901 ms worst, recovery averaged 0.6030 ms/frame with 2.6470 ms p95, and total vehicle update averaged 1.082 ms/frame. The driver targets are 0.5 ms average and 1.0 ms p95; the vehicle target is 1.0 ms average. The complete six-city evidence below also fails those CPU bands in `chase` and `rampage`. Those measurements motivated geometry-cache/cull and conditional-hold optimizations. The corrected build verification above records the new complete fixture and city evidence; remaining CPU failures still prevent full acceptance.

## First city run

All six city processes completed with exit status 0 and screenshots, but this physical recovery build **does not meet city acceptance**. The evidence is preserved in `build/shots/cj002/after/cj016-recovery-20261005/manifest-20261005T161444495450Z-city-12584.json`. Its executable SHA-256 is `0fcab6511870ad394d46467bdce694485c30a54898634a77f2cb01eb814ac114`, matching the passing isolated build. The before city reference is `build/shots/cj002/before/review-20261003/manifest-20261003T151315124021Z-city-34016.json`, executable SHA-256 `f8c37c6b3113b36e8f14b70ef891a1c4697d9da318b6ba73d320b513d92baa86`. Vehicle configuration hashes match. These are 1,500-frame runs except `rampage`, which runs 3,600 frames.

Every comparison cell shows **before → first physical city run**. Traffic counts are knock events / completed rejoins / recovery give-ups, not end-of-run abandoned-car counts.

| Scenario | Position flips | Heading flips | Max overlap (px) | Deep frames | Traffic knocked / rejoined / gave up | Vehicle average (ms/frame) |
|---|---|---|---|---|---|---|
| `crash` | 11 → 7 | 30 → 11 | 0.3 → 0.2 | 0 → 0 | 11 / 9 / 1 → 10 / 0 / 0 | 0.542 → 1.082 |
| `derby` | 3 → 3 | 3 → 3 | 0.1 → 0.1 | 0 → 0 | 1 / 1 / 0 → 1 / 0 / 0 | 0.418 → 0.523 |
| `chase` | 1 → 4 | 10 → 13 | 0.1 → 0.2 | 0 → 0 | 5 / 5 / 0 → 4 / 0 / 0 | 0.442 → 0.895 |
| `foot` | 0 → 0 | 0 → 0 | 0.0 → 0.0 | 0 → 0 | 0 / 0 / 0 → 0 / 0 / 0 | 0.422 → 0.486 |
| `day` | 0 → 0 | 0 → 0 | 0.0 → 0.0 | 0 → 0 | 0 / 0 / 0 → 0 / 0 / 0 | 0.437 → 0.505 |
| `rampage` | 22 → 12 | 62 → 30 | 5.4 → 0.5 | 4 → 0 | 36 / 25 / 8 → 23 / 0 / 0 | 0.557 → 2.805 |

| Scenario | Driver average (ms/frame) | Driver p95 (ms/frame) | Driver worst (ms/frame) | CPU bands |
|---|---|---|---|---|
| `crash` | 0.8689 | 2.9536 | 12.2901 | FAIL: driver average/p95 and vehicle average |
| `derby` | 0.3401 | 0.5426 | 1.4491 | Within measured driver/vehicle bands |
| `chase` | 0.7145 | 1.5723 | 5.4058 | FAIL: driver average/p95 |
| `foot` | 0.3097 | 0.5371 | 0.9945 | Within measured driver/vehicle bands |
| `day` | 0.3233 | 0.5412 | 1.0743 | Within measured driver/vehicle bands |
| `rampage` | 2.5818 | 6.6903 | 14.4683 | FAIL: driver average/p95 and vehicle average |

These pre-fix city runs recorded no completed rejoin, including `crash`, `derby`, `chase` and `rampage` where cars were knocked off their lane. Zero give-ups confirms that the old recovery timeout did not abandon drivers; it does not establish successful recovery in general city traffic. The later regression reproduced a false initial-contact classification that can veto otherwise ready cars whenever a separated box is nearby. This is a concrete source defect, not evidence that every unresolved city case was geometrically impossible; late impacts, alignment and real moving obstructions can also prevent a handoff. Post-fix general city recovery, moving obstacles and shared conflicts remain unaccepted.

The lower `rampage` overlap and changed event counts are **not a contact-solver fix**. Recovery and recycling behaviour change actor trajectories and how the shared gameplay RNG is consumed; the city scenes therefore diverge despite the same scripted scenario. The previously recorded 5.4 px/4-deep-frame defect remains a known baseline finding. The isolated, frozen geometry demonstrates the bounded physical recovery result; these city differences alone cannot prove a solver improvement.

## Nearby-box diagnosis

`OBBOverlap` sets a large initial depth and may return false on a separating axis without clearing that output. `AddNearby` previously discarded the boolean and stored the positive depth. `RecoveryCanRejoin` then treated a separated building/vehicle box as initial contact and rejected rejoin. The source fix stores zero depth unless the overlap result is true. Actual contact and the configured clearance margin remain separate vetoes; `RejoinCause` now distinguishes them from unsafe sweep, incomplete stop, missing observations and capacity limits. See [Testing](../testing.md#nearby-box-clearance-regression) for the diagnostic meanings and regression contract.

The separate frozen fixture `cj016-clearance-v1`, seed `0x000c0016`, preserves the original recovery fixture unchanged. It completes eight Taxi cases at 60 Hz/20 Hz, 26 checks, 420 rendered frames, 280 explicit API calls and 160 real physics steps. The pre-fix manifest is `build/shots/cj016-clearance/before/depth-contract/manifest-20261005T170722846100Z-35064.json`: all cases and required captures completed, exit status 1, complete evidence, four failed checks. Its executable SHA-256 is `90c5c93b13a608d41316e716d0ccda4c743b3c07364bf420168cdc121a18b311`; clearance source SHA-256 is `1ea244a311cb74276db401f449f1348d312257f177280a94afac6892bba6907d`. Vehicle/traffic configuration hashes match the pre-fix physical build above.

| Clearance case | Pre-fix failed checks | Pre-fix result |
|---|---|---|
| `clear-nearby-boxes-60hz` | 2: API expectation, actual rejoin | No rejoin within 2 s |
| `actual-overlap-60hz` | 0 | Correctly rejects; body and ownership unchanged |
| `clearance-margin-only-60hz` | 0 | Correctly rejects; body and ownership unchanged |
| `forward-blocker-60hz` | 0 | Correctly rejects; body and ownership unchanged |
| `clear-nearby-boxes-20hz` | 2: API expectation, actual rejoin | No rejoin within 2 s |
| `actual-overlap-20hz` | 0 | Correctly rejects; body and ownership unchanged |
| `clearance-margin-only-20hz` | 0 | Correctly rejects; body and ownership unchanged |
| `forward-blocker-20hz` | 0 | Correctly rejects; body and ownership unchanged |

All API body/driver preservation checks passed before and after. The after manifest `build/shots/cj016-clearance/after/depth-contract/manifest-20261005T170916382681Z-48360.json` completed all 26 checks with zero failures and exit status 0. Both clear cases actually rejoined at 0.700 s; all six obstacle cases still rejected the API handoff. Counts, labelled captures and final screenshot were complete; before/after screenshots show separated boxes remain clear and the guard geometry remains present. Its executable matches the corrected recovery/city build above.

## Preserved attempts

These runs remain in `build/shots/cj016/`; none replaces the final binary's required evidence.

| Run and manifest | Result | Role |
|---|---|---|
| `after/first-recovery/manifest-20261005T094845541872Z-53588.json` | Taxi: 0 failed checks | Initial passing single-class recovery attempt |
| `after/first-recovery/manifest-20261005T095451640351Z-55612.json` | Bus: 2 failed checks | Revealed heavy-vehicle garage rejoin failure |
| `after/bounded-recovery/manifest-20261005T100613495879Z-43648.json` | Taxi, Bus, BoxTruck: 2 failed checks each | Preserved failed optimization attempt |
| `after/aligned-recovery/manifest-20261005T101819106131Z-18128.json` | All 156 checks passed | Passing intermediate before further broad-phase, stopping-tail and numeric safety hardening |

The original `before` attempt also remains: it completed with 14 failed checks per class but truncated its long timing lines. The `complete-timing` baseline rerun preserved the same geometry and checks, reproduced those failures and retained complete timing.

## Limits and remaining work

This evidence uses one fixed seed, three classes and mostly static geometry. It does not establish the specification's ten-seed acceptance, all shipped vehicle classes, moving-gap negotiation, mutual yielding, driver ownership on foot, incident interruption or confrontation/combat behaviour. Ordinary traffic stays on rails under Accepted [ADR-0004](../adr/0004-kinematic-rail-traffic.md); [ADR-0008](../adr/0008-human-like-traffic.md) stays Proposed, and CJ-002 handling remains pending.

Other physical vehicles are forecast from their current linear and angular velocity; pedestrians are forecast from their current velocity. Simultaneously starting recovery moves that cross each other's future path do not yet reserve space or negotiate intent. This increment does not prove guaranteed multi-vehicle conflict resolution. Shared conflict decisions and reservations belong to the next increment.

The isolated fixture disables world-edge contacts. Rollouts check world-edge endpoints at each force sample, but a dedicated city-edge recovery acceptance fixture is still missing.

`no_feasible_manoeuvre` means no safe candidate was found within the bounded local planner, not that every possible global escape is impossible. Only the fully enclosed fixture has known impassable geometry. The first city suite completed but failed CPU acceptance and demonstrated no rejoins. The corrected full series above establishes isolated clearance/recovery and observed city rejoins, while retaining CPU failures. Full-population CPU acceptance, general city recovery and the user playtest remain open; [CJ-016](../backlog.md#cj-016-road-rage-and-traffic-incidents) remains In progress.
