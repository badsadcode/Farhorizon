# FARHORIZON — handoff to a local session

_Written 2026-11-04 at the end of the cloud session and updated by the first local session. Branch: `claude/happy-mayer-kejesk`._

## Where the project is
* **Stages 0–6 are done** (see `docs/STATUS.md`, `CHANGELOG.md`, ADRs in `docs/decisions/`):
  flight, docking, walkable ship/station, mining/trade/save, travel drive + planet landing,
  combat, contracts and upgrades. CI (Windows MSVC + Linux sanitizers) is green, and it uploads a
  playable Windows zip (`FARHORIZON-windows-x64` artifact).
* **Stage 7 (polish) is under way. The UI pass is finished:**
  * TrueType UI font: Share Tech Mono + Share Tech (SIL OFL) in `content/fonts/`, baked with
    `stb_truetype` into one atlas (`engine/ui/font_atlas.*`). The overlay now has UVs
    (`engine/debug/overlay.*`, `shaders/overlay.*`) and a chamfered `panel()` primitive.
  * UI theme `content/ui/theme.json` (colours, sizes, panel style). This is a placeholder until
    the owner's reference art arrives; the image list and prompts were given in chat.
  * Immediate-mode widget kit `game/ui/ui.*`. Screens are in `game/ui/menus.*`: main menu
    (world orbiting behind it), pause menu (Esc), options (graphics/audio/controls with key
    rebinding/gameplay), load/save with 3 slots + quicksave + autosave, confirmations.
  * `settings.json` (`game/settings.*`) beside the game: look, vsync, fullscreen, FOV offset, UI
    scale, camera shake, volumes, mouse sensitivity/invert, binding overrides, HUD toggles.
  * Input: `app.menu` = Esc, `app.toggleMouse` = backtick; clicking the game window recaptures
    the mouse. Benchmarks and `--frames` runs ignore settings and skip menus.

## Dev log
Keep `docs/devlog/index.html` up to date: a plain-language entry with real screenshots for each
milestone. How to add one is in the comment at the top of its styles.

## Next steps
**See `docs/ROADMAP.md` for the agreed order.** UI (0.7.0, ADR 0010) and lighting (0.7.1, ADR 0011) are
done in code and waiting for the owner's playtest. Feedback may send work back there. Next is
**points of interest**: The Ash Wren can be flown to, explored and fought through (0.9.0, `game/foot/`). Next is what the owner says after playing it, then more weapons and enemies. The Blender editor
itself is done (0.8.0, ADR 0012, `docs/BLENDER_GUIDE.md`). Rebuild the sample with
`blender -b --factory-startup --python tools/blender/build_sample_derelict.py`.

The owner's constraint: **all content original.** Atmosphere inspired by 70s/80s lonely
"used future" sci-fi, but never copy assets, names or designs from existing works.

## Build on Windows
Prerequisites:
* Visual Studio 2022 with **Desktop development with C++**.
* CMake 3.24 or newer (VS ships one).
* Git.
* **Vulkan SDK** 1.3 or newer (LunarG); provides `glslangValidator`.
* Python 3, optional (asset/audio tools).

```bat
git clone https://github.com/badsadcode/Farhorizon.git
cd Farhorizon
git checkout claude/happy-mayer-kejesk
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
build\Release\farhorizon.exe
```
SDL3 is fetched and built statically by CMake on the first configure (needs internet).
The exe finds `content/` and its shaders through compiled-in paths when run from the build tree.
Useful flags:
* `--start-docked`, `--start-landed`, `--start-in ship|station|surface`: skip the menu and start
  in that place.
* `--continue`: load the quicksave.
* `--validation`: Vulkan validation layers.
* `--benchmark bench.combat_skirmish`: run a benchmark.
* `--frames N --screenshot x.png`: render N frames and save a screenshot.
* `--ui-script file --settings file`: drive the real UI with scripted input (`docs/BUILDING.md`).

The content validator is `build\Release\content_validator.exe content`.

## Conventions worth knowing
* `fh_core` is GPU-free and unit-tested. `fh_engine` holds SDL/Vulkan and the
  `farhorizon` exe. Tests use a small custom harness in `tests/unit`.
* All gameplay content is data (`content/`, validated). Docs for the schemas are in
  `docs/data_schemas/`.
* Commits end with the co-author/session trailer lines used in the history.
* Keep `docs/STATUS.md`, `CHANGELOG.md` and `docs/ISSUES.md` honest at each step.
