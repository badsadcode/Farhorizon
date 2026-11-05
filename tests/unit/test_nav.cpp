// Navigation grid and pathfinding: around walls, through gaps, up ramps, and on the real test derelict.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "game/content/map_def.h"
#include "game/foot/nav_grid.h"
#include "test_framework.h"

using namespace fh;

namespace {

void quad(std::vector<MeshVertex>& v, std::vector<std::uint32_t>& idx, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
  const auto base = std::uint32_t(v.size());
  for (const Vec3& p : {a, b, c, d}) {
    MeshVertex m{};
    m.position = p;
    v.push_back(m);
  }
  for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + i);
}

// 20 x 20 m floor; a wall across z = 0 from x = -10 to `gapAt` (then a 1.2 m gap), and on to x = 10 after
// the gap when `closed` is false the gap stays open.
TriangleCollider walledRoom(bool closed) {
  std::vector<MeshVertex> v;
  std::vector<std::uint32_t> idx;
  quad(v, idx, {-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}, {10, 0, -10});
  quad(v, idx, {-10, 0, 0}, {6, 0, 0}, {6, 3, 0}, {-10, 3, 0});
  if (closed) quad(v, idx, {6, 0, 0}, {7.2f, 0, 0}, {7.2f, 3, 0}, {6, 3, 0});
  quad(v, idx, {7.2f, 0, 0}, {10, 0, 0}, {10, 3, 0}, {7.2f, 3, 0});
  TriangleCollider c;
  c.build(v, idx);
  return c;
}

float pathLength(const Vec3& from, const std::vector<Vec3>& path) {
  float len = 0;
  Vec3 at = from;
  for (const Vec3& p : path) {
    len += length(p - at);
    at = p;
  }
  return len;
}

}  // namespace

TEST(nav_walks_around_a_wall_through_its_gap) {
  const TriangleCollider c = walledRoom(false);
  NavGrid nav;
  CHECK(nav.build(c));
  CHECK(nav.nodeCount() > 1000);
  std::vector<Vec3> path;
  const Vec3 from{0, 0, -4}, to{0, 0, 4};
  CHECK(!nav.walkableLine(from, to, 0.8f));  // the wall is in the way
  CHECK(nav.findPath(from, to, 0.8f, path));
  CHECK(!path.empty());
  // It goes through the gap (x between 6 and 7.2) and ends where asked.
  bool throughGap = false;
  Vec3 at = from;
  for (const Vec3& p : path) {
    if ((at.z < 0) != (p.z < 0)) {
      const float t = at.z / (at.z - p.z);
      const float x = at.x + (p.x - at.x) * t;
      throughGap |= x > 6.0f && x < 7.2f;
    }
    CHECK(nav.walkableLine(at, p, 0.8f));
    at = p;
  }
  CHECK(throughGap);
  CHECK(length(path.back() - to) < 0.3f);
  // Straightened: a handful of corners, not one point per cell.
  CHECK(path.size() <= 4);
  const float len = pathLength(from, path);
  CHECK(len > 14.0f && len < 19.0f);  // the detour is about 2 x sqrt(6.6^2 + 4^2) = 15.4 m
}

TEST(nav_finds_no_way_through_a_closed_wall) {
  const TriangleCollider c = walledRoom(true);
  NavGrid nav;
  CHECK(nav.build(c));
  std::vector<Vec3> path;
  CHECK(!nav.findPath({0, 0, -4}, {0, 0, 4}, 0.8f, path));
}

TEST(nav_climbs_a_ramp_to_an_upper_floor_and_respects_headroom) {
  std::vector<MeshVertex> v;
  std::vector<std::uint32_t> idx;
  quad(v, idx, {-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}, {10, 0, -10});   // ground
  quad(v, idx, {2, 3, -10}, {2, 3, 10}, {10, 3, 10}, {10, 3, -10});      // deck at y = 3 for x > 2
  quad(v, idx, {-6, 0, -10}, {-6, 0, -8}, {2, 3, -8}, {2, 3, -10});      // ramp up along x
  quad(v, idx, {-10, 1.0f, 4}, {-10, 1.0f, 10}, {-4, 1.0f, 10}, {-4, 1.0f, 4});  // low shelf (1 m headroom)
  TriangleCollider c;
  c.build(v, idx);
  NavGrid nav;
  CHECK(nav.build(c));
  std::vector<Vec3> path;
  CHECK(nav.findPath({-8, 0, 0}, {6, 3, 0}, 0.8f, path));
  CHECK(!path.empty());
  CHECK(std::fabs(path.back().y - 3.0f) < 0.1f);
  bool usedRamp = false;
  for (const Vec3& p : path) usedRamp |= p.z < -7.5f;
  CHECK(usedRamp);
  // Under the shelf: fine for a crawler (0.8 m), not for a brute (1.5 m), which must go around.
  std::vector<Vec3> small, big;
  CHECK(nav.findPath({-7, 0, 2}, {-7, 0, 9}, 0.8f, small));
  CHECK(nav.findPath({-7, 0, 2}, {-7, 0, 9.6f}, 1.5f, big) == false);  // the shelf covers everything up to the wall edge
}

TEST(nav_bakes_the_test_derelict_quickly) {
  std::vector<std::string> errors;
  auto map = loadMap(std::filesystem::path(FH_TEST_DATA_DIR) / "derelict_fixture.glb", &errors);
  CHECK(map.has_value());
  if (!map || !map->def.meshCollider) return;
  NavGrid nav;
  const auto t0 = std::chrono::steady_clock::now();
  CHECK(nav.build(*map->def.meshCollider));
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("    derelict nav: %zu nodes, %zu links, %.0f ms\n", nav.nodeCount(), nav.edgeCount(), ms);
  CHECK(nav.nodeCount() > 500);
  CHECK(ms < 4000.0);
  // From the way in to the enemy spawns.
  const auto& spawns = map->def.spawns;
  const auto start = spawns.count("start") ? spawns.at("start").position : spawns.begin()->second.position;
  int reached = 0, total = 0;
  for (const auto& m : map->def.markers) {
    if (m.type != "enemy_spawn") continue;
    ++total;
    std::vector<Vec3> path;
    reached += nav.findPath(start, m.position, 0.8f, path);
  }
  CHECK(total > 0);
  CHECK(reached == total);
}
