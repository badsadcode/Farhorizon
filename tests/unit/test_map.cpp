// Mesh collision for the on-foot controller, and reading a map exported by the Blender add-on.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "game/content/map_def.h"
#include "game/player/character_controller.h"
#include "test_framework.h"

using namespace fh;

namespace {
constexpr float kDt = 1.0f / 60.0f;

struct Builder {
  std::vector<MeshVertex> v;
  std::vector<std::uint32_t> i;
  void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    const auto base = std::uint32_t(v.size());
    for (const Vec3& p : {a, b, c, d}) {
      MeshVertex m{};
      m.position = p;
      v.push_back(m);
    }
    for (std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) i.push_back(base + k);
  }
  void floor(float size, float y = 0) { quad({-size, y, -size}, {size, y, -size}, {size, y, size}, {-size, y, size}); }
  void wallAtZ(float z, float size, float height) { quad({-size, 0, z}, {size, 0, z}, {size, height, z}, {-size, height, z}); }
  TriangleCollider build(float cell = 2.0f) const {
    TriangleCollider c;
    c.build(v, i, cell);
    return c;
  }
};

void walk(CharacterState& s, const CharacterParams& p, const TriangleCollider& mesh, Vec2 move, float seconds) {
  CharacterInput in;
  in.move = move;
  for (int k = 0; k < int(seconds / kDt); ++k) stepCharacter(s, in, p, {}, kDt, nullptr, &mesh);
}
}  // namespace

TEST(mesh_collision_stands_on_a_floor) {
  Builder b;
  b.floor(20);
  const TriangleCollider mesh = b.build();
  CharacterParams p;
  CharacterState s;
  s.feet = {0, 3, 0};
  walk(s, p, mesh, {}, 2.0f);
  CHECK_NEAR(s.feet.y, 0, 0.02);
  CHECK(s.grounded);
}

TEST(mesh_collision_stops_at_a_wall_and_slides_along_it) {
  Builder b;
  b.floor(40);
  b.wallAtZ(-5, 40, 4);
  const TriangleCollider mesh = b.build();
  CharacterParams p;
  CharacterState s;
  walk(s, p, mesh, {0, 1}, 5.0f);
  CHECK(s.feet.z > -5.0f + p.radius - 0.05f);
  CHECK(s.feet.z < -3.0f);  // it did reach the wall
  walk(s, p, mesh, {1, 1}, 1.0f);
  CHECK(s.feet.x > 1.0f);  // pressing into it at an angle still moves along it
}

TEST(mesh_collision_does_not_tunnel_through_a_thin_wall_when_falling_fast) {
  Builder b;
  b.floor(20);
  const TriangleCollider mesh = b.build();
  CharacterParams p;
  CharacterState s;
  s.feet = {0, 30, 0};
  s.velocity = {0, -40, 0};
  walk(s, p, mesh, {}, 2.0f);
  CHECK(s.feet.y > -0.1f);
}

TEST(mesh_collision_keeps_a_player_inside_a_closed_room) {
  Builder b;
  b.floor(5);
  b.wallAtZ(-5, 5, 3);
  b.wallAtZ(5, 5, 3);
  b.quad({-5, 0, -5}, {-5, 0, 5}, {-5, 3, 5}, {-5, 3, -5});
  b.quad({5, 0, -5}, {5, 0, 5}, {5, 3, 5}, {5, 3, -5});
  const TriangleCollider mesh = b.build();
  CharacterParams p;
  CharacterState s;
  for (Vec2 m : {Vec2{1, 0}, Vec2{0, 1}, Vec2{-1, 0}, Vec2{0, -1}, Vec2{1, 1}}) walk(s, p, mesh, m, 4.0f);
  CHECK(std::fabs(s.feet.x) < 5.0f);
  CHECK(std::fabs(s.feet.z) < 5.0f);
  CHECK_NEAR(s.feet.y, 0, 0.05);
}

TEST(mesh_collision_walks_up_a_ramp) {
  Builder b;
  b.floor(30);
  b.quad({-3, 0, -2}, {3, 0, -2}, {3, 3, -12}, {-3, 3, -12});  // about 17 degrees
  const TriangleCollider mesh = b.build();
  CharacterParams p;
  CharacterState s;
  float highest = 0;
  for (int k = 0; k < 300; ++k) {
    walk(s, p, mesh, {0, 1}, kDt * 1.5f);
    highest = std::max(highest, s.feet.y);
  }
  CHECK(highest > 2.5f);
}

TEST(mesh_collision_climbs_stairs) {
  Builder b;
  b.floor(30);
  for (int k = 0; k < 10; ++k) {  // 18 cm risers, 30 cm treads: real stairs
    const float y0 = float(k) * 0.18f, y1 = y0 + 0.18f, z0 = -2.0f - float(k) * 0.3f, z1 = z0 - 0.3f;
    b.quad({-2, y0, z0}, {2, y0, z0}, {2, y1, z0}, {-2, y1, z0});
    b.quad({-2, y1, z0}, {2, y1, z0}, {2, y1, z1}, {-2, y1, z1});
  }
  const TriangleCollider mesh = b.build();
  CharacterParams p;
  CharacterState s;
  float highest = 0;
  for (int k = 0; k < 240; ++k) {
    walk(s, p, mesh, {0, 1}, kDt * 1.5f);
    highest = std::max(highest, s.feet.y);
  }
  CHECK(highest > 1.5f);  // the top of the flight is 1.8 m up
}

TEST(mesh_collision_grid_finds_each_triangle_once) {
  Builder b;
  b.floor(50);  // large triangles span many cells
  const TriangleCollider mesh = b.build();
  std::vector<std::uint32_t> near;
  mesh.nearby({0, 0, 0}, 5.0f, near);
  CHECK(near.size() == 2);
  mesh.nearby({0, 500, 0}, 1.0f, near);
  CHECK(near.empty());
}

TEST(mesh_collision_ignores_degenerate_triangles) {
  Builder b;
  b.quad({1, 0, 1}, {1, 0, 1}, {1, 0, 1}, {1, 0, 1});
  CHECK(b.build().empty());
}

// ---- The sample map, exported through the add-on from tools/blender/build_sample_derelict.py ----

TEST(map_sample_derelict_loads_with_everything_the_add_on_exports) {
  std::vector<std::string> errors;
  const auto map = loadMap(std::filesystem::path(FH_TEST_CONTENT_DIR) / "maps" / "derelict_sample.glb", &errors);
  for (const auto& e : errors) fhtest::reportFailure(__FILE__, __LINE__, e);
  CHECK(map.has_value());
  if (!map) return;
  CHECK(map->warnings.empty());
  CHECK(map->def.name == "Derelict Sample");
  CHECK(map->def.spawns.count("start") == 1);
  CHECK(map->mesh.triangleCount() > 100);
  CHECK(map->def.meshCollider && !map->def.meshCollider->empty());
  CHECK(!map->mesh.images.empty());
  bool normal = false, roughness = false, emissive = false;
  for (const auto& m : map->mesh.materials) {
    normal |= m.normalImage >= 0;
    roughness |= m.metalRoughImage >= 0;
    emissive |= m.emissive.x + m.emissive.y + m.emissive.z > 0.01f;
  }
  CHECK(normal);
  CHECK(roughness);
  CHECK(emissive);
  bool lamp = false;
  for (const auto& l : map->def.lights) lamp |= l.range > 0;
  CHECK(lamp);
  bool exitFound = false, logFound = false;
  for (const auto& it : map->def.interactables) {
    exitFound |= it.action == "leaveMap";
    logFound |= it.action.rfind("say:", 0) == 0 && it.action.size() > 10;
  }
  CHECK(exitFound);
  CHECK(logFound);
  bool enemy = false, loot = false;
  for (const auto& m : map->markers) {
    enemy |= m.type == "enemy_spawn";
    loot |= m.type == "loot";
  }
  CHECK(enemy);
  CHECK(loot);
}

TEST(map_sample_player_stands_on_the_floor_and_walks) {
  const auto map = loadMap(std::filesystem::path(FH_TEST_CONTENT_DIR) / "maps" / "derelict_sample.glb", nullptr);
  CHECK(map.has_value());
  if (!map) return;
  CharacterParams p;
  CharacterState s;
  const InteriorSpawn start = map->def.spawns.at("start");
  s.feet = start.position;
  s.yaw = radians(start.yawDeg);
  const float y0 = s.feet.y;
  walk(s, p, *map->def.meshCollider, {}, 2.0f);
  CHECK(std::fabs(s.feet.y - y0) < 0.6f);  // did not fall through
  CHECK(s.grounded);
  const Vec3 standing = s.feet;
  walk(s, p, *map->def.meshCollider, {0, 1}, 1.0f);
  CHECK(length(s.feet - standing) > 1.0f);  // not stuck inside anything
}

TEST(map_without_a_player_start_is_a_clear_error) {
  std::vector<std::string> errors;
  const auto map = loadMap(std::filesystem::path(FH_TEST_CONTENT_DIR) / "models" / "interior" / "platform_large.glb", &errors);
  CHECK(!map.has_value());
  CHECK(!errors.empty());
  CHECK(errors.front().find("Player Start") != std::string::npos);
}
