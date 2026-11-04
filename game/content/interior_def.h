#pragma once
// Walkable interior scene (ship cabin, station hangar). Built from reusable piece templates
// (mesh + box colliders in tile units) placed on a grid, plus authored colliders, spawn points,
// interactables and lights. Schema: docs/data_schemas/interior.md.
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "game/content/light_def.h"
#include "game/content/material_override.h"
#include "game/physics/collision.h"

namespace fh {

struct InteriorPieceTemplate {
  AssetID mesh;
  std::vector<Aabb> colliders;  // template-local, before scale (Kenney tiles: 1 unit)
};

struct InteriorPiece {
  std::string templ;
  Vec3 position{};
  int yawQuarterTurns = 0;      // 0..3 (multiples of 90 degrees about +Y)
  float scale = 1.0f;
};

struct InteriorSpawn {
  Vec3 position{};              // feet
  float yawDeg = 0;             // 0 looks down -Z
};

struct Interactable {
  std::string id;
  std::string action;           // takeSeat, exitShip, boardShip, terminal
  std::string prompt;
  Vec3 position{};
  float radius = 1.6f;
};

struct InteriorLighting {
  Vec3 keyDirection{0.3f, 1.0f, 0.2f};  // toward the key light
  Vec3 keyColor{1.0f, 0.95f, 0.9f};
  Vec3 ambientSky{0.08f, 0.08f, 0.1f};
  Vec3 ambientGround{0.04f, 0.035f, 0.03f};
  Vec3 fillDirection{-0.4f, 0.3f, -0.6f};
  Vec3 fillColor{0.05f, 0.05f, 0.08f};
  Vec3 rimColor{0.0f, 0.0f, 0.0f};
  float rimPower = 3.0f;
  float exposure = 1.0f;
  Vec3 fogColor{0, 0, 0};
  float fogDensity = 0.0f;   // per metre
};

struct InteriorDef {
  std::string id;
  std::string name;
  std::string kind = "ship";    // ship | station
  float gravity = 9.81f;
  bool showSky = false;         // e.g. a hangar with an open bay door
  InteriorLighting lighting;
  MaterialOverrides materialOverrides;  // applied to every piece mesh
  std::map<std::string, InteriorPieceTemplate> templates;
  std::vector<InteriorPiece> pieces;
  std::vector<Aabb> colliders;  // extra authored colliders (interior space)
  std::vector<LightDef> lights;
  std::map<std::string, InteriorSpawn> spawns;
  std::vector<Interactable> interactables;
  std::optional<InteriorSpawn> parkedShip;  // station hangars draw the player's ship here
  Vec3 parkedShipColliderHalfExtents{};     // walk-blocking box around the parked ship (0 = none)

  // All colliders in interior space (templates placed, rotated and scaled + authored ones).
  std::vector<Aabb> buildColliders() const;
};

std::optional<InteriorDef> parseInteriorDef(const std::string& json, const std::string& context,
                                            std::vector<std::string>* errors);
std::optional<InteriorDef> loadInteriorDef(const AssetManifest& manifest, const AssetID& id,
                                           std::vector<std::string>* errors);

}  // namespace fh
