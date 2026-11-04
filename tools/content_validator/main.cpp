// content_validator: loads the asset manifest and every ship/scene/mesh it references,
// printing actionable errors. Exit code 0 = content OK. Intended for CI and pre-commit use.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/assets/gltf_loader.h"
#include "engine/core/log.h"
#include "game/content/scene_def.h"
#include "game/content/ship_def.h"
#include "game/content/visual_profile.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: content_validator <content-dir>\n");
    return 2;
  }
  fh::log::setMinLevel(fh::log::Level::Off);  // we print our own summary
  const std::filesystem::path root = argv[1];
  std::vector<std::string> errors;
  auto manifest = fh::AssetManifest::load(root, &errors);
  int checked = 0;
  if (manifest) {
    for (auto& p : manifest->validateFiles()) errors.push_back(p);
    for (const auto& [id, entry] : manifest->entries()) {
      ++checked;
      if (entry.type == "ship") {
        fh::loadShipDef(*manifest, id, &errors);
      } else if (entry.type == "scene") {
        fh::loadSceneDef(*manifest, id, &errors);  // also checks referenced ships and meshes
      } else if (entry.type == "visuals") {
        fh::loadVisualProfiles(*manifest, id, &errors);
      } else if (entry.type == "benchmark") {
        if (auto b = fh::loadBenchmarkDef(*manifest, id, &errors); b && !fh::loadSceneDef(*manifest, b->scene, &errors))
          errors.push_back("benchmark '" + id + "': scene '" + b->scene + "' failed to load");
      } else if (entry.type == "mesh") {
        std::string err;
        if (auto path = manifest->resolve(id, "mesh"); path && !fh::loadGltfMesh(*path, &err))
          errors.push_back("mesh '" + id + "': " + err);
      }
    }
  }
  for (const auto& e : errors) std::printf("ERROR: %s\n", e.c_str());
  std::printf("%d assets checked, %zu problems\n", checked, errors.size());
  return errors.empty() ? 0 : 1;
}
