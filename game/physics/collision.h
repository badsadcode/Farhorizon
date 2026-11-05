#pragma once
// Small collision toolkit (design doc 9.4): boxes and spheres, enough for ship-vs-station and
// the on-foot capsule. Not a general physics engine.
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "engine/assets/mesh_data.h"
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
// Collision against a triangle mesh, for maps made in Blender: walls, floors, ramps and stairs simply
// work as modelled. Triangles are two-sided. A uniform grid keeps lookups local.
class TriangleCollider {
 public:
  void build(const std::vector<MeshVertex>& vertices, const std::vector<std::uint32_t>& indices, float cellSize = 2.0f);
  bool empty() const { return triangles_.empty(); }
  std::size_t triangleCount() const { return triangles_.size(); }
  const Aabb& bounds() const { return bounds_; }  // of every triangle (valid when not empty)
  // Triangles that might touch the sphere, each once.
  void nearby(const Vec3& centre, float radius, std::vector<std::uint32_t>& out) const;
  bool sphereVsTriangle(std::uint32_t triangle, const Vec3& centre, float radius, Contact& out) const;
  // Nearest surface along a unit-length ray, both sides of a triangle count. False if none within maxDistance.
  bool raycast(const Vec3& origin, const Vec3& direction, float maxDistance, float& outDistance) const;

 private:
  struct Triangle {
    Vec3 a, b, c;
  };
  std::uint64_t cellKey(int x, int y, int z) const;
  std::vector<Triangle> triangles_;
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> cells_;
  float cellSize_ = 2.0f;
  Aabb bounds_{};
};

// Rotates an axis-aligned box by a multiple of 90 degrees about +Y (interior pieces).
Aabb rotateAabbYaw90(const Aabb& box, int quarterTurns);

}  // namespace fh
