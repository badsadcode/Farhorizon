// Stage 4 exit criterion: complete a profitable mining trip, save/reload, and verify economy,
// cargo and world deviations are preserved with no duplication.
#include <cmath>
#include <filesystem>
#include <functional>
#include <sstream>

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

std::unique_ptr<GameSession> makeSession(std::optional<SystemPosition> start, SceneDef* sceneOut = nullptr) {
  std::vector<std::string> errors;
  auto m = AssetManifest::load(FH_TEST_CONTENT_DIR, &errors);
  if (!m) return nullptr;
  auto scene = loadSceneDef(*m, "scene.stage2_flight", &errors);
  auto ship = loadShipDef(*m, "ship.sparrow", &errors);
  auto db = loadCommodities(*m, "economy.commodities", &errors);
  if (!scene || !ship || !db) return nullptr;
  auto s = std::make_unique<GameSession>(buildSpaceWorld(*scene, *ship, {}, start));
  s->setShipInterior(ship->interior);
  for (const char* id : {"interior.sparrow", "interior.kestrel_hangar"})
    if (auto d = loadInteriorDef(*m, id, &errors)) s->addInterior(id, *d);
  s->setEconomy(*db, scene->startingCredits);
  for (const auto& p : scene->props)
    if (p.docking && !p.docking->market.empty())
      if (auto md = loadMarketDef(*m, p.docking->market, s->commodities(), &errors)) s->addMarket(p.docking->market, *md);
  if (sceneOut) *sceneOut = *scene;
  for (auto& e : errors) std::fprintf(stderr, "    session: %s\n", e.c_str());
  return s;
}

bool runUntil(GameSession& s, float seconds, const std::function<bool()>& done) {
  for (int i = 0; i < int(seconds / kDt); ++i) {
    s.fixedUpdate(kDt);
    if (done()) return true;
  }
  return false;
}

// Teleports the ship next to the given asteroid, facing it, at rest.
void parkNextTo(GameSession& s, std::uint32_t ai, float standoff) {
  SpaceWorld& w = s.world();
  const Asteroid& a = w.asteroids()[ai];
  Vec3 away = normalize(w.player().state.position - a.local);
  if (length(away) < 0.5f) away = {0, 1, 0};
  auto& st = w.player().state;
  st.position = a.local + away * (a.radius + standoff);
  st.velocity = {};
  st.angularVelocity = {};
  // Face the asteroid: rotate -Z onto the direction to the target.
  const Vec3 f = normalize(a.local - st.position);
  const Vec3 axis = cross(Vec3{0, 0, -1}, f);
  const float ang = std::acos(std::clamp(dot(Vec3{0, 0, -1}, f), -1.0f, 1.0f));
  st.orientation = length(axis) > 1e-5f ? Quat::fromAxisAngle(axis, ang) : Quat{};
  w.player().previous = st;
}
}  // namespace

TEST(mining_yields_chunks_scoops_and_depletes) {
  auto s = makeSession(std::nullopt);
  CHECK(s != nullptr);
  if (!s) return;
  SpaceWorld& w = s->world();
  // Pick a mineable asteroid of moderate size.
  std::uint32_t pick = 0;
  for (std::uint32_t i = 0; i < w.asteroids().size(); ++i)
    if (!w.asteroids()[i].resource.empty() && w.asteroids()[i].radius > 15 && w.asteroids()[i].radius < 30) pick = i;
  // Move the floating origin near it so the test runs at local precision.
  const SystemPosition target = w.frame().toSystem(w.asteroids()[pick].local);
  s = makeSession(SystemPosition{target.metres + DVec3{0, w.asteroids()[pick].radius + 60.0, 0}});
  SpaceWorld& w2 = s->world();
  parkNextTo(*s, pick, 40.0f);
  w2.targetNearestAsteroid();
  CHECK(w2.target() && w2.target()->index == pick);
  const int y0 = w2.asteroids()[pick].yield;
  CHECK(y0 > 0);
  s->mining().startScan(pick, w2.simulationTime());
  s->setMiningTrigger(true);
  const bool got = runUntil(*s, 30.0f, [&] { return s->cargo().usedTons() >= 5.0f; });
  CHECK(got);
  CHECK(s->mining().isScanned(pick));
  const Asteroid& a = w2.asteroids()[pick];
  const int inHold = s->cargo().units(a.resource);
  CHECK(inHold >= 5);
  CHECK(y0 - a.yield >= inHold);                       // never more cargo than mined
  CHECK(int(s->mining().chunks().size()) <= s->mining().params().maxChunks);
  // Not facing the rock: no beam.
  w2.player().state.orientation = Quat::fromAxisAngle({0, 1, 0}, kPi);
  s->fixedUpdate(kDt);
  CHECK(!s->mining().beamActive());
}

TEST(save_rejects_corrupt_and_unsupported_files) {
  std::string err;
  CHECK(!deserializeSave("{ this is not json", &err).has_value());
  CHECK(!err.empty());
  CHECK(!deserializeSave(R"({"schemaVersion": 99})", &err).has_value());
  CHECK(err.find("unsupported") != std::string::npos);
  CHECK(!deserializeSave(R"({"schemaVersion": 1, "sceneId": "x"})", &err).has_value());
}

TEST(stage4_profitable_mining_trip_save_reload) {
  SceneDef scene;
  auto s = makeSession(std::nullopt, &scene);
  CHECK(s != nullptr);
  if (!s) return;
  // Start docked at Kestrel (as a new pilot would after the tutorial).
  CHECK(s->docking().forceDock(s->world()));
  const std::int64_t startCredits = s->credits();

  // Undock and "fly" to a rich asteroid (teleport stands in for the cruise).
  CHECK(s->docking().undock(s->world()));
  CHECK(runUntil(*s, 6.0f, [&] { return s->docking().state() == DockState::Undocked; }));
  SpaceWorld& w = s->world();
  std::uint32_t pick = 0;
  int bestYield = 0;
  for (std::uint32_t i = 0; i < w.asteroids().size(); ++i)
    if (w.asteroids()[i].resource == "ore.nickel" && w.asteroids()[i].yield > bestYield &&
        w.asteroids()[i].radius < 40) {
      bestYield = w.asteroids()[i].yield;
      pick = i;
    }
  CHECK(bestYield >= 12);
  parkNextTo(*s, pick, 40.0f);
  w.targetNearestAsteroid();
  s->setMiningTrigger(true);
  CHECK(runUntil(*s, 60.0f, [&] { return s->cargo().freeTons() < 1.0f; }));
  s->setMiningTrigger(false);
  const int mined = s->cargo().units("ore.nickel");
  CHECK(mined >= 11);

  // Back to the station: autopilot docking from nearby.
  const Prop* station = nullptr;
  for (const auto& p : w.props())
    if (p.def.docking && p.def.id == "station.kestrel") station = &p;
  CHECK(station != nullptr);
  w.player().state.position = station->local + Vec3{300, 250, 400};
  w.player().state.velocity = {};
  w.player().previous = w.player().state;
  CHECK(s->docking().request(w));
  CHECK(runUntil(*s, 3.0f, [&] { return s->docking().state() == DockState::ApproachAuthorized; }));
  s->docking().toggleAutopilot();
  CHECK(runUntil(*s, 240.0f, [&] { return s->docking().state() == DockState::Docked; }));

  // Sell everything: profitable trip.
  const TradeResult sold = s->trade("ore.nickel", -mined);
  CHECK(sold.units == mined);
  CHECK(s->credits() > startCredits);
  // Buy a little cargo so the save has some.
  CHECK(s->trade("goods.rations", 3).units == 3);

  // Save -> file -> fresh session -> load.
  const SaveGame saved = captureSave(*s, scene.id);
  const auto path = std::filesystem::temp_directory_path() / "farhorizon_test_save.json";
  std::string err;
  CHECK(writeSaveFile(path, saved, &err));
  CHECK(writeSaveFile(path, saved, &err));  // second write creates the backup
  {
    auto bak = path;
    bak += ".bak";
    CHECK(std::filesystem::exists(bak));
  }
  auto loaded = readSaveFile(path, &err);
  CHECK(loaded.has_value());
  if (!loaded) return;
  auto s2 = makeSession(SystemPosition{loaded->shipPosition});
  CHECK(s2 != nullptr);
  if (!s2) return;
  CHECK(applySave(*s2, *loaded, &err));
  CHECK(s2->credits() == s->credits());
  CHECK(s2->cargo().contents() == s->cargo().contents());
  CHECK(s2->docking().state() == DockState::Docked);
  CHECK(s2->location() == Location::Cockpit);
  CHECK(s2->world().asteroids()[pick].yield == s->world().asteroids()[pick].yield);
  Market* m1 = s->dockedMarket();
  Market* m2 = s2->dockedMarket();
  CHECK(m1 && m2);
  if (m1 && m2) CHECK(m1->stocks() == m2->stocks());
  // Idempotent: saving the reloaded session yields the same snapshot (no duplication on reload).
  const std::string a = serializeSave(saved);
  const std::string b = serializeSave(captureSave(*s2, scene.id));
  CHECK(a == b);
  if (a != b) {
    std::istringstream sa(a), sb(b);
    std::string la, lb;
    while (std::getline(sa, la) && std::getline(sb, lb))
      if (la != lb) std::fprintf(stderr, "    diff: '%s' vs '%s'\n", la.c_str(), lb.c_str());
  }
  // Reloading twice does not duplicate cargo either.
  auto s3 = makeSession(SystemPosition{loaded->shipPosition});
  CHECK(applySave(*s3, *loaded, &err));
  CHECK(s3->cargo().units("goods.rations") == 3);
  std::filesystem::remove(path);
}

TEST(save_on_foot_restores_location) {
  auto s = makeSession(std::nullopt);
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->docking().forceDock(s->world()));
  CHECK(s->leaveSeat().ok);
  CHECK(s->perform("exitShip").ok);
  const SaveGame saved = captureSave(*s, "scene.stage2_flight");
  CHECK(saved.location == "station");
  std::string err;
  auto parsed = deserializeSave(serializeSave(saved), &err);
  CHECK(parsed.has_value());
  auto s2 = makeSession(SystemPosition{saved.shipPosition});
  CHECK(applySave(*s2, *parsed, &err));
  CHECK(s2->location() == Location::StationInterior);
  CHECK(s2->docking().state() == DockState::Docked);
}
