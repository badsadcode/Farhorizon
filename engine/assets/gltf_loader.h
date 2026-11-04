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

std::optional<MeshData> loadGltfMesh(const std::filesystem::path& path, std::string* error = nullptr);

}  // namespace fh
