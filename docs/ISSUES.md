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
| 11 | partly fixed (0.12) | camera | Walking to the cockpit now shows the real view, but in flight the cockpit camera still sees the exterior hull top as a flat "dash". The walkable cabin exists, but flight uses the exterior view. |
| 12 | open | render | The retro look costs ~25 ms more than `clean` on the software rasteriser (expected to be negligible on GPUs). Bloom is iterative Gaussian, not a mip pyramid. |
| 13 | fixed (0.5) | gameplay | In-system travel drive implemented (ADR 0008). |
| 14 | open | interiors | Interior lighting is flat (one key light + fill + rim, no point lights or shadows). The Kenney placeholder pieces leave small ceiling gaps that show the clear colour or sky. |
| 15 | open | docking | The approach autopilot assumes an approach from above or the side. From directly below the hub it can press against the hull. |
| 16 | open | interiors | Interior transitions are instant cuts (no seat/airlock animation or loading fade). |
| 17 | open | interiors | The on-foot camera is not interpolated between fixed steps (possible slight judder at high refresh rates). |
| 18 | open | audio | The placeholder sounds have only been verified by signal analysis (SDL disk driver), not by listening. The mix needs a human pass. |
| 19 | partly fixed (0.8.3) | render | All 16 lamps in local scenes cast shadows. Flight-scene flares still shine through geometry. |
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
| 47 | fixed (0.8.2) | maps | Maps can be docked with in the game world. Adding one still takes two hand edits (#53). |
| 48 | open | render | No transparency in materials: everything is opaque. Spot lights act as point lights, and Area lights do not export. |
| 49 | open | maps | A map's visible geometry merges into one mesh, with no per-object culling, and each lamp's shadow pass redraws it. Very large maps will need splitting. |
| 50 | info | blender | The add-on was run in Blender 5.1 only. It declares 4.2 as the minimum, untested there. |
| 51 | open | render | Placed probes are captured once at load: moving things and the player never appear in them, and they are not recaptured when a save or hot reload changes the map. |
| 52 | open | render | Probe mips are a plain box filter, so glossy reflections are less accurate than a prefiltered probe, and the first frames of a map use the live view until each probe is ready. Glow sprites, particles, clouds and stars are not in any probe. |
| 53 | open | maps | Placing a map in the world means editing the manifest and scene JSON by hand. The add-on could do it. |
| 54 | open | maps | The Ash Wren's exterior is the interior model seen from outside, and shows its map name, "Derelict Sample", on entry. It has no hull markings where you land. |
| 55 | open | render | Emissive surfaces are approximated by point lights, so light has no true panel shape, and a light offset from a flat panel can look slightly off up close. Nothing is emitted in reflections beyond the surface itself. |
| 56 | fixed (0.8.3) | render | Lights were chosen nearest-first per frame, so a room with more than 8 popped as you walked. Now 16 slots, chosen by contribution, with hysteresis and fades (`LightSelector`). |
| 58 | open | render | In the test derelict the corridor floor is much darker with lamp shadows on than off, though a plain test hall with 12 lamps lights its floor properly with them on. Not understood yet. The owner's map hides it with its big emissive panels. |
| 59 | fixed (0.10) | foot | Enemies follow paths from a navigation grid baked per map (ADR 0013). |
| 60 | fixed (0.10) | foot | Weapons, enemy types and loot tables are content files (`content/foot/`). |
| 61 | open | foot | Crawlers walk through each other, and the gun can cast a sun shadow. Lamps ignore it. |
| 57 | open | render | Every lamp's shadow cube is redrawn each frame, 96 faces at most. Cheap on today's small maps, about 1.5 ms a frame on an RTX 3080. A big map will need static lamps' cubes cached. |
| 62 | open | foot | Enemies still walk through each other, and the navigation grid does not know about them, so a pack in a doorway jostles. |
| 63 | open | foot | The navigation grid treats every surface with headroom as floor, including tops of crates and pipes. It is walkable in principle, but enemies cannot jump, so a path may ask for a step they slide off. |
| 64 | open | foot | Spitter globs do not lead their target, by design, so a player walking in a straight line is easy to hit and one who strafes almost never is. Tune with playtests. |
| 65 | open | foot | The brute is one sphere for hits; there are no weak spots or armour plates yet. |
| 66 | open | foot | The test range uses plain untextured materials; it is a test map, not a showcase. |
| 67 | open | foot | The new UI scripts run slowly on the software renderer CI uses, so `test_range.txt` is a manual showcase and is not run in CI. |
| 68 | open | render | On the software renderer (llvmpipe) a few single frames come out washed out, with the whole room brightly lit (seen during a flechette volley and a brute's charge in the test range). The owner's GPU screenshots of the same kind of scene are dark and correct. Unconfirmed on a real GPU: watch for it. |
| 69 | open | ships | The Sparrow's two-deck interior is much bigger than its exterior model, and its windows do not line up with the hull. The exterior needs rebuilding to fit. |
| 70 | open | render | Aboard a ship, placed reflection probes are skipped, so shiny surfaces there only get the live probe. Sun shadows also reach only as far as the cascades cover. |
| 71 | open | ships | Interior screens and the reactor core are flat bright blocks with no detail, and crates are plain colours. |
| 72 | open | net | A networked game has no pirates, mining or fights: they are not shared yet, so they are switched off. Terminals offer buying and selling only. |
| 73 | partly fixed (0.14) | net | The server requires a password. Game traffic is not encrypted, the password is stored as readable text in server_config.json, and hosting from the game is command-line only. |
| 74 | open | net | The first real game (Poland to Canada, 0.14) showed rubberbanding, fixed in 0.15 but measured so far only with simulated delay. Packet loss has not been simulated. |
| 75 | open | net | Other players are a stiff figure with no walking or sitting animation, one colour for everyone, and no sound of their footsteps. |
| 76 | fixed (0.16) | net | A client now sees the travel drive effects, and a passenger gets a hint line saying who has the helm. |
| 77 | partly fixed (0.15) | net | Players can trade on a server. Repairs, upgrades and contracts aren't offered to them, and anything the session charges to its first slot goes to the server's empty slot. |
| 78 | fixed (0.14) | net | A client that fails to join or is removed now gets a message box with the reason. |
| 79 | open | net | The server cannot test its own port forwarding or show its public address, and there is no limit on how fast someone can reconnect to guess a password. |
