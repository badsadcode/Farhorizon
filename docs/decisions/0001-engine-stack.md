# ADR 0001 — Engine stack and Vulkan baseline

**Status:** accepted (Stage 0/1) · **Date:** 2026-11-04

## Context
The design brief requires a purpose-built engine. No Godot, Unity, Unreal, O3DE, Stride, Bevy or
Flax, and no all-in-one scene renderer. Small infrastructure libraries are allowed. The delivery
target is Windows x64, with a baseline around an RTX 3060 / Ryzen 5 at 1080p.

## Decision
* **Language/build:** C++20, CMake ≥ 3.24, warnings as a matter of course (`/W4`, `-Wall -Wextra
  -Wpedantic -Wshadow`), optional ASan/UBSan.
* **Platform:** SDL3 (pinned `release-3.4.8`, static) for window, events and Vulkan surface.
* **Graphics:** our own renderer on **Vulkan 1.3**, requiring the core features `dynamicRendering`
  and `synchronization2`. That removes render-pass/framebuffer boilerplate and gives one barrier
  API. Every GPU in the target class (NVIDIA GTX 900+/RTX, AMD GCN4+/RDNA, Intel Gen9+ on current
  drivers) supports 1.3. Device selection checks the version, the features and the presentation
  queue, and fails with a clear message.
* **Depth:** reversed-Z with an infinite far plane (D32_SFLOAT, compare GREATER, clear 0). That
  gives the best precision for space scenes with metre-scale ships against an infinite backdrop.
* **Colour:** sRGB swapchain. Stage 1 tone-maps (ACES fit) inside each shader. An HDR offscreen
  target with a separate tonemap/bloom pass is the next render milestone (design doc 9.3 step 3).
* **Memory:** one `vkAllocateMemory` per resource while counts are tiny. A sub-allocator will come
  once asset counts grow; adopting VMA would need its own ADR.
* **Assets:** cgltf parses glTF 2.0/GLB, stb_image decodes images, nlohmann/json reads content.
  All are vendored single headers with pinned versions (see `docs/DEPENDENCIES.md`).
* **Math:** our own small library (`engine/math`) covered by unit tests. It uses GLSL-style
  column-major matrices and the right-handed, +Y up, −Z forward convention (matches glTF).
* **Shaders:** GLSL in `shaders/`, compiled to SPIR-V at build time with `glslangValidator`, and
  loaded at runtime from files so they can be swapped without rebuilding C++.
* **Physics:** none yet. A minimal custom module is planned for Stage 2/3 (design doc 9.4).

## Consequences
* Pre-2016 GPUs and outdated drivers are unsupported. That fits the stated target hardware.
* The Linux build exists for development and CI. Mesa lavapipe enables headless rendering tests.
* A missing feature leads to an early, explicit startup error, never undefined rendering.
