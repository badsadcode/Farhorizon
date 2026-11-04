# ADR 0007 — Atmosphere: point lights, fog, flicker, "Used Future" look, audio

**Status:** accepted · **Date:** 2026-11-04

## Context
The project owner asked for the *feel* of late-70s to 90s industrial sci-fi: dread, loneliness,
the void, old-school equipment. **Everything stays original.** No assets, names, designs or
sounds are taken from any film or game; only the mood is the target.

## Decisions
* **Point lights (8 per frame, nearest first)** in the forward mesh shader, with a GGX
  specular term and a smooth windowed inverse-square falloff. Interior lamps, station work
  floods and the mining impact light surfaces. This is what makes "pools of light in darkness"
  possible.
* **Exponential distance fog** per interior (`lighting.fogColor/fogDensity`). Lamp sprites keep
  their hot core but scatter their halo into the fog.
* **Light flicker:** `flicker` (0..1) produces irregular bursts of dropouts (hashed time slots,
  clustered into "bad seconds"), which reads as failing hardware rather than a metronome.
* **"Used Future" visual profile (new default):** sodium-amber highlights over teal-green
  shadows, low saturation, higher contrast, heavy vignette, scanlines, grain and slight edge
  fringing, plus an amber / phosphor-green CRT HUD. `Retro 80s` and `Clean` remain available (F4).
* **Darker scene art direction:** cold teal-grey fill and rim, a dusty neutral nebula, and dim
  stations lit only by their own work lights.
* **Audio (doc 12):**
  * a small software mixer (`engine/audio/mixer.*`: voices, loop, pitch, pan, buses, fades,
    soft clip; unit-tested) behind an SDL3 device (`engine/audio/audio_device.*`) that fails
    gracefully to silence;
  * an `AudioDirector` driven by `content/audio/audio.json`: ambience beds per location with
    crossfades, randomly timed hull groans, an engine rumble that follows thrust, the mining
    laser, and event one-shots (clamps, airlock, radio chirp, UI beeps, scoop ticks, impacts).
  * All sounds are **synthesised by our own script** (`tools/audio_tools/generate_placeholder_sounds.py`).
    They are placeholders meant to be replaced by designed audio; the data file makes that a
    pure content change.
  * Verified headlessly with SDL's `disk` driver. Signal levels and spectra change with location
    and thrust. Nobody has *listened* to them yet, so the mix will need ears.

## Consequences
* Interiors cost more on the CPU rasteriser (fog and 8 lights per fragment); on real GPUs this is
  minor. There are still no shadows (doc 9.3 step 5); lights pass through walls, so lamp placement
  must account for it.
* The WAVs are mono PCM (~6 MB). A compressed format (OGG via stb_vorbis) can come when the
  real audio arrives.
