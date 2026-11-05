// Walking around inside the ship while it flies (roadmap 4.2).
#include <cmath>

#include "engine/assets/asset_manifest.h"
#include "game/content/interior_def.h"
#include "game/economy/economy.h"
#include "game/persistence/save_game.h"
#include "game/session/game_session.h"
#include "game/session/sim_player.h"
#include "game/session/world_builder.h"
#include "test_framework.h"

using namespace fh;

namespace {
constexpr float kDt = 1.0f / 60.0f;

std::unique_ptr<GameSession> flyingSession() {
  std::vector<std::string> errors;
  auto m = AssetManifest::load(FH_TEST_CONTENT_DIR, &errors);
  if (!m) return nullptr;
  auto scene = loadSceneDef(*m, "scene.stage2_flight", &errors);
  auto ship = loadShipDef(*m, "ship.sparrow", &errors);
  auto db = loadCommodities(*m, "economy.commodities", &errors);
  if (!scene || !ship || !db) return nullptr;
  auto s = std::make_unique<GameSession>(buildSpaceWorld(*scene, *ship, {}, std::nullopt));
  s->setShipInterior(ship->interior);
  auto def = loadInteriorDef(*m, ship->interior, &errors);
  if (!def) return nullptr;
  s->addInterior(ship->interior, *def);
  s->setEconomy(*db, 500);
  for (auto& e : errors) std::fprintf(stderr, "    flyingSession: %s\n", e.c_str());
  return s;
}

// Game coordinates from the Blender plan in tools/blender/build_ship_interior.py.
constexpr Vec3 kAirlock{2.0f, 0.0f, -4.2f};
constexpr Vec3 kEngineRoom{-2.0f, 0.0f, 7.0f};
constexpr Vec3 kBunks{-0.5f, 2.8f, 3.0f};
constexpr Vec3 kCockpit{0.8f, 2.8f, -7.0f};
}  // namespace

TEST(ship_walk_you_can_leave_the_seat_in_flight_and_the_ship_brakes) {
  auto s = flyingSession();
  CHECK(s != nullptr);
  if (!s) return;
  s->world().player().state.velocity = s->world().player().state.orientation.forward() * 80.0f;
  CHECK(s->freeFlight());
  CHECK(s->leaveSeat().ok);
  CHECK(s->location() == Location::ShipInterior);
  CHECK(s->notice().find("brakes") != std::string::npos);
  for (int i = 0; i < 60 * 40; ++i) {
    s->world().player().input = FlightInput{};  // what the app does when nobody is at the controls
    s->fixedUpdate(kDt);
  }
  CHECK(length(s->world().player().state.velocity) < 2.0f);
}

TEST(ship_walk_the_airlock_stays_shut_in_flight) {
  auto s = flyingSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->leaveSeat().ok);
  CHECK(!s->perform("exitShip").ok);
  CHECK(s->location() == Location::ShipInterior);
}

TEST(ship_walk_you_stand_still_aboard_while_the_ship_moves_and_can_sit_back_down) {
  auto s = flyingSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->leaveSeat().ok);
  for (int i = 0; i < 30; ++i) s->fixedUpdate(kDt);  // settle on the floor
  const Vec3 standing = s->character().feet;
  PlayerShip& ship = s->world().player();
  ship.state.velocity = ship.state.orientation.forward() * 300.0f;
  for (int i = 0; i < 120; ++i) s->fixedUpdate(kDt);
  CHECK(length(s->character().feet - standing) < 0.02f);
  CHECK(s->perform("takeSeat").ok);
  CHECK(s->location() == Location::Cockpit);
  CHECK(s->player(s->localPlayer())->pilot);
}

TEST(ship_walk_every_part_of_both_decks_can_be_reached_on_foot) {
  auto s = flyingSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->leaveSeat().ok);
  SimPlayer walker(*s, s->localPlayer());
  const float startDeck = s->character().feet.y;
  CHECK(startDeck > 2.5f);  // the seat is on the upper deck
  for (const Vec3& goal : {kBunks, kAirlock, kEngineRoom, kCockpit}) {
    walker.walkTo(goal, 1.0f);
    for (int i = 0; i < 60 * 45 && !walker.arrived(); ++i) {
      walker.step();
      s->fixedUpdate(kDt);
    }
    if (!walker.arrived() || walker.gaveUp()) {
      const Vec3 at = s->character().feet;
      std::fprintf(stderr, "    stuck at %.2f %.2f %.2f, going to %.2f %.2f %.2f\n", at.x, at.y, at.z, goal.x, goal.y, goal.z);
    }
    CHECK(walker.arrived());
    CHECK(!walker.gaveUp());
    CHECK(std::fabs(s->character().feet.y - goal.y) < 0.4f);
  }
}

TEST(ship_walk_a_save_made_walking_aboard_in_flight_loads_there) {
  auto s = flyingSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->leaveSeat().ok);
  std::string error;
  const auto parsed = deserializeSave(serializeSave(captureSave(*s, "scene.stage2_flight")), &error);
  CHECK(parsed.has_value());
  if (!parsed) return;
  auto t = flyingSession();
  CHECK(t != nullptr);
  if (!t) return;
  CHECK(applySave(*t, *parsed, &error));
  CHECK(t->location() == Location::ShipInterior);
  CHECK(!t->parked());
}

TEST(ship_walk_a_crash_throws_you_about) {
  auto s = flyingSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->leaveSeat().ok);
  for (int i = 0; i < 30; ++i) s->fixedUpdate(kDt);
  const Vec3 before = s->character().feet;
  PlayerShip& ship = s->world().player();
  ship.lastImpactSpeed = 30.0f;
  ++ship.collisions;
  s->fixedUpdate(kDt);
  CHECK(s->takeShipJolt() > 0.5f);
  CHECK(s->takeShipJolt() == 0.0f);
  for (int i = 0; i < 20; ++i) s->fixedUpdate(kDt);
  CHECK(s->character().feet.z < before.z - 0.2f);  // towards the bow
}

TEST(ship_walk_nobody_at_the_helm_does_not_stop_a_fight) {
  auto s = flyingSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->combatActive());
  CHECK(s->leaveSeat().ok);
  CHECK(s->combatActive());
}

TEST(ship_walk_being_destroyed_while_walking_ends_up_towed_in) {
  auto s = flyingSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->leaveSeat().ok);
  s->combat().player().hull = 0;
  s->fixedUpdate(kDt);
  CHECK(s->parked());
  CHECK(s->location() == Location::ShipInterior);
  CHECK(s->combat().player().hull > 0);
}
