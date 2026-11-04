// Stage 5: travel drive, terrain walking, and the landing/launch exit criterion.
#include <cmath>
#include <functional>

#include "engine/assets/asset_manifest.h"
#include "game/content/interior_def.h"
#include "game/economy/economy.h"
#include "game/persistence/save_game.h"
#include "game/session/game_session.h"
#include "game/session/world_builder.h"
#include "test_framework.h"

using namespace fh;

namespace {
constexpr float kDt = 1.0f / 60.0f;

std::unique_ptr<GameSession> session5(std::optional<SystemPosition> start = std::nullopt) {
  std::vector<std::string> errors;
  auto m = AssetManifest::load(FH_TEST_CONTENT_DIR, &errors);
  if (!m) return nullptr;
  auto scene = loadSceneDef(*m, "scene.stage2_flight", &errors);
  auto ship = loadShipDef(*m, "ship.sparrow", &errors);
  auto db = loadCommodities(*m, "economy.commodities", &errors);
  if (!scene || !ship || !db) return nullptr;
  auto s = std::make_unique<GameSession>(buildSpaceWorld(*scene, *ship, {}, start));
  s->setShipInterior(ship->interior);
  for (const char* id : {"interior.sparrow", "interior.kestrel_hangar", "interior.gale_ridge"})
    if (auto d = loadInteriorDef(*m, id, &errors)) s->addInterior(id, *d);
  s->setEconomy(*db, scene->startingCredits);
  for (const char* id : {"market.kestrel", "market.gale_ridge"})
    if (auto md = loadMarketDef(*m, id, s->commodities(), &errors)) s->addMarket(id, *md);
  for (auto& e : errors) std::fprintf(stderr, "    session5: %s\n", e.c_str());
  return s;
}

bool runUntil(GameSession& s, float seconds, const std::function<bool()>& done) {
  for (int i = 0; i < int(seconds / kDt); ++i) {
    s.fixedUpdate(kDt);
    const Vec3 p = s.world().player().state.position;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    if (length(p) > 20000.0f) return false;  // the floating origin must keep the ship local
    if (s.location() != Location::Cockpit && !s.parked()) return false;
    if (done()) return true;
  }
  return false;
}

bool walkTo(GameSession& s, float x, float z, float maxSeconds = 40.0f) {
  return runUntil(s, maxSeconds, [&] {
    CharacterState& c = s.character();
    const float dx = x - c.feet.x, dz = z - c.feet.z;
    if (std::sqrt(dx * dx + dz * dz) < 0.35f) {
      s.characterInput().move = {};
      return true;
    }
    c.yaw = std::atan2(dx, -dz);
    s.characterInput().move = {0, 1};
    return false;
  });
}

void lookAt(GameSession& s, const Vec3& target) {
  CharacterState& c = s.character();
  const Vec3 to = target - c.eye(s.characterParams());
  c.yaw = std::atan2(to.x, -to.z);
  c.pitch = std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z));
}

const Interactable* find(const GameSession& s, const std::string& action) {
  for (const auto& i : s.activeInterior()->interactables)
    if (i.action == action) return &i;
  return nullptr;
}

std::optional<TargetRef> navTo(GameSession& s, TargetRef::Kind kind) {
  for (const auto& t : s.world().navDestinations())
    if (t.kind == kind) return t;
  return std::nullopt;
}

SystemPosition shipSys(GameSession& s) { return s.world().frame().toSystem(s.world().player().state.position); }
}  // namespace

TEST(terrain_is_walkable_and_cliffs_block) {
  TerrainParams p;
  p.seed = 9;
  p.size = 200;
  p.cellSize = 4;
  p.rimHeight = 60;
  p.rimWidth = 40;
  p.flatZones = {{{0, 0}, 30, 20, 0}};
  HeightField hf(p);
  CHECK_NEAR(hf.heightAt(0, 0), 0.0, 1e-4);
  CHECK(hf.heightAt(95, 0) > 20.0f);  // cliffs at the rim
  CHECK(hf.normalAt(0, 0).y > 0.99f);
  const MeshData m = hf.buildMesh();
  CHECK(m.triangleCount() == size_t(2 * 50 * 50));
  // Walk toward the rim: the character must stop at the cliff foot instead of climbing it.
  CharacterState c;
  c.feet = {0, 0.5f, 0};
  c.yaw = radians(90.0f);  // +X
  CharacterInput in;
  in.move = {0, 1};
  in.run = true;
  CharacterParams cp;
  for (int i = 0; i < 60 * 40; ++i) stepCharacter(c, in, cp, {}, kDt, &hf);
  CHECK(c.feet.x < 98.0f);
  CHECK(c.feet.y < 40.0f);
  CHECK(c.feet.y >= hf.heightAt(c.feet.x, c.feet.z) - 0.05f);  // never below the ground
}

TEST(travel_drive_rules_and_exact_arrival) {
  auto s = session5();
  CHECK(s != nullptr);
  if (!s) return;
  SpaceWorld& w = s->world();
  // Close to Kestrel: mass-locked.
  auto station = navTo(*s, TargetRef::Kind::Prop);
  auto site = navTo(*s, TargetRef::Kind::Site);
  CHECK(station && site);
  if (!site) return;
  w.player().state.position = w.props()[station->index].local + Vec3{0, 0, 800};
  w.player().previous = w.player().state;
  w.setTarget(*site);
  CHECK(!s->engageTravel());
  CHECK(s->travel().message().find("Mass-locked") != std::string::npos);
  // Move clear of everything, then jump to the site's orbit marker (~9,000 km).
  auto& st = w.player().state;
  st.position += Vec3{0, 4500, 0};
  w.player().previous = st;
  runUntil(*s, 0.1f, [] { return false; });
  CHECK(s->engageTravel());
  const DVec3 arrival = w.navArrivalPoint(*site);
  bool sawTransit = false;
  CHECK(runUntil(*s, 120.0f, [&] {
    sawTransit = sawTransit || s->travel().state() == TravelState::Transit;
    CHECK(s->travel().speed() <= w.player().def.travel.maxSpeed + 1.0f);
    return !s->travel().active();
  }));
  CHECK(sawTransit);
  CHECK(length(shipSys(*s).metres - arrival) < 50.0);  // exact arrival despite ~thousands of rebases
  CHECK(w.frame().generation() > 1000u);
  CHECK(!w.player().kinematic);
  // Too close for another jump.
  CHECK(!s->engageTravel());
}

// Stage 5 exit criterion: complete planet landing/launch cycles repeatedly, including after
// reload; no precision jitter or broken return destinations.
TEST(stage5_landing_launch_cycles_with_reload) {
  auto s = session5();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->docking().forceDock(s->world()));
  const std::int64_t startCredits = s->credits();
  for (int cycle = 0; cycle < 3; ++cycle) {
    SpaceWorld& w = s->world();
    // Buy supplies at Kestrel, undock, climb clear of the mass lock.
    CHECK(s->trade("goods.rations", 6).units == 6);
    CHECK(s->docking().undock(w));
    CHECK(runUntil(*s, 8.0f, [&] { return s->docking().state() == DockState::Undocked; }));
    w.player().state.position += Vec3{0, 3500, 0};
    w.player().previous = w.player().state;
    auto site = navTo(*s, TargetRef::Kind::Site);
    w.setTarget(*site);
    CHECK(s->engageTravel());
    CHECK(runUntil(*s, 120.0f, [&] { return !s->travel().active(); }));
    // Land.
    CHECK(s->requestLanding());
    CHECK(runUntil(*s, 40.0f, [&] { return s->landing().state() == LandState::Landed; }));
    CHECK(s->surfaceScene() != nullptr);
    // Walk to the supply shed over the terrain and trade.
    CHECK(s->leaveSeat().ok);
    CHECK(s->perform("exitShip").ok);
    CHECK(s->activeInteriorId() == "interior.gale_ridge");
    const Interactable* shed = find(*s, "terminal");
    const Interactable* board = find(*s, "boardShip");
    CHECK(shed && board);
    if (!shed || !board) return;
    const Vec3 spawn = s->character().feet;
    CHECK(walkTo(*s, spawn.x, 46.0f));
    CHECK(walkTo(*s, 0.0f, 46.0f));
    CHECK(walkTo(*s, 0.0f, shed->position.z - 1.4f));
    lookAt(*s, shed->position);
    CHECK(s->interact().ok);
    CHECK(s->consumeMarketRequest());
    const TradeResult sold = s->trade("goods.rations", -6);
    CHECK(sold.units == 6);
    CHECK(s->trade("ore.nickel", 6).units == 6);  // cheap ore for the trip home
    CHECK(walkTo(*s, 0.0f, 46.0f));
    CHECK(walkTo(*s, spawn.x, 46.0f));
    CHECK(walkTo(*s, board->position.x + 1.3f, board->position.z));
    lookAt(*s, board->position);
    CHECK(s->interact().ok);
    CHECK(s->perform("takeSeat").ok);

    if (cycle == 1) {  // save while landed and continue from a freshly loaded session
      const SaveGame saved = captureSave(*s, "scene.stage2_flight");
      CHECK(!saved.landedSite.empty());
      std::string err;
      auto parsed = deserializeSave(serializeSave(saved), &err);
      CHECK(parsed.has_value());
      auto s2 = session5(SystemPosition{parsed->shipPosition});
      CHECK(applySave(*s2, *parsed, &err));
      CHECK(s2->landing().state() == LandState::Landed);
      CHECK(s2->cargo().contents() == s->cargo().contents());
      CHECK(serializeSave(captureSave(*s2, "scene.stage2_flight")) == serializeSave(saved));
      s = std::move(s2);
    }

    // Launch: must come back to the site's orbit marker, moving away from the planet.
    SpaceWorld& w2 = s->world();
    const Planet& varga = w2.planets().at(s->landing().site()->planet);
    const DVec3 orbit = varga.siteOrbit(s->landing().site()->site);
    CHECK(s->launch());
    CHECK(runUntil(*s, 25.0f, [&] { return s->landing().state() == LandState::None; }));
    const double fromOrbit = length(shipSys(*s).metres - orbit);
    CHECK(fromOrbit < 2000.0);  // ~750 m climb during the ascent phase
    const double alt = length(shipSys(*s).metres - varga.def.center.metres) - varga.def.radius;
    CHECK(alt > varga.def.orbitAltitude);
    // Home to Kestrel, dock with the autopilot, sell the ore.
    auto station = navTo(*s, TargetRef::Kind::Prop);
    w2.setTarget(*station);
    CHECK(s->engageTravel());
    CHECK(runUntil(*s, 120.0f, [&] { return !s->travel().active(); }));
    CHECK(s->docking().request(w2));
    CHECK(runUntil(*s, 3.0f, [&] { return s->docking().state() == DockState::ApproachAuthorized; }));
    s->docking().toggleAutopilot();
    CHECK(runUntil(*s, 240.0f, [&] { return s->docking().state() == DockState::Docked; }));
    CHECK(s->trade("ore.nickel", -6).units == 6);
  }
  CHECK(s->credits() > startCredits);  // the supply run pays
  CHECK(s->world().player().collisions == 0u);
}
