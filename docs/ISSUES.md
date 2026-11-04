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
| 9 | fixed (0.3) | physics | Props support oriented-box colliders; docking uses scripted capture onto a roof pad. |
| 10 | open | input | No in-game rebinding UI; rebinding means editing `input.json` and pressing F5. |
| 11 | open | camera | In flight, the cockpit camera still sees the exterior hull top as a flat "dash". The walkable cabin exists, but flight uses the exterior view. |
| 12 | open | render | The retro look costs ~25 ms more than `clean` on the software rasteriser (expected to be negligible on GPUs). Bloom is iterative Gaussian, not a mip pyramid. |
| 13 | open | gameplay | No in-system travel drive yet (design 4.2); the station is 3 km away and the asteroid field 8 km away to keep cruise travel short. |
| 14 | open | interiors | Interior lighting is flat (one key light + fill + rim, no point lights or shadows). The Kenney placeholder pieces leave small ceiling gaps that show the clear colour or sky. |
| 15 | open | docking | The approach autopilot assumes an approach from above or the side. From directly below the hub it can press against the hull. |
| 16 | open | interiors | Interior transitions are instant cuts (no seat/airlock animation or loading fade). |
| 17 | open | interiors | The on-foot camera is not interpolated between fixed steps (possible slight judder at high refresh rates). |
| 18 | open | audio | The placeholder sounds have only been verified by signal analysis (SDL disk driver), not by listening. The mix needs a human pass. |
| 19 | open | render | Point lights have no shadows and shine through walls; lamp placement accounts for it. |
| 20 | open | economy | One station market only, so trade routes need a second market (the outpost, Stage 5). |
| 21 | open | mining | Asteroids do not shrink visually as they deplete (yield only). |
| 22 | open | ui | The market panel is the only menu. Settings, a main menu and save slots come in Stage 7. |
