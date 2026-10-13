# Pedestrian attention and evasion proposal

The [CJ-038](../backlog.md#cj-038-pedestrian-attention-and-evasion) plan accepted for implementation on 2026-10-12: people differ in what they notice, how soon they react and how quickly they can get out of a vehicle's way. The 50–65 % escape target in `rampage` is a game design choice. Measurements and the user's playtest determine whether the result is ready for acceptance.

## Contents

- [Evidence and reusable references](#evidence-and-reusable-references)
- [Behaviour](#behaviour)
- [Data](#data)
- [Acceptance criteria](#acceptance-criteria)
- [Measurement plan](#measurement-plan)

## Evidence and reusable references

[Thompson et al. (2013), *Impact of social and technological distraction on pedestrian crossing behaviour*](https://injuryprevention.bmj.com/content/19/4/232.short) observed 1,102 people crossing at 20 high-risk intersections in Seattle. Distracting activity occurred in 29.8 % of crossings: music 11.2 %, texting 7.3 % and a handheld call 6.2 %. Texting was associated with 3.9 times the likelihood of at least one unsafe crossing behaviour. These observations support a varied population, but do not measure the chance of escaping a car driven into a crowd. The game uses about 30 % distraction as a starting population setting; its categories combine the observed distractions.

[Nie et al. (2021), *Safety envelope of pedestrians upon motor vehicle conflicts identified via active avoidance behaviour*](https://www.nature.com/articles/s41598-021-82331-z.pdf) recorded 40 VR crossing trials by 22 healthy young men. The car started at 80 km/h and braked at 0.7 g. Successful backward and forward avoidance had decision delays of 0.17 and 0.24 s, then 1.52 and 1.84 s available for execution; unsuccessful reactions averaged 0.41 s before execution. Perception preceded those delays and took about 0.5 s. Backward avoidance decelerated at an average 2.4 m/s² for about 0.73 s; forward avoidance accelerated at 7.4 m/s² for about 0.12 s to approximately 1.8 m/s. Obstructed vision produced more collisions. The abstract's 70 % avoidance result depends on the tested cases and reaction groups; it is not a general population escape rate. These results support separate noticing, decision and finite movement, rather than calibrating the city escape target directly.

[Fiset, Lamontagne and McFadyen (2023), *Effects of age on the visuo-locomotor control used to circumvent a virtual pedestrian with different limb movements*](https://pubmed.ncbi.nlm.nih.gov/36368219/) compared 14 young adults with 14 adults over 70 years in a virtual shopping mall. Older adults walked more slowly and needed more time to process visual information. The [Université Laval report](https://www.cirris.ulaval.ca/en/seniors-at-higher-risk-of-pedestrian-collisions/) describes their later onset of deviation. This is avoidance of another person, so the game's age factors are a design inference, not measured vehicle reaction times.

No cited study supplies the game's freezing fraction, headphone hearing rule or exact profile ranges. These remain explicit tuning choices and receive isolated behaviour checks.

| Candidate | Licence checked | Useful part and decision |
|---|---|---|
| [OpenSteer](https://github.com/meshula/OpenSteer), [licence](https://github.com/meshula/OpenSteer/blob/master/COPYING.OPENSTEER) | MIT | `SimpleVehicle::applySteeringForce` clips force, integrates acceleration and limits speed; `SteerLibrary` has distance and viewing-angle filters. Use as a conceptual reference for bounded locomotion and perception; retain the game's current steering and collision systems. |
| [RVO2](https://github.com/snape/RVO2), [licence](https://github.com/snape/RVO2/blob/main/LICENSE) | Apache-2.0 | Useful reference for nearby-agent collision prediction. Its ordinary `Agent::update` assigns the new velocity immediately, and it supplies no attention or reaction model. Do not add it as a dependency for CJ-038. |

Both code licences permit redistribution subject to their notices. This item uses the references without importing third-party code or assets.

## Behaviour

Each newly created person draws one persistent attention profile, a reaction time and whether they briefly freeze. A look supplies mobility and reaction factors: older looks walk and evade more slowly and can react later. The CJ-035 balance distribution in `impacts.cfg` stays unchanged, with no age scaling; physical contact still follows [Vehicles hitting people](impacts.md).

Attentive people scan frequently and notice a predicted conflict earlier. Distracted people scan less often and notice it later. Headphone wearers share the distracted timing and do not use a vehicle behind them as an auditory warning. Facing, the data-driven viewing angle and the sampled profile gate danger perception, including the recheck after a reaction delay, so an unseen rear vehicle cannot initiate a dodge through another code path. Someone already dodging can track the hazard they perceived, even after turning away. Crossing gap checks remain conservative and omnidirectional: this item does not make people enter an unsafe crossing for the sake of increasing impacts.

Once a perceived threat has persisted through the person's reaction time, the person chooses an available direction out of the vehicle's path. A small sampled share freezes briefly before starting that movement. Velocity approaches the desired step with a finite acceleration and a personal maximum speed; turning, replanning and avoidance forces must not bypass those limits. A wall or furniture can prevent escape. The low-speed contact stagger remains a contact response, whose immediate impulse is covered by CJ-035 rather than the voluntary dodge limits.

## Data

The planned required records in [`civilians.cfg`](../../assets/data/civilians.cfg) are:

```text
ATTENTION <attentive|distracted|headphones> <share> <scan s> <notice horizon s> <view degrees> <reaction min s> <reaction max s> <behind reaction factor> <hears behind 0|1>
DODGE <fast m/s> <slow m/s> <acceleration m/s²> <turn rad/s>
FREEZE <share> <min s> <max s>
CIVILIAN <file> <walk cycle m> <mobility factor> <reaction factor>
```

All three attention records are required; shares must form one population. Scan/reaction times and physical limits must be positive and ranges ordered; a freeze duration may be zero. Shares are between 0 and 1 and viewing angles between 180° and 200°. Each look explicitly supplies its factors. Invalid records fail loading with the file and line; missing records name the file and requirement, following [ADR-0010](../adr/0010-no-fallback-content.md). See [the format reference](../guides/adding-content.md#civilians) for validated numeric ranges.

Initial tuning, subject to measured before/after results:

| Profile | Share | Scan | Notice horizon | View | Reaction range | Rear reaction factor | Hears behind |
|---|---|---|---|---|---|---|---|
| Attentive | 70 % | 0.1 s | 1.6 s | 200° | 0.18–0.45 s | 1.5 | Yes |
| Distracted | 19 % | 0.35 s | 1.0 s | 200° | 0.35–0.75 s | 1.5 | Yes |
| Headphones | 11 % | 0.35 s | 1.0 s | 200° | 0.35–0.75 s | 1.5 | No |

The initial fast dodge is 4.5 m/s, the slow step 1.8 m/s, acceleration 6 m/s² and turn rate 20 rad/s. Initially, 8 % freeze for 0.25–0.65 s. Older looks use a mobility factor of 0.75 and reaction factor of 1.25; other looks use 1.0. These values are game tuning rather than a fit to the studies. The data file is the source of truth once tuning is measured.

## Acceptance criteria

| Criterion | Required result |
|---|---|
| `rampage`, 3,600 frames | 50–65 % of people counted in the car's path escape, replacing CJ-010's at-least-70 % target |
| `foot` and `day`, 1,500 frames each | No traffic hits; retain the existing crossing, false-alarm and population checks |
| Pedestrian CPU time | At most 0.5 ms per frame in the city scenarios; compare alternating before/after builds on the same machine |
| Rear perception | A fresh headphone wearer facing away from the approaching vehicle cannot receive a vehicle alarm or start a dodge before seeing it or being touched |
| Reaction and freezing | No voluntary evasion before the configured decision delay and any freeze; the person responds after those delays if the perceived threat remains |
| Mobility | Older-look factors delay the reaction and reduce voluntary dodge capability in otherwise identical cases |
| Motion limits | Each voluntary dodge velocity change stays within acceleration × mobility × elapsed time, with a small numeric tolerance; turning and avoidance cannot increase speed above the target or a faster inherited speed, which decelerates within the same bound |
| Persistent traits | Creation samples the profile once; later scans and threats do not redraw it; fixed test seeds reproduce the same profiles |
| Impacts | Existing CJ-035 contact, no-pushing, injury, throw, run-over and get-up checks still pass |
| Visual result | Screenshot series and the user's playtest show understandable late reactions, accelerating steps and brief freezes without sideways gliding |

## Measurement plan

Use the reusable [person-hit scenes](../testing.md#person-hit-scenes) with a focused scene file, `tests/person-evasion.cfg`, before changing production behaviour and again afterward:

```bash
./ConcreteJungle.exe --shot build/shots/cj038/evasion.png --scenario person-hits --test tests/person-evasion.cfg --frames 1000000 --uncapped > build/shots/cj038/evasion.log 2>&1
```

The scenes cover an approaching car meeting a crowd, paired attentive and distracted people, a rear approach to a headphone wearer, old and young looks under identical conditions, a forced brief freeze and an accelerating dodge from rest. Trait-specific cases use explicit test traits so that a missing behaviour cannot pass by chance. Record profile, notice and dodge onset, speed and acceleration, contact and escape counts. Keep crowd counts and escape shares for both builds; a crowd scene supplements the city's `rampage` measure rather than replacing it.

Preserve logs and screenshots under `build/shots/cj038/`. Run `foot`, `day` and `rampage` on both builds with the same arguments, then the existing CJ-035 scene file. Compare the actual newly measured baseline; the historical 85 % recorded in the backlog is context, not a substitute for this session's before run. Review screenshot series for the isolated scenes and the city, update [Testing](../testing.md) with measured results, and run `python tools/check_docs.py`.

The implementation was continued on the current `main` on 2026-10-13; its measured tuning and evidence are recorded in the [measurement report](pedestrian-attention-results.md). The initial values above remain the agreed starting point, rather than the shipped tuning. The focused escape, traffic-hit and CPU checks pass, but the retained-crossing criterion remains unmet: time at the traffic's green increased. Its blocked-crossing/contact follow-up is recorded under [CJ-031](../backlog.md#cj-031-pedestrian-blocking-and-contact-realism). CJ-038 awaits the user's playtest with that known regression; implementation does not imply full acceptance.
