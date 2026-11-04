# Issue log

| # | Status | Area | Description |
|---|---|---|---|
| 1 | open | build | The Windows build has only been authored, not yet compiled. The first verification comes from the CI `windows` job (`.github/workflows/build.yml`). |
| 2 | open | render | No HDR target yet: per-shader ACES tonemapping means additive stars are tone-mapped individually. Fix with an HDR offscreen target and a tonemap pass (Stage 1 follow-up). |
| 3 | open | render | No image-based lighting. The shadow side of ships relies on a hemispheric ambient approximation. |
| 4 | open | render | One device allocation per buffer/image. Fine for Stage 1, needs a sub-allocator before asset counts grow. |
| 5 | open | assets | The importer bakes node transforms into vertices. Sockets (pilot seat, airlock, hardpoints) and collision helpers need node-preserving import (Stage 2/3). |
| 6 | open | assets | Hot reload is manual (F5). There is no file watcher yet. |
| 7 | open | platform | Under Xvfb (no window manager) the F11 toggle state cannot be observed. Verify on Windows. |
| 8 | info | perf | The software rasterizer (llvmpipe) is GPU-bound on the procedural sky (5-octave fbm ×3 per pixel). This should be negligible on real GPUs. Bake it to a cubemap if it ever shows up in profiles. |
