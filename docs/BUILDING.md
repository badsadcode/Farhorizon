# Building and running

## Windows (primary target)

1. Install Visual Studio 2022 (Desktop C++), CMake ≥ 3.24, Git, and the
   [LunarG Vulkan SDK](https://vulkan.lunarg.com/) (1.3.250 or newer; provides headers, loader,
   `glslangValidator` and the validation layer). The SDK installer sets `VULKAN_SDK`.
2. Configure and build:
   ```bat
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ctest --test-dir build -C Release --output-on-failure
   build\Release\farhorizon.exe
   ```
SDL3 is linked statically, so no DLL has to be copied. The executable finds `content/` and the
compiled shaders through absolute paths baked in at configure time. Override them with `--content`
and `--shaders` when you move the build.

## Linux (development / CI)

```bash
sudo apt-get install ninja-build libvulkan-dev glslang-tools vulkan-validationlayers \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxkbcommon-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFH_SANITIZERS=ON
cmake --build build && ctest --test-dir build --output-on-failure
```

Headless, without a GPU (software Vulkan via Mesa lavapipe):

```bash
sudo apt-get install mesa-vulkan-drivers xvfb
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  xvfb-run -a -s "-screen 0 1920x1080x24" ./build/farhorizon --frames 30 --screenshot shot.png
```

## Build options

| Option | Default | Meaning |
|---|---|---|
| `FH_BUILD_GAME` | ON | Build the game executable (needs Vulkan + SDL3). OFF builds only GPU-free code + tests. |
| `FH_BUILD_TESTS` | ON | Unit tests (`fh_unit_tests`) and content validation via CTest |
| `FH_SANITIZERS` | OFF | ASan + UBSan in Debug builds (GCC/Clang) |
| `FH_SYSTEM_SDL3` | OFF | Use an installed SDL3 package instead of the pinned fetch |

## Command line

```
farhorizon [--content DIR] [--shaders DIR] [--scene ID] [--size WxH] [--camera chase|cockpit|orbit|free]
           [--look retro80s|clean] [--autofly] [--validation | --no-validation] [--no-vsync] [--no-overlay]
           [--frames N] [--screenshot FILE] [--benchmark ID] [--bench-out PREFIX]
           [--start-docked] [--start-landed] [--start-in ship|station|surface] [--load FILE] [--continue]
```

Validation is on by default in Debug builds. With `--validation`, any validation message makes the
process exit with code 2, so CI catches API misuse.

## Benchmarks

Benchmarks are data (`content/benchmarks/*.json`): a scene, a start pose, a camera and scripted input.

```bash
farhorizon --no-vsync --size 1920x1080 --benchmark bench.empty_space_cockpit
farhorizon --no-vsync --size 1920x1080 --benchmark bench.asteroid_field_chase
farhorizon --no-vsync --size 1920x1080 --benchmark bench.station_approach
farhorizon --no-vsync --size 1920x1080 --benchmark bench.docking_autopilot
farhorizon --no-vsync --size 1920x1080 --benchmark bench.ship_interior
farhorizon --no-vsync --size 1920x1080 --benchmark bench.station_hangar
farhorizon --no-vsync --size 1920x1080 --benchmark bench.mining
farhorizon --no-vsync --size 1920x1080 --benchmark bench.planet_orbit
farhorizon --no-vsync --size 1920x1080 --benchmark bench.landing
farhorizon --no-vsync --size 1920x1080 --benchmark bench.surface_landed
farhorizon --no-vsync --size 1920x1080 --benchmark bench.surface_outpost
```

## Audio
Audio uses the default playback device through SDL3 (WASAPI on Windows). With no device the game
runs silently and logs a warning. Benchmarks and `--frames` runs are always silent. To capture the
mix without speakers: `SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=out.raw` (float32 stereo
48 kHz).
Each run writes `benchmarks/<id>.csv` (per frame: cpu_ms, gpu_ms, draw_calls, triangles) and
`benchmarks/<id>.json` (summary).
