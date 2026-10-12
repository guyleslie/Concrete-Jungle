# Adding content

Vehicles, people, weapons, foliage, sounds and radio stations are data-driven: you add them by dropping files into `assets/` and adding a line to a data file, without changing code (see [ADR-0002](../adr/0002-data-driven-content.md)).

## Contents

- [Data file format](#data-file-format)
- [Vehicles](#vehicles)
- [Vehicles hitting people](#vehicles-hitting-people)
- [Traffic behaviour](#traffic-behaviour)
- [Player](#player)
- [Civilians](#civilians)
- [Weapons](#weapons)
- [Foliage](#foliage)
- [Loading screen](#loading-screen)
- [Sounds](#sounds)
- [Radio stations](#radio-stations)
- [Street furniture](#street-furniture)
- [Art and licence requirements](#art-and-licence-requirements)

## Data file format

All data files live in `assets/data/` and share one format:

- one record per line; tokens are separated by whitespace;
- the first token is the record type (case-insensitive);
- `#` starts a comment.

Every data file, and every file a record names, is required ([ADR-0010](../adr/0010-no-fallback-content.md)): when one is missing or unreadable, or a record names an unknown vehicle class or paint colour, the game stops loading and the loading screen and the log name the problem. Other malformed records are reported as warnings in the log and skipped.

## Vehicles

File: `assets/data/vehicles.cfg`. Images go in `assets/vehicles/` as PNG, **facing up** (the front of the vehicle at the top of the image), at 16 px = 1 m or higher resolution — the sprite is scaled to the class length and its width follows the aspect ratio.

Every image is drawn by the same rule: transparent edges are trimmed at alpha 0.1, and the remaining rectangle is centred on the vehicle and scaled so that its height equals the class length. The collision width is the drawn width unless the class has a [`WIDTH`](#width--collision-width) record. Export new art on a 512 × 512 px transparent canvas with the visible part 448 px tall and centred at (256, 256) px. Bake any registration or scale correction into the pixels: there are no per-image offset or scale settings. See the [CJ-004 export convention](../../assets/art/cj004/README.md#shared-vehicle-convention).

### `CLASS` — a type of vehicle

```
CLASS <name> <length m> <height m> <top speed km/h> <accel m/s²> <brake m/s²>
      <reverse km/h> <steer rad/s> <grip> <mass t> <hp> <traffic weight> [flags]
```

| Field | Meaning |
|---|---|
| length, height | Real dimensions; height is used for drawing, lights and draw order |
| top speed, reverse | Maximum forward and reverse speed |
| accel, brake | Arcade-tuned acceleration and braking (not real-world values) |
| steer | Yaw rate at full lock |
| grip | Rate at which the tyres kill sideways sliding (≈ 7–12) |
| mass | Tonnes; decides who wins a collision |
| hp | Health |
| traffic weight | Relative frequency in traffic; 0 = never spawns as traffic |
| flags | `police`, `emergency` (siren and light bar), `large` (slower AI, wide turns, no kerb mounting), `two_wheeler` (single headlight, rider can be thrown off) |

Redefining an existing class name replaces it. The handling model these values feed is described in [Vehicles](../design/vehicles.md).

### `TURN` — turning circle

```
TURN <class> <turning circle m>
```

The kerb-to-kerb turning circle at full lock (traced by the outer front wheel), as manufacturers publish it: about 11 m for a car, 23 m for a 12 m bus. Traffic sizes its [turn paths](../design/traffic.md#turn-paths) from it, at load. A class without a `TURN` record gets 2.3 times its length.

### `WIDTH` — collision width

```
WIDTH <class> <width m>
```

The width of the collision box. A class without one uses its sprite's drawn width, mirrors included. The sprite keeps its aspect either way, so mirrors or handlebars drawn wider than the `WIDTH` reach beyond the collision box, as on a real vehicle. The shipped two-wheelers give the handlebar width of comparable real bikes; the cars get their records with [CJ-023](../design/street-geometry-proposal.md#vehicle-widths).

### `FRONT` — front edge

```
FRONT <class> <height m>
```

The height of the front's leading edge: the bonnet edge of a car (about 0.6–0.8 m), or the top of a flat front such as a van's, a bus's or a truck's. A person struck below their centre of mass wraps onto the vehicle; struck higher, they are thrown forward ([CJ-035](../backlog.md#cj-035-vehicles-hitting-people)). Every class needs one: a class without it stops loading. The [person-hit scenes](../testing.md#person-hit-scenes) test a new class without changes.

### `SPRITE` — an image for a class

```
SPRITE <class> <file> <extra paint variants 0-3>
```

Each extra variant re-tints the coloured body panels (green, blue, silver) and leaves glass and tyres untouched.

### `BIKE` — a motorbike with and without its rider

```
BIKE <class> <empty file> <ridden file>
```

Parked, abandoned and wrecked bikes show the empty image; a bike with a rider shows the ridden one. Both images need the same visible size and centre, with the bike at the same place in both, so the bike stays put when the rider gets on or comes off; the loader logs a warning if their aspects differ. Each `BIKE` line adds one look to the class: add more lines, for example the same bike in other colours, for variety.

### `DERIVE` and `COMPOSE` — new vehicles from existing art

```
DERIVE  <class> stretch <file> <band start> <band end> <factor> <paint colours...>
COMPOSE <class> <front file> <from> <to> <rear file> <from> <to> <rear stretch> <paint colours...>
```

`DERIVE` stretches a horizontal band of a sprite (fractions of its height) — for example a limousine from a car. `COMPOSE` stacks the front section of one sprite on the rear section of another — for example a truck cab on a van body. Each paint colour adds one repainted variant; this keeps the original art style.

### Colours

Paint colours of `DERIVE` and `COMPOSE`: `red blue white black silver green darkgreen beige teal maroon yellow orange cream grey darkgrey navy brown purple pink`, or `r,g,b`.

## Vehicles hitting people

File: `assets/data/impacts.cfg`: how vehicles meet people ([design](../design/impacts.md)). Every record is required, once, with every value; a missing, repeated or out-of-range one stops loading with its line.

```
PERSON     <mass kg> <centre of mass m>
BALANCE    <min m/s> <max m/s> <stagger s>
GROUND     <friction> <tumble deg per m>
WRAP       <launch deg> <impact factor>
PROJECTION <launch deg> <impact factor>
THROW      <share>
INJURY     <km/h> <per km/h>
DOWN       <km/h> <fall min s> <max s> <throw min s> <max s>
RUNOVER    <shove m> <jolt km/h>
```

| Record | Meaning |
|---|---|
| `PERSON` | A person's mass, and the height of their centre of mass: a vehicle's front edge (`FRONT`) below it wraps them, at or above it throws them forward |
| `BALANCE` | The speed change a civilian withstands on their feet, drawn between the two values (the player has the mean), and how long a stagger lasts; another contact meanwhile knocks them down |
| `GROUND` | The friction of a body sliding on the road, and of its landing; how far it turns per metre as it bounces and rolls along, so that it tumbles to rest instead of gliding |
| `WRAP`, `PROJECTION` | The launch angle and the impact factor (the share of the speed the body keeps) for the two kinds of front |
| `THROW` | The share of a real collision's throw distance a body travels, for play: 1 is real, 0.125 an eighth ([ADR-0012](../adr/0012-shorter-throws-for-play.md)); the injury and the time on the ground still follow the real collision |
| `INJURY` | The speed change at which half the people die, and the slope of the logistic curve |
| `DOWN` | The speed change that separates a fall from a throw, and the time on the ground after coming to rest for each |
| `RUNOVER` | How far a body driven over moves, and how much speed the vehicle loses |

Change these with the [person-hit scenes](../testing.md#person-hit-scenes), which check the result against the published measurements.

## Traffic behaviour

File: `assets/data/traffic.cfg`. It configures physical recovery after a car is knocked from its lane, cooperative yielding, driver incidents and turning through junctions. See [Traffic](../design/traffic.md#knocked-off-the-lane) for the behaviour contract.

```text
RECOVERY <name> <value>
YIELD    <name> <value>
INCIDENT <name> <value>
TURN     <name> <value>
```

Use these lowercase parameter names. The record type is case-insensitive, and `#` starts a comment. Values must be finite and within the listed inclusive range. Unknown names, unknown record types, malformed records and invalid values emit a warning and retain the prior validated value. Missing parameters use the built-in defaults; the shipped file is the reference for current tuning. Settings load once per process, so restart the game after changing the file. Measurement runs may read another file with `--traffic-config <path>`.

### Recovery

| Name | Unit and valid range | Meaning |
|---|---|---|
| `forward_speed` | 20–100 px/s | Desired speed during a forward recovery move |
| `reverse_speed` | 15–80 px/s | Desired magnitude of reverse recovery speed |
| `horizon` | 1–4 s | Time covered by each full candidate prediction |
| `stop_tail` | 0.25–1 s | End of the prediction reserved for stopping; also bounded to half the horizon |
| `clearance` | 0.25–1.6 px | Safety margin around checked actor and obstacle footprints |
| `planning_interval` | 0.1–1 s | Scheduled delay between full planning attempts |
| `commitment` | 0.15–1 s | Minimum commitment to a selected move, unless a hazard invalidates it |
| `hysteresis` | 0–30 score units | Penalty for changing between forward and reverse |
| `stall_time` | 0.5–3 s | Poor-progress duration before penalising repetition of the same move |
| `nearby_radius` | 300–1,500 px | Search extent for potential nearby recovery obstacles |
| `planning_steps` | 300–20,000 force steps | Shared rollout budget per 1/60 s of frame time; planning jobs beyond it continue in later frames ([planning budget](../design/traffic.md#planning-budget)) |

The predictor uses the current production vehicle forces. It matches the measured 60 Hz/20 Hz frame intervals with 240 Hz/160 Hz force steps respectively, and caps faster-frame forecasts at 240 Hz. Control refreshes follow the actual frame interval, rounded to the next forecast sample, with the same cap. Tuning does not bypass class dimensions, steering limits or physical contacts. Lowering a safety margin changes the accepted clearance, so compare the [frozen recovery scenarios](../testing.md#cj-016-recovery-measurements) before and after tuning. A smaller `planning_steps` lowers the CPU peak but delays decisions when many cars recover at once.

### Yielding

See [Cooperative yielding](../design/traffic.md#cooperative-yielding).

| Name | Unit and valid range | Meaning |
|---|---|---|
| `enabled` | 0–1 | 0 restores the earlier stand-off: nobody gives way (baseline measurements) |
| `detect_time` | 0.1–3 s | How long two drivers must wait on each other before one gives way |
| `clear_time` | 0.2–5 s | How long the conflict must look resolved before the yielder drives on |
| `retreat_speed` | 20–120 px/s | Reversing speed along the driven path |
| `retreat_accel` | 40–400 px/s² | Reversing acceleration |
| `retreat_extra` | 0–200 px | Room beyond a knocked car's length that the rail car backs up |
| `max_chain` | 1–6 drivers | Longest chain of queued drivers backing up together |
| `cycles` | 0–1 | 0 keeps only the pair rule: nothing resolves two knocked cars or a loop of three or more drivers (baseline measurements) |
| `min_room` | 2–60 px | Least room a knocked car must be able to make, moving straight away from the driver waiting on it, to take the making-room role |

### Turning

See [Turn paths](../design/traffic.md#turn-paths). Turn paths are planned while the game loads.

| Name | Unit and valid range | Meaning |
|---|---|---|
| `lateral_accel` | 1–8 m/s² | Lateral acceleration allowed at the rear axle in a turn; it sets the turning speed |
| `easement` | 0–1.5 radii | Length of the gradual steering into and out of the arc (clothoids) |
| `left_radius` | 3–12 m | Preferred radius of a left turn; never tighter than the class can turn |
| `max_swing` | 0–4 m | How far a wide turn may move towards the centre lines before turning |

### Incidents

See [Driver incidents](../design/traffic.md#driver-incidents). For a playtest, a temporary `INCIDENT aggressive_share 1` makes every driver aggressive.

| Name | Unit and valid range | Meaning |
|---|---|---|
| `enabled` | 0–1 | 0: drivers never react to a collision (baseline measurements) |
| `aggressive_share` | 0–1 | Share of new traffic drivers who are aggressive |
| `calm_share` | 0–1 | Share who are calm; the rest are normal (honk). Reduced if the shares exceed 1 |
| `min_impact` | 10–400 px/s | Closing speed that a driver takes personally |
| `serious_dv` | 50–1,000 px/s | Delta-V above which the crash is serious and nobody starts an argument |
| `confront_chance` | 0–1 | Probability that an aggressive driver gets out after such an impact |
| `argue_time` | 0.5–15 s | Face-to-face argument before it escalates or ends |
| `give_up_distance` | 100–2,000 px | The other party has gone beyond this distance |
| `fight_time` | 2–60 s | Typical length of a fight (±20 %) |
| `exit_wait` | 0.5–15 s | Time to wait for a safe side to get out before driving on |
| `pair_memory` | 0–600 s | One incident per pair of cars within this time |

## Player

File: `assets/data/player.cfg`, written by [make_player.py](../../tools/cj011/make_player.py) with the atlases in `assets/characters/player/`: grids of square frames, the person **facing up**, every layer pivoted on the frame centre.

```
FRAME <px> <metres>
SCALE <factor>
COLS  <frames per row>
LEGS  <file>
GAIT  <gait> <first> <count> dist <metres per cycle>
GAIT  <gait> <first> <count> cadence <steps/s> <at m/s>
SET   <set> <file>
UPPER <set> <state> <first> <count>
LAMP  <set> <frame> <right> <ahead> <height> <yaw> <pitch>
DOWN  <file> <px> <metres>
FALL  <first> <count> <s>
LIE   <frame>
GETUP <first> <count> <s>
```

| Record | Meaning |
|---|---|
| `FRAME` | Frame size in pixels and the metres one frame covers at life size |
| `SCALE` | How much larger the player is drawn, on top of `CHAR_SCALE` (1.12, as the civilians) |
| `COLS` | Frames per atlas row |
| `LEGS` | The legs layer's atlas |
| `GAIT` | Where a gait's legs are in it, and how they play: by the distance moved (`dist`, a cycle of two steps per that many metres) or by cadence (`cadence`, steps per second at a speed, scaled with the speed between 0.6 and 1.3 times). Gaits: `stand`, `walk`, `run`, `strafel`, `strafer`, `back`, `jogl`, `jogr` |
| `SET` | The upper-body atlas of a weapon set (the `anim set` of `weapons.cfg`) |
| `LAMP` | A flashlight's lens in one upper-body frame of a set: metres from the frame centre at life size (right, ahead, above the ground) and the beam's direction (degrees right of the facing, below the horizontal); the beam starts there, so it moves with the drawn lamp. Written by `make_player.py --lamp` |
| `UPPER` | Where a set's frames are: a gait (the weapon lowered, in step with the legs; as many frames as the gait's legs) or `aim`, `fire` (two: the recoil, the recovery), `reload` (four), `melee` (two: the fists' two punches, or a wind-up and the blow) |
| `DOWN` | The atlas of the player on the ground: the whole body without the weapon, in frames of their own size (large enough for the body lying), each centred on the pelvis ([CJ-035](../design/impacts.md#on-the-ground)) |
| `FALL`, `GETUP` | Where falling and getting up are in it, and the seconds each plays |
| `LIE` | The frame of the player lying |

Every gait must be present in the legs and in every set, every weapon's set must have an atlas, and the frames on the ground must be there; otherwise loading fails ([ADR-0009](../adr/0009-required-character-atlases.md)). At load, every frame is cut to its visible part and packed. To change the player, see the [CJ-011 player art](../../assets/art/cj011/README.md).

## Civilians

File: `assets/data/civilians.cfg`. Atlases go in `assets/characters/civilians/` as horizontal strips of square frames, the person **facing up**.

```
FRAME    <px> <metres>
SCALE    <factor>
SWAY     <radians>
ANIM     <animation> <first frame> <frames> [<s>]
GAIT     <jog|run> <from m/s> <steps/s> <to m/s> <steps/s>
WALK     <metres>
CIVILIAN <file> [walk cycle m]
```

| Record | Meaning |
|---|---|
| `FRAME` | Frame size in pixels and the metres one frame covers at life size |
| `SCALE` | How much larger standing civilians are drawn, on top of `CHAR_SCALE`, so that an average man reads as large as the player |
| `SWAY` | How far the game turns a walking person with the steps; 0 for rendered walks, which sway by themselves |
| `ANIM` | Where an animation's frames are: `walk`, `idle`, `lying`, `punch` (right, left), `fist` (two shake positions), `jog`, `run`, `fall`; jog and run are optional. The fall gives the seconds it plays at least (a longer flight stretches it) and must end in the lying pose; falling and lying frames cover the frame's metres × `CHAR_SCALE`, so that a body on the ground draws at real size |
| `GAIT` | From which speed a jog or run shows (it holds until 0.3 m/s below that), and its cadence, rising linearly between the two points |
| `WALK` | Walk cycle (two steps) for `CIVILIAN` lines that give none |
| `CIVILIAN` | One look and its walk cycle: the look's stride, so that a planted foot stays put |

The walk advances with the distance walked; the jog and run play by cadence, because their feet touch the ground for a frame at most. Every atlas needs the same frame size and count. The lying frame covers `FRAME` × `CHAR_SCALE` metres, so that the body shows at real size. At load, every frame is cut to its visible part and packed, so the transparent margins of the frames cost no video memory. At least one loadable `CIVILIAN` line is required; without one, loading fails ([ADR-0009](../adr/0009-required-character-atlases.md)).

The shipped looks are rendered from 3D models: to change or add one, edit [looks.json](../../tools/cj004/looks.json) and run `python tools/cj004/make_civilians.py` (see the [civilian 3D pipeline](../../assets/art/cj004/README.md#civilian-3d-pipeline)).

## Weapons

File: `assets/data/weapons.cfg`. The order of the lines is the order of the number keys 1–9.

```
WEAPON <name> <anim set> <damage> <shots/s> <spread deg> <pellets> <range m>
       <clip> <start ammo> <reload s> <melee|semi|auto> <sound> [light] [shells <s> <pump s>]
```

| Field | Meaning |
|---|---|
| anim set | A set from `player.cfg` |
| spread, pellets | Random cone per shot; pellets > 1 makes a shotgun |
| clip, start ammo, reload | Magazine size; reference ammunition (a pickup gives half of it; a gun with 0 never appears as a pickup); reload time |
| mode | `melee`, `semi` (one shot per click) or `auto` (hold to fire) |
| sound | `punch`, `knife`, `pistol`, `shotgun` or `rifle` |
| `light` | The weapon casts a flashlight beam |
| `shells` | Reloads shell by shell: only the missing shells, `<s>` each, then `<pump s>` to rack the pump; the `reload` time is then unused |

## Foliage

File: `assets/data/foliage.cfg`. Images go in `assets/foliage/` as top-down canopies.

```
TREE <file>
BUSH <file>
```

Trees are used in parks, streets and courtyards; bushes along paths, in planters and on sidewalks.

## Loading screen

File: `assets/data/loading.cfg`. `BACKGROUND`, `WORDMARK`, `FONT_BOLD` and `FONT_SEMI` select optional cosmetic resources. `HEADLINE`, `LABEL` and `DETAIL` take a registered task ID and joined words; dynamic details and qualified counts come from the loader. `TEXT` changes general presentation strings; `COLOR` takes a named color and four byte channels. The file header documents accepted keys. Missing resources retain the primitive presentation.

Adding content to existing vehicle, character or foliage definitions automatically changes their local work plans. Overall estimates use current metadata and optional unit-cost measurements from previous successful starts. A new loading subsystem also needs a registered parent and actual completion reports in code, before the overall plan freezes; cosmetics cannot create tasks or readiness.

## Sounds

Put a WAV file named after the sound into `assets/sounds/` to replace the synthesised version. The names are listed in [Audio](../design/audio.md#sound-types); `shout1.wav`–`shout3.wav` replace the three driver shouts.

## Radio stations

File: `assets/data/radio.cfg`; its header documents every record. How the stations, the programme and the sound of a passing car work is in [Radio](../design/radio.md).

### What a station needs

The user's requirements (2026-10-12):

| Content | Requirement |
|---|---|
| Songs | Ten instrumental songs of the station's kind, without vocals |
| Talk clips | Ten clips between the songs, DJ talk and commercials, of American-sounding gibberish rather than meaningful speech, in the voice and energy of the station's kind |
| Sound | The programme mixes and orders them so that the station sounds like a real one of its kind |

The clips are MP3 (or any format raylib streams: OGG, WAV, QOA). They are used as they are, never re-encoded; keep only the audio stream and drop cover art and tags, for example with `ffmpeg -i in.mp3 -map 0:a -c:a copy -map_metadata -1 -id3v2_version 0 out.mp3`.

### Adding a station or clips

1. Put the clips into `assets/radio/<folder>/`, named by kind and number: `song_01.mp3`, `dj_01.mp3`, `ad_01.mp3`.
2. Add a `STATION <folder> <name>` record, its `PROGRAMME` and one `SONG`, `DJ` or `AD` record per clip, with a gain of `+0.0`:

   ```
   STATION   asphalt_rock Asphalt Rock FM
   PROGRAMME 0.65 5 3
   SONG song_01.mp3 +0.0
   DJ   dj_01.mp3   +0.0
   AD   ad_01.mp3   +0.0
   ```

3. Run `python tools/radio_gain.py` (it needs ffmpeg): it measures every clip's loudness and writes the gain that brings it to the `LOUDNESS` target. `--check` only reports.
4. Raise the `PROGRAMME` gaps with the number of clips: each must stay below the number of songs or clips. With ten of each, about 6 songs and 5 clips.
5. Run the [radio scenes](../testing.md#radio-scenes): `python tools/run_cj040.py`.
6. Record where the clips come from and their licence in [CREDITS.md](../../CREDITS.md).

Up to eight stations are supported. A missing clip or a bad record stops loading ([ADR-0010](../adr/0010-no-fallback-content.md)).

## Street furniture

Street furniture is not data-driven yet ([CJ-005](../backlog.md#cj-005-data-driven-street-furniture)). Adding an item currently takes code changes in three places:

1. `spritegen::Prop` and `MakeProp` (`src/sprite_gen.*`) — the sprite;
2. `Dim` (`src/city_map.cpp`) — real-world size, collision radius and whether bullets break it;
3. `ApplyMaterial` (`src/city_map.cpp`) — breakaway strength, loose mass, soft or box shape (see [Physics › Street furniture](../design/physics.md#street-furniture));

plus a placement rule in the city generator.

## Art and licence requirements

- Match the existing art: glossy, detailed top-down sprites in the style of the Unlucky Studio vehicles. Flat cartoon or pixel art does not fit.
- Keep real-world proportions (16 px = 1 m).
- Use PNG; this raylib build cannot load JPG.
- Only use assets whose licence allows redistribution, and record every third-party asset in [CREDITS.md](../../CREDITS.md) with its author, licence and source. Assets that require attribution must also be credited in the game.
