# Changelog

## 0.6.0 — Stage 6: combat, contracts and upgrades (2026-11-04)
* **Combat:** shields and hull with delayed recharge, collision damage, weapon heat and
  overheat lock. Twin **pulse lasers** (simulated bolts, lead pip) and homing **seeker rockets**
  (RMB). Weapon selector: **1** guns / **2** mining laser. LMB fires the selected system.
* **AI pilots** fly the same flight model as you. **Rust Jackal raiders** patrol the deep side
  of the Cinder Drift and intercept, attack, evade and retreat. **Ossa Freight Co-op haulers**
  run a route past Kestrel and flee when shot.
* Explosions with bloom flash, point light and spark debris. Wrecks drop salvage canisters
  the scoop collects. Bounties pay on the kill.
* **Defeat:** the insurer tows you to Kestrel. Cargo and courier jobs are lost, and a 250 cr
  deductible is charged.
* **Contracts:** courier (sealed crates), procurement (ore for the Kestrel refinery, spares for
  Gale Ridge), bounty (Rust Jackals). Boards refresh each world day, you can hold 3 jobs, and
  jobs are paid when you arrive at the right place with the goods.
* **Shipyard:** 6 upgrades (cargo rack, hull plating, shield booster, thruster re-jetting,
  mining laser Mk II, weapon capacitors) and hull repair.
* The terminal now has tabs: **MARKET / SERVICES / CONTRACTS**.
* Combat HUD: shield, hull and heat meters, ship markers (triangle hostile, square civilian),
  target health, lead pip, cyan and red edge flashes, MISSILE INBOUND / HULL CRITICAL /
  OVERHEATED warnings, contract tracker.
* New synthesised sounds: laser shot, slug cannon, rocket launch, shield hit, hull clang,
  explosion, klaxon. Combat camera shake.
* Saves **v3** with **v2 → v3 migration**.
* **Stage 6 exit test in CTest:** take a bounty and a procurement job, fight raiders until the
  bounty is met, dock and get paid, save/reload, mine and deliver the ore, buy an upgrade.
  Tests: 70.
* Benchmark `bench.combat_skirmish`. The validator checks weapons, upgrades, contracts and
  weapon mounts.

## 0.5.0 — Stage 5: travel drive, planet and landing site (2026-11-04)
* In-system **travel drive** (N select, J engage): align, spool, transit, decelerate; exact
  double-precision arrival; mass lock; FOV stretch and engine howl in transit.
* **Varga** (planet) and **Ossa** (moon) rendered as ray-traced impostors: procedural surface,
  craters, terminator, atmospheric limb and halo, scissored to their screen bounds.
* **Landing:** descent corridor with re-entry heat and shake, a cloud-deck fade hiding the scene
  swap, a scripted final approach onto the pad in the surface scene, then Landed. Launch reverses
  it and returns to the site's orbit marker.
* **Gale Ridge Outpost:** procedural terrain with cliff boundaries, dusk atmospheric sky, dust
  fog, low gravity, sodium masts, an outpost corridor with the Supply Shed market, and wind audio.
* **Trade route:** the outpost pays well for supplies and sells ore cheaply; Kestrel the reverse.
* "Parked" generalisation: the seat, airlock and market work at stations and landing sites alike.
* Saves **v2** (landing state) with **v1 → v2 migration**.
* **Stage 5 exit test in CTest:** 3 landing/launch trade cycles, including continuing after a
  reload while landed; return destinations and precision checked. Tests: 59.
* Debug: `--start-landed`, `--start-in surface`; benchmarks for orbit, landing and surface.

## 0.4.0 — Stage 4: mining, cargo, market, save/load + atmosphere pass (2026-11-04)
* Economy: data-driven commodities (3 ores, 4 goods) and the Kestrel Station Exchange with
  bounded scarcity pricing, a spread floor, per-unit slippage and deterministic tick restocking.
  The cargo hold is measured in mass tons.
* Mining: asteroid composition, yield and hardness per field; scan (R); mining laser (LMB/Alt)
  with aim cone and range; ore chunks with scoop field and hard caps; depletion persists.
* Market terminal UI (M when docked, or F at the hangar terminal): CRT-style panel, keyboard and
  mouse, buy/sell/sell-all, ×10.
* Save/load: versioned JSON snapshots, atomic write plus `.bak`, F7/F8, autosave on docking,
  `--load`/`--continue`, saving on foot. Byte-identical save→load→save. Hot reload now uses the
  same snapshot path.
* **Stage 4 exit test in CTest:** profitable mining trip, save/reload, no duplication.
* Atmosphere: 8 point lights per frame, distance fog, flickering failing lamps, the new
  **Used Future** look (default) with an amber/phosphor CRT HUD, darker and colder art direction,
  station work floods, and a mining impact light.
* Audio: software mixer and SDL3 device, a data-driven AudioDirector (ambience beds per
  location, random hull groans, engine rumble following thrust, laser, clamps, airlock, radio,
  UI, scoop). All sounds are original, synthesised placeholders.
* Fixed: a crash on exit (audio stream destroyed after `SDL_Quit`).
* New benchmark: `bench.mining`. Tests: 56.

## 0.3.0 — Stage 3: docking and walkable interiors (2026-11-04)
* Docking state machine: clearance request, approach authorisation, deterministic tolerance-based
  capture with scripted settle, clamps, and undocking. Optional approach autopilot (K).
  Radio-chatter subtitles and approach HUD (pad marker, approach column, distance/speed/alignment
  readout, context hints).
* Station proxy: oriented-box colliders, a roof landing pad with blinking guide lights, and a dish
  collider.
* Walkable interiors from data (piece templates with colliders, spawns, interactables, lights):
  the **Sparrow cabin** (cockpit seat, windows onto space, cargo, airlock) and **Kestrel Hangar 1**
  (open bay to space, your parked ship, services terminal, pillars and props), built from CC0
  Kenney pieces.
* First-person capsule controller (collide-and-slide, gravity, jump, no tunnelling).
* `GameSession` (location state, interaction rules, active interior) and `world_builder`, shared by
  the app and the headless tests.
* **Stage 3 exit test in CTest:** 20 full fly → dock → walk → exit → walk → board → sit → undock
  cycles on real content.
* Debug tools: F6 / `--start-docked` force-dock, `--start-in ship|station`.
* New benchmarks: docking autopilot, ship interior, station hangar.
* Renderer: optional sky and stars, star-glare control, clear colour for enclosed interiors.

## 0.2.0 — Stage 2: ship flight, plus a retro 80s look (2026-11-04)
* 6DOF flight model (assist on/off, precision/cruise/boost, energy-limited boost), driven entirely
  by ship JSON. Deterministic and unit-tested.
* Keyboard and mouse virtual-stick controls. Mouse capture toggles on Esc; quit moved to F10.
* Chase and cockpit cameras; developer orbit/free cameras moved to F2.
* HUD: reticle, prograde/retrograde, speed/set-speed, throttle and boost bars, navigation markers,
  target bracket with range and range rate, off-screen arrows, impact/proximity warnings.
* Targeting (cycle, nearest asteroid, clear).
* Procedural asteroid meshes and deterministic clustered fields (instanced, frustum culled);
  station proxy from CC0 Kenney pieces; sphere collisions.
* The floating origin follows the player ship; static bodies are recomputed from double positions.
* glTF socket import (`socket_*`, `hardpoint_*`) with ship-JSON fallbacks.
* Renderer: HDR target, bloom, final composite with colour grading and lens/film effects;
  instanced mesh pipeline; additive glow sprites (engine exhaust, running lights); data-driven
  sky, fill and rim lighting.
* Data-driven looks (`content/visuals/post_profiles.json`): **Retro 80s** (default) and **Clean**,
  each with its own HUD palette. F4 cycles, `--look` selects.
* Data-driven benchmark paths (`content/benchmarks/`): empty-space cockpit, asteroid field chase,
  station approach.
* New schemas: ship (extended), scene (player/props/asteroidFields/sky), visuals, benchmark, input
  (mouse). `tools/asset_tools/repair_kenney_glb.py` fixes the kit's invalid scene roots.
* CI: the Windows job installs the Vulkan SDK with the official LunarG installer.

## 0.1.0 — Stage 0 + Stage 1 engineering spike (2026-11-04)
* CMake/C++20 project skeleton with the `fh_core` (GPU-free) / `fh_engine` / `farhorizon` split.
  Warnings on, optional sanitizers.
* SDL3 window and action-based input loaded from `content/config/input.json`.
* Vulkan 1.3 renderer (dynamic rendering, sync2): swapchain with resize/out-of-date handling, two
  frames in flight, validation layer + debug messenger, GPU timestamp queries, PNG screenshot
  readback.
* Passes: procedural sky (galactic band, dust, star disc and glare), 9,000-star instanced
  starfield (deterministic seed), PBR (GGX) mesh pass with reversed-Z infinite depth, and a
  developer overlay.
* glTF/GLB importer (cgltf) and data-driven ship and scene definitions with material overrides.
  The asset manifest uses stable AssetIDs.
* Double-precision system coordinates, floating local origin with rebasing, camera-relative
  rendering.
* Fixed-timestep simulation clock with capped catch-up.
* Orbit and free-fly developer camera, F5 hot reload with rollback on invalid content, F11
  fullscreen, F12 screenshots.
* Frame statistics overlay and benchmark mode with CSV/JSON export.
* `content_validator` tool, 21 unit tests, and a CI workflow (Windows MSVC build plus Linux
  sanitizer build with a headless smoke render).
