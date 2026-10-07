# CJ-020 turning results

Before/after evidence for [CJ-020](../backlog.md#cj-020-turning-kinematics-of-traffic), turning kinematics of traffic, measured on 2026-10-07. Before: commit `f66ef9d` (the measurements only). After: commit `d978621`. The behaviour is described in [Traffic › Turn paths](traffic.md#turn-paths); the fixture in [Testing › Turning fixture](../testing.md#turning-fixture).

## Contents

- [Summary](#summary)
- [Method](#method)
- [Turning fixture](#turning-fixture)
- [City scenarios](#city-scenarios)
- [CJ-016 regressions](#cj-016-regressions)
- [Changes to the approved specification](#changes-to-the-approved-specification)
- [Limits and remaining work](#limits-and-remaining-work)
- [Evidence](#evidence)

## Summary

| Measure | Before | After |
|---|---|---|
| Turning fixture (`cj020-turns-v2`, 96 cases) | 32 of 96 accepted, 186 failed checks | 96 of 96 accepted, 0 failed checks |
| Rear-axle slip while turning, fixture | 13–70° | 0.7–1.9° |
| Rear-axle slip while turning, city (share over 5°, maximum) | 70–75 %, up to 76° | 0.0 %, at most 1.9° |
| Rail cars overlapping each other, city total | 1.65 pair-s, up to 5.9 px | 0.33 pair-s (`rampage` only), up to 8.9 px |
| Right-turn radius of the rear axle (Taxi) | 1.8 m | 3.6 m (class minimum 3.3 m) |
| Lateral acceleration in a right turn (Taxi) | 36.8 m/s² | 2.7 m/s² |

Rail cars no longer slide through turns. Cars with real-world widths turn cleanly; the classes whose sprites make them too wide take wide turns or, where even that does not fit between the square kerbs, avoid the turn ([CJ-023](../backlog.md#cj-023-vehicle-widths-and-street-geometry)). `rampage` exceeds the decision CPU targets, which the user accepted as [CJ-024](../backlog.md#cj-024-recovery-cost-in-dense-traffic).

## Method

- **Turning fixture.** `cj020-turns-v2`: every traffic class turns right, turns left and goes straight through an empty junction at 60 Hz and 20 Hz, 96 cases ([Testing](../testing.md#turning-fixture)). The v2 definitions were settled during the work (wheels and body against the kerbs, the stop-line zone, fitting turns, a 16 px span for the radius). To compare like with like, the before build was measured with the v2 fixture too: commit `f66ef9d` in a separate worktree with the v2 fixture source and a shim for the new turn-path query (every turn fits, large vehicles turn wide). The first v1 baseline run of the same day is kept as well.
- **City scenarios.** `day`, `chase`, `drive`, `overview` (1,800 frames), `crash` (1,500) and `rampage` (3,600), through `tools/run_cj020_turns.py`. Before: the baseline run at `f66ef9d`.
- **Regressions.** The CJ-016 recovery (Taxi, Bus, BoxTruck), clearance, conflict and incident fixtures on the after commit.

## Turning fixture

60 Hz cases; the 20 Hz slip is in brackets. Radius: the smallest circumradius of rear-axle points 16 px apart, with the class's tightest rear-axle radius from its turning circle. Kerb: the deepest reach over a kerb of the wheels (the body between the axles) and of the whole body. Encroachment: outside the box and the stop-line zone. Path: how the planner classed the turn; traffic avoids a turn that does not fit.

### Right turns

| Class | Slip, ° | Rear radius, m (class minimum) | Lateral acceleration, m/s² | Wheels over kerb, px | Body over kerb, px | Encroachment, px | Path |
|---|---|---|---|---|---|---|---|
| Stinger | 49.5 → 1.7 (1.7) | 1.81 → 4.11 (3.47) | 37.0 → 2.8 | 0 → 0 | 0 → 0 | 0 → 0.5 | clean |
| Viper | 49.5 → 1.7 (1.8) | 1.81 → 3.94 (3.84) | 37.0 → 3.1 | 0 → 0 | 0 → 0 | 0 → 0.8 | clean |
| Bruiser | 51.1 → 1.7 (1.5) | 1.77 → 4.10 (4.01) | 36.8 → 2.8 | 0 → 0 | 0 → 0 | 0 → 1.5 | clean |
| Taxi | 51.1 → 1.9 (1.9) | 1.77 → 3.59 (3.33) | 36.8 → 2.7 | 0 → 0 | 0 → 0 | 0 → 3.2 | wide |
| Pickup | 52.8 → 1.7 (1.7) | 1.86 → 3.94 (3.87) | 34.4 → 3.2 | 1.3 → 0 | 1.3 → 0 | 0 → 13.8 | wide |
| Van | 51.7 → 1.5 (1.6) | 1.83 → 4.57 (3.89) | 35.9 → 3.6 | 0 → 0 | 0 → 0 | 0 → 8.8 | wide |
| Limo | 55.6 → 1.5 (1.4) | 2.05 → 4.74 (4.31) | 29.6 → 3.0 | 2.5 → 0.5 | 2.5 → 0.5 | 0 → 23.7 | does not fit |
| Ambulance | 54.3 → 1.8 (1.8) | 1.90 → 3.87 (3.77) | 32.3 → 2.8 | 4.4 → 0 | 4.4 → 0 | 0.3 → 20.7 | wide |
| Bus | 70.1 → 1.0 (0.9) | 1.45 → 7.29 (7.10) | 13.2 → 3.3 | 18.2 → 0 | 18.2 → 20.4 | 26.9 → 63.4 | does not fit |
| BoxTruck | 70.3 → 1.3 (1.3) | 1.25 → 5.53 (4.92) | 27.8 → 3.0 | 0 → 0.3 | 0 → 0.3 | 8.9 → 31.6 | does not fit |
| Semi | 70.2 → 1.4 (1.4) | 1.25 → 4.85 (4.22) | 20.7 → 3.0 | 12.3 → 0.8 | 12.3 → 2.6 | 18.7 → 56.3 | does not fit |
| FireTruck | 70.2 → 1.3 (1.3) | 1.41 → 5.22 (4.76) | 23.6 → 3.2 | 1.5 → 0 | 1.5 → 0 | 12.1 → 40.1 | does not fit |
| Garbage | 70.3 → 1.3 (1.3) | 1.42 → 5.25 (4.87) | 25.1 → 2.9 | 0.8 → 0.3 | 0.8 → 0.3 | 11.3 → 38.1 | does not fit |
| Sportbike | 28.9 → 1.3 (1.3) | 1.64 → 5.53 (1.72) | 44.4 → 2.9 | 0 → 0 | 0 → 0 | 0 → 0 | clean |
| Chopper | 32.3 → 1.3 (1.3) | 1.59 → 5.38 (2.10) | 42.4 → 3.2 | 0 → 0 | 0 → 0 | 0 → 0 | clean |
| Scooter | 26.6 → 1.1 (1.2) | 1.72 → 5.71 (1.17) | 43.3 → 3.3 | 0 → 0 | 0 → 0 | 0 → 0 | clean |

### Left turns

| Class | Slip, ° | Rear radius, m (class minimum) | Lateral acceleration, m/s² | Wheels over kerb, px | Body over kerb, px | Encroachment, px | Path |
|---|---|---|---|---|---|---|---|
| Stinger | 24.2 → 1.1 (1.2) | 4.55 → 5.97 (3.47) | 17.0 → 3.4 | 0 → 0 | 0 → 0 | 0 → 0 | clean |
| Viper | 24.2 → 1.1 (1.2) | 4.55 → 5.97 (3.84) | 17.0 → 3.4 | 0 → 0 | 0 → 0 | 0 → 0 | clean |
| Bruiser | 24.5 → 1.2 (1.2) | 4.17 → 5.97 (4.01) | 16.4 → 3.3 | 0 → 0 | 0 → 0 | 0 → 0 | clean |
| Taxi | 24.5 → 0.9 (1.0) | 4.17 → 7.30 (3.33) | 16.4 → 3.1 | 0 → 0 | 0 → 0.1 | 0 → 0 | clean |
| Pickup | 26.0 → 0.9 (0.9) | 4.23 → 7.62 (3.87) | 15.1 → 3.4 | 0 → 0 | 0 → 1.5 | 0 → 0.2 | wide |
| Van | 25.2 → 0.9 (0.9) | 4.17 → 7.70 (3.89) | 15.8 → 3.1 | 0 → 0 | 0 → 0 | 0 → 0 | clean |
| Limo | 31.4 → 1.1 (1.1) | 4.30 → 6.22 (4.31) | 14.6 → 3.4 | 0 → 0 | 0 → 0 | 0 → 10.1 | wide |
| Ambulance | 29.4 → 1.0 (1.0) | 4.40 → 6.82 (3.77) | 16.1 → 3.5 | 0 → 0 | 0 → 0 | 0 → 9.1 | wide |
| Bus | 46.0 → 0.7 (0.9) | 4.87 → 8.09 (7.10) | 7.1 → 3.5 | 0 → 1.4 | 11.5 → 24.4 | 0 → 41.4 | does not fit |
| BoxTruck | 34.1 → 0.8 (0.9) | 4.32 → 8.68 (4.92) | 8.1 → 3.3 | 0 → 0 | 0 → 9.2 | 0 → 0.2 | wide |
| Semi | 38.4 → 0.8 (1.0) | 4.52 → 8.38 (4.22) | 7.8 → 3.4 | 0 → 2.7 | 6.0 → 16.2 | 0 → 26.9 | does not fit |
| FireTruck | 36.0 → 0.9 (1.0) | 4.45 → 8.02 (4.76) | 7.6 → 3.4 | 0 → 0 | 0 → 1.8 | 0 → 21.4 | wide |
| Garbage | 35.8 → 0.9 (0.9) | 4.43 → 8.03 (4.87) | 7.7 → 3.5 | 0 → 0 | 0 → 0.1 | 0 → 21.9 | wide |
| Sportbike | 14.4 → 1.2 (1.2) | 4.30 → 5.95 (1.72) | 19.3 → 3.0 | 0 → 0 | 0 → 0 | 0 → 0 | clean |
| Chopper | 15.7 → 1.2 (1.2) | 4.06 → 5.91 (2.10) | 17.0 → 3.0 | 0 → 0 | 0 → 0 | 0 → 0 | clean |
| Scooter | 13.0 → 1.2 (1.1) | 4.57 → 5.94 (1.17) | 22.4 → 3.0 | 0 → 0 | 0 → 0 | 0 → 0 | clean |

Straight-on cases passed before and after. Before, every right and left turn failed its slip and lateral acceleration checks, and most right turns and the large vehicles' left turns were tighter than the class can steer; 12 cases put wheels over a kerb (the Bus by 18 px). After, every case passes; the 16 turns that do not fit are only logged for kerb and encroachment.

Turning speed follows from the lateral acceleration: the slowest point of a turn is now 11–15 km/h through a right turn and 15–19 km/h through a left turn (a Taxi: 10.6 and 16.6 km/h), where a Taxi took them at 27.5 and 29.0 km/h before.

## City scenarios

Before: `f66ef9d`; after: `d978621`. Slip: share of the turning time over 5° and the maximum. Decision CPU: average / 95th percentile, targets 0.5 / 1.0 ms. The `TRAFFIC` counts are snapshots at the end of the run.

| Scenario | Turning slip | Rail overlaps, pair-s (deepest) | Decision CPU, ms | Average speed, km/h | Blocked over 3 s | Wait cycles formed | Knocked / rejoined |
|---|---|---|---|---|---|---|---|
| `day` | 75.2 %, 70.3° → 0.0 %, 1.9° | 0.37 (5.3 px) → 0 | 0.16 / 0.27 → 0.21 / 0.29 | 22 → 23 | 3 → 4 | 0 → 1 | 0 / 0 → 0 / 0 |
| `chase` | 72.5 %, 74.7° → 0.0 %, 1.9° | 0.50 (5.3 px) → 0 | 0.39 / 0.93 → 0.40 / 0.71 | 26 → 22 | 6 → 7 | 1 → 1 | 8 / 3 → 4 / 3 |
| `drive` | 73.9 %, 70.3° → 0.0 %, 1.9° | 0.32 (5.3 px) → 0 | 0.18 / 0.31 → 0.22 / 0.35 | 27 → 26 | 2 → 3 | 0 → 0 | 1 / 0 → 1 / 1 |
| `overview` | 73.4 %, 75.9° → 0.0 %, 1.9° | 0.43 (5.9 px) → 0 | 0.14 / 0.21 → 0.18 / 0.27 | 26 → 24 | 0 → 4 | 0 → 1 | 0 / 0 → 0 / 0 |
| `crash` | 73.5 %, 70.3° → 0.0 %, 1.9° | 0 → 0 | 0.28 / 0.54 → 0.38 / 0.89 | 23 → 22 | 2 → 2 | 1 → 1 | 4 / 0 → 6 / 3 |
| `rampage` | 70.4 %, 70.3° → 0.0 %, 1.9° | 0.03 (2.4 px) → 0.33 (8.9 px) | 0.38 / 0.82 → 0.60 / 1.17 | 25 → 13 | 0 → 15 | 5 → 16 | 24 / 15 → 33 / 19 |

Lane-change slip stays at most 0.3°, straight driving at most 0.4° (before up to 15.9° in `chase`). Turns take longer: 1.3–2.3 times the car-seconds of turning, from the lower turning speed. Of the turns planned, about a third are wide (`TRAFFIC turns planned`: for example 70 clean, 30 wide in `day`) and at most one per run does not fit and is taken because there was no other way.

In `rampage` the traffic round the cars the player knocks off is denser: about 7 cars recover at a time instead of 4.5, the decision CPU exceeds its targets ([CJ-024](../backlog.md#cj-024-recovery-cost-in-dense-traffic)) and the end-of-run snapshot shows a slower, more blocked city. The run diverges from the before run within seconds, so it does not compare like with like; small changes to the incident tuning reproduced the same course in both builds. The recovery decisions themselves are unchanged: the CJ-016 fixtures pass.

## CJ-016 regressions

All on `d978621`, clean working tree:

| Fixture | Result |
|---|---|
| `cj016-recovery-v1`, Taxi, Bus, BoxTruck | 3 × 52 checks, 0 failures |
| `cj016-clearance-v1` | 26 checks, 0 failures |
| `cj016-conflict-v2` | 100 of 100 cases, lane-change slip check included |
| `cj016-incident-v2` | 80 of 80 cases |

During the work two further changes came out of the city runs and are part of `d978621`. A knocked car's rejoin path ran straight from its actual pose and then kinked onto the lane by up to about 7°, which the new pose rule turned into up to 12° of slip; it now joins the lane along a cubic Hermite curve. And a slow turning car and a car coming straight through met in a junction box (16 px of overlap in `crash`): a car now occupies the box once its nose is in it or it has passed its stop line.

## Changes to the approved specification

The specification approved on 2026-10-07 held; the user agreed these refinements during the work, when the measurements showed what the square 8 m streets and the sprite widths allow:

- **Stop-line zone.** A body corner in the first 60 px past the box does not count as encroachment: cars waiting at a stop line 72 px back leave it free, and a tight turn's front swings through it. The strict value is still logged (`strict_encroachment_px`).
- **Wheels and overhangs.** The wheels (the body between the axles) must stay off the kerbs for every class. A car's body may reach at most 4 px over one; a large vehicle's overhangs may sweep over a corner, as real buses' and trucks' do, and the planner keeps that small.
- **Wide turns for over-wide cars.** The spec gave the 1.5 m wide-turn allowance to large vehicles in right turns. Several car classes are 2.4–3 m wide in the game ([CJ-023](../backlog.md#cj-023-vehicle-widths-and-street-geometry)) and cannot turn cleanly either, so any class may take a wide turn, in either direction, and has the junction box to itself. A clean turn tolerates 2 px of encroachment.
- **Turns that do not fit.** Even a wide turn does not fit for the Bus, the Semi and the right turns of the BoxTruck, FireTruck, Garbage truck and Limo: their fronts would sweep 2–4 m across waiting cars. Traffic avoids those turns unless it has no other way (`TRAFFIC turns planned` counts the exceptions).
- **CPU in `rampage`.** Accepted above the targets as [CJ-024](../backlog.md#cj-024-recovery-cost-in-dense-traffic).
- **U-turns.** Out of scope, but their old curve began with a 25° kink and had a 0.9 m apex radius, which made rear-axle slip of up to 22° under the new pose rule. They now follow a tangent semicircle of 2 m at a limited speed; a realistic three-point turn is [CJ-022](../backlog.md#cj-022-three-point-turns).

## Limits and remaining work

- **Geometry.** Square junction corners and the over-wide sprites keep the Bus and the Semi from turning and push several classes into wide turns ([CJ-023](../backlog.md#cj-023-vehicle-widths-and-street-geometry)).
- **Wide turns and through traffic.** In `rampage` a Pickup on a wide right turn and a Bus going straight overlapped by up to 8.9 px for 0.33 s: a wide turn's front sweeps up to 1.5 m into the oncoming half of the exit road, where a driver does not look for it. Wide turns come from the over-wide sprites ([CJ-023](../backlog.md#cj-023-vehicle-widths-and-street-geometry)).
- **Police.** Police paths are unchanged; they steer physically along the lane planner's curves ([CJ-018](../backlog.md#cj-018-police-driving-and-reactions)).
- **Playtest.** The user playtested the turns on 2026-10-07 and accepted them; the minor glitches left need wider roads and junctions and a rethought sidewalk ([CJ-023](../backlog.md#cj-023-vehicle-widths-and-street-geometry)).

## Evidence

All under `build/shots/` (not version-controlled):

| Run | Location | Manifest |
|---|---|---|
| Before, v1 fixture and city | `cj020/before/baseline/` | `manifest-20261007T092921374053Z-24092.json` (`f66ef9d`, clean) |
| Before, v2 fixture | `cj020/before/v2-fixture/` | Log, captures and the shim header; build of `f66ef9d` with the v2 fixture |
| After, fixture and city | `cj020/after/final-v4/` | `manifest-20261007T120129217516Z-21724.json` (`d978621`, clean) |
| CJ-016 regressions | `cj016/`, `cj016-clearance/`, `cj016-conflict/`, `cj016-incident/` under `after/cj020-final-v4/` | `manifest-20261007T120607365594Z-13196.json`, `…121033186324Z-10696.json`, `…121053114961Z-10064.json`, `…121312064588Z-10724.json` (`d978621`, clean) |

Each fixture case ends with a capture of the rear-axle (amber) and front-axle (cyan) traces and the body outline every 0.2 s, for example `traffic-turns_Taxi-right-60hz.png` before and after.
