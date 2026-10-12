# ADR-0011: A contact between a vehicle and a person is one impact

- **Status:** Accepted; throws shortened for play by [ADR-0012](0012-shorter-throws-for-play.md)
- **Date:** 2026-10-11

## Context

Vehicles pushed people. A person whose circle overlapped a vehicle's box was moved out of it in every frame, so a vehicle could carry the player or a body along at any speed: below 21–25 km/h nobody was hurt or fell, and above it the player was shoved and damaged frame after frame while a civilian slid a few metres ahead and was knocked on again. The player and civilians had separate rules, and the injury rose so steeply that everyone hit at 41 km/h died ([CJ-035](../backlog.md#cj-035-vehicles-hitting-people)). In real life a person's legs cannot keep up with a vehicle: the contact lasts a fraction of a second, and they stagger, fall or are thrown; a vehicle drives over someone lying on the road.

## Decision

Every contact between a vehicle moving towards a person and that person is resolved once, as an impact, by one function for civilians and the player (`VehicleMeetsPerson`, `src/impact.*`); afterwards both move on their own, and nothing moves a person along with a vehicle:

- Momentum is conserved between the two bodies. Below the person's balance limit they stagger and step out of the way; another contact while they stagger knocks them down.
- Above it they are launched as in Searle's throw model, with a launch angle and an impact factor for the two kinds of front (wrapping below the person's centre of mass, projecting above it), calibrated so that throw distances fall within Happer's regression and Searle's corridor. A body in the air touches no vehicle.
- Vehicles drive over people lying on the road, once per pass: the living die, the body moves a little, the vehicle jolts. They are neither pushed nor do they stop the vehicle.
- The injury is a logistic function of the person's speed change fitted to Tefft (2011).
- Every value is data (`assets/data/impacts.cfg`, `FRONT` in `vehicles.cfg`), and a scene test measures the model for every vehicle class against the published data.

## Alternatives considered

- **Keep resolving overlaps by position and only tune the numbers.** Any positional resolution of a sustained overlap is a push; slow vehicles would still carry people along.
- **People in the vehicle contact solver, or a ragdoll.** A rigid body in the vehicle solver would push like the old rule unless it were also given friction, falling and getting up; a ragdoll would cost far more CPU than 300 people allow and still need the same injury and throw calibration. The impulse event gives the measured throws and injuries with a few lines.
- **A vehicle stopped by a person lying on the road.** No pushing and no passing through leave only stopping or driving over. A body about 25 cm high is a kerb that a car climbs, and the player's vehicle would get stuck on bodies; in real life people are driven over.
- **Separate tables of outcomes per speed band (pushed aside, knocked down, thrown).** One physical model gives the bands by itself, and keeps the player and civilians, braking and non-braking vehicles, cars and trucks consistent.

## Consequences

- No vehicle pushes anyone: a contact lasts at most a few frames in every measured case, and a slow vehicle that keeps going makes people step aside, or knocks them down and drives over them.
- A vehicle that does not brake drives over the person it threw at lower speeds; from about 80 km/h a car passes under the body (a roof vault); buses and trucks drive over the people they hit.
- The player needs frames for falling, lying and getting up, and is not controlled meanwhile.
- New vehicles must declare their front edge; the scene test covers them without changes.
- Being knocked down by a blow or a blast uses the same body on the ground, with the old sliding distances.
