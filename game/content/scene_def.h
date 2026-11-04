#pragma once
// Space scene definition (schema: docs/data_schemas/scene.md).
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/math/frames.h"
#include "game/content/light_def.h"
#include "game/content/material_override.h"

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
  float galacticPlaneTiltDeg = 30.0f;
};

struct AmbientDef {
  Vec3 skyColor{0.02f, 0.025f, 0.04f};
  Vec3 groundColor{0.008f, 0.006f, 0.005f};
  Vec3 fillDirection{0, 1, 0};   // toward the fill light (nebula glow, planet bounce)
  Vec3 fillColor{0, 0, 0};       // linear colour * intensity
  Vec3 rimColor{0, 0, 0};
  float rimPower = 3.0f;
};

struct SkyDef {
  Vec3 baseColor{0.0016f, 0.0019f, 0.0030f};
  Vec3 bandColor{0.020f, 0.018f, 0.024f};
  float bandIntensity = 1.0f;
  Vec3 nebulaColor{0.010f, 0.004f, 0.012f};
  float nebulaStrength = 1.0f;
};

struct PlayerStartDef {
  AssetID ship;
  SystemPosition position;
  Vec3 rotationDeg{0, 0, 0};
};

struct BoxColliderDef {
  Vec3 center{};        // prop space (scaled metres)
  Vec3 halfExtents{1, 1, 1};
  Vec3 rotationDeg{};
};

struct DockingBayDef {
  std::string id;
  std::string name;
  Vec3 position{};      // prop space: where the ship's centre rests when clamped
  Vec3 rotationDeg{};   // prop space: ship orientation when clamped (nose = -Z, up = +Y)
};

struct DockingDef {
  AssetID interior;               // station interior scene entered through the airlock
  AssetID market;                 // station market (optional)
  float clearanceRange = 3000.0f; // metres from the prop centre
  float captureDistance = 12.0f;  // metres from the bay point
  float captureSpeed = 6.0f;      // m/s
  float captureAngleDeg = 15.0f;
  float approachHeight = 80.0f;   // autopilot approach column height above the bay
  std::vector<DockingBayDef> bays;
  std::vector<std::string> services;  // terminal services: "repair", "upgrades", "contracts"
};

// Static (optionally spinning) scene object such as a station proxy or a beacon.
struct PropDef {
  std::string id;
  std::string name;
  AssetID mesh;
  float scale = 1.0f;
  SystemPosition position;
  Vec3 rotationDeg{0, 0, 0};
  Vec3 spinAxis{0, 1, 0};
  float spinDegPerSec = 0.0f;
  float collisionRadius = 0.0f;     // 0 = derive from mesh bounds
  bool targetable = true;
  std::string marker = "station";   // HUD marker category: station, outpost, beacon...
  MaterialOverrides materialOverrides;
  std::vector<LightDef> lights;     // prop space (scaled metres), rotate with the prop
  std::vector<BoxColliderDef> colliders;  // empty = bounding sphere (collisionRadius)
  std::optional<DockingDef> docking;
};

struct AsteroidResourceDef {
  std::string commodity;
  float weight = 1.0f;          // relative chance of being an asteroid's primary resource
};

struct AsteroidFieldDef {
  std::vector<AsteroidResourceDef> resources;  // empty = not mineable
  float yieldPerMetre = 1.0f;   // mineable units per metre of asteroid radius
  float hardnessMin = 0.8f, hardnessMax = 1.6f;
  std::string id;
  std::string name;
  SystemPosition center;
  std::uint32_t seed = 1;
  std::uint32_t count = 200;
  float radius = 3000.0f;
  float thickness = 0.35f;
  std::uint32_t clusters = 6;
  float minScale = 4.0f;
  float maxScale = 60.0f;
  std::uint32_t variants = 4;
  int detail = 2;
  float roughness = 0.35f;
  Vec3 color{0.30f, 0.27f, 0.24f};
};

struct LandingSiteDef {
  std::string id;
  std::string name;
  float latDeg = 0, lonDeg = 0;   // planet-local direction of the site
  AssetID interior;               // surface scene (pad, terrain, outpost)
  AssetID market;                 // optional
  std::vector<std::string> services;  // terminal services: "repair", "upgrades", "contracts"
};

// A planet or moon: rendered as a far-field impostor, never as a physics body (design 4.3 / 5.3).
struct PlanetDef {
  std::string id;
  std::string name;
  SystemPosition center;
  double radius = 1.0e6;          // metres
  Vec3 colorA{0.35f, 0.3f, 0.25f};  // surface palette (linear)
  Vec3 colorB{0.15f, 0.14f, 0.13f};
  float seed = 1.0f;
  float craters = 0.5f;           // 0..1
  Vec3 atmosphereColor{0.5f, 0.45f, 0.4f};
  float atmosphereThickness = 0.0f;  // fraction of radius (0 = airless)
  double orbitAltitude = 150000.0;   // metres above the surface for travel/landing approach
  std::vector<LandingSiteDef> sites;
};

// Pirate (or other) ships that appear when the player comes near a zone (design doc 8.3).
struct EncounterDef {
  std::string id;
  AssetID ship;
  SystemPosition center;
  float radius = 1500.0f;          // ships spawn and patrol inside this sphere
  float triggerRange = 6000.0f;    // player distance from the centre that activates the zone
  float spawnMinDistance = 1200.0f; // never spawn closer than this to the player
  int count = 2;                   // ships alive at once
  float respawnSeconds = 90.0f;    // time to refill the zone (split between ships)
};

// Civilian ships shuttling between waypoints while the player is near the route.
struct TrafficDef {
  std::string id;
  AssetID ship;
  std::vector<SystemPosition> waypoints;
  int count = 1;
  float triggerRange = 8000.0f;    // player distance from the first waypoint
  float respawnSeconds = 60.0f;
};

struct CameraDef {
  std::string initialMode = "chase";  // chase, cockpit, orbit, free
  float fovDeg = 65.0f;               // orbit/free developer cameras
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
  SkyDef sky;
  float exposure = 1.0f;
  std::int64_t startingCredits = 500;
  std::int64_t insuranceDeductible = 250;  // charged when the ship is lost
  double rebaseDistance = 4096.0;
  CameraDef camera;
  PlayerStartDef player;
  std::vector<PropDef> props;
  std::vector<AsteroidFieldDef> asteroidFields;
  std::vector<PlanetDef> planets;
  std::vector<EncounterDef> encounters;
  std::vector<TrafficDef> traffic;
};

std::optional<SceneDef> parseSceneDef(const std::string& json, const std::string& context,
                                      std::vector<std::string>* errors);
std::optional<SceneDef> loadSceneDef(const AssetManifest& manifest, const AssetID& id,
                                     std::vector<std::string>* errors);

// Fixed, repeatable benchmark path (design doc 14): a scene, a start pose and scripted input.
struct BenchmarkDef {
  std::string id;
  AssetID scene;
  float durationSeconds = 10.0f;
  std::string camera = "chase";
  std::optional<PlayerStartDef> playerOverride;  // ship field unused; pose only
  float throttle = 0.0f;
  Vec3 rotation{0, 0, 0};  // pitch, yaw, roll commands in [-1,1]
  std::string startIn;     // "", "ship" or "station": on-foot benchmark (starts docked)
  float lookTurnDegPerSec = 0.0f;  // on-foot: slow camera pan
  bool autoDock = false;   // request docking and engage the approach autopilot as soon as cleared
  bool mine = false;       // target the nearest asteroid, face it and hold the mining laser
  bool startLanded = false;  // start landed at the first landing site
  bool autoLand = false;     // request a landing at the nearest site immediately
};

std::optional<BenchmarkDef> loadBenchmarkDef(const AssetManifest& manifest, const AssetID& id,
                                             std::vector<std::string>* errors);

}  // namespace fh
