#include "game/content/map_def.h"
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
#include "game/content/interior_def.h"
#include "game/economy/economy.h"
#include "game/audio_director.h"
#include "game/combat/weapon_def.h"
#include "game/missions/contracts.h"
#include "game/progression/upgrades.h"
#include "game/foot/foot_defs.h"

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--map") {  // the Blender add-on checks its export here
    std::vector<std::string> errors;
    const auto map = fh::loadMap(argv[2], &errors);
    for (const auto& e : errors) std::printf("ERROR: %s\n", e.c_str());
    if (!map) return 1;
    for (const auto& w : map->warnings) std::printf("WARNING: %s\n", w.c_str());
    std::printf("OK: %zu triangles, %zu lights, %zu usable things, %zu markers, %zu images\n", map->mesh.triangleCount(),
                map->def.lights.size(), map->def.interactables.size(), map->markers.size(), map->mesh.images.size());
    return 0;
  }
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
        if (auto ship = fh::loadShipDef(*manifest, id, &errors)) {
          // Cross-check weapon references against every weapons file in the manifest.
          for (const auto& [wid, we] : manifest->entries()) {
            if (we.type != "weapons") continue;
            std::vector<std::string> ignore;
            auto db = fh::loadWeapons(*manifest, wid, &ignore);
            if (!db) continue;
            for (const auto* group : {&ship->combat.primary, &ship->combat.secondary})
              for (const auto& m : *group) {
                if (!db->find(m.weapon))
                  errors.push_back("ship '" + id + "': unknown weapon '" + m.weapon + "'");
                if (!ship->sockets.count(m.mount))
                  errors.push_back("ship '" + id + "': weapon mount '" + m.mount + "' is not a socket");
              }
          }
        }
      } else if (entry.type == "weapons") {
        fh::loadWeapons(*manifest, id, &errors);
      } else if (entry.type == "upgrades") {
        fh::loadUpgrades(*manifest, id, &errors);
      } else if (entry.type == "contracts") {
        if (auto db = fh::loadCommodities(*manifest, "economy.commodities", &errors))
          fh::loadContracts(*manifest, id, *db, &errors);
      } else if (entry.type == "scene") {
        fh::loadSceneDef(*manifest, id, &errors);  // also checks referenced ships and meshes
      } else if (entry.type == "audio") {
        fh::AudioDirector director;
        director.load(*manifest, id, &errors);
      } else if (entry.type == "commodities") {
        fh::loadCommodities(*manifest, id, &errors);
      } else if (entry.type == "market") {
        if (auto db = fh::loadCommodities(*manifest, "economy.commodities", &errors))
          fh::loadMarketDef(*manifest, id, *db, &errors);
      } else if (entry.type == "interior") {
        fh::loadInteriorDef(*manifest, id, &errors);
      } else if (entry.type == "map") {
        // Markers must name enemy types and loot tables that exist.
        if (auto def = fh::loadInteriorDef(*manifest, id, &errors)) {
          std::vector<std::string> ignore;
          if (auto foot = fh::loadFootContent(*manifest, &ignore))
            for (const auto& m : def->markers) {
              const std::string kind = m.kind.empty() ? (m.type == "loot" ? "salvage" : "crawler") : m.kind;
              if (m.type == "enemy_spawn" && foot->enemyIndex(kind) < 0)
                errors.push_back("map '" + id + "': enemy spawn '" + m.name + "' names unknown enemy '" + kind + "'");
              if (m.type == "loot" && !foot->lootTable(kind))
                errors.push_back("map '" + id + "': loot '" + m.name + "' names unknown loot table '" + kind + "'");
            }
        }
      } else if (entry.type == "foot_weapons") {
        fh::loadFootContent(*manifest, &errors);  // all three foot files, cross-checked
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
