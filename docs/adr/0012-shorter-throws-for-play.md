# ADR-0012: Throws shortened for play

- **Status:** Accepted
- **Date:** 2026-10-12

## Context

[ADR-0011](0011-vehicle-person-impact.md) launches a person hit by a vehicle as in Searle's throw model, calibrated to real collisions: Happer's regression of 106 real accidents, the UK Forensic Science Service's rule of thumb and staged impacts all put the throw at about 40 m at 80 km/h. In the playtest of 2026-10-12 the user found this far too far for the game: bodies seemed to fly about, especially at the speeds cars reach in it. Shown the measurements, the user chose shorter throws for play, about 4 m in the air and 1 m along the road at 80 km/h, an eighth of the real distance ([CJ-035](../backlog.md#cj-035-vehicles-hitting-people)).

## Decision

- A person hit by a vehicle, and a rider thrown off a motorbike, is launched at the square root of a share of the real launch speed, so that the throw is that share of a real one: `THROW 0.125` in `assets/data/impacts.cfg`. At 80 km/h a body flies 0.6 s, at most 0.4 m high, over 3.7 m, and then slides 1.3 m.
- The injury and the time on the ground still follow the real collision's speed change: being hit at 80 km/h is as dangerous as before, the body only travels less.
- A body slower than the vehicle that hit it must not be run into again by a bumper that merely reaches over it. A vehicle therefore drives over someone lying only with its wheels (the body between the axles, 0.32 lengths either side of the centre). Someone lying under a moving vehicle waits for it to pass or drive over them; from under a standing one they crawl out towards its nearer end before getting up.
- The person-hit scenes check the throw against an eighth of Happer's band, and against Searle's corridor for the launch speed, which still governs the flight and the slide.

## Alternatives considered

- **Keep the real throws.** Rejected by the user for play.
- **Real throws up to a speed, compressed above it.** The user's figure for 80 km/h is shorter than real throws at 30 km/h, so the compression would have to start at walking pace.
- **A real launch with a much higher drag in the air and on the road.** A body leaving at 70 km/h would stop within 5 m, as if it hit a wall, and land under a car braking less hard.

## Consequences

- A car that does not brake passes under the body it hit from about 30 km/h (a roof vault) and leaves it behind; a braking car stops before the body lands. A bus or a truck, whose flat front cannot let the body pass over it, drives over the person it hit at higher speeds even while braking, which is common in real collisions with trucks.
- Throws, heights and times in the air are an eighth, about a third and about a third of real ones; the throw-distance criterion of CJ-035 is an eighth of Happer's band.
- Changing `THROW` back to 1 restores the real throws; the person-hit scenes' reference share must then change with it.
