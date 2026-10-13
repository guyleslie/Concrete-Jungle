# ADR-0015: Radio songs aired as excerpts, sought on a worker thread

- **Status:** Accepted
- **Date:** 2026-10-13

## Context

In the playtest of 2026-10-13 the user did not hear the talk between the songs ([CJ-040](../backlog.md#cj-040-car-radio)). With whole songs of 2:20–3:19 and a clip after 65 % of them, every game opened with a song and no talk for its first three minutes (the median), and ten minutes of radio held about two clips. The user chose a clip after every song, a broadcast joined at a random point when a game starts, and songs aired as an excerpt of about a minute and a half, a different part of the song every time.

[ADR-0014](0014-live-radio-stations.md) kept the clips MP3 because nothing jumped into a clip during play: an MP3 seek in raylib 6.0 decodes from the start of the file and takes 0.3–0.7 s. An excerpt from a random point, and a join at a random point, both need such a seek.

## Decision

- **A song longer than the station's `EXCERPT` is aired as a slice of that length** (90 s for Asphalt Rock FM) from a random point of it, drawn by the programme. The slice fades in (2 s, unless it starts at the beginning) and out (4 s) with a smoothstep curve. The station's processor applies the fades on the audio thread, so a passing car's radio fades as well. Talk clips play whole.
- **The next clip is prepared while the current one plays.** The main thread opens its file; a worker thread seeks into it (raylib's `SeekMusicStream` does not hold the mixer's lock while it decodes). When the current slot has been aired, the main thread only switches streams, and opens the following clip a frame later.
- **A game joins every station at a random point.** At start-up the worker seeks into the first song's slice at a random point, while the title screen shows; the broadcast holds until play begins.
- **Positions are handed to the processor in seconds.** The processor converts them with the device's sample rate as it is measured, so that the slot, the fades and the clip's end stay right when the rate is learnt after the first second.

## Alternatives considered

- **Re-encode the clips as OGG Vorbis or QOA**, whose seeks take 8 ms and 2–3 ms. OGG is a second lossy encoding of the delivered MP3s; QOA doubles their size; both still seek on the main thread.
- **Excerpts from the start of the song, faded out.** No seek at all, and natural for a radio, but the user chose a different part every time: with eight songs, less repetition.
- **Seek on the main thread at the change of clip.** A stall of up to 0.7 s, visible in the game and audible as a gap.

## Consequences

- ADR-0014 holds otherwise: one decoder and one processor per station, every station decoding all the time.
- The main thread's radio update stays under 5 ms in the frame of a change of clip (filling the new clip's buffers costs about 2.5 ms); a seek never runs on it.
- A clip shorter than the seek into the next one (the shortest talk clip lasts 4.8 s, a seek at most 0.7 s) would leave silence until the seek is done; the station waits for it rather than skip.
- raylib restarts its own frame count when a sought stream starts playing; the radio counts the slot itself on the audio thread, and an excerpt always ends before the end of its file.
