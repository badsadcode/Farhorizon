#pragma once
// Showcase / space scene definition (schema: docs/data_schemas/scene.md).
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/math/frames.h"

namespace fh {

struct StarDef {
  SystemPosition position;          // the system primary; usually the origin
  Vec3 color{1, 0.95f, 0.88f};      // linear
  float intensity = 3.0f;           // illuminance multiplier for the directional light
  float angularDiameterDeg = 0.6f;  // visual disc size seen from the play area
};

struct StarfieldDef {
  std::uint32_t seed = 1;
  std::uint32_t count = 8000;
  float brightness = 1.0f;
  float galacticPlaneTiltDeg = 30.0f;  // concentrates faint stars into a band
};

struct AmbientDef {
  Vec3 skyColor{0.02f, 0.025f, 0.04f};
  Vec3 groundColor{0.008f, 0.006f, 0.005f};
};

struct SceneObjectDef {
  AssetID ship;                     // ship definition ID
  SystemPosition position;
  Vec3 rotationDeg{0, 0, 0};
};

struct CameraDef {
  float fovDeg = 65.0f;
  float orbitDistance = 30.0f;
  float yawDeg = 35.0f;
  float pitchDeg = 15.0f;
  float autoRotateDegPerSec = 6.0f;
};

struct SceneDef {
  std::string id;
  std::string name;
  StarDef star;
  StarfieldDef starfield;
  AmbientDef ambient;
  float exposure = 1.0f;
  double rebaseDistance = 4096.0;
  CameraDef camera;
  std::vector<SceneObjectDef> objects;
};

std::optional<SceneDef> parseSceneDef(const std::string& json, const std::string& context,
                                      std::vector<std::string>* errors);
std::optional<SceneDef> loadSceneDef(const AssetManifest& manifest, const AssetID& id,
                                     std::vector<std::string>* errors);

}  // namespace fh
