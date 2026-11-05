#include <cmath>
#include <functional>

#include "engine/assets/asset_manifest.h"
#include "game/content/interior_def.h"
#include "game/economy/economy.h"
#include "game/session/game_session.h"
#include "game/session/sim_player.h"
#include "game/session/world_builder.h"
#include "test_framework.h"

using namespace fh;

namespace {
constexpr float kDt = 1.0f / 60.0f;

struct Fixture {
  AssetManifest manifest;
  SceneDef scene;
  ShipDef ship;
  std::unique_ptr<GameSession> session;
};

// Real content, no GPU: the same scene, ship and interiors the game uses.
std::optional<Fixture> makeFixture(const DVec3& startOffsetFromStation) {
  std::vector<std::string> errors;
  auto m = AssetManifest::load(FH_TEST_CONTENT_DIR, &errors);
  if (!m) return std::nullopt;
  auto scene = loadSceneDef(*m, "scene.stage2_flight", &errors);
  auto ship = loadShipDef(*m, "ship.sparrow", &errors);
  if (!scene || !ship) return std::nullopt;
  const PropDef* station = nullptr;
  for (const auto& p : scene->props)
    if (p.docking && p.id == "station.kestrel") station = &p;
  if (!station) return std::nullopt;
  Fixture f{*m, *scene, *ship, nullptr};
  const SystemPosition start{station->position.metres + startOffsetFromStation};
  f.session = std::make_unique<GameSession>(buildSpaceWorld(*scene, *ship, {}, start));
  f.session->setShipInterior(ship->interior);
  for (const char* id : {"interior.sparrow", "interior.kestrel_hangar"}) {
    auto def = loadInteriorDef(*m, id, &errors);
    if (!def) return std::nullopt;
    f.session->addInterior(id, *def);
  }
  auto db = loadCommodities(*m, "economy.commodities", &errors);
  if (!db) return std::nullopt;
  f.session->setEconomy(*db, scene->startingCredits);
  for (const auto& p : scene->props)
    if (p.docking && !p.docking->market.empty())
      if (auto md = loadMarketDef(*m, p.docking->market, f.session->commodities(), &errors))
        f.session->addMarket(p.docking->market, *md);
  for (auto& e : errors) std::fprintf(stderr, "    fixture: %s\n", e.c_str());
  return f;
}

bool runUntil(GameSession& s, float maxSeconds, const std::function<bool()>& done) {
  for (int i = 0; i < int(maxSeconds / kDt); ++i) {
    s.fixedUpdate(kDt);
    const Vec3 p = s.world().player().state.position;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    // Invariant: the player can only be out of the cockpit while docked.
    if (s.location() != Location::Cockpit && s.docking().state() != DockState::Docked) return false;
    if (done()) return true;
  }
  return false;
}

// Walks the on-foot character to (x, z) with real physics; fails if it gets stuck.
// Across the ship's decks, the way a player would go: by the stair.
bool walkAboard(GameSession& s, const Vec3& goal) {
  SimPlayer walker(s, s.localPlayer());
  walker.walkTo(goal, 0.9f);
  for (int i = 0; i < 60 * 40 && !walker.arrived(); ++i) {
    walker.step();
    s.fixedUpdate(kDt);
  }
  return walker.arrived() && !walker.gaveUp();
}

bool walkTo(GameSession& s, float x, float z, float maxSeconds = 20.0f) {
  return runUntil(s, maxSeconds, [&] {
    CharacterState& c = s.character();
    const float dx = x - c.feet.x, dz = z - c.feet.z;
    if (std::sqrt(dx * dx + dz * dz) < 0.3f) {
      s.characterInput().move = {};
      return true;
    }
    c.yaw = std::atan2(dx, -dz);  // forward = (sin yaw, 0, -cos yaw)
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
}  // namespace

TEST(docking_refuses_out_of_range_and_invalid_actions) {
  auto f = makeFixture({20000, 0, 0});
  CHECK(f.has_value());
  if (!f) return;
  GameSession& s = *f->session;
  CHECK(!s.docking().request(s.world()));
  CHECK(s.docking().state() == DockState::Undocked);
  CHECK(!s.docking().undock(s.world()));
  CHECK(!s.perform("exitShip").ok);         // not aboard on foot
  CHECK(s.location() == Location::Cockpit);
}

TEST(docking_manual_capture_requires_tolerances) {
  auto f = makeFixture({300, 400, 300});
  CHECK(f.has_value());
  if (!f) return;
  GameSession& s = *f->session;
  CHECK(s.docking().request(s.world()));
  CHECK(runUntil(s, 3.0f, [&] { return s.docking().state() == DockState::ApproachAuthorized; }));
  // Teleport next to the bay but too fast and misaligned: no capture.
  const Transform bay = s.docking().bayTransform(s.world());
  auto& st = s.world().player().state;
  st.position = bay.position + bay.rotation.up() * 8.0f;
  st.orientation = normalize(bay.rotation * Quat::fromAxisAngle({0, 1, 0}, radians(40)));
  st.velocity = bay.rotation.up() * -20.0f;
  s.fixedUpdate(kDt);
  CHECK(s.docking().state() == DockState::ApproachAuthorized);
  CHECK(!s.docking().approach().speedOk || !s.docking().approach().angleOk);
  // Aligned, slow, close: capture, then docked after the settle time.
  st.position = bay.position + bay.rotation.up() * 6.0f;
  st.orientation = bay.rotation;
  st.velocity = bay.rotation.up() * -2.0f;
  s.world().player().previous = st;
  CHECK(runUntil(s, 0.5f, [&] { return s.docking().state() == DockState::Capture; }));
  CHECK(runUntil(s, 4.0f, [&] { return s.docking().state() == DockState::Docked; }));
  CHECK_VEC_NEAR(s.world().player().state.position, s.docking().bayTransform(s.world()).position, 1e-2);
}

// Stage 3 exit criterion: fly -> dock -> leave seat -> exit ship -> return -> launch, 20 times.
TEST(stage3_dock_walk_launch_loop_20_times) {
  auto f = makeFixture({600, 300, 900});
  CHECK(f.has_value());
  if (!f) return;
  GameSession& s = *f->session;
  for (int cycle = 0; cycle < 20; ++cycle) {
    // Fly in and dock with the approach autopilot.
    CHECK(s.docking().request(s.world()));
    CHECK(runUntil(s, 3.0f, [&] { return s.docking().state() == DockState::ApproachAuthorized; }));
    CHECK(s.docking().toggleAutopilot());
    const bool docked = runUntil(s, 240.0f, [&] { return s.docking().state() == DockState::Docked; });
    CHECK(docked);
    if (!docked) {
      std::fprintf(stderr, "    cycle %d: stuck in %s, dist %.1f\n", cycle, dockStateName(s.docking().state()),
                   s.docking().approach().distance);
      return;
    }
    // Leave the seat and walk to the airlock, a deck below.
    CHECK(s.leaveSeat().ok);
    CHECK(s.location() == Location::ShipInterior);
    const Interactable* airlock = find(s, "exitShip");
    CHECK(airlock != nullptr);
    if (!airlock) return;
    CHECK(walkAboard(s, airlock->position - Vec3{0.4f, 1.0f, 0.0f}));
    lookAt(s, airlock->position);
    CHECK(s.focusedInteractable() == airlock);
    CHECK(s.interact().ok);
    CHECK(s.location() == Location::StationInterior);
    // Walk around the hangar to the services terminal and back to the ship.
    const Interactable* terminal = find(s, "terminal");
    const Interactable* board = find(s, "boardShip");
    CHECK(terminal && board);
    if (!terminal || !board) return;
    const Vec3 spawn = s.character().feet;
    CHECK(walkTo(s, spawn.x, terminal->position.z - 1.5f, 30.0f));
    CHECK(walkTo(s, terminal->position.x, terminal->position.z - 1.5f, 30.0f));
    lookAt(s, terminal->position);
    CHECK(s.interact().ok);
    CHECK(s.consumeMarketRequest());
    CHECK(walkTo(s, spawn.x, terminal->position.z - 1.5f, 30.0f));
    CHECK(walkTo(s, board->position.x + 1.2f, board->position.z, 30.0f));
    lookAt(s, board->position);
    CHECK(s.interact().ok);
    CHECK(s.location() == Location::ShipInterior);
    // Back to the seat and undock.
    const Interactable* seat = find(s, "takeSeat");
    CHECK(seat != nullptr);
    if (!seat) return;
    CHECK(walkAboard(s, seat->position + Vec3{0.9f, -1.0f, 0.5f}));
    lookAt(s, seat->position);
    CHECK(s.interact().ok);
    CHECK(s.location() == Location::Cockpit);
    CHECK(s.docking().undock(s.world()));
    CHECK(runUntil(s, 6.0f, [&] { return s.docking().state() == DockState::Undocked; }));
    // Fly away from the station for a while, then the next cycle requests docking again.
    s.world().player().input = FlightInput{};
    s.world().player().input.throttle = 0.5f;
    CHECK(runUntil(s, 6.0f, [] { return false; }) == false);  // just simulate
    s.world().player().input.throttle = 0.0f;
    CHECK(s.world().player().collisions == 0u);
  }
}
