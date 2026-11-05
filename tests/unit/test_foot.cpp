// Fighting on foot: the gun, the crawler, and a list of players that co-op can fill later.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "game/content/map_def.h"
#include "game/content/scene_def.h"
#include "game/content/ship_def.h"
#include "game/economy/economy.h"
#include "game/foot/foot_combat.h"
#include "game/persistence/save_game.h"
#include "game/session/game_session.h"
#include "game/session/world_builder.h"
#include "test_framework.h"

using namespace fh;

namespace {

void addQuad(std::vector<MeshVertex>& v, std::vector<std::uint32_t>& idx, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
  const auto base = std::uint32_t(v.size());
  for (const Vec3& p : {a, b, c, d}) {
    MeshVertex m{};
    m.position = p;
    v.push_back(m);
  }
  for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + i);
}

// A big floor at y = 0, with an optional wall across it at z = wallZ.
TriangleCollider arena(bool wall = false, float wallZ = -5.0f) {
  std::vector<MeshVertex> v;
  std::vector<std::uint32_t> idx;
  addQuad(v, idx, {-50, 0, -50}, {-50, 0, 50}, {50, 0, 50}, {50, 0, -50});
  if (wall) addQuad(v, idx, {-50, 0, wallZ}, {50, 0, wallZ}, {50, 4, wallZ}, {-50, 4, wallZ});
  TriangleCollider c;
  c.build(v, idx);
  return c;
}

FootPlayer playerAt(Vec3 feet, Vec3 look = {0, 0, -1}) {
  FootPlayer p;
  p.feet = feet;
  p.eye = feet + Vec3{0, 1.65f, 0};
  p.look = normalize(look);
  return p;
}

// Aims from the eye at the middle of the first enemy.
void aimAt(FootPlayer& p, const FootEnemy& e, const FootCombat& f) {
  p.look = normalize(e.body.feet + Vec3{0, f.content().enemies[0].hitCentreHeight, 0} - p.eye);
}

int countEvents(const std::vector<FootEvent>& events, FootEvent::Type t) {
  int n = 0;
  for (const FootEvent& e : events) n += e.type == t;
  return n;
}

constexpr float kDt = 1.0f / 60.0f;

}  // namespace

TEST(collider_raycast_finds_the_nearest_surface_and_misses_past_its_range) {
  const TriangleCollider c = arena(true, -5.0f);
  float t = 0;
  CHECK(c.raycast({0, 1, 0}, {0, 0, -1}, 60, t));
  CHECK(std::fabs(t - 5.0f) < 0.01f);
  CHECK(!c.raycast({0, 1, 0}, {0, 0, -1}, 4, t));
  CHECK(!c.raycast({0, 1, 0}, {0, 1, 0}, 60, t));
  CHECK(c.raycast({0, 1, 0}, {0, -1, 0}, 60, t));
  CHECK(std::fabs(t - 1.0f) < 0.01f);
}

TEST(foot_gun_fires_at_its_rate_and_empties_into_a_reload) {
  FootCombat f;
  f.begin({});
  const TriangleCollider c = arena();
  std::vector<FootPlayer> players{playerAt({0, 0, 0})};
  players[0].trigger = true;
  int shots = 0;
  for (int i = 0; i < 60; ++i) {
    f.update(kDt, players, {}, &c);
    shots += countEvents(f.takeEvents(), FootEvent::Type::Shot);
  }
  CHECK(shots >= 4 && shots <= 5);  // about one every 0.24 s
  for (int i = 0; i < 60 * 3; ++i) f.update(kDt, players, {}, &c);
  CHECK(players[0].reloading());  // twelve gone, and it reloads by itself
  CHECK(f.magazine(players[0]) == 0);
  players[0].trigger = false;
  for (int i = 0; i < 60 * 2; ++i) f.update(kDt, players, {}, &c);
  CHECK(!players[0].reloading());
  CHECK(f.magazine(players[0]) == f.content().weapons[0].magazine);
  CHECK(f.reserve(players[0]) == f.content().startAmmo.at("ammo.rivets") - f.content().weapons[0].magazine);
}

TEST(foot_gun_with_nothing_left_clicks) {
  FootCombat f;
  f.begin({});
  std::vector<FootPlayer> players{playerAt({0, 0, 0})};
  f.outfit(players[0]);
  players[0].magazines[0] = 0;
  players[0].ammo[0] = 0;
  players[0].trigger = true;
  f.update(kDt, players, {}, nullptr);
  const auto events = f.takeEvents();
  CHECK(countEvents(events, FootEvent::Type::Dry) == 1);
  CHECK(countEvents(events, FootEvent::Type::Shot) == 0);
}

TEST(foot_reload_is_only_taken_when_it_would_do_something) {
  FootCombat f;
  f.begin({});
  std::vector<FootPlayer> players{playerAt({0, 0, 0})};
  players[0].reloadRequest = true;
  f.update(kDt, players, {}, nullptr);
  CHECK(!players[0].reloading());  // already full
  players[0].magazines[0] = 3;
  players[0].reloadRequest = true;
  f.update(kDt, players, {}, nullptr);
  CHECK(players[0].reloading());
}

TEST(foot_shots_stop_at_walls_and_kill_what_they_reach) {
  FootCombat f;
  f.begin({{0, {0, 0, -8}, 0}});
  const TriangleCollider walled = arena(true, -5.0f);
  std::vector<FootPlayer> players{playerAt({0, 0, 0})};
  aimAt(players[0], f.enemies()[0], f);
  players[0].trigger = true;
  f.update(kDt, players, {}, &walled);
  CHECK(f.enemies()[0].health == f.content().enemies[0].health);  // the wall took it

  FootCombat open;
  open.begin({{0, {0, 0, -8}, 0}});
  const TriangleCollider floor = arena();
  std::vector<FootPlayer> shooter{playerAt({0, 0, 0})};
  shooter[0].trigger = true;
  int died = 0;
  for (int i = 0; i < 60 * 4 && open.aliveCount() > 0; ++i) {
    aimAt(shooter[0], open.enemies()[0], open);
    open.update(kDt, shooter, {}, &floor);
    died += countEvents(open.takeEvents(), FootEvent::Type::EnemyDied);
  }
  CHECK(died == 1);
  CHECK(open.aliveCount() == 0);
  CHECK(open.enemies()[0].state == CrawlerState::Dead);
}

TEST(foot_crawler_sleeps_until_it_sees_or_hears_someone) {
  FootCombat f;
  f.begin({{0, {0, 0, -30}, 0}});
  const TriangleCollider c = arena();
  std::vector<FootPlayer> players{playerAt({0, 0, 0})};  // too far to see
  for (int i = 0; i < 60; ++i) f.update(kDt, players, {}, &c);
  CHECK(f.enemies()[0].state == CrawlerState::Asleep);

  FootCombat noisy;
  noisy.begin({{0, {0, 0, -18}, 180}});  // close enough to hear, facing away and behind a wall
  const TriangleCollider walled = arena(true, -5.0f);
  std::vector<FootPlayer> shooter{playerAt({0, 0, 0}, {1, 0, 0})};
  for (int i = 0; i < 30; ++i) noisy.update(kDt, shooter, {}, &walled);
  CHECK(noisy.enemies()[0].state == CrawlerState::Asleep);
  shooter[0].trigger = true;
  noisy.update(kDt, shooter, {}, &walled);
  CHECK(noisy.enemies()[0].state == CrawlerState::Chasing);
}

TEST(foot_crawler_chases_and_hurts_and_a_step_back_dodges) {
  FootCombat f;
  f.begin({{0, {0, 0, -10}, 0}});  // yaw 0 looks down -Z, away from the player: wake it by noise
  const TriangleCollider c = arena();
  std::vector<FootPlayer> players{playerAt({0, 0, 0})};
  f.enemies()[0].state = CrawlerState::Chasing;
  f.enemies()[0].lastSeen = players[0].feet;
  int struck = 0;
  for (int i = 0; i < 60 * 6; ++i) {
    f.update(kDt, players, {}, &c);
    struck += countEvents(f.takeEvents(), FootEvent::Type::PlayerHurt);
  }
  CHECK(struck >= 2);
  CHECK(players[0].health < 100);

  // Winding up, then the player is suddenly out of reach: no damage.
  FootCombat g;
  g.begin({{0, {0, 0, -1}, 0}});
  std::vector<FootPlayer> dodger{playerAt({0, 0, 0})};
  g.enemies()[0].state = CrawlerState::Chasing;
  bool wound = false;
  for (int i = 0; i < 60 && !wound; ++i) {
    g.update(kDt, dodger, {}, &c);
    wound = g.enemies()[0].state == CrawlerState::WindingUp;
  }
  CHECK(wound);
  dodger[0].feet = {0, 0, 8};
  dodger[0].eye = dodger[0].feet + Vec3{0, 1.65f, 0};
  for (int i = 0; i < 30; ++i) g.update(kDt, dodger, {}, &c);
  CHECK(dodger[0].health == 100);
}

TEST(foot_crawler_goes_for_the_nearest_player_still_standing) {
  FootCombat f;
  f.begin({{0, {0, 0, -3}, 0}});
  const TriangleCollider c = arena();
  std::vector<FootPlayer> players{playerAt({0, 0, 0}), playerAt({0, 0, -8})};
  players[0].health = 0;  // the nearer one is down
  f.enemies()[0].state = CrawlerState::Chasing;
  for (int i = 0; i < 60; ++i) f.update(kDt, players, {}, &c);
  CHECK(f.enemies()[0].body.feet.z < -3.5f);  // heading away from the fallen player
}

TEST(foot_two_players_each_have_their_own_gun) {
  FootCombat f;
  f.begin({});
  std::vector<FootPlayer> players{playerAt({0, 0, 0}), playerAt({2, 0, 0})};
  players[1].trigger = true;
  f.update(kDt, players, {}, nullptr);
  CHECK(f.magazine(players[0]) == f.content().weapons[0].magazine);
  CHECK(f.magazine(players[1]) == f.content().weapons[0].magazine - 1);
  const auto events = f.takeEvents();
  CHECK(events.size() >= 1);
  CHECK(events[0].player == 1);
}

TEST(foot_return_home_restores_the_living_and_leaves_the_dead) {
  FootCombat f;
  f.begin({{0, {0, 0, -3}, 0}, {1, {5, 0, -3}, 0}});
  f.enemies()[0].state = CrawlerState::Dead;
  f.enemies()[1].state = CrawlerState::Chasing;
  f.enemies()[1].health = 5;
  f.enemies()[1].body.feet = {0, 0, 0};
  f.returnHome();
  CHECK(f.enemies()[0].state == CrawlerState::Dead);
  CHECK(f.enemies()[1].state == CrawlerState::Asleep);
  CHECK(f.enemies()[1].health == f.content().enemies[0].health);
  CHECK(length(f.enemies()[1].body.feet - Vec3{5, 0, -3}) < 0.01f);
}

TEST(foot_same_inputs_give_the_same_fight) {
  auto run = [] {
    FootCombat f;
    f.begin({{0, {0, 0, -9}, 0}, {1, {3, 0, -9}, 0}});
    const TriangleCollider c = arena();
    std::vector<FootPlayer> players{playerAt({0, 0, 0})};
    players[0].trigger = true;
    for (int i = 0; i < 300; ++i) {
      aimAt(players[0], f.enemies()[0], f);
      f.update(kDt, players, {}, &c);
    }
    return std::make_pair(players[0].health, f.enemies()[1].body.feet);
  };
  const auto a = run(), b = run();
  CHECK(a.first == b.first);
  CHECK(length(a.second - b.second) == 0.0f);
}

TEST(shaders_loop_over_every_lamp_the_renderer_sends) {
  // 16 were sent while the shader looped over 8, and everything still built and ran.
  const auto root = std::filesystem::path(FH_TEST_DATA_DIR).parent_path().parent_path();
  auto read = [&](const char* name) {
    std::ifstream f(root / "shaders" / name);
    std::stringstream t;
    t << f.rdbuf();
    return t.str();
  };
  CHECK(read("common.glsl").find("#define MAX_POINT_LIGHTS 16") != std::string::npos);
  const std::string frag = read("mesh.frag");
  CHECK(frag.find("i < MAX_POINT_LIGHTS") != std::string::npos);
}

// ---- In a real session, on the test derelict ---------------------------------------------------------

namespace {

std::unique_ptr<GameSession> derelictSession() {
  std::vector<std::string> errors;
  auto m = AssetManifest::load(FH_TEST_CONTENT_DIR, &errors);
  if (!m) return nullptr;
  auto scene = loadSceneDef(*m, "scene.stage2_flight", &errors);
  auto ship = loadShipDef(*m, "ship.sparrow", &errors);
  auto db = loadCommodities(*m, "economy.commodities", &errors);
  auto map = loadMap(std::filesystem::path(FH_TEST_DATA_DIR) / "derelict_fixture.glb", &errors);
  if (!scene || !ship || !db || !map) return nullptr;
  auto s = std::make_unique<GameSession>(buildSpaceWorld(*scene, *ship, {}, std::nullopt));
  s->setShipInterior(ship->interior);
  s->setEconomy(*db, 100);
  s->addInterior("map.test", std::move(map->def));
  return s;
}

int lootMarker(const GameSession& s, const char* kind) {
  const auto& markers = s.activeInterior()->markers;
  for (int i = 0; i < int(markers.size()); ++i)
    if (markers[size_t(i)].type == "loot" && markers[size_t(i)].kind == kind) return i;
  return -1;
}

}  // namespace

TEST(derelict_fight_loot_stays_taken_and_pays_once) {
  auto s = derelictSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->enterMap("map.test").ok);
  CHECK(s->footArmed());
  CHECK(s->foot().aliveCount() == 2);
  const int salvage = lootMarker(*s, "salvage");
  CHECK(salvage >= 0);
  const std::string action = "loot:" + std::to_string(salvage);
  const auto credits = s->credits();
  const int reserve = s->foot().reserve(s->footPlayers().front());
  CHECK(s->perform(action).ok);
  CHECK(s->credits() == credits + 150);
  CHECK(s->foot().reserve(s->footPlayers().front()) == reserve + 12);
  CHECK(!s->perform(action).ok);
  CHECK(s->credits() == credits + 150);

  s->footPlayers().front().health = 30;
  const int medical = lootMarker(*s, "medical");
  CHECK(medical >= 0);
  CHECK(s->perform("loot:" + std::to_string(medical)).ok);
  CHECK(s->footPlayers().front().health == 70.0f);
}

TEST(derelict_being_knocked_out_sends_you_back_and_calms_them_down) {
  auto s = derelictSession();
  CHECK(s != nullptr);
  if (!s) return;
  s->enterMap("map.test");
  const Vec3 start = s->character().feet;
  // Stand right in front of a crawler with one hit left.
  FootEnemy& e = s->foot().enemies().front();
  s->character().feet = e.body.feet + Vec3{0.8f, 0, 0};
  s->footPlayers().front().health = 1;
  bool down = false;
  for (int i = 0; i < 60 * 5 && !down; ++i) {
    s->fixedUpdate(kDt);
    for (const FootEvent& ev : s->takeFootEvents()) down = down || ev.type == FootEvent::Type::PlayerDown;
  }
  CHECK(down);
  CHECK(length(s->character().feet - start) < 1.0f);
  CHECK(s->footPlayers().front().health == 60.0f);
  CHECK(s->foot().enemies().front().state != CrawlerState::Chasing);
  CHECK(s->notice().find("black out") != std::string::npos);
}

TEST(derelict_the_dead_stay_dead_when_you_come_back_and_in_saves) {
  auto s = derelictSession();
  CHECK(s != nullptr);
  if (!s) return;
  s->enterMap("map.test");
  // Kill the first one for real, with the gun, from where it can be seen.
  FootEnemy& e = s->foot().enemies().front();
  s->character().feet = e.body.feet + Vec3{0, 0, 5};
  FootPlayer& me = s->footPlayers().front();
  me.trigger = true;
  for (int i = 0; i < 60 * 3 && s->foot().enemies().front().state != CrawlerState::Dead; ++i) {
    const Vec3 to = s->foot().enemies().front().body.feet + Vec3{0, 0.42f, 0} - s->character().eye(s->characterParams());
    s->character().yaw = std::atan2(to.x, -to.z);
    s->character().pitch = std::asin(to.y / length(to));
    me.health = 100;
    s->fixedUpdate(kDt);
  }
  me.trigger = false;
  CHECK(s->foot().enemies().front().state == CrawlerState::Dead);
  CHECK(s->clearedMarkers().size() == 1);

  s->enterMap("map.test");
  CHECK(s->foot().aliveCount() == 1);

  s->docking().forceDock(s->world());  // on foot, a save has to be parked somewhere
  std::string error;
  const auto parsed = deserializeSave(serializeSave(captureSave(*s, "scene.stage2_flight")), &error);
  if (!parsed) std::fprintf(stderr, "    %s\n", error.c_str());
  CHECK(parsed.has_value());
  if (parsed) {
    CHECK(parsed->clearedMarkers.size() == 1);
    CHECK(parsed->footMagazines.at("weapon.rivet_driver") < 12);
  }
}

TEST(derelict_a_safe_place_refills_you) {
  auto s = derelictSession();
  CHECK(s != nullptr);
  if (!s) return;
  // The same map with nothing hostile in it.
  auto safe = loadMap(std::filesystem::path(FH_TEST_DATA_DIR) / "derelict_fixture.glb", nullptr);
  CHECK(safe.has_value());
  if (!safe) return;
  safe->def.markers.clear();
  s->addInterior("map.safe", std::move(safe->def));
  s->enterMap("map.test");
  s->footPlayers().front().health = 20;
  s->footPlayers().front().magazines[0] = 1;
  s->footPlayers().front().ammo[0] = 0;
  CHECK(s->enterMap("map.safe").ok);
  CHECK(!s->footArmed());
  CHECK(s->foot().aliveCount() == 0);
  CHECK(s->footPlayers().front().health == 100.0f);
  CHECK(s->foot().magazine(s->footPlayers().front()) == s->foot().content().weapons[0].magazine);
  CHECK(s->foot().reserve(s->footPlayers().front()) == s->foot().content().startAmmo.at("ammo.rivets"));
}
