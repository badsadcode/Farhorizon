#pragma once
// Small collision toolkit (design doc 9.4): boxes and spheres, enough for ship-vs-station and
// the on-foot capsule. Not a general physics engine.
#include <vector>

#include "engine/math/math.h"

namespace fh {

struct Aabb {
  Vec3 min{}, max{};
  Vec3 center() const { return (min + max) * 0.5f; }
  Vec3 halfExtents() const { return (max - min) * 0.5f; }
};

// Oriented box in some local frame: centre, rotation and half extents.
struct Obb {
  Vec3 center{};
  Quat rotation{};
  Vec3 halfExtents{1, 1, 1};
};

struct Contact {
  Vec3 normal{0, 1, 0};  // points from the obstacle toward the sphere centre
  float depth = 0.0f;    // penetration depth (> 0 when overlapping)
};

Vec3 closestPointOnAabb(const Aabb& box, const Vec3& p);
// Returns true and fills `out` when a sphere overlaps the box.
bool sphereVsAabb(const Vec3& centre, float radius, const Aabb& box, Contact& out);
bool sphereVsObb(const Vec3& centre, float radius, const Obb& box, Contact& out);
// Rotates an axis-aligned box by a multiple of 90 degrees about +Y (interior pieces).
Aabb rotateAabbYaw90(const Aabb& box, int quarterTurns);

}  // namespace fh
