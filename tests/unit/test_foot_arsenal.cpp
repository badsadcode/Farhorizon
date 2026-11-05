// 0.10: weapons and enemies as content, the flechette caster and arc cutter, spitters and brutes,
// pathfinding chases, loot tables, enemy drops and save v5.
#include <cmath>
#include <filesystem>

#include "engine/assets/asset_manifest.h"
#include "game/content/interior_def.h"
#include "game/content/map_def.h"
#include "game/content/scene_def.h"
#include "game/content/ship_def.h"
#include "game/economy/economy.h"
#include "game/foot/foot_combat.h"
#include "game/foot/foot_defs.h"
#include "game/foot/nav_grid.h"
#include "game/persistence/save_game.h"
#include "game/session/game_session.h"
#include "game/session/world_builder.h"
#include <nlohmann/json.hpp>

#include "test_framework.h"

using namespace fh;

namespace {

constexpr float kDt = 1.0f / 60.0f;

const FootContent& realContent() {
  static const FootContent c = [] {
    std::vector<std::string> errors;
    auto m = AssetManifest::load(FH_TEST_CONTENT_DIR, &errors);
    auto f = m ? loadFootContent(*m, &errors) : std::nullopt;
    for (const auto& e : errors) std::fprintf(stderr, "    foot content: %s\n", e.c_str());
    return f ? *f : builtinFootContent();
  }();
  return c;
}

void quad(std::vector<MeshVertex>& v, std::vector<std::uint32_t>& idx, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
  const auto base = std::uint32_t(v.size());
  for (const Vec3& p : {a, b, c, d}) {
    MeshVertex m{};
    m.position = p;
    v.push_back(m);
  }
  for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + i);
}

// A 60 x 60 floor, optionally with a wall across z = 0 that has a gap at x in [6, 7.4].
TriangleCollider ground(bool wallWithGap = false) {
  std::vector<MeshVertex> v;
  std::vector<std::uint32_t> idx;
  quad(v, idx, {-30, 0, -30}, {-30, 0, 30}, {30, 0, 30}, {30, 0, -30});
  if (wallWithGap) {
    quad(v, idx, {-30, 0, 0}, {6, 0, 0}, {6, 3, 0}, {-30, 3, 0});
    quad(v, idx, {7.4f, 0, 0}, {30, 0, 0}, {30, 3, 0}, {7.4f, 3, 0});
  }
  TriangleCollider c;
  c.build(v, idx);
  return c;
}

FootPlayer standing(const FootCombat& f, Vec3 feet, Vec3 look = {0, 0, -1}) {
  FootPlayer p;
  f.outfit(p);
  p.feet = feet;
  p.eye = feet + Vec3{0, 1.65f, 0};
  p.look = normalize(look);
  return p;
}

void aimAt(FootPlayer& p, const FootCombat& f, const FootEnemy& e) {
  p.look = normalize(e.body.feet + Vec3{0, f.enemyDef(e).hitCentreHeight, 0} - p.eye);
}

int count(const std::vector<FootEvent>& ev, FootEvent::Type t) {
  int n = 0;
  for (const auto& e : ev) n += e.type == t;
  return n;
}

}  // namespace

TEST(foot_content_files_load_and_cross_check) {
  const FootContent& c = realContent();
  CHECK(c.weapons.size() == 3);
  CHECK(c.enemies.size() == 3);
  CHECK(c.ammo.size() == 3);
  CHECK(c.weaponIndex("weapon.flechette_caster") >= 0);
  CHECK(c.enemyIndex("spitter") >= 0);  // a map's fh_kind
  CHECK(c.enemyIndex("enemy.brute") >= 0);
  CHECK(c.lootTable("salvage") != nullptr);
  CHECK(c.lootTable("loot.cache_arc") != nullptr);
  // The crawler and rivet driver in content match the built-in ones the old code had.
  const FootContent b = builtinFootContent();
  const auto& crawler = c.enemies[size_t(c.enemyIndex("crawler"))];
  CHECK(crawler.health == b.enemies[0].health && crawler.attackDamage == b.enemies[0].attackDamage);
  CHECK(c.weapons[0].damage == b.weapons[0].damage && c.weapons[0].magazine == b.weapons[0].magazine);
}

TEST(foot_flechette_caster_throws_a_cone_of_pellets) {
  FootCombat f;
  f.configure(&realContent());
  f.begin({{0, {0, 0, -6}, 180, "crawler"}});
  const TriangleCollider c = ground();
  std::vector<FootPlayer> players{standing(f, {0, 0, 0})};
  CHECK(f.unlock(players[0], "weapon.flechette_caster"));
  CHECK(!f.unlock(players[0], "weapon.flechette_caster"));  // once
  players[0].switchRequest = realContent().weaponIndex("weapon.flechette_caster");
  f.update(kDt, players, {}, &c);
  CHECK(players[0].switching());
  CHECK(count(f.takeEvents(), FootEvent::Type::WeaponSwitched) == 1);
  for (int i = 0; i < 30; ++i) f.update(kDt, players, {}, &c);  // finish switching
  CHECK(!players[0].switching());
  const int mag = f.magazine(players[0]);
  aimAt(players[0], f, f.enemies()[0]);
  players[0].trigger = true;
  f.update(kDt, players, {}, &c);
  players[0].trigger = false;
  const auto ev = f.takeEvents();
  CHECK(f.magazine(players[0]) == mag - 1);       // one shell
  CHECK(count(ev, FootEvent::Type::HitEnemy) >= 4);  // most of the cone lands at 6 m
  CHECK(f.enemies()[0].health < f.enemyDef(f.enemies()[0]).health - 30);
}

TEST(foot_arc_cutter_burns_continuously_and_drains_its_cell) {
  FootCombat f;
  f.configure(&realContent());
  f.begin({{0, {0, 0, -4}, 180, "crawler"}});
  const TriangleCollider c = ground();
  std::vector<FootPlayer> players{standing(f, {0, 0, 0})};
  const int arc = realContent().weaponIndex("weapon.arc_cutter");
  f.unlock(players[0], "weapon.arc_cutter");
  players[0].weapon = arc;  // already in hand
  const auto& w = realContent().weapons[size_t(arc)];
  players[0].trigger = true;
  int starts = 0, stops = 0;
  float t = 0;
  while (f.enemies()[0].state != EnemyState::Dead && t < 3.0f) {
    aimAt(players[0], f, f.enemies()[0]);
    f.update(kDt, players, {}, &c);
    const auto ev = f.takeEvents();
    starts += count(ev, FootEvent::Type::BeamStart);
    t += kDt;
  }
  CHECK(f.enemies()[0].state == EnemyState::Dead);
  const float expected = f.enemyDef(f.enemies()[0]).health / w.damage;  // about one second
  CHECK(std::fabs(t - expected) < 0.2f);
  CHECK(starts == 1);
  CHECK(std::abs((w.magazine - f.magazine(players[0])) - int(w.ammoPerSecond * t)) <= 1);
  players[0].trigger = false;
  f.update(kDt, players, {}, &c);
  stops += count(f.takeEvents(), FootEvent::Type::BeamStop);
  CHECK(stops == 1);
  CHECK(!players[0].beamActive);
  // Out of range it touches nothing.
  CHECK(w.range < 10);
}

TEST(foot_you_cannot_switch_to_a_weapon_you_do_not_have) {
  FootCombat f;
  f.configure(&realContent());
  f.begin({});
  std::vector<FootPlayer> players{standing(f, {0, 0, 0})};
  players[0].switchRequest = realContent().weaponIndex("weapon.arc_cutter");
  f.update(kDt, players, {}, nullptr);
  CHECK(players[0].weapon == realContent().weaponIndex("weapon.rivet_driver"));
  CHECK(!players[0].switching());
}

TEST(foot_spitter_keeps_its_distance_and_its_globs_can_be_dodged) {
  FootCombat f;
  f.configure(&realContent());
  f.begin({{0, {0, 0, -14}, 0, "spitter"}});
  const TriangleCollider c = ground();
  std::vector<FootPlayer> players{standing(f, {0, 0, 0})};
  f.enemies()[0].state = EnemyState::Chasing;
  f.enemies()[0].lastSeen = players[0].feet;
  const auto& d = f.enemyDef(f.enemies()[0]);
  // Standing still: it closes to about its preferred range, throws, and hits.
  int fired = 0, hurt = 0;
  for (int i = 0; i < 60 * 8; ++i) {
    f.update(kDt, players, {}, &c);
    const auto ev = f.takeEvents();
    fired += count(ev, FootEvent::Type::EnemyFired);
    hurt += count(ev, FootEvent::Type::PlayerHurt);
  }
  const float dist = length(f.enemies()[0].body.feet - players[0].feet);
  CHECK(dist > d.retreatRange && dist < d.preferredRange + 1.5f);
  CHECK(fired >= 2);
  CHECK(hurt >= 1);
  CHECK(players[0].health < 100);
  // Side-stepping as soon as it throws: the glob misses.
  FootCombat g;
  g.configure(&realContent());
  g.begin({{0, {0, 0, -9}, 0, "spitter"}});
  std::vector<FootPlayer> dodger{standing(g, {0, 0, 0})};
  g.enemies()[0].state = EnemyState::Chasing;
  int thrown = 0, hits = 0;
  for (int i = 0; i < 60 * 6; ++i) {
    g.update(kDt, dodger, {}, &c);
    for (const auto& e : g.takeEvents()) {
      if (e.type == FootEvent::Type::EnemyFired) {
        ++thrown;
        dodger[0].feet.x += (thrown % 2 ? 2.2f : -2.2f);  // step aside
        dodger[0].eye = dodger[0].feet + Vec3{0, 1.65f, 0};
      }
      hits += e.type == FootEvent::Type::PlayerHurt;
    }
  }
  CHECK(thrown >= 2);
  CHECK(hits == 0);
  CHECK(dodger[0].health == 100);
}

TEST(foot_spitter_backs_off_when_you_get_close) {
  FootCombat f;
  f.configure(&realContent());
  f.begin({{0, {0, 0, -2.5f}, 0, "spitter"}});
  const TriangleCollider c = ground();
  std::vector<FootPlayer> players{standing(f, {0, 0, 0})};
  f.enemies()[0].state = EnemyState::Chasing;
  f.enemies()[0].cooldown = 5;  // not throwing for a while
  for (int i = 0; i < 60; ++i) f.update(kDt, players, {}, &c);
  CHECK(length(f.enemies()[0].body.feet - players[0].feet) > 3.0f);
}

TEST(foot_brute_charges_knocks_you_back_then_stands_stunned) {
  FootCombat f;
  f.configure(&realContent());
  f.begin({{0, {0, 0, -9}, 180, "brute"}});
  const TriangleCollider c = ground();
  std::vector<FootPlayer> players{standing(f, {0, 0, 0})};
  FootEnemy& b = f.enemies()[0];
  b.state = EnemyState::Chasing;
  const auto& d = f.enemyDef(b);
  bool windup = false, charged = false, struck = false, stunned = false;
  for (int i = 0; i < 60 * 6 && !stunned; ++i) {
    f.update(kDt, players, {}, &c);
    windup |= f.enemies()[0].state == EnemyState::ChargeWindup;
    for (const auto& e : f.takeEvents()) {
      charged |= e.type == FootEvent::Type::EnemyCharged;
      struck |= e.type == FootEvent::Type::EnemyStruck;
      stunned |= e.type == FootEvent::Type::EnemyStunned;
    }
  }
  CHECK(windup);
  CHECK(charged);
  CHECK(struck);
  CHECK(stunned);
  CHECK(players[0].health == 100 - d.chargeDamage);
  CHECK(length(players[0].knock) > d.chargeKnockback * 0.9f);
  CHECK(f.enemies()[0].state == EnemyState::Recovering);
  // While stunned it takes extra damage.
  const float before = f.enemies()[0].health;
  aimAt(players[0], f, f.enemies()[0]);
  players[0].trigger = true;
  players[0].cooldown = 0;
  f.update(kDt, players, {}, &c);
  CHECK(std::fabs((before - f.enemies()[0].health) - realContent().weapons[0].damage * d.recoverDamageScale) < 0.01f);
}

TEST(foot_brute_charge_misses_if_you_step_out_of_its_line) {
  FootCombat f;
  f.configure(&realContent());
  f.begin({{0, {0, 0, -9}, 180, "brute"}});
  const TriangleCollider c = ground();
  std::vector<FootPlayer> players{standing(f, {0, 0, 0})};
  f.enemies()[0].state = EnemyState::Chasing;
  bool charged = false, stunned = false;
  for (int i = 0; i < 60 * 6 && !stunned; ++i) {
    f.update(kDt, players, {}, &c);
    for (const auto& e : f.takeEvents()) {
      if (e.type == FootEvent::Type::EnemyCharged && !charged) {
        charged = true;
        players[0].feet.x += 3.0f;  // dive aside
        players[0].eye = players[0].feet + Vec3{0, 1.65f, 0};
      }
      stunned |= e.type == FootEvent::Type::EnemyStunned;
    }
  }
  CHECK(charged);
  CHECK(stunned);  // it ran its full distance and stopped
  CHECK(players[0].health == 100);
}

TEST(foot_crawler_finds_its_way_around_a_wall_with_a_nav_grid) {
  const TriangleCollider c = ground(true);
  NavGrid nav;
  CHECK(nav.build(c));
  auto chase = [&](const NavGrid* grid) {
    FootCombat f;
    f.configure(&realContent());
    f.begin({{0, {0, 0, -5}, 0, "crawler"}});
    std::vector<FootPlayer> players{standing(f, {0, 0, 5})};
    players[0].noisy = true;  // running: it hears where you are through the wall
    f.enemies()[0].state = EnemyState::Chasing;
    for (int i = 0; i < 60 * 15; ++i) {
      f.update(kDt, players, {}, &c, grid);
      if (length(f.enemies()[0].body.feet - players[0].feet) < 2.0f) return true;
    }
    return false;
  };
  CHECK(chase(&nav));      // through the gap, 7 m to the side
  CHECK(!chase(nullptr));  // straight at you, it just scrapes along the wall
}

TEST(foot_loot_rolls_are_repeatable_and_cover_their_table) {
  const FootContent& c = realContent();
  const LootTable* salvage = c.lootTable("salvage");
  CHECK(salvage != nullptr);
  if (!salvage) return;
  const LootRoll a = rollLoot(*salvage, lootSeed("map.x#3")), b = rollLoot(*salvage, lootSeed("map.x#3"));
  CHECK(a.credits == b.credits && a.ammo == b.ammo);
  bool sawCells = false, sawRivets = false;
  for (int i = 0; i < 200; ++i) {
    const LootRoll r = rollLoot(*salvage, lootSeed("map.x#" + std::to_string(i)));
    CHECK(r.credits >= 60 && r.credits <= 420);
    sawCells |= r.ammo.count("ammo.cells") > 0;
    sawRivets |= r.ammo.count("ammo.rivets") > 0;
  }
  CHECK(sawCells && sawRivets);
  // A tool cache gives the weapon, loaded, and ammo is capped at what you can carry.
  FootCombat f;
  f.configure(&c);
  FootPlayer p = standing(f, {0, 0, 0});
  int credits = 0;
  const std::string got = f.apply(p, rollLoot(*c.lootTable("cache_arc"), 1), &credits);
  const int arc = c.weaponIndex("weapon.arc_cutter");
  CHECK(p.owned[size_t(arc)]);
  CHECK(p.magazines[size_t(arc)] == c.weapons[size_t(arc)].magazine);
  CHECK(p.switchRequest == arc);
  CHECK(got.find("ARC CUTTER") != std::string::npos);
  const size_t cells = size_t(c.ammoIndex("ammo.cells"));
  const int room = c.ammo[cells].maxReserve - p.ammo[cells];
  CHECK(f.addAmmo(p, "ammo.cells", 100000) == room);
  CHECK(p.ammo[cells] == c.ammo[cells].maxReserve);
  CHECK(f.addAmmo(p, "ammo.cells", 5) == 0);
}

namespace {

std::unique_ptr<GameSession> arsenalSession(const FootContent* content) {
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
  s->setFootContent(content);
  for (const char* id : {"interior.sparrow", "interior.kestrel_hangar"})
    if (auto d = loadInteriorDef(*m, id, &errors)) s->addInterior(id, *d);
  s->addInterior("map.test", std::move(map->def));
  return s;
}

void killFront(GameSession& s) {
  FootEnemy& e = s.foot().enemies().front();
  s.character().feet = e.body.feet + Vec3{0, 0, 5};
  FootPlayer& me = s.footPlayers().front();
  me.trigger = true;
  for (int i = 0; i < 60 * 4 && s.foot().enemies().front().state != EnemyState::Dead; ++i) {
    const Vec3 to = s.foot().enemies().front().body.feet + Vec3{0, 0.42f, 0} - s.character().eye(s.characterParams());
    s.character().yaw = std::atan2(to.x, -to.z);
    s.character().pitch = std::asin(to.y / length(to));
    me.health = 100;
    s.fixedUpdate(kDt);
  }
  me.trigger = false;
}

}  // namespace

TEST(foot_session_bakes_a_nav_grid_and_enemies_drop_loot_that_saves) {
  static FootContent generous = realContent();
  for (auto& e : generous.enemies) e.dropChance = 1.0f;  // always drops, for the test
  auto s = arsenalSession(&generous);
  CHECK(s != nullptr);
  if (!s) return;
  CHECK(s->enterMap("map.test").ok);
  CHECK(s->activeNav() != nullptr);
  CHECK(s->activeNav() && s->activeNav()->nodeCount() > 500);
  killFront(*s);
  CHECK(s->foot().enemies().front().state == EnemyState::Dead);
  CHECK(s->footDrops().size() == 1);
  CHECK(s->dropPositions().size() == 1);
  // Saved while it is still lying there, and back after loading.
  s->docking().forceDock(s->world());
  std::string err;
  auto parsed = deserializeSave(serializeSave(captureSave(*s, "scene.stage2_flight")), &err);
  CHECK(parsed.has_value());
  if (!parsed) return;
  CHECK(parsed->footDrops.size() == 1);
  auto s2 = arsenalSession(&generous);
  CHECK(applySave(*s2, *parsed, &err));
  if (!err.empty()) std::fprintf(stderr, "    %s\n", err.c_str());
  s2->enterMap("map.test");
  CHECK(s2->footDrops().size() == 1);
  // Walking over it picks it up, once.
  s2->character().feet = s2->footDrops().front().position;
  s2->character().velocity = {};
  int pickups = 0;
  for (int i = 0; i < 5; ++i) {
    s2->fixedUpdate(kDt);
    pickups += count(s2->takeFootEvents(), FootEvent::Type::Pickup);
  }
  CHECK(pickups == 1);
  CHECK(s2->footDrops().empty());
  CHECK(s2->dropPositions().empty());
  s2->enterMap("map.test");
  CHECK(s2->footDrops().empty());
}

TEST(foot_save_keeps_the_loadout_and_v4_files_migrate) {
  auto s = arsenalSession(&realContent());
  CHECK(s != nullptr);
  if (!s) return;
  FootPlayer& me = s->footPlayers().front();
  s->foot().unlock(me, "weapon.flechette_caster");
  me.weapon = realContent().weaponIndex("weapon.flechette_caster");
  me.magazines[size_t(me.weapon)] = 2;
  s->foot().addAmmo(me, "ammo.cells", 77);
  const SaveGame saved = captureSave(*s, "scene.stage2_flight");
  std::string err;
  auto parsed = deserializeSave(serializeSave(saved), &err);
  CHECK(parsed.has_value());
  auto s2 = arsenalSession(&realContent());
  CHECK(applySave(*s2, *parsed, &err));
  const FootPlayer& you = s2->footPlayers().front();
  CHECK(you.weapon == me.weapon);
  CHECK(you.owned == me.owned);
  CHECK(you.magazines == me.magazines);
  CHECK(you.ammo == me.ammo);
  CHECK(serializeSave(captureSave(*s2, "scene.stage2_flight")) == serializeSave(saved));

  // A v4 file: one gun, its magazine and its rivets.
  nlohmann::json v4 = nlohmann::json::parse(serializeSave(saved));
  v4["schemaVersion"] = 4;
  auto& p = v4["player"];
  p.erase("footWeapons");
  p.erase("footWeapon");
  p.erase("footMagazines");
  p.erase("footAmmo");
  v4.erase("footDrops");
  p["footMagazine"] = 5;
  p["footReserve"] = 20;
  auto old = deserializeSave(v4.dump(), &err);
  CHECK(old.has_value());
  if (!old) return;
  CHECK(old->footWeapons == std::vector<std::string>{"weapon.rivet_driver"});
  CHECK(old->footMagazines.at("weapon.rivet_driver") == 5);
  CHECK(old->footAmmo.at("ammo.rivets") == 20);
}
