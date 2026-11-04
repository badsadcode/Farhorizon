# ADR 0011 — Lighting pipeline: shadows, ambient occlusion, reflection probe, surface detail

**Status:** accepted · **Date:** 2026-11-04

## Context
The first real look at the game on a GPU showed four problems: heavy film grain, no shadows, no
contact darkening, and flat single-colour surfaces (the placeholder models have no textures). The
budget is generous: the owner plays on an RTX 3080, and the old software-rasteriser limit is gone.
The renderer stays **forward**: few lights, a simple material model, and no reason to pay for a
G-buffer.

## Frame order
1. Sun shadow cascades (when the scene has a real sun).
2. Lamp shadow cubes (the nearest lamps in local scenes).
3. Reflection probe: one cube face, then its mip chain.
4. Depth prepass.
5. Ambient occlusion: half resolution, then a small blur.
6. Shaded pass: sky, stars, planets, meshes, glows. Depth is read-only here.
7. Bloom, tonemap, HUD.

## Decisions
* **Sun: 3 cascades, 2048² each, ending at 40, 220 and 1400 m.** A bounding sphere per view slice
  keeps each cascade's size constant while turning, and snapping to whole texels stops edges swimming
  while moving. Rendering is camera-relative, so the snap uses the camera's world position
  (`RenderFrameParams::cameraPosition`). Filtering: 8 rotated disc taps.
* **Lamps: cube shadow maps for the 4 nearest lamps, 512² per face**, in a cube array. Caster
  lists are culled per lamp by range. **Emissive submeshes do not cast**, so a lamp's own fixture
  cannot black out its light. This fixes light passing through walls (issue #19). Lamps 5-8 and
  flight-scene flares stay unshadowed.
* **Shadow vertex shaders write the Vulkan cube table directly** (`sc`, `tc`, `ma`) rather than
  building view matrices. Sampling a direction then lands on the rendered texel by construction, with
  no up-vector or handedness conventions to get wrong.
* **Interior "key" lights do not cast.** They stand in for sky light in enclosed spaces, and a roof
  would black them out. Only open scenes (flight, planet surface) use sun shadows.
* **Reflections: one live probe at the camera, not sky-only IBL.** Space has little sky to reflect,
  and interiors have none, so sky-only image-based lighting would add almost nothing where players
  spend their time. A 128² cube renders the real scene, one face per frame (a full refresh every 6
  frames), the first frame fills all six. Rough surfaces read blurrier mips, and the smallest mips
  supply ambient light from the surroundings. The chain is a plain box filter, not GGX-prefiltered:
  cheap and good enough for worn surfaces. The BRDF term is an analytic approximation (Karis), so no
  lookup texture. The probe excludes the sun disc, because direct light already counts it.
* **Ambient occlusion needs depth before shading, so there is a depth prepass.** Forward shading can
  then apply AO to indirect light only, instead of darkening everything afterwards. `mesh.vert` is
  shared and `gl_Position` is `invariant`, so the shaded pass can test with `>=`. Depth is read-only
  in that pass, which lets AO sample it. Algorithm: screen-space obscurance (McGuire et al.), which
  only needs depth.
* **Procedural surface detail stands in for textures for now.** With no textures, even correct
  lighting looks flat. `mesh.frag` adds object-space grime, per-panel tone, panel seams and fine bump
  (surface gradients, so no tangents). `MaterialData::panels` is 0 for rock and ground. Emissive
  surfaces are skipped. Strength is per look (`surfaceDetail`). Seams are filtered, so a line thinner
  than a pixel gets fainter instead of dotted. **When real textures arrive (roadmap step 2), this
  should become an optional per-material effect.**
* **Grain, scanlines and chromatic aberration turned well down** on the default looks. Grain at 0.05
  in display space hid more than everything else cost.
* **Tonemapping stays ACES-fitted for now.** It was not what looked wrong, and changing it would mean
  re-tuning every look.

## Options
Shadows, Ambient occlusion and Reflections are toggles in Options > Graphics. Scripted runs use
the defaults (all on).

## Cost (RTX 3080, 1600×900)
Measured in the benchmarks; see `docs/reports/stage7_lighting_benchmark.md`.

## Consequences and limits
* The probe sits at the camera, so reflections have no parallax, and they lag up to 6 frames. Small
  bright lights can shimmer in rough reflections through the box-filtered mips.
* Sun shadows reach 1400 m, plus casters within 4 km toward the sun. Nothing shadows at planet scale.
* AO has a small blur without depth awareness, so thin halos are possible at depth edges.
* PBR texture maps (normal, metallic-roughness, emissive, occlusion) are deliberately not in this
  step: no asset has them yet. They arrive with the Blender pipeline.
* Overlapping coplanar placeholder pieces now show more clearly, because their detail differs. One
  case in the Sparrow cabin was fixed by moving a hatch 2 cm; others remain (issue #39).
