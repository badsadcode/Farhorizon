#pragma once
// Ship definition loaded from content/ships/*.json (schema: docs/data_schemas/ship.md).
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/math/math.h"

namespace fh {

struct MaterialOverride {
  std::optional<Vec4> baseColor;  // linear RGBA
  std::optional<float> metallic;
  std::optional<float> roughness;
  std::optional<Vec3> emissive;
};

struct ShipFlightParams {
  float forwardAcceleration = 0;
  float lateralAcceleration = 0;
  float angularAcceleration = 0;
  float maxCruiseSpeed = 0;
};

struct ShipDef {
  std::string id;
  std::string name;
  AssetID mesh;
  float meshScale = 1.0f;
  Vec3 meshRotationDeg{0, 0, 0};  // corrects exporter axis conventions
  std::map<std::string, MaterialOverride> materialOverrides;  // by glTF material name
  float massTons = 0;
  float cargoCapacityTons = 0;
  float maxHull = 0;
  float maxShield = 0;
  ShipFlightParams flight;
  std::vector<std::string> mounts;
};

std::optional<ShipDef> parseShipDef(const std::string& json, const std::string& context,
                                    std::vector<std::string>* errors);
std::optional<ShipDef> loadShipDef(const AssetManifest& manifest, const AssetID& id,
                                   std::vector<std::string>* errors);

}  // namespace fh
