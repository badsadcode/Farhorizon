#pragma once
// Ship definition loaded from content/ships/*.json (schema: docs/data_schemas/ship.md).
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/math/math.h"
#include "game/content/light_def.h"
#include "game/content/material_override.h"
#include "game/ships/flight_model.h"

namespace fh {

// Attachment point in ship space (metres, after meshScale/meshRotation are applied).
struct SocketDef {
  Vec3 position{};
  Vec3 rotationDeg{};
};

struct ShipCameraDefs {
  std::string cockpitSocket = "socket_pilot";
  float cockpitFovDeg = 75.0f;
  float chaseDistance = 25.0f;
  float chaseHeight = 6.0f;
  float chasePositionLag = 7.0f;  // higher = stiffer (1/s)
  float chaseRotationLag = 9.0f;
  float chaseFovDeg = 65.0f;
};

struct ShipDef {
  std::string id;
  std::string name;
  AssetID mesh;
  AssetID interior;               // walkable interior (optional)
  float meshScale = 1.0f;
  Vec3 meshRotationDeg{0, 0, 0};  // corrects exporter axis conventions
  MaterialOverrides materialOverrides;  // by glTF material name
  float massTons = 0;
  float cargoCapacityTons = 0;
  float maxHull = 0;
  float maxShield = 0;
  float collisionRadius = 0;       // metres; 0 = derive from mesh bounds
  FlightParams flight;
  ShipCameraDefs cameras;
  std::map<std::string, SocketDef> sockets;  // fallbacks when the mesh has no socket nodes
  std::vector<LightDef> lights;              // ship space
  EngineGlowDef engineGlow;
  std::vector<std::string> mounts;
};

std::optional<ShipDef> parseShipDef(const std::string& json, const std::string& context,
                                    std::vector<std::string>* errors);
std::optional<ShipDef> loadShipDef(const AssetManifest& manifest, const AssetID& id,
                                   std::vector<std::string>* errors);

}  // namespace fh
