# ADR-0005: Sub-stepped impulse solver for vehicle contacts

- **Status:** Accepted
- **Date:** 2026-09-27

## Context

Players and AI cars regularly got stuck after hitting something, with the sprite jumping in place. Measurements with the `crash` and `derby` test scenarios showed overlaps of up to 33 px and hundreds of frames of back-and-forth motion. The causes were:

1. vehicle-versus-object contacts used an inverted normal and pulled cars *into* poles, trees and planters;
2. contacts were resolved one at a time by teleporting positions, so a car wedged between a wall and another car was pushed back and forth;
3. rail cars acted as infinitely heavy bulldozers in gentle contacts and could push the player into walls;
4. steering overwrote the spin from an impact within a few frames, and the steering direction flipped as the forward speed crossed zero;
5. there were no sub-steps, so a single long frame could move a car 45 px into a wall.

## Decision

Replace the ad-hoc collision code with a small rigid-body contact solver in the style of Box2D v3 (`src/physics.*`):

- contacts generated once per frame with face clipping (up to two points) and a speculative margin;
- sequential impulses with accumulated, clamped normal impulses and Coulomb friction;
- 240 Hz sub-steps with warm starting, a soft (spring-damper) push-out limited to 3 m/s, and a relax pass without push-out;
- speed-dependent restitution applied once, only above 1 m/s;
- rail cars as kinematic bodies interpolated over the sub-steps, knocked into physics on a real hit or after pushing for 0.35 s;
- breakaway strength and loose mass for street furniture, soft drag for shrubs;
- physics emits impact events; the game applies damage by delta-V and all other consequences.

The tyre model was adapted to run per sub-step, with limited yaw authority, saturated sliding friction and static friction.

## Alternatives considered

- **Fix only the inverted normal** — removes the worst symptom but keeps the order-dependent teleporting and the bulldozing rail cars.
- **Link the Box2D library** — proven, but adds a dependency, and the custom tyre model, kinematic rail cars and breakaway objects would still need glue code of about the same size as the solver itself.

## Consequences

- No measurable penetration or jitter in the test scenarios; knocked traffic recovers.
- Crashes are physically plausible: mass matters, cars slide and spin, furniture breaks away.
- Damage balance changed: ramming parked cars does less damage than before, wall hits slightly more.
- The tyre model must only change velocities; positions belong to the solver.
- Solver constants are documented in [Physics](../design/physics.md#tuning-reference).
