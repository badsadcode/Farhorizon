# ADR 0004 — HDR pipeline, post-processing and "looks"

**Status:** accepted · **Date:** 2026-11-04

## Context
Stage 1 tone-mapped inside each shader, so it had no proper HDR. The design asks for HDR
tonemapping, exposure and restrained bloom (doc 9.3 step 3). The project owner also asked for an
**80s sci-fi vibe** through post-processing.

## Decision
* Scene passes (sky, stars, meshes, glow sprites) render **linear radiance** into an
  `R16G16B16A16_SFLOAT` target with reversed-Z depth.
* **Bloom:** a quarter-resolution soft-knee bright pass (with firefly clamp), then N iterations of a
  separable 9-tap Gaussian at constant width. Repeated constant-width blurs widen smoothly; growing
  the tap spacing caused visible blocky ringing around the sun. A full mip-pyramid bloom can come
  later if needed.
* **Final composite** (`tonemap.frag`) draws to the sRGB swapchain in this order:
  1. bloom add, exposure, ACES fit;
  2. split-tone grade (shadow and highlight tints), contrast, saturation;
  3. radial chromatic aberration, vignette, scanlines, animated film grain.
  The HUD/debug overlay is drawn afterwards and stays crisp.
* **Looks are data**, defined in `content/visuals/post_profiles.json`. Each profile holds all post
  parameters plus the **HUD palette**. It ships with:
  * `retro80s` (default): strong bloom, teal shadows and magenta highlights, subtle scanlines,
    edge fringing, grain, and a hot-pink/cyan HUD.
  * `clean`: light bloom, no lens/film effects, amber HUD. This is for accessibility and for players
    who dislike grain or scanlines (design doc 11).
  F4 cycles looks at runtime and `--look <id>` selects one at startup.
* **Scene art direction** is data too (`sky`, `ambient.fill*`, `ambient.rim*` in the scene JSON):
  the deep-space colour, galactic band, nebula tint, a soft coloured fill light, and a rim light
  that keeps backlit hulls readable. The same file carries ship/prop **running lights** and the
  ship's **engine glow** (additive, depth-tested billboards that grow with thrust and recolour on
  boost).

## Consequences
* Cost on real GPUs is small: 1 + 2×N quarter-resolution passes plus one fullscreen composite.
  `bloomStrength: 0` skips the bloom passes completely.
* The HDR target, bloom chain and composite are generic, so later stages can tune looks or add
  profiles without touching renderer code.
