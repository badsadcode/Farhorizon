#include <cstring>

#include "engine/assets/gltf_loader.h"
#include "game/world/asteroid_gen.h"
#include "game/world/space_world.h"
#include "test_framework.h"

using namespace fh;

static constexpr double kAU = 149597870700.0;

TEST(asteroid_mesh_is_valid_and_deterministic) {
  AsteroidMeshParams p;
  p.seed = 12;
  const MeshData a = generateAsteroidMesh(p);
  const MeshData b = generateAsteroidMesh(p);
  CHECK(a.triangleCount() == 320u);
  CHECK(a.vertices.size() == b.vertices.size());
  CHECK(std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(MeshVertex)) == 0);
  float maxR = 0;
  for (const auto& v : a.vertices) {
    maxR = std::max(maxR, length(v.position));
    CHECK_NEAR(length(v.normal), 1.0, 1e-3);
    CHECK(dot(v.normal, v.position) > -0.05f);  // normals face outward
  }
  CHECK_NEAR(maxR, 1.0, 1e-4);
  p.seed = 13;
  const MeshData c = generateAsteroidMesh(p);
  CHECK(std::memcmp(a.vertices.data(), c.vertices.data(), sizeof(MeshVertex) * 3) != 0);
}

TEST(asteroid_field_respects_parameters) {
  AsteroidFieldParams p;
  p.seed = 4417;
  p.count = 500;
  p.radius = 3000;
  p.minScale = 5;
  p.maxScale = 75;
  p.variants = 6;
  const auto f = generateAsteroidField(p);
  CHECK(f.size() == 500u);
  for (const auto& a : f) {
    CHECK(a.radius >= 5.0f && a.radius <= 75.0f);
    CHECK(a.variant < 6u);
    CHECK(length(a.offset) < 3000.0f * 2.0f);
  }
  const auto g = generateAsteroidField(p);
  CHECK(std::memcmp(&f[0], &g[0], sizeof(AsteroidInstance)) == 0);
}

TEST(gltf_sockets_are_imported_in_mesh_space) {
  auto mesh = loadGltfMesh(std::filesystem::path(FH_TEST_DATA_DIR) / "socket_test.gltf");
  CHECK(mesh.has_value());
  if (!mesh) return;
  CHECK(mesh->sockets.size() == 2u);  // "decoration" is not a socket
  const MeshSocket* pilot = nullptr;
  const MeshSocket* left = nullptr;
  for (const auto& s : mesh->sockets) {
    if (s.name == "socket_pilot") pilot = &s;
    if (s.name == "hardpoint_left") left = &s;
  }
  CHECK(pilot && left);
  if (!pilot || !left) return;
  // Parent: translate (10,0,0), scale 2.  socket_pilot local (0,1,-2) -> (10,2,-4).
  CHECK_VEC_NEAR(pilot->position, Vec3(10, 2, -4), 1e-4);
  CHECK_VEC_NEAR(left->position, Vec3(8, 0, 0), 1e-4);
  // hardpoint_left rotated 90 deg about +Y: its forward (-Z) points to -X.
  CHECK_VEC_NEAR(left->rotation.forward(), Vec3(-1, 0, 0), 1e-4);
}

namespace {
SpaceWorld makeWorld() {
  SpaceWorld w;
  w.setOrigin({{kAU, 0, 0}}, 4096.0);
  w.player().collisionRadius = 5.0f;
  Prop station;
  station.def.id = "station.test";
  station.def.name = "Test Station";
  station.system = {{kAU, 0, -1000}};
  station.collisionRadius = 100.0f;
  w.addProp(station);
  AsteroidFieldRuntime field;
  field.def.name = "Field";
  w.addAsteroidField(field);
  Asteroid a;
  a.system = {{kAU + 300, 0, 0}};
  a.radius = 20.0f;
  w.addAsteroid(a);
  return w;
}
}  // namespace

TEST(world_collision_pushes_ship_out_and_bounces) {
  SpaceWorld w = makeWorld();
  w.player().input.throttle = 0.5f;
  // Fly straight at the station for long enough to hit it.
  for (int i = 0; i < 60 * 20; ++i) w.fixedUpdate(1.0f / 60.0f);
  const Vec3 toStation = w.props()[0].local - w.player().state.position;
  CHECK(length(toStation) >= 100.0f + 5.0f - 0.01f);  // never inside
  CHECK(w.player().collisions >= 1u);
}

TEST(world_rebase_keeps_everything_consistent) {
  SpaceWorld w = makeWorld();
  auto& s = w.player().state;
  s.position = {5000, 0, 0};
  s.velocity = {100, 0, 0};
  w.player().previous = s;
  const SystemPosition before = w.frame().toSystem(s.position);
  const SystemPosition stationBefore = w.frame().toSystem(w.props()[0].local);
  CHECK(w.rebaseIfNeeded().has_value());
  CHECK_VEC_NEAR(w.player().state.position, Vec3(0, 0, 0), 1e-3);
  const SystemPosition after = w.frame().toSystem(w.player().state.position);
  CHECK_NEAR(after.metres.x, before.metres.x, 1e-3);
  const SystemPosition stationAfter = w.frame().toSystem(w.props()[0].local);
  CHECK_NEAR(stationAfter.metres.z, stationBefore.metres.z, 1e-3);
  CHECK_VEC_NEAR(w.playerRenderTransform(0.5f).position, Vec3(0, 0, 0), 1e-3);  // no interpolation jump
  CHECK(!w.rebaseIfNeeded().has_value());
}

TEST(world_targeting_cycles_props_then_asteroids) {
  SpaceWorld w = makeWorld();
  w.cycleTarget();
  CHECK(w.target().has_value() && w.target()->kind == TargetRef::Kind::Prop);
  w.cycleTarget();
  CHECK(w.target()->kind == TargetRef::Kind::Asteroid);
  w.cycleTarget();
  CHECK(w.target()->kind == TargetRef::Kind::Prop);  // wraps
  w.targetNearestAsteroid();
  auto info = w.targetInfo();
  CHECK(info.has_value());
  if (info) {
    CHECK_NEAR(info->distance, 300.0, 1e-3);
    CHECK(info->marker == "asteroid");
  }
  w.player().state.velocity = {50, 0, 0};
  CHECK_NEAR(w.targetInfo()->closingSpeed, 50.0, 1e-3);
  w.clearTarget();
  CHECK(!w.target().has_value());
}
