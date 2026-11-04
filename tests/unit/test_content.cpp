#include "engine/assets/asset_manifest.h"
#include "game/content/scene_def.h"
#include "game/content/ship_def.h"
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
  auto scene = loadSceneDef(*m, "scene.stage1_showcase", &errors);
  CHECK(scene.has_value());
  if (scene) {
    CHECK(!scene->objects.empty());
    for (const auto& o : scene->objects) CHECK(loadShipDef(*m, o.ship, &errors).has_value());
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
