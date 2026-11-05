// Co-op groundwork (roadmap 4.1): several players in one session, each with their own body, wallet
// and place, sharing one ship and its hold. Driven headless by SimPlayer, no window and no network.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/assets/asset_manifest.h"
#include "game/content/map_def.h"
#include "game/content/scene_def.h"
#include "game/content/ship_def.h"
#include "game/economy/economy.h"
#include "game/foot/foot_defs.h"
#include "game/persistence/save_game.h"
#include "game/session/game_session.h"
#include "game/session/sim_player.h"
#include "game/session/world_builder.h"
#include "test_framework.h"

using namespace fh;

namespace {

constexpr float kDt = 1.0f / 60.0f;

const FootContent& crewContent() {
  static const FootContent c = [] {
    std::vector<std::string> errors;
    auto m = AssetManifest::load(FH_TEST_CONTENT_DIR, &errors);
    auto f = m ? loadFootContent(*m, &errors) : std::nullopt;
    return f ? *f : builtinFootContent();
  }();
  return c;
}

std::unique_ptr<GameSession> crewSession(const FootContent* content = &crewContent()) {
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
  s->setEconomy(*db, 1000);
  s->setFootContent(content);
  for (const char* id : {"interior.sparrow", "interior.kestrel_hangar"})
    if (auto d = loadInteriorDef(*m, id, &errors)) s->addInterior(id, *d);
  s->addInterior("map.test", std::move(map->def));
  for (const auto& p : scene->props)
    if (p.docking && !p.docking->market.empty())
      if (auto md = loadMarketDef(*m, p.docking->market, s->commodities(), &errors)) s->addMarket(p.docking->market, *md);
  return s;
}

int count(const std::vector<FootEvent>& events, FootEvent::Type t) {
  int n = 0;
  for (const FootEvent& e : events) n += e.type == t;
  return n;
}

}  // namespace

TEST(crew_join_seated_and_the_helm_goes_to_whoever_is_first) {
  auto s = crewSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->crew().size() == 1);  // solo: just you, at the helm
  CHECK(s->crew().front().pilot);
  const PlayerId mara = s->addPlayer("Mara");
  const PlayerId oskar = s->addPlayer("Oskar");
  const PlayerId ines = s->addPlayer("");
  CHECK(mara != 0 && oskar != 0 && ines != 0 && mara != oskar && oskar != ines);
  CHECK(s->addPlayer("Fifth") == 0);  // four seats
  CHECK(s->crew().size() == size_t(GameSession::kMaxPlayers));
  CHECK(!s->player(mara)->pilot);  // you already have the helm
  CHECK(s->player(mara)->location == Location::Cockpit);
  CHECK(!s->player(ines)->name.empty());
  CHECK(!s->removePlayer(s->localPlayer()));  // this machine's player never leaves its own session
  CHECK(s->removePlayer(oskar));
  CHECK(s->player(oskar) == nullptr);
  CHECK(!s->removePlayer(oskar));
  CHECK(s->crew().size() == 3);
}

TEST(crew_undock_needs_the_pilot_and_everyone_aboard) {
  auto s = crewSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->docking().forceDock(s->world()));
  const PlayerId mara = s->addPlayer("Mara");
  CHECK(s->leaveSeat(mara).ok);  // walks back into the ship
  CHECK(s->player(mara)->location == Location::ShipInterior);
  CHECK(s->location() == Location::Cockpit);  // you did not move
  ActionResult r = s->undock();
  CHECK(!r.ok);
  CHECK(r.message.find("aboard") != std::string::npos);
  CHECK(s->docking().state() == DockState::Docked);
  CHECK(!s->undock(mara).ok);  // not the pilot (and not even seated)
  CHECK(s->perform(mara, "takeSeat").ok);
  CHECK(!s->player(mara)->pilot);  // a passenger seat
  CHECK(!s->undock(mara).ok);
  CHECK(s->undock().ok);
  for (int i = 0; i < 60 * 10 && s->docking().state() != DockState::Undocked; ++i) s->fixedUpdate(kDt);
  CHECK(s->docking().state() == DockState::Undocked);
  CHECK(!s->leaveSeat(mara).ok);  // nobody walks about in flight
}

TEST(crew_in_different_places_have_their_own_worlds_on_foot) {
  auto s = crewSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->docking().forceDock(s->world()));
  const PlayerId mara = s->addPlayer("Mara");
  CHECK(s->enterMap("map.test").ok);
  CHECK(s->leaveSeat(mara).ok);
  CHECK(s->activeInteriorId() == "map.test");
  CHECK(s->player(mara)->interior == "interior.sparrow");
  const FootCombat* range = s->footCombatAt("map.test");
  const FootCombat* ship = s->footCombatAt("interior.sparrow");
  CHECK(range != nullptr && ship != nullptr);
  if (!range || !ship) return;
  CHECK(!range->enemies().empty());
  CHECK(ship->enemies().empty());
  CHECK(s->footArmed());
  // Mara walking around the ship does not move you, and the reverse.
  const Vec3 mine = s->character().feet;
  const Vec3 hers = s->player(mara)->character.feet;
  s->player(mara)->input.move = {0, 1};
  for (int i = 0; i < 60; ++i) s->fixedUpdate(kDt);
  CHECK(length(s->character().feet - mine) < 0.3f);
  CHECK(length(s->player(mara)->character.feet - hers) > 0.5f);
  // A place nobody is in is put away; who was killed there is remembered.
  FootEnemy& first = s->foot().enemies().front();
  first.health = 0;
  first.state = EnemyState::Dead;
  CHECK(s->perform(mara, "takeSeat").ok);
  CHECK(s->footCombatAt("interior.sparrow") == nullptr);
  CHECK(!s->leaveSeat().ok);  // you are on foot already
  CHECK(s->footCombatAt("map.test") != nullptr);  // you are still in it
}

TEST(crew_fight_together_and_each_sees_the_events_as_their_own) {
  static FootContent generous = crewContent();
  for (auto& e : generous.enemies) e.dropChance = 0.0f;
  auto s = crewSession(&generous);
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->docking().forceDock(s->world()));
  const PlayerId mara = s->addPlayer("Mara");
  CHECK(s->enterMap("map.test").ok);
  CHECK(s->enterMap(mara, "map.test").ok);
  CHECK(length(s->character().feet - s->player(mara)->character.feet) > 0.5f);  // not inside each other
  CHECK(s->footCombatAt("map.test") == &s->foot());  // one fight for the place, shared
  // Both stand back from the first enemy and open fire.
  const Vec3 enemy = s->foot().enemies().front().body.feet;
  s->character().feet = enemy + Vec3{0, 0, 5};
  s->player(mara)->character.feet = enemy + Vec3{0, 0, 6.5f};
  SimPlayer you(*s, s->localPlayer()), her(*s, mara);
  you.fightNearest(true, 8.0f);
  her.fightNearest(true, 8.0f);
  std::vector<FootEvent> mine, hers;
  for (int i = 0; i < 60 * 6 && s->foot().enemies().front().state != EnemyState::Dead; ++i) {
    you.step();
    her.step();
    s->footPlayers()[0].health = s->footPlayers()[1].health = 100;
    s->fixedUpdate(kDt);
    for (auto& e : s->takeFootEvents()) mine.push_back(e);
    for (auto& e : s->player(mara)->footEvents) hers.push_back(e);
    s->player(mara)->footEvents.clear();
  }
  CHECK(s->foot().enemies().front().state == EnemyState::Dead);
  CHECK(s->markerCleared("map.test", s->foot().enemies().front().markerIndex));
  CHECK(count(mine, FootEvent::Type::EnemyDied) >= 1);
  CHECK(count(hers, FootEvent::Type::EnemyDied) >= 1);
  // Shots: each list calls its owner's shots player 0 and the other's 1 + slot.
  int myOwn = 0, myOfHers = 0, herOwn = 0, herOfMine = 0;
  for (const FootEvent& e : mine)
    if (e.type == FootEvent::Type::Shot) (e.player == 0 ? myOwn : myOfHers) += 1;
  for (const FootEvent& e : hers)
    if (e.type == FootEvent::Type::Shot) (e.player == 0 ? herOwn : herOfMine) += 1;
  CHECK(myOwn > 0 && herOwn > 0);
  CHECK(myOwn == herOfMine);
  CHECK(herOwn == myOfHers);
  // Ammunition is personal too.
  const int rivets = generous.ammoIndex("ammo.rivets");
  CHECK(rivets >= 0);
}

TEST(crew_wallets_are_personal_and_the_hold_is_shared) {
  auto s = crewSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->docking().forceDock(s->world()));
  const PlayerId mara = s->addPlayer("Mara");
  s->player(mara)->credits = 500;
  const std::int64_t mine = s->credits();
  const TradeResult r = s->trade(mara, "goods.rations", 2);
  CHECK(r.units == 2);
  CHECK(s->player(mara)->credits == 500 - r.credits);
  CHECK(s->credits() == mine);  // not yours
  CHECK(s->cargo().units("goods.rations") == 2);  // but it is in your hold
  // You can sell it (the hold is shared) and the money is yours.
  const TradeResult sold = s->trade("goods.rations", -2);
  CHECK(sold.units == 2);
  CHECK(s->credits() == mine + sold.credits);
  // A broke crew member cannot spend your money.
  s->player(mara)->credits = 0;
  CHECK(s->trade(mara, "goods.rations", 1).units == 0);
}

TEST(crew_wallets_wait_for_whoever_left_and_survive_a_save) {
  auto s = crewSession();
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->docking().forceDock(s->world()));
  const PlayerId mara = s->addPlayer("Mara");
  s->player(mara)->credits = 321;
  const PlayerId oskar = s->addPlayer("Oskar");
  s->player(oskar)->credits = 77;
  CHECK(s->removePlayer(mara));
  CHECK(s->crewWallets().at("Mara") == 321);
  std::string err;
  auto parsed = deserializeSave(serializeSave(captureSave(*s, "scene.stage2_flight")), &err);
  CHECK(parsed.has_value());
  if (!parsed) return;
  CHECK(parsed->crewWallets.at("Mara") == 321);
  CHECK(parsed->crewWallets.at("Oskar") == 77);  // still aboard: saved too
  auto s2 = crewSession();
  CHECK(applySave(*s2, *parsed, &err));
  CHECK(s2->crew().size() == 1);  // nobody is connected on load: they rejoin
  const PlayerId back = s2->addPlayer("Mara");
  CHECK(s2->player(back)->credits == 321);
  CHECK(s2->player(s2->addPlayer("Newcomer"))->credits == 0);
  // v5 files were single player: no crew.
  nlohmann::json v5 = nlohmann::json::parse(serializeSave(*parsed));
  v5["schemaVersion"] = 5;
  v5.erase("crew");
  auto old = deserializeSave(v5.dump(), &err);
  CHECK(old.has_value());
  if (old) CHECK(old->crewWallets.empty());
}

namespace {

// Four simulated players wander the map and fight whatever they meet. Returns where everyone ended up.
std::string soak(int seconds) {
  static FootContent tough = crewContent();
  auto s = crewSession(&tough);
  if (!s) return "no session";
  s->docking().forceDock(s->world());
  std::vector<SimPlayer> sims{SimPlayer(*s, s->localPlayer())};
  for (const char* n : {"Mara", "Oskar", "Ines"}) sims.emplace_back(*s, s->addPlayer(n));
  for (SimPlayer& p : sims) s->enterMap(p.id(), "map.test");
  const NavGrid* nav = s->navAt("map.test");
  if (!nav || nav->empty()) return "no nav";
  std::uint32_t rng = 12345;
  auto next = [&] { return rng = rng * 1664525u + 1013904223u; };
  for (SimPlayer& p : sims) p.fightNearest(true, 12.0f);
  std::vector<float> walked(sims.size(), 0.0f);
  std::vector<int> goals(sims.size(), 0);
  for (int i = 0; i < seconds * 60; ++i) {
    std::vector<Vec3> before;
    for (size_t k = 0; k < sims.size(); ++k) {
      SimPlayer& p = sims[k];
      if (p.arrived()) {
        p.walkTo(nav->nodePosition(int(next() % std::uint32_t(nav->nodeCount()))));
        ++goals[k];
      }
      p.step();
      before.push_back(s->crew()[k].character.feet);
    }
    for (FootPlayer& f : s->footPlayers()) f.health = std::max(f.health, 30.0f);  // a soak, not a contest
    s->fixedUpdate(kDt);
    for (size_t k = 0; k < sims.size(); ++k) walked[k] += length(s->crew()[k].character.feet - before[k]);
    for (const CrewMember& m : s->crew()) const_cast<CrewMember&>(m).footEvents.clear();
  }
  std::string out;
  for (const CrewMember& m : s->crew()) {
    const Vec3 f = m.character.feet;
    if (!std::isfinite(f.x) || !std::isfinite(f.y) || !std::isfinite(f.z) || f.y < -40) return "lost " + m.name;
    char buf[96];
    const size_t k = size_t(&m - s->crew().data());
    std::snprintf(buf, sizeof buf, "%s %.3f %.3f %.3f walked %.0f m, %d goals|", m.name.c_str(), f.x, f.y, f.z, walked[k], goals[k]);
    out += buf;
  }
  int dead = 0;
  for (const FootEnemy& e : s->foot().enemies()) dead += e.state == EnemyState::Dead;
  return out + std::to_string(dead);
}

}  // namespace

TEST(crew_four_simulated_players_soak_a_map_headless_and_repeatably) {
  const std::string a = soak(40);
  CHECK(a.find('|') != std::string::npos);
  std::fprintf(stderr, "    crew soak (name x y z | enemies dead): %s\n", a.c_str());
  CHECK(a == soak(40));  // same inputs, same world: what lockstep or replays will need
}
