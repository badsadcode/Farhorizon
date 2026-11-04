# Issue log

| # | Status | Area | Description |
|---|---|---|---|
| 1 | fixed (0.7) | build | Windows MSVC build + tests green in CI. The game now also builds and runs on a Windows GPU (RTX 3080). |
| 2 | fixed (0.2) | render | HDR target + tonemap pass + bloom (ADR 0004). |
| 3 | fixed (0.7.1) | render | Live reflection probe supplies reflections and ambient light. Simple box-filtered mips, no parallax (ADR 0011). |
| 4 | open | render | One device allocation per buffer/image. Fine for Stage 1, needs a sub-allocator before asset counts grow. |
| 5 | partial (0.2) | assets | Sockets (`socket_*`, `hardpoint_*`) are imported. Collision helpers and LOD nodes are still baked into the visual mesh. |
| 6 | open | assets | Hot reload is manual (F5). There is no file watcher yet. |
| 7 | fixed (0.7) | platform | Fullscreen toggles verified on Windows, from Options, with the swapchain recreated both ways. F11 itself is the same code path. |
| 8 | info | perf | The software rasterizer (llvmpipe) is GPU-bound on the procedural sky (5-octave fbm ×3 per pixel). This should be negligible on real GPUs. Bake it to a cubemap if it ever shows up in profiles. |
| 9 | fixed (0.3) | physics | Props support oriented-box colliders; docking uses scripted capture onto a roof pad. |
| 10 | fixed (0.7) | input | In-game rebinding in Options > Controls (ADR 0010). One key per action in the UI; several keys per action still work through `input.json`. |
| 11 | open | camera | In flight, the cockpit camera still sees the exterior hull top as a flat "dash". The walkable cabin exists, but flight uses the exterior view. |
| 12 | open | render | The retro look costs ~25 ms more than `clean` on the software rasteriser (expected to be negligible on GPUs). Bloom is iterative Gaussian, not a mip pyramid. |
| 13 | fixed (0.5) | gameplay | In-system travel drive implemented (ADR 0008). |
| 14 | open | interiors | Interior lighting is flat (one key light + fill + rim, no point lights or shadows). The Kenney placeholder pieces leave small ceiling gaps that show the clear colour or sky. |
| 15 | open | docking | The approach autopilot assumes an approach from above or the side. From directly below the hub it can press against the hull. |
| 16 | open | interiors | Interior transitions are instant cuts (no seat/airlock animation or loading fade). |
| 17 | open | interiors | The on-foot camera is not interpolated between fixed steps (possible slight judder at high refresh rates). |
| 18 | open | audio | The placeholder sounds have only been verified by signal analysis (SDL disk driver), not by listening. The mix needs a human pass. |
| 19 | partly fixed (0.7.1) | render | The 4 nearest lamps in local scenes cast shadows. Lamps 5-8 and flight-scene flares still shine through geometry. |
| 20 | fixed (0.5) | economy | Gale Ridge Supply Shed creates the first trade route. |
| 21 | open | mining | Asteroids do not shrink visually as they deplete (yield only). |
| 22 | fixed (0.7) | ui | Main, pause, options, load/save screens and a restyled terminal. |
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
| 33 | open | ui | The UI theme is a placeholder until the owner's reference art arrives. |
| 34 | open | ui | Settings other than key bindings are written when you leave Options, so closing the window from inside Options loses changes made there. |
| 35 | open | ui | Menus and the terminal are keyboard and mouse only; there is no gamepad support. Mouse buttons cannot be rebound. |
| 36 | open | hud | Marker labels can overlap when objects line up on screen (station and target, for example). |
| 37 | open | hud | At 720p and 100% scale the small text is about 11 px, which is hard to read; Interface scale helps. Not judged by a human yet. |
| 38 | fixed (0.7) | tools | Loader messages about third-party Vulkan layers on the player's PC (seen with ReShade, which declines apps it is not set up for) no longer count as validation messages. They are logged at info level. |
| 39 | open | content | Overlapping coplanar placeholder pieces show as stripes or moire, for example on the hangar's back wall near the terminals and on the station hull. Blender-built maps replace them. |
| 40 | open | render | Reflections come from one probe at the camera: no parallax, and up to 6 frames of lag. Fast lighting changes may visibly trail. |
| 41 | open | render | Procedural surface detail uses object-space coordinates, so identical pieces share a pattern, and it does not yet understand real textures. Revisit with the Blender pipeline. |
| 42 | open | ui | There is no map. Fast travel is still N to cycle destinations and J to engage, with no galaxy or system map screen. |
| 43 | open | render | Volumetric clouds cost about 2 ms on an RTX 3080 at 1600x900 and are drawn at half resolution, so hull edges can pick up a slight halo in thick gas. They do not light the ship or block sunlight, and do not appear in reflections. |
| 44 | open | fx | Particle tuning for the travel drive is in code, not data. Particle textures are generated, with no way to supply your own image yet. |
| 45 | open | fx | An exhaust ribbon disappears at once when an NPC ship is destroyed or despawns, instead of fading. The explosion usually covers it. |
| 46 | open | fx | Tunnel, dust and ribbon strengths are in shaders and code, not data. There is no option to turn off the tunnel for motion-sensitive players yet. |
