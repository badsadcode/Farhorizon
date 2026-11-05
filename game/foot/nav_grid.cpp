#include "game/foot/nav_grid.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace fh {

namespace {
constexpr int kDx[8] = {0, 1, 0, -1, 1, 1, -1, -1};
constexpr int kDz[8] = {-1, 0, 1, 0, -1, 1, 1, -1};
}  // namespace

bool NavGrid::build(const TriangleCollider& mesh, const Params& params) {
  nodes_.clear();
  columnStart_.clear();
  mesh_ = &mesh;
  p_ = params;
  if (mesh.empty()) return false;
  const Aabb b = mesh.bounds();
  origin_ = {b.min.x, b.min.y, b.min.z};
  nx_ = std::max(1, int(std::ceil((b.max.x - b.min.x) / p_.cell)));
  nz_ = std::max(1, int(std::ceil((b.max.z - b.min.z) / p_.cell)));
  if (std::int64_t(nx_) * nz_ > 2'000'000) return false;
  const float top = b.max.y + 1.0f;
  const float depth = b.max.y - b.min.y + 2.0f;

  // ---- Floors in every column, top down ----
  columnStart_.resize(size_t(nx_) * nz_ + 1, 0);
  for (int z = 0; z < nz_; ++z) {
    for (int x = 0; x < nx_; ++x) {
      columnStart_[size_t(column(x, z))] = std::uint32_t(nodes_.size());
      const float wx = origin_.x + (float(x) + 0.5f) * p_.cell, wz = origin_.z + (float(z) + 0.5f) * p_.cell;
      float y = top;
      for (int layer = 0; layer < 16; ++layer) {
        float t = 0;
        if (!mesh.raycast({wx, y, wz}, {0, -1, 0}, y - (top - depth), t)) break;
        const float floorY = y - t;
        // Headroom: the next surface straight up. The underside of a floor is right above itself.
        float up = 0;
        const float headroom = mesh.raycast({wx, floorY + 0.02f, wz}, {0, 1, 0}, 4.0f, up) ? up + 0.02f : 4.0f;
        if (headroom >= p_.minHeadroom) {
          Node n;
          n.pos = {wx, floorY, wz};
          n.headroom = headroom;
          n.next.fill(-1);
          const Vec3 knee{wx, floorY + p_.probeHeight, wz};
          for (int d = 0; d < 8 && !n.tight; ++d) {
            const Vec3 dir = normalize(Vec3{float(kDx[d]), 0, float(kDz[d])});
            float h = 0;
            n.tight = mesh.raycast(knee, dir, p_.wallMargin, h);
          }
          nodes_.push_back(n);
        }
        y = floorY - 0.05f;
        if (y <= top - depth) break;
      }
    }
  }
  columnStart_.back() = std::uint32_t(nodes_.size());

  // ---- Links: straight neighbours first, then diagonals that do not cut a corner ----
  for (int z = 0; z < nz_; ++z)
    for (int x = 0; x < nx_; ++x)
      for (std::uint32_t i = columnStart_[size_t(column(x, z))]; i < columnStart_[size_t(column(x, z)) + 1]; ++i)
        for (int d = 0; d < 4; ++d) {
          const int ox = x + kDx[d], oz = z + kDz[d];
          if (ox < 0 || oz < 0 || ox >= nx_ || oz >= nz_) continue;
          Node& n = nodes_[i];
          int best = -1;
          float bestDy = p_.maxStep;
          for (std::uint32_t j = columnStart_[size_t(column(ox, oz))]; j < columnStart_[size_t(column(ox, oz)) + 1]; ++j) {
            const float dy = std::fabs(nodes_[j].pos.y - n.pos.y);
            if (dy <= bestDy) {
              bestDy = dy;
              best = int(j);
            }
          }
          if (best >= 0 && clearBetween(n.pos, nodes_[size_t(best)].pos)) n.next[size_t(d)] = best;
        }
  for (Node& n : nodes_)
    for (int d = 4; d < 8; ++d) {
      // NE = N then E, SE = E then S, SW = S then W, NW = W then N.
      const int a = d - 4, b2 = (d - 3) % 4;
      const int viaA = n.next[size_t(a)], viaB = n.next[size_t(b2)];
      if (viaA < 0 || viaB < 0) continue;
      const int t1 = nodes_[size_t(viaA)].next[size_t(b2)], t2 = nodes_[size_t(viaB)].next[size_t(a)];
      if (t1 >= 0 && t1 == t2) n.next[size_t(d)] = t1;
    }
  return true;
}

std::size_t NavGrid::edgeCount() const {
  std::size_t n = 0;
  for (const Node& node : nodes_)
    for (int l : node.next) n += l >= 0;
  return n;
}

bool NavGrid::clearBetween(const Vec3& a, const Vec3& b) const {
  const Vec3 from = a + Vec3{0, p_.probeHeight, 0}, to = b + Vec3{0, p_.probeHeight, 0};
  const Vec3 d = to - from;
  const float len = length(d);
  if (len < 1e-4f) return true;
  float t = 0;
  return !mesh_->raycast(from, d / len, len, t);
}

int NavGrid::nodeAt(const Vec3& p) const {
  if (nodes_.empty()) return -1;
  const int cx = int(std::floor((p.x - origin_.x) / p_.cell)), cz = int(std::floor((p.z - origin_.z) / p_.cell));
  int best = -1;
  float bestScore = std::numeric_limits<float>::max();
  for (int ring = 0; ring <= 2 && best < 0; ++ring)
    for (int z = cz - ring; z <= cz + ring; ++z)
      for (int x = cx - ring; x <= cx + ring; ++x) {
        if (x < 0 || z < 0 || x >= nx_ || z >= nz_) continue;
        if (ring > 0 && std::abs(x - cx) != ring && std::abs(z - cz) != ring) continue;
        for (std::uint32_t i = columnStart_[size_t(column(x, z))]; i < columnStart_[size_t(column(x, z)) + 1]; ++i) {
          const Vec3& q = nodes_[i].pos;
          const float dy = p.y - q.y;
          if (dy < -0.6f || dy > 2.2f) continue;  // a floor we are standing on or just above
          const float score = std::fabs(dy) + std::fabs(q.x - p.x) + std::fabs(q.z - p.z);
          if (score < bestScore) {
            bestScore = score;
            best = int(i);
          }
        }
      }
  return best;
}

bool NavGrid::walkableLine(const Vec3& a, const Vec3& b, float agentHeight) const {
  if (!clearBetween(a, b)) return false;
  const Vec3 d = b - a;
  const float len = length(Vec3{d.x, 0, d.z});
  const int steps = std::max(1, int(std::ceil(len / (p_.cell * 0.7f))));
  float lastY = a.y;
  for (int i = 1; i <= steps; ++i) {
    const Vec3 at = a + d * (float(i) / float(steps));
    const int n = nodeAt(Vec3{at.x, lastY + 0.3f, at.z});
    if (n < 0) return false;
    const Node& node = nodes_[size_t(n)];
    if (node.headroom < agentHeight || std::fabs(node.pos.y - lastY) > p_.maxStep) return false;
    if (std::fabs(node.pos.x - at.x) > p_.cell || std::fabs(node.pos.z - at.z) > p_.cell) return false;
    lastY = node.pos.y;
  }
  return std::fabs(lastY - b.y) <= p_.maxStep;  // ended on b's floor, not one above or below it
}

bool NavGrid::findPath(const Vec3& from, const Vec3& to, float agentHeight, std::vector<Vec3>& out,
                       int maxExpanded) const {
  out.clear();
  const int start = nodeAt(from), goal = nodeAt(to);
  if (start < 0 || goal < 0) return false;
  if (start == goal) {
    out.push_back(to);
    return true;
  }
  const size_t n = nodes_.size();
  std::vector<float> cost(n, std::numeric_limits<float>::max());
  std::vector<int> came(n, -1);
  std::vector<bool> closed(n, false);
  using Item = std::pair<float, int>;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
  const Vec3 goalPos = nodes_[size_t(goal)].pos;
  cost[size_t(start)] = 0;
  open.push({length(goalPos - nodes_[size_t(start)].pos), start});
  int expanded = 0;
  bool found = false;
  while (!open.empty()) {
    const int cur = open.top().second;
    open.pop();
    if (closed[size_t(cur)]) continue;
    closed[size_t(cur)] = true;
    if (cur == goal) {
      found = true;
      break;
    }
    if (++expanded > maxExpanded) break;
    const Node& node = nodes_[size_t(cur)];
    for (int link : node.next) {
      if (link < 0 || closed[size_t(link)]) continue;
      const Node& nb = nodes_[size_t(link)];
      if (nb.headroom < agentHeight) continue;
      const float step = length(nb.pos - node.pos) * (nb.tight ? 2.5f : 1.0f);
      const float c = cost[size_t(cur)] + step;
      if (c < cost[size_t(link)]) {
        cost[size_t(link)] = c;
        came[size_t(link)] = cur;
        open.push({c + length(goalPos - nb.pos), link});
      }
    }
  }
  if (!found) return false;
  std::vector<Vec3> raw;
  for (int at = goal; at >= 0; at = came[size_t(at)]) raw.push_back(nodes_[size_t(at)].pos);
  std::reverse(raw.begin(), raw.end());
  raw.back() = Vec3{to.x, raw.back().y, to.z};
  // String pulling: from each anchor, jump to the furthest point still reachable in a straight walk.
  Vec3 anchor{from.x, nodes_[size_t(start)].pos.y, from.z};
  size_t i = 1;
  while (i < raw.size()) {
    size_t far = i;
    for (size_t j = raw.size() - 1; j > i; --j)
      if (walkableLine(anchor, raw[j], agentHeight)) {
        far = j;
        break;
      }
    out.push_back(raw[far]);
    anchor = raw[far];
    i = far + 1;
  }
  if (out.empty()) out.push_back(raw.back());
  return true;
}

}  // namespace fh
