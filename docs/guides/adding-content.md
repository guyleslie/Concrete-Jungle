# Adding content

Vehicles, characters, weapons, foliage and sounds are data-driven: you add them by dropping files into `assets/` and adding a line to a data file, without changing code (see [ADR-0002](../adr/0002-data-driven-content.md)).

## Contents

- [Data file format](#data-file-format)
- [Vehicles](#vehicles)
- [Characters](#characters)
- [Weapons](#weapons)
- [Foliage](#foliage)
- [Sounds](#sounds)
- [Street furniture](#street-furniture)
- [Art and licence requirements](#art-and-licence-requirements)

## Data file format

All data files live in `assets/data/` and share one format:

- one record per line; tokens are separated by whitespace;
- the first token is the record type (case-insensitive);
- `#` starts a comment.

Every loader has built-in defaults, so a missing file never stops the game. Unknown classes, files or generators are reported as warnings in the log.

## Vehicles

File: `assets/data/vehicles.cfg`. Images go in `assets/vehicles/` as PNG, **facing up** (the front of the vehicle at the top of the image), at 16 px = 1 m or higher resolution — the sprite is scaled to the class length and its width follows the aspect ratio.

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

### `SPRITE` — an image for a class

```
SPRITE <class> <file> <extra paint variants 0-3>
```

Each extra variant re-tints the coloured body panels (green, blue, silver) and leaves glass and tyres untouched.

### `DERIVE` and `COMPOSE` — new vehicles from existing art

```
DERIVE  <class> stretch <file> <band start> <band end> <factor> <paint colours...>
COMPOSE <class> <front file> <from> <to> <rear file> <from> <to> <rear stretch> <paint colours...>
```

`DERIVE` stretches a horizontal band of a sprite (fractions of its height) — for example a limousine from a car. `COMPOSE` stacks the front section of one sprite on the rear section of another — for example a truck cab on a van body. Each paint colour adds one repainted variant; this keeps the original art style.

### `GEN` — procedural placeholder art

```
GEN <class> <generator> <colours...>
```

Generators: `car_hatch`, `car_sedan`, `car_coupe`, `car_suv`, `car_limo`, `bus`, `boxtruck`, `firetruck`, `garbage`, `bike_sport`, `bike_chopper`, `bike_scooter`. Each colour token adds one variant; a token may join up to three colours with `/` (main paint / second colour / third colour — for bikes: paint / jacket / helmet). Motorbike generators also create the riderless version used for parked and abandoned bikes.

### Colours

Named colours: `red blue white black silver green darkgreen beige teal maroon yellow orange cream grey darkgrey navy brown purple pink`, or `r,g,b`.

## Characters

File: `assets/data/characters.cfg`. Frames go in `assets/<folder>/<prefix><n>.png` with *n* = 0, 1, 2…, **facing right** (+X).

```
SCALE <world px per source px>
ANIM  <set> <state> <frames> <pivot x> <pivot y> <folder> <file prefix>
FEET  <anim> <frames> <folder> <file prefix>
```

| Record | Meaning |
|---|---|
| `SCALE` | Size of a source pixel in the world (the Survivor frames use 0.062) |
| `ANIM` | Upper-body animation of a set; `state` is `idle`, `move`, `shoot`, `reload` or `melee`; the pivot is the body centre in source pixels |
| `FEET` | Leg animation drawn under the body; `anim` is `idle`, `walk`, `run`, `strafe_left` or `strafe_right` |

A weapon chooses which set the player's body uses.

## Weapons

File: `assets/data/weapons.cfg`. The order of the lines is the order of the number keys 1–9.

```
WEAPON <name> <anim set> <damage> <shots/s> <spread deg> <pellets> <range m>
       <clip> <start ammo> <reload s> <melee|semi|auto> <sound> [light]
```

| Field | Meaning |
|---|---|
| anim set | A set from `characters.cfg` |
| spread, pellets | Random cone per shot; pellets > 1 makes a shotgun |
| clip, start ammo, reload | Magazine size; reference ammunition (a pickup gives half of it; a gun with 0 never appears as a pickup); reload time |
| mode | `melee`, `semi` (one shot per click) or `auto` (hold to fire) |
| sound | `punch`, `knife`, `pistol`, `shotgun` or `rifle` |
| `light` | The weapon casts a flashlight beam |

## Foliage

File: `assets/data/foliage.cfg`. Images go in `assets/foliage/` as top-down canopies.

```
TREE <file>
BUSH <file>
```

Trees are used in parks, streets and courtyards; bushes along paths, in planters and on sidewalks.

## Sounds

Put a WAV file named after the sound into `assets/sounds/` to replace the synthesised version. The names are listed in [Audio](../design/audio.md#sound-types).

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
