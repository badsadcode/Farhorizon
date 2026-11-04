# Audio (`content/audio/audio.json`, type `audio`)

| Field | Notes |
|---|---|
| `ambienceGain`, `effectsGain`, `engineGain`, `uiGain` | bus levels |
| `sounds` | `{ name: { file (16-bit PCM WAV, relative to content/), bus: ambience/effects/engine/ui, gain } }` |
| `events` | `{ eventName: soundName }`. Events raised by the game: `clamp`, `impact`, `airlock`, `ui`, `radio`, `scoop`, `warning` |
| `locations` | `{ key: { beds: [ { sound, gain } ], random: { sounds[], minInterval, maxInterval, gain } } }`. Keys: `space` or an interior AssetID |

Special sound names: `engine_loop` (pitch and gain follow thrust) and `laser_loop` (while mining).
Regenerate the placeholders with `python3 tools/audio_tools/generate_placeholder_sounds.py content/audio`.
