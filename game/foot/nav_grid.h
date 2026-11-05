#pragma once
// Where things can walk in a map, and how to get from here to there.
//
// Baked once per map from its collision mesh: every 0.5 m column is probed from the top down, and
// each floor with enough headroom becomes a node (so catwalks above a hold are separate nodes).
// Neighbouring nodes connect when the height step is small and nothing blocks the way at knee height.
// Paths come from A* and are then straightened ("string pulling") so enemies walk direct lines between
// corners instead of zig-zagging across cells. GPU-free and deterministic.
#include <array>
#include <cstdint>
#include <vector>

#include "engine/math/math.h"
#include "game/physics/collision.h"

namespace fh {

class NavGrid {
 public:
  struct Params {
    float cell = 0.5f;          // column spacing, metres
    float maxStep = 0.5f;       // largest height change between neighbouring cells (stairs, ramps)
    float minHeadroom = 0.85f;  // lowest ceiling anything can use; taller agents filter further
    float probeHeight = 0.35f;  // knee height for wall checks
    float wallMargin = 0.3f;    // cells closer than this to a wall cost more (keeps paths off walls)
  };

  // Returns false when the mesh is empty or too large to bake (more than ~2 million columns).
  bool build(const TriangleCollider& mesh, const Params& params);
  bool build(const TriangleCollider& mesh) { return build(mesh, Params{}); }
  bool empty() const { return nodes_.empty(); }
  std::size_t nodeCount() const { return nodes_.size(); }
  std::size_t edgeCount() const;

  // The node a point stands on (or the nearest one within two cells); -1 when none.
  int nodeAt(const Vec3& p) const;
  Vec3 nodePosition(int node) const { return nodes_[size_t(node)].pos; }

  // Walkable route as points on the floor, from (near) `from` to (near) `to`, straightened. The first
  // point is the first corner to head for (not `from` itself). False when no route exists.
  bool findPath(const Vec3& from, const Vec3& to, float agentHeight, std::vector<Vec3>& out,
                int maxExpanded = 20000) const;
  // A straight walk from a to b stays on floor and hits no wall (used for string pulling and by AI).
  bool walkableLine(const Vec3& a, const Vec3& b, float agentHeight) const;

 private:
  struct Node {
    Vec3 pos;
    float headroom = 0;
    bool tight = false;            // near a wall
    std::array<int, 8> next{};     // -1 = no link; order: N, E, S, W, NE, SE, SW, NW
  };
  int column(int x, int z) const { return z * nx_ + x; }
  bool clearBetween(const Vec3& a, const Vec3& b) const;

  const TriangleCollider* mesh_ = nullptr;
  Params p_;
  Vec3 origin_{};
  int nx_ = 0, nz_ = 0;
  std::vector<std::uint32_t> columnStart_;  // nx*nz + 1 offsets into nodes_ (nodes sorted by column)
  std::vector<Node> nodes_;
};

}  // namespace fh
