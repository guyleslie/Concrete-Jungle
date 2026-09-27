# Audio

Every sound in the game is synthesised at start-up, so the game needs no audio files. Any sound can be replaced by a recording without code changes.

Source: `src/audio.h`, `src/audio.cpp`.

## Contents

- [Sound types](#sound-types)
- [Positional playback](#positional-playback)
- [Replacing sounds](#replacing-sounds)

## Sound types

| Type | Sounds | How they are made |
|---|---|---|
| One-shots | pistol, shotgun, rifle, punch, knife, crash, crash_small, explosion, horn, door, pickup, mission_pass, mission_fail, wasted, splash, glass, footstep, reload, scream | Filtered noise bursts and tones with envelopes; five overlapping voices per effect |
| Loops | engine, engine_diesel, siren, skid, ambience | Seamless buffers (a whole number of cycles), streamed so their pitch and volume can change every frame |

The engine loop follows the player's vehicle: its pitch tracks a simulated rev counter and throttle, and large vehicles blend in the diesel loop. The siren loop follows the nearest active siren; the skid loop follows the player's tyre slip.

## Positional playback

One-shots are played at a world position. Their volume falls off with the distance from the listener (the camera) and they are panned left or right.

## Replacing sounds

Drop a WAV file named after the sound into `assets/sounds/`, for example `assets/sounds/explosion.wav` or `assets/sounds/engine.wav`. A file found there overrides the synthesised version at start-up. See [Adding content › Sounds](../guides/adding-content.md#sounds).
