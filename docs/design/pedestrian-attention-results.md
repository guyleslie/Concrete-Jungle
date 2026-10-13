# Pedestrian attention and evasion measurements

Before and after [CJ-038](../backlog.md#cj-038-pedestrian-attention-and-evasion), measured with the [agreed plan](pedestrian-attention-proposal.md). The city escape range is a game target; the research informs the behaviour rather than prescribing a population escape rate. User playtest acceptance is pending.

## Contents

- [Evidence](#evidence)
- [City](#city)
- [Isolated scenes](#isolated-scenes)
- [Limits and playtest](#limits-and-playtest)

## Evidence

`python tools/run_cj038.py --phase before --run-name baseline-city --suite city` records the executable, scene/configuration/source hashes and git state in `build/shots/cj038/before/baseline-city/manifest.json`. The measurement-only harness was built on revision `90a7a3373710`; production pedestrian behaviour had not changed. The old executable is retained as `build/shots/cj038/baseline/ConcreteJungle-before.exe`, alongside copies of the original civilian configuration and evasion scenes.

The isolated before measurement is `build/shots/cj038/before/baseline-evasion/manifest.json`. Explicit scene seeds use a separate layout generator, so adding personal random draws does not move the crowd between builds. The original, unseeded CJ-035 scenes retain their previous seed and layout convention. Positions, facings and the person skins in the seven comparison scenes are the same before and after.

After evidence is pending. The runner preserves failures even when a process exits successfully, refuses to overwrite an existing evidence directory and runs one window at a time. City captures are saved every 300 frames; isolated scenes request captures at named times.

## City

Fixed 1/60 s clock, `foot` and `day` for 1,500 frames, `rampage` for 3,600 frames, uncapped on the same machine.

| Metric | Before | After | Criterion |
|---|---|---|---|
| `foot`: traffic hits | 0 | Pending | 0 |
| `day`: traffic hits | 0 | Pending | 0 |
| `rampage`: people in the car's path / hit | 122 / 26 | Pending | Record both counts |
| `rampage`: escaped | 78.7 % | Pending | 50–65 % |
| `foot`: pedestrian CPU | 0.600 ms | Pending | At most 0.5 ms |
| `day`: pedestrian CPU | 0.479 ms | Pending | At most 0.5 ms |
| `rampage`: pedestrian CPU | 0.477 ms | Pending | At most 0.5 ms |

The first before `foot` run already exceeded the CPU target and averaged 3.32 visible people, below CJ-010's historical target of four. These are baseline failures, not successful checks. Timing comparisons need repeats when other work or machine load differs; a single lower after number alone is not evidence of a CPU improvement.

## Isolated scenes

The seven comparison scenes in [`tests/person-evasion.cfg`](../../tests/person-evasion.cfg) use a taxi driving straight at the prescribed speed without braking. The escape denominator includes only people whose initial positions are in its swept body plus pedestrian radius, excluding safe bystanders. A row of 18 people is spaced by 2 m; a crowd contains 40 people in a disc of radius 6 m. The rows and crowds begin at different warning distances, so their outcomes need not match the city's target.

| Scene | Before escaped | After escaped |
|---|---|---|
| Long warning, front-facing row, 50 km/h | 100 % | Pending |
| Long warning, rear-facing row, 50 km/h | 100 % | Pending |
| Short warning, front-facing row, 50 km/h | 66.7 % | Pending |
| Short warning, rear-facing row, 50 km/h | 61.1 % | Pending |
| Long warning crowd, 50 km/h | 92.3 % | Pending |
| Short warning crowd, 50 km/h | 23.1 % | Pending |
| Slow front-facing row, 10 km/h | 100 % | Pending |

Before: all seven scenes completed, 698 checks, 97 failures, all in the proposed voluntary acceleration ceiling of 6 m/s². The largest observed unhit dodge acceleration was 12 m/s². Contact, carry and overlap checks passed. The old model already limited its desired acceleration, but used a much stronger dodge gait; the new limit must also cover the final velocity after heading projection.

Additional pinned-trait scenes will check rear headphone perception, attentive/distracted timing, old-look mobility and a forced freeze, without relying on a random person happening to have the desired trait. The acceleration observer excludes contact impulses, subsequent falls and getting up.

## Limits and playtest

One city seed, one machine and straight isolated cars do not describe every possible crowd or driver. Hearing is a profile gate rather than an acoustic simulation; visual perception uses facing, not a new occlusion system. Crossing gap checks retain the existing conservative prediction and normal traffic still brakes for people. The injury, balance, throw and run-over model is unchanged.

The playtest should judge whether late reactions, slower old people and brief freezes look natural, whether normal crossings remain safe, and whether evasion avoids visible sideways gliding. Keep CJ-038 awaiting playtest until the user accepts the feel.
