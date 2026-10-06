# CJ-016 yielding and incident results

The second CJ-016 increment (2026-10-06) bounds recovery CPU peaks, adds cooperative yielding between two drivers stopped behind each other, and adds driver incidents: stop, get out, confront, fight and drive the same car on. Measured on one build: the cooperative yielding fixture went from 6 to 60 resolved cases out of 60, the incident fixture from 20 to 80 accepted cases out of 80, and the frozen recovery and clearance fixtures still pass all 182 checks with unchanged rejoin times. The worst city decision frame fell from 11.6 ms to 2.5 ms; `crash` now meets both decision targets, but `chase` and `rampage` still exceed them. The user playtest is pending.

## Contents

- [Evidence](#evidence)
- [CPU peaks](#cpu-peaks)
- [Cooperative yielding](#cooperative-yielding)
- [Driver incidents](#driver-incidents)
- [Recovery regression](#recovery-regression)
- [Defects found by the new fixtures](#defects-found-by-the-new-fixtures)
- [Limits and remaining work](#limits-and-remaining-work)

## Evidence

All final runs used one executable, SHA-256 `0fd9611793313d22b7b6de385a2591850c4d841ec8b22f1dc8a226c35269818f`, and `assets/data/traffic.cfg` SHA-256 `6dd93537925e99a1876b66404e3d1a816c112714c221a590d52b38c66db92e9d`. The manifests record the revision, dirty state, every input hash and the command; logs and screenshots are beside them.

| Run | Manifest | Result |
|---|---|---|
| Recovery fixture | `build/shots/cj016/after/final3-20261006/manifest-20261006T033237852537Z-31696.json` | 156 / 156 checks |
| Clearance fixture | `build/shots/cj016-clearance/after/final3-20261006/manifest-20261006T033708062572Z-31844.json` | 26 / 26 checks |
| Yielding, before | `build/shots/cj016-conflict/before/final3-20261006/manifest-20261006T033727108955Z-35820.json` | 6 / 60 cases, 128 failed checks |
| Yielding, after | `build/shots/cj016-conflict/after/final3-20261006/manifest-20261006T034041356576Z-35116.json` | 60 / 60 cases |
| Incidents, before | `build/shots/cj016-incident/before/final3-20261006/manifest-20261006T034211734698Z-19920.json` | 20 / 80 cases, 320 failed checks |
| Incidents, after | `build/shots/cj016-incident/after/final3-20261006/manifest-20261006T034416048020Z-43484.json` | 80 / 80 cases |
| City | `build/shots/cj002/after/cj016-final3-20261006/manifest-20261006T034836172930Z-city-29748.json` | 6 / 6 completed |

The before phases run the same executable with a copy of `traffic.cfg` beside their evidence: `YIELD enabled 0` restores the stand-off rule of the first increment, `INCIDENT enabled 0` the rule under which no driver reacts to a collision. Both incident phases set `INCIDENT confront_chance 1` (scripted aggressive drivers). Earlier runs are retained but not used below: `session-20261006` preceded the last CPU fixes; `final-20261006` measured the same code with an unused function that produced a compiler warning and an incident car that stayed protected after a lost car (fixed before `final3`); `final2-20261006` was interrupted. Its fixture results matched `final3` exactly. Profiling runs are in `build/shots/prof/`.

## CPU peaks

The city reference is the previous increment's final series (`build/shots/cj002/after/cj016-final-fixed-20261005/`). The scripted scenes diverge between builds as traffic decisions change, so knock counts differ; compare the costs together with the work they did. Targets: driver decisions at most 0.5 ms average and 1.0 ms 95th percentile per frame, total vehicle update at most 1.0 ms average.

| Scenario | Driver avg (ms) | Driver p95 (ms) | Driver worst (ms) | Vehicle avg (ms) | Knocked / rejoined / gave up |
|---|---|---|---|---|---|
| `crash` | 0.684 → 0.434 | 2.756 → 0.933 | 5.520 → 1.742 | 0.899 → 0.618 | 10 / 1 / 0 → 4 / 0 / 0 |
| `derby` | 0.308 → 0.353 | 0.502 → 0.781 | 1.278 → 2.025 | 0.473 → 0.512 | 1 / 0 / 0 → 2 / 2 / 0 |
| `chase` | 0.313 → 0.739 | 0.625 → 1.455 | 4.945 → 2.294 | 0.479 → 0.910 | 5 / 1 / 0 → 10 / 2 / 0 |
| `foot` | 0.271 → 0.296 | 0.457 → 0.665 | 1.142 → 0.994 | 0.430 → 0.458 | 0 / 0 / 0 → 0 / 0 / 0 |
| `day` | 0.294 → 0.291 | 0.492 → 0.674 | 1.250 → 1.391 | 0.460 → 0.456 | 0 / 0 / 0 → 0 / 0 / 0 |
| `rampage` | 1.149 → 0.799 | 3.904 → 1.617 | 11.600 → 2.457 | 1.362 → 0.992 | 23 / 11 / 0 → 29 / 14 / 0 |

Recovery give-ups stay at zero everywhere; overlap stays at most 0.7 px with no deep frame. `crash` meets both decision targets and `rampage` the vehicle target; `chase` and `rampage` remain above both decision targets. The `final-20261006` run of the previous binary measured `crash` at 0.451 / 1.014 ms, so `crash` sits close to the 95th-percentile limit. `chase` now carries more recovery work (10 knocks, on average 3.7 cars recovering) than the reference scene (5 knocks). Two repeated `foot` runs of this build gave 95th percentiles of 0.712 and 0.568 ms (averages 0.293 and 0.277 ms), so differences of about 0.15 ms in the 95th percentile of the quiet scenes are within run-to-run noise on this machine.

### Diagnosis

`RECOVERY WORK` and `RECOVERY WORST` attribute recovery time to its stages. In the first profiled `rampage` run, the worst frame (9.0 ms) ran three full 19-candidate plans at once (57 rollouts, 20,341 force steps); a force step cost about 0.25 µs for vehicle forces and 0.3–0.6 µs for swept checks and forecasts. Planning was synchronised: cars knocked together replanned together every 0.25 s. After slicing, the remaining average came from two behaviours rather than from planning volume:

- A recovering car moved off a queue, the immediate check failed one frame later and the car stopped and replanned, up to 453 times per car in `rampage`. The failing actor was a follower behind it (a patrolling police car, a queued rail car) forecast at constant speed into the car's stopping tail.
- Every moving recovering car simulated about 200 force steps per frame for its immediate check.

### Changes

| Change | Effect on the measurements |
|---|---|
| Sliced planning jobs under a shared, deterministic step budget (`planning_steps 600`), oldest job first | `rampage` worst recovery frame 9.0 → 2.3 ms in the first sliced run (the scene diverged) |
| Stationary forecasts and conservative cull margins | Fewer forecast samples per step; results identical |
| Holds replanned only when a blocker, the own pose or the static obstacles change (at least every 2 s) | Holds no longer replan four times a second in a busy street |
| The rear-end rule for followers and planned-stop forecasts for rail cars | `crash` immediate-hazard stops: 167 → 3 per car |
| Extended immediate checks reused while everything moves as forecast | `crash` driver average 0.60 → 0.43 ms, p95 1.16 → 0.96 ms in the profiling run before the final series |

## Cooperative yielding

Fixture `cj016-conflict-v1`, 10 seeds, 60 Hz and 20 Hz; times are seconds from the start of the case. See [Testing](../testing.md#cooperative-yielding-fixture) for the set-up.

| Situation, rate | Before: resolved | After: resolved | After: resolved median / max | After: completed max | Roles (chain) |
|---|---|---|---|---|---|
| `passing-head-on` 60 Hz | 0 / 10 | 10 / 10 | 5.18 / 5.72 | 10.45 | 10 (0) |
| `passing-head-on` 20 Hz | 0 / 10 | 10 / 10 | 5.15 / 6.00 | 10.35 | 10 (0) |
| `knocked-needs-room` 60 Hz | 2 / 10 | 10 / 10 | 3.48 / 3.75 | 6.82 | 8 (0) |
| `knocked-needs-room` 20 Hz | 2 / 10 | 10 / 10 | 3.35 / 3.65 | 6.55 | 8 (0) |
| `knocked-queue` 60 Hz | 1 / 10 | 10 / 10 | 4.02 / 4.28 | 7.72 | 9 (9) |
| `knocked-queue` 20 Hz | 1 / 10 | 10 / 10 | 3.65 / 3.95 | 7.20 | 9 (9) |

Before, every passing head-on case stayed a stand-off for 30 s; the knocked cases resolved only in the seeded poses where the knocked car could escape without room from behind, which is also why two (and one) seeds per rate need no role after the change. After the change no rail car was knocked, no role flipped, no vehicle overlapped another (0.000 px) or a wall, and no pose jumped. Screenshots of every case end are in the evidence folders.

## Driver incidents

Fixture `cj016-incident-v1`, 10 seeds, 60 Hz and 20 Hz. *Settled* is the time at which every drivable traffic car has its driver back and its lane. See [Testing](../testing.md#driver-incident-fixture).

| Situation (per rate) | Before: accepted | After: accepted | Incidents / exits / fights / back in own car | First exit (s) | Fight starts (s) | Settled median / max (s) |
|---|---|---|---|---|---|---|
| `aggressive-pair` 60 Hz | 0 / 10 | 10 / 10 | 10 / 20 / 10 / 20 | 0.93–1.63 | 6.22–7.42 | 39.2 / 48.8 |
| `aggressive-pair` 20 Hz | 0 / 10 | 10 / 10 | 10 / 20 / 10 / 20 | 0.95–1.45 | 5.95–7.40 | 38.4 / 46.6 |
| `calm-pair` 60 Hz | 10 / 10 | 10 / 10 | 0 / 0 / 0 / 0 | — | — | 3.0 / 3.0 |
| `calm-pair` 20 Hz | 10 / 10 | 10 / 10 | 0 / 0 / 0 / 0 | — | — | 3.1 / 4.0 |
| `aggressive-player` 60 Hz | 0 / 10 | 10 / 10 | 10 / 10 / 10 / 10 | 1.20–1.78 | 6.68–9.25 | 21.7 / 25.6 |
| `aggressive-player` 20 Hz | 0 / 10 | 10 / 10 | 10 / 10 / 10 / 10 | 1.10–1.80 | 6.95–9.25 | 21.5 / 24.0 |
| `interrupted` 60 Hz | 0 / 10 | 10 / 10 | 10 / 20 / 10 / 10 | 0.83–1.45 | 5.90–7.37 | 20.0 / 27.7 |
| `interrupted` 20 Hz | 0 / 10 | 10 / 10 | 10 / 20 / 10 / 10 | 0.90–1.25 | 6.00–7.30 | 20.7 / 31.0 |

Every contact occurred within the first second and was solved by production physics. Fights started 13.8–17.8 px apart (the two people side by side), after a 3 s argument. Every `interrupted` case ended the first driver's part with `car_lost` while the other driver drove on. Over all 80 cases there were no ownership violations, no duplicate drivers, no removed or teleported vehicles and no driver death; the punches against the player landed in every `aggressive-player` case. Before, with incidents disabled, nobody got out, so only the calm cases were accepted.

The long settle times of the aggressive pairs are mostly behaviour, not waiting: a fight lasts until one person drops below 45 health (6–8 s), the loser backs off for 1.5–2.5 s, both walk back, and the cars, pressed together by the rear-end, release the contact and re-queue before rejoining.

## Recovery regression

The frozen `cj016-recovery-v1` fixture passed all 156 checks with the same rejoin times as the previous increment: Taxi garage 10.833 / 10.900 s, Bus 21.233 / 20.800 s, BoxTruck 13.767 / 13.900 s at 60 / 20 Hz, reverse distances unchanged. Isolated decision cost stayed at about 0.10 ms average and at most 0.39 ms 95th percentile per physics step in the heaviest (garage) phases. The separate clearance regression passed all 26 checks.

## Defects found by the new fixtures

| Defect | Since | Fix |
|---|---|---|
| A failed mid-block U-turn left the car with an empty route; its next pose was sampled at the map origin, so it vanished off-screen | Before CJ-016 | A refused U-turn restores the route; the swept turn must miss other vehicles |
| A car in contact (within the 1 px margin) could never move off: the sweep pad of the first clear step reached back into the contact | First CJ-016 increment | The no-deepening rule holds until the swept test clears |
| An aligned car stopped behind a stopped car could not rejoin: the rejoin rollout demands free road ahead | First CJ-016 increment | Queued hold, `queued_behind` rejoin, creep forward to release a rear contact |
| A person with their back to the target never set off: the gait turns only once moving | CJ-010 locomotion | New on-foot states face their target |
| The stationary-forecast shortcut first grew the forecast box, which read as a deepening contact | This increment, before the final build | The residual motion goes into the sweep pad |

## Limits and remaining work

- **CPU.** `chase` (0.739 / 1.455 ms) and `rampage` (0.799 / 1.617 ms) exceed the decision targets; `crash` (0.434 / 0.933 ms) is close to the 95th-percentile limit. The remaining cost is immediate checks of moving recovering cars near people and moving traffic, where extended checks cannot be reused, and planning for cars that keep manoeuvring.
- **Yielding scope.** Roles are taken for pairs, with a chain behind the yielder. Two knocked cars blocking each other, junction gridlock and larger wait-for cycles have no roles yet. At the end of `crash` and `rampage` one mutual pair each was still unresolved, and four `rampage` cars were still giving way.
- **Incidents.** Only vehicle–vehicle contacts start incidents. Police do not react to a fight, and there is no shouting sound or dedicated gesture art; the punch frame stands in for a raised fist.
- **Fixtures.** One fixed seed family per fixture, uniform road, world-edge contacts disabled. The confrontation probability is scripted to 1 in the incident fixture.
- **Playtest.** The user playtest of queues, two-car conflicts, heavy-vehicle reversing and a full confrontation is pending. [CJ-016](../backlog.md#cj-016-road-rage-and-traffic-incidents) stays In progress.
