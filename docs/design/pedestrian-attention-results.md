# Pedestrian attention and evasion measurements

Before and after [CJ-038](../backlog.md#cj-038-pedestrian-attention-and-evasion), measured with the [agreed plan](pedestrian-attention-proposal.md). The city escape range is a game target; the research informs the behaviour rather than prescribing a population escape rate. The focused escape, traffic-hit and CPU checks pass, but the agreed retained-crossing criterion is not met. User playtest acceptance is pending with that known regression.

## Contents

- [Evidence](#evidence)
- [City](#city)
- [Diagnosis and tuning](#diagnosis-and-tuning)
- [Isolated scenes](#isolated-scenes)
- [Limits and playtest](#limits-and-playtest)

## Evidence

`python tools/run_cj038.py --phase before --run-name baseline-city --suite city` records the executable, scene/configuration/source hashes and git state in `build/shots/cj038/before/baseline-city/manifest.json`. The measurement-only harness was built on revision `90a7a3373710`; production pedestrian behaviour had not changed. The old executable is retained as `build/shots/cj038/baseline/ConcreteJungle-before.exe`, alongside copies of the original civilian configuration and evasion scenes.

The isolated before measurement is `build/shots/cj038/before/baseline-evasion/manifest.json`; its original scene file is retained as `build/shots/cj038/baseline/person-evasion-before.cfg`. The current expanded file is not the old executable's before fixture. Explicit scene seeds use a separate layout generator, so adding personal random draws does not move the crowd between builds. The original, unseeded CJ-035 scenes retain their previous seed and layout convention. Positions, facings and the person skins in the seven comparison scenes are the same before and after.

The 2026-10-12 implementation evidence is under `after/initial-city` and `after/initial-evasion`, with later `after/diagnostic-day*` logs. Its saved source/configuration hashes differ from the WIP sources brought onto the current `main`; those old measurements are retained, but do not describe the freshly compiled WIP. After integration with the CJ-040 radio and 0.7.0, revision `f4a561821182` was built and measured in `after/integrated-before-fix` before the continuation's traffic fix and tuning. The radio sources are unchanged; its [city measurements](../testing.md#radio-scenes) preserve the simulation metrics, so the original before measurement remains valid.

The final focused evidence is `build/shots/cj038/after/final-suite/manifest.json`: the evasion fixture and all three city scenarios completed, exited with code 0 and passed the runner's explicit primary checks. Its `accepted=true` does not cover the agreed retained-crossing criterion, which remains unmet. The runner preserves failures even when a process exits successfully, refuses to overwrite an existing evidence directory and runs one window at a time. City captures are saved every 300 frames; isolated scenes request captures at named times. `--suite all` now means the evasion fixture plus `foot`, `day`, `rampage`, matching the requested test scope; impact and turning regressions are optional suites.

## City

Fixed 1/60 s clock, `foot` and `day` for 1,500 frames, `rampage` for 3,600 frames, uncapped on the same machine.

| Metric | Before | After | Criterion |
|---|---|---|---|
| `foot`: traffic hits | 0 | 0 | 0 |
| `day`: traffic hits | 0 | 0 | 0 |
| `rampage`: people in the car's path / hit | 122 / 26 | 112 / 47 | Record both counts |
| `rampage`: escaped | 78.69 % | 58.04 % | 50–65 % |
| `foot`: pedestrian CPU | 0.600 ms | 0.438 ms | At most 0.5 ms |
| `day`: pedestrian CPU | 0.479 ms | 0.464 ms | At most 0.5 ms |
| `rampage`: pedestrian CPU | 0.477 ms | 0.411 ms | At most 0.5 ms |
| `foot` / `day`: crossing at traffic green | 2.5 person-s | 12.0 person-s | Retain earlier crossing checks: **not met** |

The first before `foot` run already exceeded the CPU target and averaged 3.32 visible people, below CJ-010's historical target of four. These are baseline failures, not successful checks. The body-scan-only continuation also exceeded the CPU budget in `foot` (0.510 ms). An after/before/after `day` sequence for the 1.1 s / 4.0 m/s candidate measured 0.464 / 0.466 / 0.398 ms, with zero traffic hits in each run; manifests are `after/final-suite`, `before/baseline-day-timing` and `after/final-day-timing`. The old executable's SHA-256 is `1188412c51374dc47613e4ee6d21a84b84792fda09688d8c4e5c866fe7dc62eb`, matching the original before executable. Every field its civilian loader reads matches the old configuration; it ignores the added records and fields. This sequence meets the CPU budget without establishing a general speed improvement from a single timing pair.

Other city observations remain visible alongside the requested acceptance checks:

| Metric | Before | Final |
|---|---|---|
| `foot` / `day`: visible people | 3.32 | 3.38 |
| `foot` / `day`: fleeing / dodging average | 0.39 / 0.34 | 0.00 / 0.11 |
| `foot` / `day`: on the road off a crossing | 0.00 | 0.00 |
| `foot` / `day`: pedestrian overlaps | 1.44/s | 1.44/s |
| `foot` / `day`: still crossing at the traffic's green | 2.5 person-s | 12.0 person-s |
| `rampage`: still crossing at the traffic's green | 10.2 person-s | 20.6 person-s |
| Sliding while moving, all three scenarios | 0.1 % | 0.1 % |

The visible population remains below the older target of four, and time spent on crossings at the traffic's green increased despite zero normal-city hits. The crossing increase is a real secondary regression and fails the agreed retained-crossing criterion. The runner's final acceptance covers the focused fixture, escape, traffic-hit and CPU requirements; it is not a claim that every historical CJ-010 metric is now met. In `rampage`, traffic hit 19 people and the player 47; the zero-traffic-hit target applies to `foot` and `day`.

### Retained intermediate runs

All listed city runs completed, even when acceptance failed. A dash means the scenario was not run in that tuning trial. Each named run has a manifest under `build/shots/cj038/after/<name>/`.

| Run | `foot` / `day` traffic hits | `rampage` path / hit | Escaped | Acceptance issue |
|---|---|---|---|---|
| `initial-city` (2026-10-12) | 3 / 3 | 100 / 29 | 71.00 % | Traffic hits; escape too high |
| `integrated-before-fix` | 0 / 0 | 120 / 37 | 69.17 % | Escape too high |
| `body-scan-only` | 0 / 0 | 99 / 32 | 67.68 % | Escape too high; `foot` CPU 0.510 ms |
| `attentive-13` (1.3 s notice horizon) | 0 / 0 | 116 / 36 | 68.97 % | Escape too high; five false tumbling failures |
| `attentive-11` (1.1 s) | — | 121 / 42 | 65.2893 % | Escape above 65 %, without rounding it into a pass |
| `attentive-105` (1.05 s) | 0 / 0 | 125 / 41 | 67.20 % | Escape too high; evasion fixture passed |
| `attentive-10` (1.0 s) | — | 125 / 41 | 67.20 % | Same outcome as 1.05 s; escape too high |
| `attentive-11-step-40` (1.1 s, 4.0 m/s step) | — | 112 / 47 | 58.04 % | Rampage-only diagnostic passed |
| `final-suite` | 0 / 0 | 112 / 47 | 58.04 % | All four primary runs passed; crossing criterion not met |
| `crossing-preserve-16-step-40` (1.6 s, 4.0 m/s) | 0 / 0 | 107 / 37 | 65.4206 % | Escape above 65 %; crossing-green time still 6.0 person-s in `foot` / `day` |
| `crossing-preserve-16-step-38-rampage` (1.6 s, 3.8 m/s) | — | 117 / 35 | 70.09 % | Escape too high |

## Diagnosis and tuning

In the earlier `diagnostic-day-full/day.log`, an undistracted rail BoxTruck hit person 144 while turning left at 18.750 s, a Bruiser hit person 296 turning left at 20.767 s, and a Taxi hit person 126 turning right at 22.316 s. All three had no planned stop (`stopDist` = 10⁹ px), no driver distraction and no panic. The old nose-path pedestrian query did not represent the corners and sides of the rear-axle-based turning body, and discarded sidewalk people unless the car was mounting the kerb; the Bruiser and Taxi contacts were on sidewalk tiles. Drivers now query people against sampled planned body boxes plus the current body, without that tile exclusion; vehicle obstacle queries remain unchanged. The 12 px spacing and 10 px practical clearance are not a continuous swept-body proof, and future legacy blend/U-turn interpolation is not newly modelled.

The fresh compiled WIP already had zero hits in `foot` and `day`, before that traffic correction. Its personal-trait sampler preserves the shared RNG draw count by seeding a local generator from the original reaction draw. The source/configuration mismatch with the older binary matters: the continuation does not claim a newly measured 3 → 0 improvement caused solely by the body scan.

The final crossing diagnostic, `after/diagnostic-crossing-day/city-day-1.log`, retains 720 person-frames at the traffic's green. Person 155 stayed near x = 4,217 px for 360 frames in `Cross`, despite a steering velocity near −49 px/s and a target near x = 4,056 px; people 127 and 156 were progressing in `Cross` for 146 and 126 frames, and person 287 spent 88 frames on the road in `Wait`. This demonstrates blocked movement; the diagnostic does not identify the blocking actor or prove a wall, furniture or a particular car caused it. It belongs to the blocked-crossing/contact work already specified by [CJ-031](../backlog.md#cj-031-pedestrian-blocking-and-contact-realism), rather than a missing mobility multiplier in the entry-time estimate. The diagnostic run still had zero hits and 0.443 ms pedestrian CPU. Its temporary per-person logging is removed from production. Broader obstacle routing and actual-displacement locomotion are left to CJ-031; that follow-up does not make the CJ-038 crossing criterion a pass.

Shortening the attentive notice horizon alone did not reliably meet the city escape target. Conflict prediction samples every 0.1 s, so 1.05 s and 1.0 s produced the same outcome, and the city's later encounters can change with earlier reactions. The adopted tuning is a 1.1 s attentive horizon and a 4.0 m/s fast step, compared with the plan's initial 1.6 s and 4.5 m/s. Attention shares, scan periods, reaction ranges, hearing, freezing, mobility, the slow step, acceleration and turn rate remain unchanged. A personal pace factor capped at 1.15 gives a 4.6 m/s fast-step maximum before per-look mobility; the 6 m/s² voluntary acceleration bound stays in place. These are game choices measured against the agreed target, not new estimates from the research.

The `attentive-13` evasion run completed 17 scenes and 1,039 checks with five false failures in `tumble_deg_per_m`. The observer kept counting walking after a survivor got up, diluting turn per metre; for example, a survivor got up at 3.850 s, then travelled far beyond its 1.281 m throw. It also included landing-frame flight distance. The observer now measures only segments whose previous and current observations are both grounded and not airborne. The minimum remains 20°/m and production impact behaviour is unchanged. The corrected `attentive-105` fixture passed all 1,055 checks.

## Isolated scenes

The seven comparison scenes in [`tests/person-evasion.cfg`](../../tests/person-evasion.cfg) use a taxi driving straight at the prescribed speed without braking. The escape denominator includes only people whose initial positions are in its swept body plus pedestrian radius, excluding safe bystanders. A row of 18 people is spaced by 2 m; a crowd contains 40 people in a disc of radius 6 m. The rows and crowds begin at different warning distances, so their outcomes need not match the city's target.

| Scene | Before escaped | After escaped |
|---|---|---|
| Long warning, front-facing row, 50 km/h | 100 % | 16.7 % |
| Long warning, rear-facing row, 50 km/h | 100 % | 5.6 % |
| Short warning, front-facing row, 50 km/h | 66.7 % | 5.6 % |
| Short warning, rear-facing row, 50 km/h | 61.1 % | 0 % |
| Long warning crowd, 50 km/h | 92.3 % | 38.5 % |
| Short warning crowd, 50 km/h | 23.1 % | 0 % |
| Slow front-facing row, 10 km/h | 100 % | 40.0 % |

Before: all seven scenes completed, 698 checks, 97 failures, all in the proposed voluntary acceleration ceiling of 6 m/s². The largest observed unhit dodge acceleration was 12 m/s². Contact, carry and overlap checks passed. The old model already limited its desired acceleration, but used a much stronger dodge gait; the new limit must also cover the final velocity after heading projection.

Additional pinned-trait scenes check rear headphone perception, paired attentive/distracted timing (0.3 s / 0.6 s), old-look mobility, a forced freeze, inherited-speed deceleration and persistent traits, without relying on a random person happening to have the desired trait. The persistence observer records attention, scan period, notice horizon, reaction, mobility and freeze delay; timers can change, those sampled fields cannot. The acceleration observer excludes contact impulses, subsequent falls and getting up.

The final `after/final-suite/evasion-person-hits-1.log` records all 17 scenes and 1,055 checks passing. Its paired front reactions were 0.300 s and 0.600 s; the adult/older-look sampled reactions were 0.409 s / 0.512 s (ratio 1.25), and the inherited-speed case slowed by 1.641 m/s within 0.4 s. All observed persistent trait fields remained unchanged. The short rows/crowds and non-braking slow car are deliberately demanding geometry; their escape values above are recorded outcomes, without the city's 50–65 % criterion.

The earlier `after/initial-evasion` fixture passed 16 scenes and 835 checks. The retained `before/baseline-impacts/manifest.json` records all 359 CJ-035 runs and 2,077 checks passing. That impact suite was not rerun during the 2026-10-13 continuation, as requested; the production contact, injury, throw, run-over and get-up model is unchanged.

## Limits and playtest

One city seed, one machine and straight isolated cars do not describe every possible crowd or driver. Hearing is a profile gate rather than an acoustic simulation; visual perception uses facing, not a new occlusion system. Crossing gap checks retain the existing conservative prediction and normal traffic still brakes for people. The injury, balance, throw and run-over model is unchanged. The 2026-10-13 screenshot review covers reaction/freeze sequences, `day` at frames 600 and 900 and `rampage` at frames 600 and 900; these show readable poses and reactions in those captures, not proof that all movement is free of gliding.

The playtest should judge whether late reactions, slower old people and brief freezes look natural, whether normal crossings remain safe, and whether evasion avoids visible sideways gliding. Keep CJ-038 implemented, awaiting playtest with the known crossing regression; [CJ-031](../backlog.md#cj-031-pedestrian-blocking-and-contact-realism) owns its blocked-crossing/contact follow-up. Do not mark the agreed crossing criterion passed or the item fully accepted.
