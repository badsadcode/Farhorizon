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
  Vec3 scale{1, 1, 1};
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
// Part of an emissive surface, no bigger than a few metres, so a long strip becomes several lights.
struct GltfEmitterInfo {
  std::string name;
  Vec3 centroid{};
  Vec3 normalArea{};     // sum of face normal x area: its length is the flat-facing area
  float area = 0;        // m^2
  Vec3 radiance{};       // emission colour x strength, including the average of any emissive image
};
struct GltfSceneInfo {
  std::vector<GltfEmitterInfo> emitters;
  std::vector<GltfNodeInfo> nodes;  // nodes with custom properties
  std::vector<GltfLightInfo> lights;
  MeshData collision;               // triangles the player collides with (positions and indices)
};

// With `scene`, node custom property `fh_collide` also decides what each mesh is: "none" is only
// seen, "only" is only collided with, and anything else is both.
std::optional<MeshData> loadGltfMesh(const std::filesystem::path& path, std::string* error = nullptr,
                                     GltfSceneInfo* scene = nullptr);

}  // namespace fh
