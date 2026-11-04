# Changelog

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
