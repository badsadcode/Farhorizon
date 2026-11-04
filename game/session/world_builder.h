#pragma once
// Builds a SpaceWorld from scene + ship definitions. GPU-agnostic: visuals are created through
// callbacks (the app uploads meshes; tests return dummy handles), so the headless tests and the
// game construct exactly the same world.
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "game/content/scene_def.h"
#include "game/content/ship_def.h"
#include "game/world/space_world.h"

namespace fh {

struct VisualInfo {
  VisualHandle handle = 0;
  Mat4 meshFix;             // mesh space -> object space
  float boundsRadius = 1;   // object-space bounding radius (after scale)
  std::vector<MeshSocket> sockets;  // mesh space (ships only)
};

struct WorldVisuals {
  std::function<VisualInfo(const ShipDef&)> ship;
  std::function<VisualInfo(const PropDef&)> prop;
  // One handle per variant; generation of the meshes is the callback's job.
  std::function<std::vector<VisualHandle>(const AsteroidFieldDef&)> asteroidVariants;
};

Quat eulerDegToQuat(const Vec3& deg);  // X, then Y, then Z

std::unique_ptr<SpaceWorld> buildSpaceWorld(const SceneDef& scene, const ShipDef& ship, const WorldVisuals& visuals,
                                            std::optional<SystemPosition> startOverride = std::nullopt);

}  // namespace fh
