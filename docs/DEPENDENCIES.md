# Approved dependency manifest

All third-party code is small infrastructure. None of it is a game engine or scene renderer.
Versions are pinned. Any new dependency needs an entry here and a mention in an ADR.

| Library | Version | License | Scope in FARHORIZON | How it is consumed |
|---|---|---|---|---|
| SDL3 | `release-3.4.8` (git tag) | zlib | Window, input events, Vulkan surface creation. Audio later. | CMake FetchContent, static |
| Vulkan headers + loader | SDK ≥ 1.3.250 | Apache-2.0 | Graphics API | System / Vulkan SDK (`find_package(Vulkan)`) |
| glslang (`glslangValidator`) | from Vulkan SDK | BSD-3 / Apache-2.0 | Build-time GLSL → SPIR-V | Build tool only |
| cgltf | v1.15 | MIT | glTF 2.0 / GLB **parsing only**; we build our own mesh/material data | Vendored `third_party/cgltf/cgltf.h` |
| stb_image | v2.30 (commit `d14d46f`) | MIT / Public Domain | PNG/JPEG decoding | Vendored `third_party/stb/` |
| stb_image_write | commit `d14d46f` | MIT / Public Domain | Screenshot PNG encoding | Vendored |
| stb_easy_font | v1.1 (commit `d14d46f`) | MIT / Public Domain | Developer overlay text quads (dev tool only, not game UI) | Vendored |
| nlohmann/json | v3.11.3 | MIT | JSON parsing for content and config | Vendored `third_party/nlohmann/json.hpp` |

The license texts sit next to each vendored library in `third_party/`.

## Content (assets)

| Asset | Source | License |
|---|---|---|
| `content/models/ships/sparrow_placeholder.glb` | Kenney "Space Kit", `craft_cargoA` (kenney.nl) | CC0 1.0. **Placeholder** until the real Sparrow model exists. Repaired once: the scene root pointed at a child node (invalid glTF), and the grid offset was removed. See `asset.extras` in the file. |
