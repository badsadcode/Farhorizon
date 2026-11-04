// FARHORIZON entry point. Parses developer flags and runs the Stage 1 showcase.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include "engine/core/log.h"
#include "game/showcase_app.h"

namespace {
void printUsage() {
  std::printf(
      "farhorizon [options]\n"
      "  --content <dir>        content root (default: built-in path)\n"
      "  --shaders <dir>        compiled SPIR-V directory\n"
      "  --scene <assetId>      scene to load (default scene.stage1_showcase)\n"
      "  --size <W>x<H>         window size (default 1600x900)\n"
      "  --validation           enable Vulkan validation layer (default on in Debug builds)\n"
      "  --no-validation        disable validation\n"
      "  --no-vsync             present without vsync\n"
      "  --no-overlay           hide developer overlay\n"
      "  --frames <N>           exit after N frames\n"
      "  --screenshot <file>    write PNG of the last frame (use with --frames)\n"
      "  --benchmark <seconds>  record frame times, export CSV/JSON, exit\n"
      "  --bench-out <prefix>   output prefix for benchmark files\n");
}
}  // namespace

int main(int argc, char** argv) {
  SDL_SetMainReady();
  fh::AppOptions o;
  o.contentDir = FH_DEFAULT_CONTENT_DIR;
  o.shaderDir = FH_DEFAULT_SHADER_DIR;
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
    else if (a == "--no-overlay") o.overlay = false;
    else if (a == "--frames") o.exitAfterFrames = std::atoi(next());
    else if (a == "--screenshot") o.screenshotOnExit = std::filesystem::path(next());
    else if (a == "--benchmark") o.benchmarkSeconds = std::atof(next());
    else if (a == "--bench-out") o.benchmarkOut = next();
    else if (a == "--help" || a == "-h") { printUsage(); return 0; }
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); printUsage(); return 1; }
  }

  fh::log::setLogFile("farhorizon.log");
  try {
    fh::ShowcaseApp app(o);
    return app.run();
  } catch (const std::exception& e) {
    fh::log::error("fatal", "{}", e.what());
    return 1;
  }
}
