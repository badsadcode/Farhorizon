#pragma once
// A map authored in Blender with the FARHORIZON add-on: one .glb holds the geometry, materials,
// lights and entities. Reading it needs no GPU, so maps are validated headlessly and unit-tested.
// Authoring guide: docs/BLENDER_GUIDE.md.
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/gltf_loader.h"
#include "engine/assets/mesh_data.h"
#include "game/content/interior_def.h"

namespace fh {

struct MapMarker {
  std::string type;   // enemy_spawn | loot: placed now, used by gameplay in a later stage
  std::string name;
  std::string kind;   // free text, for example the enemy or loot table
  Vec3 position{};
  float yawDeg = 0;
};

struct LoadedMap {
  InteriorDef def;    // spawns, interactables, lights, lighting, collision
  MeshData mesh;      // everything visible, one mesh
  std::vector<MapMarker> markers;
  std::vector<std::string> warnings;
};

std::optional<LoadedMap> loadMap(const std::filesystem::path& path, std::vector<std::string>* errors);

// Turns emissive surfaces into lights, after the real lamps are in `lights`. A surface this close to a
// real lamp is that lamp's fixture and adds nothing.
inline constexpr float kLampFixtureRadius = 1.5f;
void appendEmissionLights(const std::vector<GltfEmitterInfo>& emitters, std::vector<LightDef>& lights);

// Blender watts to the game's light units. See docs/BLENDER_GUIDE.md.
constexpr float kBlenderWattsToLight = 0.05f;
// An emissive surface's light: radiance x facing area is the physically correct strength.
constexpr float kEmissionLightGain = 1.0f;

}  // namespace fh
