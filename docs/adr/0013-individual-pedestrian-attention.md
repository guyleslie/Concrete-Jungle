# ADR-0013: Individual pedestrian attention and bounded evasion

- **Status:** Accepted
- **Date:** 2026-10-12
- **Refines:** [ADR-0006](0006-pedestrian-steering.md)

## Context

[ADR-0006](0006-pedestrian-steering.md) removed false alarms by predicting actual vehicle conflicts, gave people personal reaction times and used anticipatory, heading-constrained steering. Its frequent perception in every direction still lets nearly everyone detect a car driven into a crowd. The [CJ-038 plan](../design/pedestrian-attention-proposal.md) accepts a population with varied attention and mobility and a game target of 50–65 % escape in `rampage`, while normal traffic must hit nobody in `foot` and `day`.

The existing locomotion clips the requested acceleration, but its final heading projection can change velocity more sharply. New attention traits therefore need a motion limit that also covers turning, replanning and avoidance. Published crossing and VR measurements support varied noticing, decision delay and finite movement; they do not specify this game's population escape rate or every tuning value.

## Decision

1. Keep the predicted-conflict model, traffic-path prediction, time-to-collision neighbour steering and heading-constrained locomotion from ADR-0006. Gate new danger detection by a person's persistent attention profile: scan period, notice horizon, viewing angle and whether they hear a rear vehicle. Apply the same gate when a delayed alarm expires. A person already dodging may track the vehicle they previously perceived after turning away.
2. Sample attention, personal reaction time and a small freezing share once when a person appears. Each civilian look supplies mobility and reaction factors, allowing older looks to walk and evade more slowly and react later. Store every profile and physical limit in `assets/data/civilians.cfg`; invalid or missing required records stop loading under [ADR-0010](0010-no-fallback-content.md).
3. Limit voluntary dodge velocity changes by the person's acceleration and elapsed time, including the final turn and avoidance result. Limit maximum dodge speed as well; an inherited faster running velocity slows within the same acceleration bound instead of being clipped immediately. A brief freeze delays voluntary movement rather than teleporting or imposing an unlimited velocity change.
4. Keep crossing gap queries conservative, independent of the attention gate and omnidirectional. A new danger-perception model must not make a waiting person enter an unsafe crossing.
5. Keep the balance distribution and physical contact consequences of [ADR-0011](0011-vehicle-person-impact.md) and [ADR-0012](0012-shorter-throws-for-play.md) unchanged. A contact stagger is an immediate physical response, separate from voluntary evasion limits.
6. Use the reusable person-hit scene runner with `tests/person-evasion.cfg` for crowd and trait-specific measurements, alongside `foot`, `day`, `rampage` and the existing impact regressions. A fixed trait case must expose a missing perception gate or violated movement limit directly.

## Alternatives considered

- **Only lengthen every reaction time.** It retains all-direction threat knowledge and uniform mobility; it does not model people who fail to notice a vehicle behind them.
- **Give a fixed share immunity from evasion or force a target number of impacts.** Escape should follow perception, decision and reachable movement. The 50–65 % target is measured after those mechanisms run.
- **Replace the steering system with RVO2.** Its permissive Apache-2.0 licence allows reuse, but its ordinary update assigns a chosen velocity immediately and supplies no attention model. It would expand CJ-038 beyond the defect and revisit ADR-0006 unnecessarily.
- **Scale age-related balance together with mobility.** This changes low-speed contact falls and the CJ-035 injury/contact behaviour. CJ-038 varies voluntary reaction and motion while preserving the impact distribution.

## Consequences

- Attention and age differences can prevent or delay escape without scripted impacts. The configured trait distribution becomes part of reproducible fixture evidence.
- `civilians.cfg` explicitly defines three attention profiles, dodge limits, freezing and per-look factors. Art generation must preserve those fields when regenerating look records.
- Fresh rear threats and remembered hazards have different perception rules. Tests must cover delayed alarms and replanning so another branch cannot reintroduce knowledge of an unseen vehicle.
- The city's 50–65 % escape target intentionally replaces CJ-010's at-least-70 % target; crossing safety, existing impact rules and the 0.5 ms pedestrian CPU budget remain required.
- Numeric and screenshot verification precede the user's playtest. Implementation acceptance of this decision does not imply playtest acceptance of CJ-038.
