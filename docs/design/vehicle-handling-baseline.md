# CJ-002: Recorded arcade baseline

Measured results from the isolated handling and collision fixtures, before production physics changes. Targets remain in the [approved proposal](vehicle-handling-proposal.md). Failed acceptance checks are baseline findings, not evidence of an interrupted run.

## Evidence

All 17 classes and the collision fixture share the same executable, configuration and fixture SHA-256 hashes. Current workspace inputs match these recorded inputs.

| Input | SHA-256 |
|---|---|
| `ConcreteJungle.exe` | `f8c37c6b3113b36e8f14b70ef891a1c4697d9da318b6ba73d320b513d92baa86` |
| `assets/data/vehicles.cfg` | `ee13108fa9692b3725bb4fb8641a9b4acfad57b5529355903efa012447e767d8` |
| `src/vehicle_tests.cpp` | `5bf35e32fb1c968d926e0fdaccadbe72b291827d863c1ee03b4baeff521dd303` |

Manifests (relative to `build/shots/cj002/before`):

- `manifest-20260929T102226232811Z-collision-39224.json`
- `manifest-20260929T102843795887Z-handling-38772.json`

## Handling

All classes completed 21 phases and 14,400 frames with no invalid state. A missing stop is shown explicitly: the car never met the sampled speed threshold of 0.1 m/s during the brake phase. Scooter acceleration and braking start at 50 km/h; all other classes use 100 km/h.

| Class | Acceleration (s) | Brake distance (m) | Steady top speed (km/h) | Reverse peak (m/s) | Failed checks |
|---|---|---|---|---|---|
| Stinger | 0.74 | No sampled stop | 186.80 | 17.48 | 25 |
| Viper | 0.69 | No sampled stop | 197.87 | 17.48 | 27 |
| Bruiser | 0.74 | No sampled stop | 183.54 | 17.48 | 25 |
| Taxi | 1.13 | No sampled stop | 148.50 | 16.09 | 23 |
| Pickup | 1.19 | 4.79 | 143.90 | 15.54 | 21 |
| Van | 1.47 | 4.99 | 131.32 | 14.98 | 21 |
| Limo | 1.38 | 4.99 | 144.42 | 12.48 | 21 |
| Ambulance | 1.12 | 4.62 | 151.84 | 14.98 | 21 |
| Police | 0.70 | 3.86 | 191.73 | 18.59 | 21 |
| Bus | Target not reached | 6.34 | 95.81 | 11.00 | 21 |
| BoxTruck | 2.52 | 6.01 | 109.41 | 12.48 | 21 |
| Semi | 2.66 | 6.33 | 112.15 | 11.09 | 21 |
| FireTruck | 2.22 | 5.93 | 116.67 | 11.09 | 21 |
| Garbage | Target not reached | 6.44 | 98.11 | 11.09 | 21 |
| Sportbike | 0.58 | No sampled stop | 198.58 | 9.43 | 27 |
| Chopper | 0.72 | No sampled stop | 172.75 | 8.32 | 27 |
| Scooter | 0.53 | 1.30 | 90.12 | 5.54 | 23 |

## Collision findings

Completed 122 phases at 60 Hz and 20 Hz, with 18 failed checks and no invalid state. These are individual check failures, not a count of distinct defects.

| Phase | Failed check | Measured value |
|---|---|---|
| `glancing-wall-60hz` | `glancing_contact_episodes` | 3.000000 |
| `symmetric-head-on-60hz` | `symmetric_yaw_rad_s` | 0.419268 |
| `rear-end-60hz` | `rear_end_final_a_kmh` | 20.882448 |
| `rear-end-60hz` | `rear_end_final_b_kmh` | 29.117552 |
| `t-bone-rear-60hz` | `mirrored_spin_rad_s` | 1.100507 |
| `rail-hard-hit-60hz` | `rear_end_final_a_kmh` | -8.066414 |
| `rail-hard-hit-60hz` | `rear_end_final_b_kmh` | 0.000000 |
| `rail-hard-hit-60hz` | `rail_released_before_contact` | -1.000000 |
| `rail-gentle-shove-60hz` | `deep_overlap_steps` | 3.000000 |
| `two-point-cap-60hz` | `breakaway_outcome` | 0.000000 |
| `two-point-cap-60hz` | `breakaway_impulse_cap` | 254.074203 |
| `glancing-wall-20hz` | `glancing_contact_episodes` | 3.000000 |
| `symmetric-head-on-20hz` | `symmetric_yaw_rad_s` | 0.533938 |
| `t-bone-rear-20hz` | `mirrored_spin_rad_s` | 0.881240 |
| `rail-gentle-shove-20hz` | `deep_overlap_steps` | 1.000000 |
| `opposing-fast-pair-20hz` | `deep_overlap_steps` | 6.000000 |
| `two-point-cap-20hz` | `breakaway_outcome` | 0.000000 |
| `two-point-cap-20hz` | `breakaway_impulse_cap` | 245.049728 |

## Interpretation and next steps

The arcade model accelerates much faster than the approved targets, has insufficient braking distance and allows excessive reverse speed. Several brake phases enter reverse before a sampled stop is recorded; retain this failure rather than inventing a stopping distance.

Each class's failed-check count includes ten missing tyre-slip sample checks. Axle tyre slip does not exist in the arcade model; it must be available in the new model. This diagnostic absence is distinct from handling quality.

An all-pass skidpad sweep provides a lower bound, and an all-fail sweep provides no measured maximum. Do not interpret the logged `usable_cornering_g` as a bracketed physical maximum unless `skid_sweep_bracketed` passes. The current narrow target-centred sweep is useful for testing the approved cornering bands, but does not establish the old model's maximum grip.

Next: implement the approved axle force model and data-driven class parameters, resolve collision failures against the recorded cases, repeat all measurements and inspect screenshots. CJ-002 remains in progress until the user accepts the driving feel.

## City evidence

All six city runs completed on the same inputs in `review-20261003/manifest-20261003T151315124021Z-city-34016.json`. Process completion alone does not prove metric acceptance; compare the PHYS/PEDS logs and screenshots before and after.

| Scenario | Position flips | Heading flips | Max penetration (px) | Deep frames |
|---|---|---|---|---|
| `crash` | 11 | 30 | 0.3 | 0 |
| `derby` | 3 | 3 | 0.1 | 0 |
| `chase` | 1 | 10 | 0.1 | 0 |
| `foot` | 0 | 0 | 0.0 | 0 |
| `day` | 0 | 0 | 0.0 | 0 |
| `rampage` | 22 | 62 | 5.4 | 4 |

The `rampage` reference is not a clean collision pass: its deep-overlap frames remain an explicit baseline defect. Do not replace the zero-deep-frame requirement for isolated fixtures with this city result.
