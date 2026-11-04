#pragma once
// Narrow glTF 2.0 / GLB importer built on cgltf (parsing only).
// Supports: triangle primitives, POSITION/NORMAL/TEXCOORD_0, node transforms (baked),
// metallic-roughness materials, emissive, embedded or external PNG/JPEG base colour textures.
// Not yet: skinning, morph targets, animation, sockets/collision helpers (Stage 2+).
#include <filesystem>
#include <optional>
#include <string>

#include "engine/assets/mesh_data.h"

namespace fh {

// What a whole-map .glb holds beyond the visible mesh.
struct GltfNodeInfo {
  std::string name;
  std::string extras;  // raw JSON of the node's custom properties; empty when it has none
  Vec3 position{};
  Quat rotation{};
};
struct GltfLightInfo {
  enum class Type { Point, Spot, Sun } type = Type::Point;
  std::string name;
  std::string extras;
  Vec3 position{};
  Vec3 direction{0, 0, -1};  // the way the light shines
  Vec3 color{1, 1, 1};
  float intensity = 1;
  float range = 0;           // 0 = not set
};
struct GltfSceneInfo {
  std::vector<GltfNodeInfo> nodes;  // nodes with custom properties
  std::vector<GltfLightInfo> lights;
  MeshData collision;               // triangles the player collides with (positions and indices)
};

// With `scene`, node custom property `fh_collide` also decides what each mesh is: "none" is only
// seen, "only" is only collided with, and anything else is both.
std::optional<MeshData> loadGltfMesh(const std::filesystem::path& path, std::string* error = nullptr,
                                     GltfSceneInfo* scene = nullptr);

}  // namespace fh
