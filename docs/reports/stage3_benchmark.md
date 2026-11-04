# Stage 3 benchmark report

**Build:** Release, `--no-vsync --size 1920x1080 --benchmark <id>`, look `retro80s`.
**Environment:** the development container's software Vulkan (llvmpipe, 4 cores, Xvfb). These
numbers are **not** representative of target GPUs; they are a regression baseline only.

| Scenario | Frames | Avg frame | p99 | Avg GPU | Notes |
|---|---|---|---|---|---|
| `bench.docking_autopilot` | 416 | 107.8 ms | 134.3 ms | 105.0 ms | requests clearance, autopilot approach, ends 2 s after clamp (~45 s) |
| `bench.station_hangar` | 88 | 112.0 ms | 135.8 ms | 109.9 ms | on foot in Hangar 1, slow pan; sky visible through the bay |
| `bench.ship_interior` | 83 | 118.7 ms | 142.9 ms | 116.4 ms | on foot in the Sparrow cabin; sky drawn for the windows |

## Reading
* Everything is fill-rate bound on the CPU rasteriser, as in Stage 2. Interior geometry is a few
  hundred instanced pieces and costs little. The fullscreen sky, HDR and post passes dominate.
* The cabin got about 45 ms slower on llvmpipe after enabling `showSky` for its windows (the
  procedural sky runs full-screen even where walls cover it). On real GPUs this is negligible;
  baking the sky to a cubemap (ISSUES #8) would remove it everywhere.
* Raw summaries: `bench.*_llvmpipe.json` in this folder.
