# Stage 2 benchmark report

**Build:** Release, `--no-vsync --size 1920x1080 --benchmark <id>`, default look `retro80s`
unless noted.

## Environment
These numbers come from the **development container, not target hardware**:
* GPU: **llvmpipe** (Mesa 25.2 software Vulkan, LLVM 20, 4 CPU cores) under Xvfb.
* Real GPU numbers (RTX 3060 class) still need someone to run the same commands on Windows.

## Results
| Scenario | Frames | Avg frame | p99 | Avg GPU | Draws | Triangles |
|---|---|---|---|---|---|---|
| `bench.empty_space_cockpit` | 89 | 110.4 ms | 130.3 ms | 108.4 ms | 36 | 140k |
| `bench.asteroid_field_chase` | 88 | 112.0 ms | 136.5 ms | 110.0 ms | 24 | 60k |
| `bench.station_approach` | 91 | 109.2 ms | 129.2 ms | 107.0 ms | 36 | 100k |
| `bench.asteroid_field_chase`, look `clean` | 117 | 84.7 ms | 105.4 ms | 82.5 ms | 32 | 37k |

Raw summaries: `bench.*_llvmpipe.json` in this folder.

## Reading
* Every scenario is fully **GPU-bound** (GPU ≈ frame time). The triangle count barely matters on
  llvmpipe: the asteroid scene, with 60k triangles, costs the same as empty space. Fill-rate work
  dominates. The fullscreen procedural sky (5-octave fbm ×3 per pixel) costs the most, followed by
  the HDR composite (chromatic aberration doubles its texture fetches, plus grain and grading).
  The retro look costs ~25 ms more than `clean` *on a CPU rasteriser*; on a real GPU that is a
  fraction of a millisecond.
* Draw calls stay low thanks to instancing: all 380 asteroids use 6 instanced draws (one per rock
  variant). CPU frustum culling reduces submitted instances to what is visible.
* Compared with Stage 1 (48.7 ms on the same machine): we added a 16-bit HDR target, a bloom chain
  and a full-resolution composite. Those passes are expected and cheap on target hardware.
* Planned mitigation if real-GPU profiles ever show the sky: bake it into a cubemap at load time
  (ISSUES #8).
