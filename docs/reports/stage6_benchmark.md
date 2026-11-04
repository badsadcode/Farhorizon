# Stage 6 benchmark report

**Environment:** development container, software Vulkan (llvmpipe, 4 cores, Xvfb), Release,
1920×1080, no vsync, look `usedfuture`. This is a regression baseline only.

| Scenario | Frames | Avg frame | p99 | Avg GPU | Notes |
|---|---|---|---|---|---|
| `bench.combat_skirmish` | 267 | 112.1 ms | 149.1 ms | 109.1 ms | 2 raiders + 1 hauler, scripted perfect-aim fight in the Cinder Drift, 1 kill in 30 s |

At 1280×720 the same run averages 60 ms (16.7 fps).

## Reading
* Combat adds little next to the asteroid field around it. Bolts, rocket trails, explosions
  and sparks are additive glow sprites (at most a few hundred), and every explosion or rocket
  adds one point light into the shared 8-light budget. CPU-side combat (≤ 3 ships, ≤ 400
  projectiles, segment tests against nearby rocks with an AABB reject) does not show up in the
  frame time.
* Most of the cost is the asteroid field (instanced rocks) plus post-processing on the
  CPU rasteriser, the same as `bench.mining`.
* ASan/UBSan: 70 unit tests and a 150-frame skirmish run show no runtime errors. The only leak
  reports come from inside SDL/X11 display initialisation (`X11_GetGlobalContentScale`), not
  game code.
