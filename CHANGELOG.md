# Changelog

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
