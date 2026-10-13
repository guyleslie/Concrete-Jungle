# Radio

The car radio: stations that broadcast live, the player's own radio, and the music of cars passing by, heard as from inside a closed car. Every station, clip and sound value comes from [`assets/data/radio.cfg`](../../assets/data/radio.cfg); the decisions behind the design are [ADR-0014](../adr/0014-live-radio-stations.md) and [ADR-0015](../adr/0015-radio-song-excerpts.md).

Source: `src/radio.h`, `src/radio.cpp`; measurements in `src/radio_tests.*`, `tests/radio.cfg` and `tools/run_cj040.py`.

## Contents

- [Stations](#stations)
- [The programme](#the-programme)
- [The player's radio](#the-players-radio)
- [Radios in traffic](#radios-in-traffic)
- [How a passing car sounds](#how-a-passing-car-sounds)
- [Threads and the stream processor](#threads-and-the-stream-processor)
- [Content of a station](#content-of-a-station)
- [Tuning](#tuning)
- [Measured behaviour](#measured-behaviour)
- [Limits](#limits)

## Stations

A station is a folder of clips under `assets/radio/` and a `STATION` record naming it. The first and only station so far is **Asphalt Rock FM**, 1980s American rock (`assets/radio/asphalt_rock/`). Stations broadcast live: from the first frame of play each one decodes its programme all the time, whether anyone listens or not. A game joins every station at a random point of its programme, so the radio is in the middle of a song when play begins. A car tuned to a station joins the broadcast where it is, and every car on a station plays the same moment of it. The title screen and the pause menu hold the broadcast.

## The programme

Each station plays songs, and between two songs at most one talk clip: the DJ (`DJ`) or a commercial (`AD`). After every song a clip follows with the station's talk share (1.0 for Asphalt Rock FM: after every song), never two clips in a row. A song comes back only after the station's song gap of other songs (5), a clip only after its talk gap of other clips (3); each gap must be smaller than the number of songs or clips, so there is always a choice. The broadcast opens with a song.

A song longer than the station's `EXCERPT` (90 s) is aired as a slice of that length from a random point of it, a different part every time ([ADR-0015](../adr/0015-radio-song-excerpts.md)). The slice fades in over 2 s, unless it starts at the beginning, and out over 4 s, with a smoothstep curve; the fades are applied by the station's processor, so a passing car's radio fades too. Talk clips play whole. With the slices, a talk clip comes about every 1.6 minutes, and talk fills 7.9 % of the airtime.

The programme has its own random generator, seeded from the clock (a fixed seed in test runs), so every game sounds different and the gameplay generator never moves for the radio. Every clip has a gain in `radio.cfg` that brings it to the `LOUDNESS` target (−16 LUFS), measured by `tools/radio_gain.py`; nothing is re-encoded.

The end of a clip's slot is counted on the audio thread, and the next clip starts within a frame of it. raylib's own end-of-stream signal comes only after padding of up to a buffer (267 ms at 48 kHz), and an excerpt ends inside its file.

## The player's radio

When the player gets into a car the radio tunes in to, in order: the player's own last choice in that car; the station the car's radio was on (a traffic car's draw); otherwise a station drawn for the car. A motorbike has no radio. Its name shows at the top centre of the screen for 3 s, as in GTA 2. In a car, **Q** or the mouse wheel steps through the stations and then *Radio off*, as they step through weapons on foot; the car remembers the choice.

The radio fades in over about 0.4 s and stops when the player gets out, like a radio switched off with the engine; it also stops in a car that is burning or wrecked. The player's radio plays at the `INCAR` volume (1.0), as it is, in stereo.

In a real car the music plays some 8–10 dB above the engine and road noise in the cabin (about 65–70 dBA at speed). At first the radio played 10 dB below the game's engine sound and could hardly be heard; the user chose the real balance (2026-10-12): the radio at full volume and the engine, heard from the cabin, 10 dB quieter than before ([Audio](audio.md#sound-types)). In steady driving the radio now plays at −21.7 LUFS against −27.7 LUFS of engine and city ambience, 6 dB above them. Other sounds outside are not muffled in the cabin yet ([CJ-012](../backlog.md#cj-012-audio-overhaul)).

## Radios in traffic

A traffic car (not a parked one, not one whose driver has got out) plays a radio with the `TRAFFIC` share (0.4). Classes named on that record never do (police, ambulance, fire engine), and neither does a two-wheeler, which has no closed body. A car's radio, its station and whether it is played loud (`CABIN`: a quarter of them, at 92 dB instead of 80 dB in the cabin) are drawn once from the car's serial, so a car keeps its music.

## How a passing car sounds

Per station, the four loudest radios in traffic are heard. For each one the main thread works out, once a frame:

| Step | Rule | Values (`radio.cfg`) |
|---|---|---|
| Level just outside the car | cabin level minus the body's loss | 80 or 92 dB, `BODY` loss 25 dB |
| Distance | 6 dB less per doubling beyond the reference distance from the car's centre | `HEARING` reference 2 m |
| Player in a car | a second body's loss | 25 dB more |
| Heard | while the level exceeds the threshold; it fades out over its last 6 dB | `HEARING` threshold 40 dB: a normal radio is heard out to 11.2 m, a loud one to 44.8 m; from inside a car only a loud one, within 2.5 m |
| Gain | `INCAR` × 10^((level − normal cabin level) / 20) | the player's radio stands for the normal cabin level |
| Direction | constant-power pan from the car's sideways offset over its distance, unity per channel in the centre | left −1 … right +1 |
| Travel time | distance / 343 m/s | |

The car body passes the bass and holds back the rest, as a second-order low-pass at the `BODY` corner (300 Hz, 12 dB per octave above it): the drums and the bass come through, the guitar and the voices are muffled. Two bodies, the player in a car, cut twice as hard. The travel time changes as the car approaches and leaves, which plays its music faster and then slower: the Doppler shift. A car passing at 50 km/h drops by 1.4 semitones, at 80 km/h by 2.3.

## Threads and the stream processor

A station has one raylib music stream, for its current clip, and one stream processor ([ADR-0014](../adr/0014-live-radio-stations.md)). The processor runs on the audio thread at the device's sample rate. For every frame it:

1. applies the clip's gain;
2. writes the mono sum into a ring of 32,768 frames (0.68 s at 48 kHz) for the travel times;
3. outputs the player's radio, if tuned to this station, at its volume;
4. for each voice, reads the ring at the voice's travel time (linearly interpolated), filters it through one body or, smoothly blended, two, and adds it with the voice's left and right gains.

The main thread hands the targets over in atomics, and the processor moves towards them: gains and the second body in 30 ms, the travel time in 50 ms. A voice that takes another car does so only after 0.25 s of silence, and starts from that car's travel time, so nothing clicks. raylib reports neither the device's sample rate nor the end of a clip, so a processor on the final mix counts the frames mixed. After the first second the nearest standard rate is taken (48 kHz is assumed until then). The slot's start, end and fades are handed over in seconds, so they stay right when the rate is learnt.

While a clip plays, the next one is prepared: the main thread opens its file, and a worker thread seeks into it (an MP3 seek decodes from the start of the file, 0.3–0.7 s). When the slot has been aired, the main thread switches streams and opens the following clip a frame later; its radio update stays under 5 ms in that frame. At start-up the worker seeks into the first song at a random point while the title screen shows.

## Content of a station

The user's requirements for every station (2026-10-12):

- **Ten songs and ten talk clips.** Mixed and ordered by the programme, they should sound like a real radio station of that kind. With ten of each, the gaps can be raised to about 6 songs and 5 clips.
- **Instrumental songs.** The songs have no vocals.
- **Talk without words.** The DJ and the commercials are not meaningful speech but American-sounding gibberish ("habla babla"), in the voice and the energy of the station's kind: an FM rock DJ, an over-enthusiastic 1980s commercial.

Asphalt Rock FM has eight songs, three DJ clips and three commercials so far, see [CJ-040](../backlog.md#cj-040-car-radio). How to add a station or clips is in [Adding content › Radio stations](../guides/adding-content.md#radio-stations).

## Tuning

| Value | Where | Effect |
|---|---|---|
| `INCAR` | `radio.cfg` | Volume of the player's radio, and with it of every radio outside |
| `CABIN` | `radio.cfg` | How loud traffic plays, and how many play loud |
| `BODY` | `radio.cfg` | How dull and how quiet a radio sounds from outside |
| `HEARING` | `radio.cfg` | How far a radio is heard |
| `TRAFFIC` | `radio.cfg` | How many cars play one, and which classes never do |
| `PROGRAMME` | `radio.cfg` | Talk between songs, and how soon a song or clip comes back |
| `EXCERPT` | `radio.cfg` | How long a song is aired, and its fades |
| `VOICES` | `RadioSystem` | Radios in traffic heard at once per station (4) |
| `GAIN_TAU`, `DELAY_TAU`, `FADE_DB`, `RELEASE_S` | `src/radio.cpp` | Smoothing and fading; changing them changes the measurements' expectations in `tools/run_cj040.py` |

## Measured behaviour

The [radio scenes](../testing.md#radio-scenes) record the final mix and the station's own signal on an empty road. The results of 2026-10-12 and 2026-10-13 are in [Testing › Radio scenes](../testing.md#radio-scenes): the programme follows its rules over 240 simulated hours, with a talk clip after every song; every clip airs its slot to the millisecond, and the fades follow their curves within 0.04 dB; clips change within 21 ms; every talk clip plays at −16.0 LUFS; a passing car's level follows the model within 0.6 dB (95 % of the windows); its sound is 26–32 dB duller than the player's radio, 25 dB duller again from inside a car; its Doppler shift is within 0.24 % of the car's motion; it falls silent where the model says, on the side where the car is, without a click. In the city the gameplay metrics are unchanged, and the radio takes 0.075 ms of main-thread time per frame.

## Limits

- Buildings do not block or muffle a radio; sound is not occluded anywhere in the game yet ([CJ-012](../backlog.md#cj-012-audio-overhaul)).
- Windows and doors are always closed: a car never plays with its windows down.
- Every station decodes all the time: about 0.5 % of a core each. Up to eight stations are supported.
- The main thread switches to the next clip: if it stalls (saving a screenshot in a test run takes up to half a second), the silence between two clips lasts as long.
- From inside a car only the radio outside is muffled by the body; every other sound outside is still heard as on foot ([CJ-012](../backlog.md#cj-012-audio-overhaul)).
- The player's radio has no volume setting of its own.
