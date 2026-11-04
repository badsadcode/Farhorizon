# Coding conventions

* C++20. `namespace fh`. Files `snake_case`, types `PascalCase`, functions/variables `camelCase`,
  members end in `_`, constants `kName`.
* Format with `.clang-format` (Google-based, 120 columns).
* **Ownership:** RAII. Every owner type is non-copyable unless copying is meaningful. No global
  mutable state except the logger and counters, which are documented as such.
* **Thread affinity** is documented in each header. Today everything except logging is main-thread.
* **Errors:** content/asset errors are reported with context (asset ID, file, field) and never
  crash the game; hot reload keeps the previous content. Vulkan *creation* failures throw (`VK_CHECK`)
  and reach a single fatal handler in `main` that writes `farhorizon.log`.
* **Layering:** `fh_core` (core, math, assets, content definitions, CPU-side debug) must not include
  SDL or Vulkan. Unit tests link only `fh_core`. `fh_engine` adds platform and render.
* **Content is data.** No hard-coded ships, scenes, bindings, shader logic, prices and so on in C++.
* **Every change:** list the touched subsystems, state the expected behaviour, build, run `ctest`,
  and add or extend a test where feasible. Update `docs/STATUS.md` honestly.
* New third-party code requires an entry in `docs/DEPENDENCIES.md` and an ADR if it is architectural.
