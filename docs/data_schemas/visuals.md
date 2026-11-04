# Visual profiles (`content/visuals/post_profiles.json`, type `visuals`)

```json
{ "schemaVersion": 1, "default": "retro80s", "profiles": [ { "id": "...", "name": "...", ... } ] }
```

| Field | Meaning |
|---|---|
| `bloomStrength` | 0 disables bloom passes |
| `bloomThreshold`, `bloomKnee` | HDR luminance where bloom starts, soft-knee width |
| `bloomRadius`, `bloomIterations` (1–4) | blur width in quarter-res texels; iterations widen smoothly |
| `shadowTint`, `highlightTint` | split-tone multipliers (1 = neutral) |
| `saturation`, `contrast` | 1 = neutral |
| `vignette`, `scanlines` | 0..1 strength |
| `scanlinePeriod` | pixels between scanlines (doubled at 1440p and above) |
| `chromaticAberration` | radial RGB split at the screen edge |
| `grain` | animated film grain amplitude |
| `hud.primary/nav/field/warn` | HUD colours as [r,g,b(,a)] 0–255 sRGB |

F4 cycles profiles in file order. `--look <id>` picks one at startup.

`surfaceDetail` (optional, default 0.5) sets the strength of the procedural grime, panel seams and
fine bump that stand in for textures. 0 turns them off. See ADR 0011.
