# FARHORIZON status

_Last updated: 2026-11-04 · Stage 0 + Stage 1 spike._
Legend: ✅ implemented and verified · 🟡 partial · ⬜ absent

## Stage 0 — bootstrap
| Item | State | Notes |
|---|---|---|
| CMake C++20 project, module layout | ✅ | `engine/`, `game/`, `tools/`, `tests/`, `content/`, `docs/` |
| Pinned dependency manifest + licenses | ✅ | `docs/DEPENDENCIES.md` |
| Vulkan minimum feature set chosen | ✅ | ADR 0001: Vulkan 1.3 + dynamicRendering + synchronization2 |
| Logging (console + `farhorizon.log`) | ✅ | per-subsystem tags, warn/error counters |
| Crash diagnostics | 🟡 | fatal errors are logged with context; no minidump/stack trace yet |
| Test harness | ✅ | `fh_unit_tests` (21 tests, GPU-free) + content validation via CTest |
| Coding conventions, issue log, changelog | ✅ | `docs/CODING_CONVENTIONS.md`, `docs/ISSUES.md`, `CHANGELOG.md` |
| Windows clean-clone build | 🟡 | authored for MSVC and covered by the CI workflow, **not yet compiled on Windows** (ISSUES #1) |

## Stage 1 — window and visible space
| Item | State | Notes |
|---|---|---|
| SDL3 window + input | ✅ | action bindings from JSON |
| Vulkan swapchain / render path | ✅ | resize and out-of-date recreation verified (xdotool resize test); minimized windows skip rendering |
| Validation layer + error surfacing | ✅ | 0 messages across all test runs; count shown on overlay; `--validation` exits with code 2 on any message |
| Camera | ✅ | orbit + free-fly; free-fly speed 1 m/s–200 km/s |
| Star field | ✅ | 9,000 instanced stars, deterministic seed, galactic band |
| Procedural sky, star disc and glare | ✅ | placeholder for authored nebula textures |
| Imported GLB ship, lit | ✅ | Kenney CC0 placeholder; GGX PBR, one directional light (star) + hemispheric ambient |
| Simple PBR material | ✅ | base colour factor/texture, metallic, roughness, emissive; data-driven overrides |
| Developer overlay | ✅ | FPS, CPU ms, GPU ms (timestamps), draws, triangles, GPU memory, origin generation, validation |
| Fullscreen toggle | 🟡 | implemented (borderless); not observable under Xvfb, verify on Windows |
| Asset reload | ✅ | F5; invalid content is rejected with precise errors and the previous scene keeps running (verified) |
| Screenshot | ✅ | F12 or `--screenshot` |
| Benchmark export | ✅ | `--benchmark` → CSV + JSON |
| Double-precision frames + floating origin | ✅ | unit tests plus in-app verification (44 rebases at ~35 km/s, no jumps) |
| Fixed-timestep loop | ✅ | 60 Hz, capped catch-up; drives auto-rotate today |
| HDR target, bloom, exposure pass | ⬜ | next render milestone |
| Shadows, IBL, LOD, particles | ⬜ | later stages |

## Later stages
Not started: ship flight model (Stage 2), docking/interiors/on-foot (3), mining/market/save (4),
planet landing (5), combat/contracts (6), polish (7). The data types for ship flight parameters are
parsed but unused.
