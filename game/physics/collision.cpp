#include "game/physics/collision.h"

#include <unordered_set>

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

namespace {
// Closest point on a triangle (Ericson, Real-Time Collision Detection).
Vec3 closestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
  const Vec3 ab = b - a, ac = c - a, ap = p - a;
  const float d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return a;
  const Vec3 bp = p - b;
  const float d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return b;
  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
  const Vec3 cp = p - c;
  const float d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return c;
  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
  const float va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
  const float denom = 1.0f / (va + vb + vc);
  return a + ab * (vb * denom) + ac * (vc * denom);
}
}  // namespace

std::uint64_t TriangleCollider::cellKey(int x, int y, int z) const {
  const auto part = [](int v) { return std::uint64_t(std::uint32_t(v + (1 << 20))) & 0x1FFFFFu; };
  return part(x) | (part(y) << 21) | (part(z) << 42);
}

void TriangleCollider::build(const std::vector<MeshVertex>& vertices, const std::vector<std::uint32_t>& indices,
                             float cellSize) {
  cellSize_ = std::max(0.25f, cellSize);
  triangles_.clear();
  cells_.clear();
  triangles_.reserve(indices.size() / 3);
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    const Triangle t{vertices[indices[i]].position, vertices[indices[i + 1]].position, vertices[indices[i + 2]].position};
    if (length(cross(t.b - t.a, t.c - t.a)) < 1e-7f) continue;  // degenerate
    const auto id = std::uint32_t(triangles_.size());
    triangles_.push_back(t);
    int lo[3], hi[3];
    for (int k = 0; k < 3; ++k) {
      lo[k] = int(std::floor(std::min({t.a[k], t.b[k], t.c[k]}) / cellSize_));
      hi[k] = int(std::floor(std::max({t.a[k], t.b[k], t.c[k]}) / cellSize_));
    }
    for (int z = lo[2]; z <= hi[2]; ++z)
      for (int y = lo[1]; y <= hi[1]; ++y)
        for (int x = lo[0]; x <= hi[0]; ++x) cells_[cellKey(x, y, z)].push_back(id);
  }
}

void TriangleCollider::nearby(const Vec3& centre, float radius, std::vector<std::uint32_t>& out) const {
  out.clear();
  int lo[3], hi[3];
  for (int k = 0; k < 3; ++k) {
    lo[k] = int(std::floor((centre[k] - radius) / cellSize_));
    hi[k] = int(std::floor((centre[k] + radius) / cellSize_));
  }
  for (int z = lo[2]; z <= hi[2]; ++z)
    for (int y = lo[1]; y <= hi[1]; ++y)
      for (int x = lo[0]; x <= hi[0]; ++x) {
        const auto it = cells_.find(cellKey(x, y, z));
        if (it != cells_.end()) out.insert(out.end(), it->second.begin(), it->second.end());
      }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
}

bool TriangleCollider::sphereVsTriangle(std::uint32_t triangle, const Vec3& centre, float radius, Contact& out) const {
  const Triangle& t = triangles_[triangle];
  const Vec3 closest = closestOnTriangle(centre, t.a, t.b, t.c);
  const Vec3 away = centre - closest;
  const float d2 = dot(away, away);
  if (d2 >= radius * radius) return false;
  const float d = std::sqrt(d2);
  if (d > 1e-5f) {
    out.normal = away / d;
  } else {  // the centre is on the surface: leave along the face, upward if there is a choice
    Vec3 n = normalize(cross(t.b - t.a, t.c - t.a));
    if (n.y < 0) n = -n;
    out.normal = n;
  }
  out.depth = radius - d;
  return true;
}

bool TriangleCollider::raycast(const Vec3& origin, const Vec3& direction, float maxDistance, float& outDistance) const {
  float best = maxDistance;
  bool found = false;
  std::vector<std::uint32_t> near;
  std::unordered_set<std::uint32_t> tested;
  // Walk along the ray a cell at a time, stopping once the best hit is behind us.
  for (float along = 0; along <= best + cellSize_; along += cellSize_) {
    near.clear();
    nearby(origin + direction * along, cellSize_, near);
    for (const std::uint32_t index : near) {
      if (!tested.insert(index).second) continue;
      const Triangle& tri = triangles_[index];
      const Vec3 e1 = tri.b - tri.a, e2 = tri.c - tri.a;
      const Vec3 p = cross(direction, e2);
      const float det = dot(e1, p);
      if (std::fabs(det) < 1e-9f) continue;
      const Vec3 to = origin - tri.a;
      const float u = dot(to, p) / det;
      if (u < 0 || u > 1) continue;
      const Vec3 q = cross(to, e1);
      const float v = dot(direction, q) / det;
      if (v < 0 || u + v > 1) continue;
      const float t = dot(e2, q) / det;
      if (t >= 0 && t <= best) {
        best = t;
        found = true;
      }
    }
  }
  if (found) outDistance = best;
  return found;
}

}  // namespace fh
