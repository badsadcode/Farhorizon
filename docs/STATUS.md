# FARHORIZON status

_Last updated: 2026-11-04 · Stage 3 (docking + walkable interiors)._
Legend: ✅ implemented and verified · 🟡 partial · ⬜ absent

## Stage 0 — bootstrap
| Item | State | Notes |
|---|---|---|
| CMake C++20 project, module layout | ✅ | |
| Dependency manifest + licenses | ✅ | `docs/DEPENDENCIES.md` |
| Vulkan minimum feature set | ✅ | ADR 0001 |
| Logging, crash diagnostics | 🟡 | contextual fatal logging; no minidumps |
| Test harness | ✅ | 44 tests (incl. headless 20× Stage 3 loop) + content validation (CTest) |
| CI | ✅ | Windows (MSVC Release build + tests) and Linux (sanitizers + headless render + validation) both green |

## Stage 1 — window and visible space
| Item | State | Notes |
|---|---|---|
| SDL3 window/input, Vulkan swapchain, resize | ✅ | |
| Validation layer + overlay counters | ✅ | 0 messages in all runs |
| Star field, procedural sky, star disc | ✅ | sky colours now data-driven per scene |
| Imported GLB ship with PBR | ✅ | |
| Developer overlay, screenshots, benchmark export | ✅ | |
| Hot reload (F5) with rollback | ✅ | now also reloads bindings/looks and keeps the player's pose |
| Double-precision frames + floating origin | ✅ | focus is now the player ship |

## Stage 2 — real ship flight
| Item | State | Notes |
|---|---|---|
| Pilot controls (6DOF) | ✅ | keyboard + mouse virtual stick; verified interactively via xdotool |
| Flight assist on/off | ✅ | set-speed + damping / inertial; unit-tested |
| Speed modes: precision, cruise, boost | ✅ | boost is energy limited with release-to-rearm |
| Velocity HUD | ✅ | speed, set-speed, throttle bar, prograde/retrograde marker, boost bar, assist/mode |
| Chase + cockpit cameras | ✅ | data-driven; cockpit at `socket_pilot` |
| Ship component data | ✅ | flight, precision, boost, cameras, sockets, engine glow, lights in ship JSON |
| Sockets / attachment points | ✅ | glTF `socket_*` / `hardpoint_*` nodes + JSON fallbacks; unit-tested |
| Target selection | ✅ | T cycle (stations first), G nearest asteroid; bracket, distance, range rate, off-screen arrow |
| Simple asteroids | ✅ | procedural, deterministic meshes + clustered fields, instanced, frustum culled |
| Station proxy | ✅ | Kenney CC0 hangar + dish, running lights, nav marker |
| Collisions | ✅ | sphere vs sphere and sphere vs oriented box (station hull), push-out/bounce, impact warning |
| Rebindable controls | 🟡 | via `input.json` + F5. No in-game rebinding UI yet. |
| Rebasing without visible jumps | ✅ | unit-tested (interpolation continuity) and observed in a 40 s flight |
| "Fun to fly for 10 minutes" | ⬜ | needs a human play session on real hardware |
| In-system travel drive | ⬜ | design 4.2. Not in the Stage 2 list. Next candidate. |

## Stage 3 — docking and walkable ship
| Item | State | Notes |
|---|---|---|
| Station geometry | 🟡 | placeholder Kenney hub + dish with box colliders, roof landing pad with guide lights |
| Docking authorisation | ✅ | L requests clearance in range; radio subtitles; clearance expires out of zone |
| Capture | ✅ | deterministic tolerances (distance/speed/alignment) → scripted settle → clamps; unit-tested |
| Docking assist | ✅ | optional approach autopilot (K); manual landing also works |
| Approach HUD | ✅ | pad marker, approach column, live tolerance readout (red/green), hints |
| Undocking | ✅ | L when docked: clamps release, lifted clear, controls restored, throttle zeroed |
| Local scene swap | ✅ | separate interior scenes; space keeps simulating; the ship stays clamped |
| On-foot controller | ✅ | capsule collide-and-slide, gravity, jump; unit-tested |
| Seat interaction | ✅ | F leaves the seat (docked only) / takes the seat |
| Walkable cabin | ✅ | Sparrow cabin: cockpit, windows onto space, cargo, airlock |
| Airlock + station hangar | ✅ | exit into Kestrel Hangar 1 with the parked ship visible; board again |
| 20× fly→dock→walk→launch | ✅ | headless CTest on real content + one interactive keyboard/mouse run in the app |
| Seat/airlock animations | ⬜ | instant cuts for now |

## Rendering (design 9.3)
| Item | State | Notes |
|---|---|---|
| HDR target + tonemap + exposure | ✅ | ADR 0004 |
| Bloom | ✅ | quarter-res soft-knee, iterative Gaussian |
| Looks / post profiles | ✅ | `retro80s` (default) and `clean`; F4; HUD palette per look |
| Colour grade, scanlines, chromatic aberration, grain, vignette | ✅ | all data-driven |
| Fill + rim lighting | ✅ | scene data |
| Engine glow, running lights | ✅ | additive billboards |
| Instanced meshes | ✅ | |
| Shadows, IBL, LOD, GPU particles, TAA | ⬜ | later |

## Later stages
Not started: mining/market/save (4), planet landing (5), combat/contracts (6), polish (7).
The hangar's services terminal is a placeholder for the Stage 4 market.
