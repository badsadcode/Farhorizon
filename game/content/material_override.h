#pragma once
// Data-driven per-material tweaks applied on top of imported glTF materials (by material name).
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/mesh_data.h"
#include "engine/math/math.h"

namespace fh {

struct MaterialOverride {
  std::optional<Vec4> baseColor;  // linear RGBA
  std::optional<float> metallic;
  std::optional<float> roughness;
  std::optional<Vec3> emissive;
};

using MaterialOverrides = std::map<std::string, MaterialOverride>;

// Applies overrides; names that match no material are reported in `warnings` (with `owner`).
inline void applyMaterialOverrides(MeshData& mesh, const MaterialOverrides& overrides, const std::string& owner,
                                   std::vector<std::string>* warnings) {
  for (const auto& [name, o] : overrides) {
    bool found = false;
    for (auto& m : mesh.materials) {
      if (m.name != name) continue;
      found = true;
      if (o.baseColor) m.baseColorFactor = *o.baseColor;
      if (o.metallic) m.metallic = *o.metallic;
      if (o.roughness) m.roughness = *o.roughness;
      if (o.emissive) m.emissive = *o.emissive;
    }
    if (!found && warnings) warnings->push_back(owner + ": materialOverride '" + name + "' matches no material");
  }
}

}  // namespace fh
