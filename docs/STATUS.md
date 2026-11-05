# FARHORIZON status

_Last updated: 2026-11-05 · Roadmap step 4.1 (multi-player simulation) done in 0.11.0: up to four crew, per-place instances, personal wallets and a shared hold, simulated players (ADR 0014). Step 3's fights are waiting for the owner's playtest. Order ahead: `docs/ROADMAP.md`._
Legend: ✅ implemented and verified · 🟡 partial · ⬜ absent

## Stage 0 — bootstrap
| Item | State | Notes |
|---|---|---|
| CMake C++20 project, module layout | ✅ | |
| Dependency manifest + licenses | ✅ | `docs/DEPENDENCIES.md` |
| Vulkan minimum feature set | ✅ | ADR 0001 |
| Logging, crash diagnostics | 🟡 | contextual fatal logging; no minidumps |
| Test harness | ✅ | 183 tests (incl. headless Stage 3, 4, 5 and 6 exit criteria) + content validation (CTest) + scripted UI runs (`tests/ui_scripts/`) |
| CI | ✅ | Windows (MSVC Release build + tests) and Linux (sanitizers + headless render + validation) both green |

## Stage 1 — window and visible space
| Item | State | Notes |
|---|---|---|
| SDL3 window/input, Vulkan swapchain, resize | ✅ | |
| Validation layer + overlay counters | ✅ | 0 messages from the game on an RTX 3080 (SDK 1.4.363), after enabling a missing device feature that CI runs had not reported |
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
| Rebindable controls | ✅ | in-game (Options > Controls), stored in `settings.json`; defaults in `input.json` |
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
| Networked co-op test | 🟡 | Host and join by command line; flying and walking shared (0.13, ADR 0016); combat etc. not yet |
| Walkable ship | ✅ | Sparrow: two decks, stair, real windows; leave the helm in flight (0.12, ADR 0015) |
| Airlock + station hangar | ✅ | exit into Kestrel Hangar 1 with the parked ship visible; board again |
| 20× fly→dock→walk→launch | ✅ | headless CTest on real content + one interactive keyboard/mouse run in the app |
| Seat/airlock animations | ⬜ | instant cuts for now |

## Stage 4 — mining, cargo, market, save/load
| Item | State | Notes |
|---|---|---|
| Mineable asteroids | ✅ | per-field resources/yield/hardness; scan reveals composition |
| Mining laser + ore pickup | ✅ | aim cone + range; chunks with scoop field; hard caps |
| Cargo inventory | ✅ | mass tons, capacity from ship data |
| Station buy/sell | ✅ | CRT terminal panel (keyboard + mouse) at the hangar terminal or M when docked |
| Market stock and credits | ✅ | bounded pricing, spread floor, slippage, deterministic restocking |
| Save/load | ✅ | versioned JSON, atomic + backup, quicksave/autosave, on-foot saves |
| Profitable trip + reload preserves everything | ✅ | headless CTest on real content |
| No duplication exploit | ✅ | buy→sell round trips lose money; save→load→save is byte-identical |

## Stage 5 — planet landing site
| Item | State | Notes |
|---|---|---|
| Orbital planet visualisation | ✅ | Varga + Ossa impostors: procedural surface, craters, terminator, atmosphere |
| Destination selection | ✅ | N cycles stations, fields and landing sites; J travel drive with exact arrival |
| Reliable descent transition | ✅ | corridor + heat + cloud-deck fade (honest scene swap), scripted approach to the pad |
| Landing pad scene + walkable outpost | ✅ | procedural terrain with cliff walls, dusk sky, outpost with market |
| Launch back into space | ✅ | to the site's orbit marker (guaranteed return destination) |
| Repeated cycles incl. after reload, no jitter | ✅ | headless CTest (3 trade cycles, reload while landed) + in-app run (1,564 rebases, 0 errors) |
| Manual surface landing | ⬜ | approach is scripted (ISSUES #24) |

## Stage 6 — combat and contracts
| Item | State | Notes |
|---|---|---|
| Ship weapons | ✅ | twin pulse lasers (projectile, heat), seeker rockets (homing); data-driven; ADR 0009 |
| Shields and hull | ✅ | shields first with delayed recharge; collision damage; unit-tested |
| Basic AI pilots | ✅ | pirate (patrol/intercept/attack/evade/retreat) and trader (route/flee) on the player's flight model |
| Encounter + traffic spawning | ✅ | Rust Jackal pack on the deep side of the Cinder Drift; Ossa Co-op hauler route near Kestrel |
| Targeting + combat HUD | ✅ | Y nearest hostile; lead pip; shield/hull/heat; triangle/square markers; flashes; text warnings |
| Explosions, debris, salvage | ✅ | bloom flash + point light + sparks; wrecks drop scoopable canisters |
| Defeat / insurance | ✅ | towed to the nearest repair station, cargo lost, deductible; unit-tested |
| Contracts | ✅ | courier / procurement / bounty; deterministic daily boards; turn-in on arrival |
| Upgrades + repair | ✅ | 6 shipyard upgrades on a derived ship def; hull repair service |
| Terminal tabs | ✅ | MARKET / SERVICES / CONTRACTS, keyboard + mouse |
| Saves v3 | ✅ | durability, upgrades, contracts; v2 → v3 migration; byte-identical round trip |
| Exit: choose combat or trade, complete a contract, buy an upgrade | ✅ | headless CTest: bounty route (kills, turn-in) + reload + mining procurement + upgrade |
| Reputation / factions standing | ⬜ | factions exist in data only (ISSUES #31) |
| Balance verified by a human | ⬜ | tuned via tests and benchmark runs only |

## Atmosphere
| Item | State | Notes |
|---|---|---|
| Fighting on foot | ✅ | built for a list of players; suit lamp, health, knock-outs |
| On-foot weapons | ✅ | rivet driver (auto), flechette caster (pellets), arc cutter (beam); per-weapon magazines, ammo types, switching (1/2/3, wheel); content files |
| On-foot enemies | ✅ | crawler (melee), spitter (ranged, dodgeable globs), brute (charge, knockback, stun); content files |
| Enemy pathfinding | ✅ | per-map navigation grid (multi-level, headroom per agent), A* + string pulling; unit-tested incl. the derelict |
| Loot tables and drops | ✅ | deterministic per map/marker; tool caches unlock weapons; enemy drops persist in saves (v5) |
| Feel of the new fights | ⬜ | balance tuned by tests and a scripted run only, not yet played by a human |
| Point lights + fog + flicker | ✅ | 16 lights/frame, all shadowed, per-interior fog, failing lamps |
| "Used Future" look | ✅ | default; amber/phosphor CRT HUD |
| Audio | 🟡 | mixer, device, data-driven director with original synthesised placeholders. Nobody has listened yet. |

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
| Shadows, AO, reflections | ✅ | ADR 0011 |
| PBR texture maps | ✅ | 0.8.0, no transparency yet |
| LOD, GPU particles, TAA | ⬜ | later |

## Stage 7 — polish
| Item | State | Notes |
|---|---|---|
| TrueType UI font + theme | ✅ | one atlas, `content/ui/theme.json`; theme is a placeholder until the owner's reference art arrives |
| Ui kit | ✅ | focus, scrolling, sliders, rows; unit-tested; ADR 0010 |
| Main, pause, load/save, confirm screens | ✅ | scripted run on an RTX 3080 |
| Options: graphics, audio, controls, gameplay | ✅ | scripted run: live fullscreen, VSync, look, FOV, scale; persisted to `settings.json`. Audio volumes not yet checked by ear (ISSUES #18). |
| Key rebinding + Reset keys | ✅ | keyboard and mouse, scrolling list, conflicts, reserved keys; unit tests + scripted run |
| Flight, on-foot and surface HUD | ✅ | checked in screenshots: combat, mining, docking, hangar, 720p, 1440p, 150% scale. Not yet judged by a human in play. |
| Station terminal restyle | ✅ | scripted run: trade, repair/fit, take a contract, open and close |
| Scripted UI tests | ✅ | `--ui-script`; on Windows here; in the Linux CI job, not yet run there |
| Owner playtest of the UI | ⬜ | next |
| Blender pipeline | ⬜ | PBR textures, `KHR_lights_punctual`, whole-level maps, scale reference scene, artist guide |
| Exhaust ribbons, engine glow, speed dust, drive tunnel | ✅ | checked in screenshots; owner to judge in motion |
| Nebula sky and volumetric clouds | 🟡 | look test at the owner's request; placement and colours are first guesses |
| Full stop | ✅ | unit test + scripted run, assist on and off |
| Blender add-on: maps, entities, checks, Play | ✅ | tested in Blender 5.1, in a real window and headless; ADR 0012 |
| PBR textures, mips, lights from Blender | ✅ | sample map; normal mapping checked by eye |
| Mesh collision | ✅ | floors, walls, ramps, stairs, closed rooms; unit tests + scripted walk |
| Ship exterior preview | 🟡 | exports and flies; making a ship permanent still needs a JSON file by hand |
| Maps reachable in the game world | ✅ | The Ash Wren: flown, docked, walked and returned from in a scripted run; unit test of the visit |
| Emissive surfaces cast light | ✅ | checked by measurement and by eye; not yet judged in play |
| Placed reflection probes | ✅ | sample map; checked by eye, not yet judged in play |
| Lighting on real GPUs | ✅ | sun cascades, lamp cube shadows, AO, live reflection probe, surface detail; ADR 0011. Checked in screenshots and scripted runs; not yet judged by a human in motion. |

## Roadmap step 4 — co-op
| Item | State | Notes |
|---|---|---|
| 4.1 Players separate from the world | ✅ | up to 4 crew in one `GameSession`; slot 0 is this machine's player; ADR 0014 |
| Per-place instances (own fight, drops, nav) | ✅ | created on first entry, put away when empty; unit tests |
| Pilot/passenger, undock and launch need everyone seated | ✅ | unit tests; the app's L key goes through the same rule |
| Personal wallets, shared hold, away wallets in saves (v6) | ✅ | unit tests, v5 migration |
| Simulated players, headless | ✅ | `SimPlayer`; four-player 40 s soak, repeatable |
| World pauses when nobody is connected | ⬜ | belongs to the server (4.3/4.4) |
| 4.2–4.5 | ⬜ | walkable moving ship, LAN test, dedicated server, crew stations |

## Later stages
Not started.
