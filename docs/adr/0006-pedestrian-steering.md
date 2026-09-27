# ADR-0006: Predictive perception and time-to-collision steering for pedestrians

- **Status:** Accepted
- **Date:** 2026-09-27

## Context

Measurements with the `foot` scenario (25 s, the player doing nothing) showed that the pedestrian AI did not hold up:

- On average 29 of 220 people were fleeing without any real danger, and 15 walked on the road outside a crossing. A person fled from any vehicle faster than 27 km/h within 9 m whose velocity pointed roughly at them — including traffic passing in its lane — and ran straight away from it, often into the road, where traffic hit 7–9 people per run.
- Knocked-down people never got up: the get-up call went through a function that ignores people who are down.
- Avoidance teleported positions and always sidestepped to the right, so people slid sideways; the body turned after the motion rather than leading it. Playtesting confirmed that the movement looked like sliding.
- With the population spread over the whole island, about 1.3 people were visible on foot.

## Decision

1. **Perception by prediction.** A person reacts to a vehicle only when the vehicle's predicted motion over the next 1.6 s reaches the person's predicted position (body plus 0.35 m). Traffic on its lane is predicted along its planned path and stops where its driver plans to stop (`DriverAI::stopDist`, `AIPathPose`); other vehicles keep their velocity and a limited turn rate. Reactions come after a personal reaction time.
2. **Dodge, not flee.** The reaction to a vehicle is a sideways move out of its path, towards the nearer free side, continued until clear. Fleeing is kept for gunfire, explosions, violence and close calls, and chooses a direction that avoids the road, walls and vehicle paths.
3. **Anticipatory steering.** People avoid each other, the player and furniture with the time-to-collision interaction law of Karamouzas, Skinner and Guy (2014), combined with a goal velocity.
4. **Heading-constrained locomotion.** A person has a body heading with a turn-rate and acceleration limit and walks only where it faces (plus a 0.3 m/s side step). Position corrections remain only for bodies that actually touch and for walls.
5. **Population around the player** in a 110 m radius, with a uniform grid for neighbour queries.

## Alternatives considered

- **Tune the old rules** (narrower cone, longer range) — still reacts to distance and direction rather than to an actual collision course, so passing traffic keeps causing false alarms, and turning cars are missed.
- **ORCA / RVO2** (reciprocal velocity obstacles, Apache-2.0 library) — collision-free in theory, but it needs a linear-programming step per agent, produces abrupt velocity changes that look mechanical at walking speed, and adds a dependency. The power-law model is a few lines, smooth, and was fitted to real crowd data.
- **Social force model** (Helbing) — reacts to distance, not to the time to collision, so people only swerve when already close; known to produce oscillations in dense counter-flow.
- **Flow fields / navigation mesh** — useful for free movement across open spaces, but pedestrians here follow sidewalk rings and crossings; not needed yet.

## Consequences

- In `foot` and `day` nobody flees without cause, nobody walks on the road outside a crossing, traffic hits nobody, and nobody slides; about six people are visible on foot. In `rampage`, 74–81 % of the people in the car's path escape (67 % before).
- Rail traffic must keep `stopDist` up to date whenever it plans to stop; `AIPathPose` is part of the traffic module's interface.
- The pedestrian update costs about 0.4 ms per frame for 300 people; the grid also removed the all-pedestrians loop from the traffic look-ahead and the vehicle–pedestrian collisions.
- Behaviour is described in [Pedestrians](../design/pedestrians.md); the metrics are in [Testing](../testing.md#metrics).
