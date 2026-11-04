# Issue log

| # | Status | Area | Description |
|---|---|---|---|
| 1 | fixed (0.2) | build | Windows MSVC build + tests green in CI (official LunarG SDK installer). The game has not yet been *run* on a Windows GPU, since CI has no GPU. |
| 2 | fixed (0.2) | render | HDR target + tonemap pass + bloom (ADR 0004). |
| 3 | open | render | No image-based lighting. The shadow side of ships relies on a hemispheric ambient approximation. |
| 4 | open | render | One device allocation per buffer/image. Fine for Stage 1, needs a sub-allocator before asset counts grow. |
| 5 | partial (0.2) | assets | Sockets (`socket_*`, `hardpoint_*`) are imported. Collision helpers and LOD nodes are still baked into the visual mesh. |
| 6 | open | assets | Hot reload is manual (F5). There is no file watcher yet. |
| 7 | open | platform | Under Xvfb (no window manager) the F11 toggle state cannot be observed. Verify on Windows. |
| 8 | info | perf | The software rasterizer (llvmpipe) is GPU-bound on the procedural sky (5-octave fbm ×3 per pixel). This should be negligible on real GPUs. Bake it to a cubemap if it ever shows up in profiles. |
| 9 | open | physics | The station proxy collides as one sphere. Docking (Stage 3) needs per-part colliders and scripted capture. |
| 10 | open | input | No in-game rebinding UI; rebinding means editing `input.json` and pressing F5. |
| 11 | open | camera | The cockpit camera sees the exterior hull top as a flat "dash" (there is no interior mesh yet). |
| 12 | open | render | The retro look costs ~25 ms more than `clean` on the software rasteriser (expected to be negligible on GPUs). Bloom is iterative Gaussian, not a mip pyramid. |
| 13 | open | gameplay | No in-system travel drive yet (design 4.2); the station is 3 km away and the asteroid field 8 km away to keep cruise travel short. |
