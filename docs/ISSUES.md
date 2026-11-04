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
| 13 | fixed (0.5) | gameplay | In-system travel drive implemented (ADR 0008). |
| 14 | open | interiors | Interior lighting is flat (one key light + fill + rim, no point lights or shadows). The Kenney placeholder pieces leave small ceiling gaps that show the clear colour or sky. |
| 15 | open | docking | The approach autopilot assumes an approach from above or the side. From directly below the hub it can press against the hull. |
| 16 | open | interiors | Interior transitions are instant cuts (no seat/airlock animation or loading fade). |
| 17 | open | interiors | The on-foot camera is not interpolated between fixed steps (possible slight judder at high refresh rates). |
| 18 | open | audio | The placeholder sounds have only been verified by signal analysis (SDL disk driver), not by listening. The mix needs a human pass. |
| 19 | open | render | Point lights have no shadows and shine through walls; lamp placement accounts for it. |
| 20 | fixed (0.5) | economy | Gale Ridge Supply Shed creates the first trade route. |
| 21 | open | mining | Asteroids do not shrink visually as they deplete (yield only). |
| 22 | open | ui | The market panel is the only menu. Settings, a main menu and save slots come in Stage 7. |
| 23 | open | render | The planet impostor is the most expensive pass on the CPU rasteriser (~178 ms with Varga filling the view). Bake the surface to a cubemap if real GPUs ever show it. |
| 24 | open | landing | The descent and approach are scripted (no manual landing on the surface yet), and the speed readout shows the cinematic plunge rate. |
| 25 | open | planets | Planets do not rotate or orbit; site lighting is fixed (dusk at Gale Ridge). |
| 26 | open | travel | Transit ignores collisions; a jump through an asteroid field passes straight through. |
| 27 | open | combat | NPC ships only collide with asteroids; they can pass through stations and each other. Projectiles also pass through stations. |
| 28 | open | combat | AI steering is direct (no obstacle avoidance), so raiders sometimes ram rocks inside dense clusters and bounce. |
| 29 | open | combat | Balance (damage, heat, pirate accuracy, rewards) is tuned only by tests and benchmark runs. It needs a human play session. |
| 30 | open | combat | Salvage canisters reuse the asteroid chunk mesh (tinted cyan glow); there is no dedicated canister model. |
| 31 | open | contracts | No reputation or faction standing; contracts have no deadlines; bounties count any kill of the faction, even outside the contract area. |
| 32 | open | combat | The raider and hauler are Kenney CC0 placeholders (`craft_speederA`, `craft_miner`). At combat range they are a few pixels, so the HUD markers do most of the work. |
