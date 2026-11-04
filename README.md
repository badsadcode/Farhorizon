# FARHORIZON

A compact, atmospheric 3D space sandbox running on a **purpose-built engine** (C++20, SDL3, Vulkan 1.3).
It does not use an existing game engine. See `docs/STATUS.md` for what works today.

![Docking autopilot on final approach to Kestrel Station](docs/screenshots/stage3_docking_approach.jpg)

| Docked on the pad | Sparrow cabin | Kestrel hangar |
|---|---|---|
| ![](docs/screenshots/stage3_docked.jpg) | ![](docs/screenshots/stage3_cabin.jpg) | ![](docs/screenshots/stage3_hangar.jpg) |

| Retro 80s look (default) | Clean look (F4) |
|---|---|
| ![](docs/screenshots/stage2_station_retro80s.jpg) | ![](docs/screenshots/stage2_station_clean.jpg) |

## Quick start

Requirements: CMake ≥ 3.24, a C++20 compiler (MSVC 2022 / GCC 13 / Clang 17), the Vulkan SDK
(headers, loader, `glslangValidator`), Git (SDL3 is fetched at configure time).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

Run `build/farhorizon` (on Windows: `build\Release\farhorizon.exe`). Full instructions,
including headless runs and benchmarks, are in [`docs/BUILDING.md`](docs/BUILDING.md).

## Controls (Stage 2 flight)

| Key | Action |
|---|---|
| Mouse (captured) | virtual stick: pitch / yaw. **Esc** frees or captures the mouse |
| Arrow keys | pitch / yaw |
| W / S | throttle up / down (assist on). Main engine / retro (assist off) |
| X | zero throttle |
| A / D, Space / Left Ctrl | strafe left / right, up / down |
| Q / E | roll |
| Left Shift | boost (energy limited; release and press again after it runs out) |
| Z | flight assist on / off |
| B | precision (docking) speed mode |
| C | recentre the virtual stick |
| T / G / Backspace | next target / nearest asteroid / clear target |
| L | request docking / cancel / undock (contextual) |
| K | docking approach autopilot on/off (while cleared) |
| F | interact: leave the pilot seat (docked), airlock, board, terminals |
| V | chase ↔ cockpit camera |
| F2 | developer cameras (orbit → free → back) |
| F1 | developer info overlay |
| F3 | autofly (scripted test flight) |
| F4 | cycle visual look (Retro 80s ↔ Clean) |
| F5 | hot-reload content (keeps old content if the new content is invalid) |
| F11 / F12 / F10 | fullscreen / screenshot / quit |

**On foot:** WASD walk, Shift run, Space jump, mouse look, F use.

All bindings and mouse settings live in `content/config/input.json`.

**Try the Stage 3 loop:** fly toward Kestrel Station (3 km ahead) and press **L** for clearance,
then land on the roof pad yourself or press **K**. Once docked, **F** leaves the seat. Walk aft to
the junction and right to the airlock, then **F** steps into the hangar. Walk back to the ship,
press **F** to board, **F** at the seat, and **L** to undock.
Debug shortcuts: `--start-docked`, `--start-in ship|station`, F6 force-dock.

## Layout

```
engine/   core, math, platform (SDL3), assets (glTF, images), render (Vulkan), debug
game/     content defs, flight model, world, docking, interiors, character controller, session, cameras, HUD, app
shaders/  GLSL, compiled to SPIR-V at build time
content/  data-driven content: asset manifest, ships, scenes, input bindings, models
tools/    content_validator, asset_tools (GLB repair)
tests/    headless unit tests (no GPU required)
docs/     status, decisions (ADRs), data schemas, reports
```
