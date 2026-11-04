# Stage 1 benchmark report

**Scenario:** `stage1_cockpit_empty_space`: Sparrow placeholder at 1 AU, auto-orbiting camera,
9,000 stars, procedural sky, overlay on.
**Build:** Release, `--no-vsync --size 1920x1080 --benchmark 10`.

## Environment
These numbers come from the **development container, not target hardware**:
* GPU: **llvmpipe** (Mesa 25.2 software Vulkan, LLVM 20, 4 CPU cores), under Xvfb.
* No discrete GPU available. RTX 3060-class numbers will be gathered once someone runs the same
  command on Windows.

## Results (204 frames)
| Metric | Value |
|---|---|
| Avg frame | 48.7 ms (20.5 FPS) |
| p99 frame | 62.2 ms |
| Max frame | 66.8 ms |
| Avg GPU time (timestamps) | 46.3 ms |
| Draw calls | 7 (sky 1, stars 1 instanced, ship 4 submeshes, overlay 1) |
| Triangles | 18,265 (264 ship, 18,000 star quads, 1 sky) |
| Engine GPU allocations | ~6 MB at 1600×900 |

Raw data: `stage1_cockpit_empty_space_llvmpipe.json`. Per-frame CSV: rerun the command, which
writes `benchmarks/*.csv`.

## Reading
* The frame is GPU-bound (GPU ≈ frame time). On llvmpipe the fragment-heavy procedural sky
  dominates (5-octave fbm evaluated 3× per pixel at 1080p). On any real GPU this is a fraction of
  a millisecond. If it ever shows in profiles, bake the sky into a cubemap at load time.
* CPU-side work is trivial at this stage: there are 7 draw calls and no per-frame allocations in
  the render loop apart from the small overlay vertex list.
