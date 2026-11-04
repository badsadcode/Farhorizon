// FARHORIZON entry point. Parses developer flags and runs the game.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_main.h>
#include <filesystem>

#include "engine/core/log.h"
#include "game/game_app.h"

namespace {
void printUsage() {
  std::printf(
      "farhorizon [options]\n"
      "  --content <dir>        content root (default: built-in path)\n"
      "  --shaders <dir>        compiled SPIR-V directory\n"
      "  --scene <assetId>      scene to load (default scene.stage2_flight)\n"
      "  --camera <mode>        chase | cockpit | orbit | free\n"
      "  --autofly              scripted gentle flight (smoke tests)\n"
      "  --look <profileId>     visual look, e.g. retro80s or clean\n"
      "  --start-docked         debug: start clamped to the nearest station pad\n"
      "  --start-landed         debug: start landed at the first planetary landing site\n"
      "  --start-in <where>     debug: start on foot: 'ship', 'station' or 'surface'\n"
      "  --load <file>          load a save game (e.g. saves/autosave.json)\n"
      "  --continue             load saves/quicksave.json\n"
      "  --map <file.glb>       start on foot in a map made with the Blender add-on\n"
      "  --ship-mesh <file.glb> fly this model instead of the ship's own\n"
      "  --start-at <prop id>   debug: start next to this object, e.g. station.derelict\n"
      "  --settings <file>      settings file (default settings.json)\n"
      "  --ui-script <file>     developer: drive the UI with scripted input (game/ui/ui_script.h)\n"
      "  --size <W>x<H>         window size (default 1600x900)\n"
      "  --validation           enable Vulkan validation layer (default on in Debug builds)\n"
      "  --no-validation        disable validation\n"
      "  --no-vsync             present without vsync\n"
      "  --no-overlay           hide developer info overlay (HUD stays)\n"
      "  --frames <N>           exit after N frames\n"
      "  --screenshot <file>    write PNG of the last frame (use with --frames)\n"
      "  --benchmark <assetId>  run a data-defined benchmark (e.g. bench.asteroid_field_chase)\n"
      "  --bench-out <prefix>   output prefix for benchmark files\n");
}
}  // namespace

int main(int argc, char** argv) {
  SDL_SetMainReady();
  fh::AppOptions o;
  o.contentDir = FH_DEFAULT_CONTENT_DIR;
  o.shaderDir = FH_DEFAULT_SHADER_DIR;
  // Packaged build: content/ and shaders/ sit next to the executable. Prefer them, and keep
  // saves, screenshots and the log beside the game too (whatever the launch directory).
  if (const char* base = SDL_GetBasePath()) {
    const std::filesystem::path root = std::filesystem::path(base);
    std::error_code ec;
    if (std::filesystem::exists(root / "content" / "asset_manifest.json", ec) &&
        std::filesystem::exists(root / "shaders", ec)) {
      o.contentDir = root / "content";
      o.shaderDir = root / "shaders";
      std::filesystem::current_path(root, ec);
    }
  }
#ifndef NDEBUG
  o.validation = true;
#endif
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", a.c_str());
        std::exit(1);
      }
      return argv[++i];
    };
    if (a == "--content") o.contentDir = next();
    else if (a == "--shaders") o.shaderDir = next();
    else if (a == "--scene") o.sceneId = next();
    else if (a == "--size") {
      if (std::sscanf(next(), "%dx%d", &o.width, &o.height) != 2) { printUsage(); return 1; }
    } else if (a == "--validation") o.validation = true;
    else if (a == "--no-validation") o.validation = false;
    else if (a == "--no-vsync") o.vsync = false;
    else if (a == "--no-overlay") o.devOverlay = false;
    else if (a == "--camera") o.cameraOverride = next();
    else if (a == "--autofly") o.autofly = true;
    else if (a == "--look") o.look = next();
    else if (a == "--start-docked") o.startDocked = true;
    else if (a == "--start-landed") o.startLanded = true;
    else if (a == "--start-in") o.startIn = next();
    else if (a == "--load") o.loadPath = next();
    else if (a == "--continue") o.loadPath = "saves/quicksave.json";
    else if (a == "--settings") o.settingsPath = next();
    else if (a == "--map") o.mapPath = next();
    else if (a == "--start-at") o.startAt = next();
    else if (a == "--ship-mesh") o.shipMesh = next();
    else if (a == "--ui-script") o.uiScript = next();
    else if (a == "--frames") o.exitAfterFrames = std::atoi(next());
    else if (a == "--screenshot") o.screenshotOnExit = std::filesystem::path(next());
    else if (a == "--benchmark") o.benchmarkId = next();
    else if (a == "--bench-out") o.benchmarkOut = next();
    else if (a == "--help" || a == "-h") { printUsage(); return 0; }
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); printUsage(); return 1; }
  }

  fh::log::setLogFile("farhorizon.log");
  try {
    fh::GameApp app(o);
    if (!o.loadPath.empty() && !app.loadGame(o.loadPath)) fh::log::warn("app", "starting a new game instead");
    return app.run();
  } catch (const std::exception& e) {
    fh::log::error("fatal", "{}", e.what());
    return 1;
  }
}
