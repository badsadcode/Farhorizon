#include "engine/assets/asset_manifest.h"
#include "game/content/scene_def.h"
#include "game/content/ship_def.h"
#include "game/content/visual_profile.h"
#include "test_framework.h"

using namespace fh;

TEST(ship_and_scene_content_validate) {
  std::vector<std::string> errors;
  auto m = AssetManifest::load(FH_TEST_CONTENT_DIR, &errors);
  CHECK(m.has_value());
  if (!m) return;
  auto ship = loadShipDef(*m, "ship.sparrow", &errors);
  CHECK(ship.has_value());
  if (ship) {
    CHECK(ship->cargoCapacityTons > 0);
    CHECK(ship->flight.maxCruiseSpeed > 0);
  }
  for (const char* id : {"scene.stage1_showcase", "scene.stage2_flight"}) {
    auto scene = loadSceneDef(*m, id, &errors);
    CHECK(scene.has_value());
    if (scene) CHECK(loadShipDef(*m, scene->player.ship, &errors).has_value());
  }
  auto stage2 = loadSceneDef(*m, "scene.stage2_flight", &errors);
  CHECK(stage2 && !stage2->props.empty() && !stage2->asteroidFields.empty());
  for (const char* id : {"bench.empty_space_cockpit", "bench.asteroid_field_chase"}) {
    auto b = loadBenchmarkDef(*m, id, &errors);
    CHECK(b.has_value());
    if (b) CHECK(loadSceneDef(*m, b->scene, &errors).has_value());
  }
  if (ship) {
    CHECK(ship->sockets.count("socket_pilot") == 1u);
    CHECK(ship->flight.boostEnergyCapacity > 0);
    CHECK_NEAR(ship->flight.maxAngularRate.x, radians(55.0f), 1e-5);
  }
  for (auto& e : errors) std::fprintf(stderr, "    content error: %s\n", e.c_str());
  CHECK(errors.empty());
}

TEST(ship_def_reports_precise_errors) {
  std::vector<std::string> errors;
  auto d = parseShipDef(R"({"id":"x","name":"X","mesh":"m","massTons":"heavy","cargoCapacityTons":1,
                            "maxHull":1,"maxShield":1})",
                        "ship 'x'", &errors);
  CHECK(!d.has_value());
  bool sawMass = false, sawFlight = false;
  for (auto& e : errors) {
    if (e.find("massTons") != std::string::npos) sawMass = true;
    if (e.find("flight") != std::string::npos) sawFlight = true;
  }
  CHECK(sawMass);
  CHECK(sawFlight);
}

TEST(visual_profiles_load_and_validate) {
  std::vector<std::string> errors;
  auto m = AssetManifest::load(FH_TEST_CONTENT_DIR, &errors);
  CHECK(m.has_value());
  if (!m) return;
  auto looks = loadVisualProfiles(*m, "visuals.post_profiles", &errors);
  CHECK(looks.has_value());
  if (!looks) return;
  CHECK(looks->indexOf("retro80s") >= 0);
  CHECK(looks->indexOf("clean") >= 0);
  CHECK(looks->indexOf(looks->defaultId) >= 0);
  CHECK(errors.empty());
  // Bad input is rejected with context.
  std::vector<std::string> e2;
  CHECK(!parseVisualProfiles(R"({"default":"x","profiles":[{"id":"y","name":"Y"}]})", "visuals 't'", &e2));
  CHECK(!parseVisualProfiles(R"({"default":"y","profiles":[{"id":"y","name":"Y","scanlines":3}]})", "visuals 't'", &e2));
  CHECK(e2.size() >= 2);
}

TEST(light_blink_pattern) {
  LightDef steady;
  CHECK_NEAR(steady.intensityAt(12.3), 1.0, 1e-6);
  LightDef blink;
  blink.blinkHz = 1.0f;
  blink.duty = 0.2f;
  CHECK(blink.intensityAt(0.1) > 0.5f);   // on, mid-pulse
  CHECK(blink.intensityAt(0.6) == 0.0f);  // off
  CHECK(blink.intensityAt(1.1) > 0.5f);   // periodic
}
