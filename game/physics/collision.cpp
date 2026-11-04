#include "game/physics/collision.h"

#include <algorithm>
#include <cmath>

namespace fh {

Vec3 closestPointOnAabb(const Aabb& b, const Vec3& p) {
  return {std::clamp(p.x, b.min.x, b.max.x), std::clamp(p.y, b.min.y, b.max.y), std::clamp(p.z, b.min.z, b.max.z)};
}

bool sphereVsAabb(const Vec3& c, float r, const Aabb& b, Contact& out) {
  const Vec3 q = closestPointOnAabb(b, c);
  const Vec3 d = c - q;
  const float d2 = dot(d, d);
  if (d2 > r * r) return false;
  if (d2 > 1e-12f) {
    const float dist = std::sqrt(d2);
    out.normal = d / dist;
    out.depth = r - dist;
    return true;
  }
  // Centre inside the box: push out along the axis of least penetration.
  const float pen[6] = {c.x - b.min.x, b.max.x - c.x, c.y - b.min.y, b.max.y - c.y, c.z - b.min.z, b.max.z - c.z};
  const Vec3 normals[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
  int best = 0;
  for (int i = 1; i < 6; ++i)
    if (pen[i] < pen[best]) best = i;
  out.normal = normals[best];
  out.depth = pen[best] + r;
  return true;
}

bool sphereVsObb(const Vec3& c, float r, const Obb& box, Contact& out) {
  const Vec3 local = box.rotation.conjugate().rotate(c - box.center);
  const Aabb a{-box.halfExtents, box.halfExtents};
  Contact lc;
  if (!sphereVsAabb(local, r, a, lc)) return false;
  out.normal = box.rotation.rotate(lc.normal);
  out.depth = lc.depth;
  return true;
}

Aabb rotateAabbYaw90(const Aabb& b, int quarterTurns) {
  const int t = ((quarterTurns % 4) + 4) % 4;
  Aabb r = b;
  for (int i = 0; i < t; ++i) {
    // +90 deg about +Y maps (x, z) -> (z, -x).
    const Aabb prev = r;
    r.min = {prev.min.z, prev.min.y, -prev.max.x};
    r.max = {prev.max.z, prev.max.y, -prev.min.x};
  }
  return r;
}

}  // namespace fh
