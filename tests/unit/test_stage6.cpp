// Stage 6: combat (durability, heat, weapons, AI), upgrades, contracts, defeat/insurance, save v3,
// and the exit criterion: the player can choose a combat or a trade/mining route, complete a
// contract with either, and buy a meaningful upgrade - surviving a save/reload in between.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <sstream>

#include "engine/assets/asset_manifest.h"
#include "game/content/interior_def.h"
#include "game/economy/economy.h"
#include "game/persistence/save_game.h"
#include "game/session/game_session.h"
#include "game/session/progression_setup.h"
#include "game/session/world_builder.h"
#include "test_framework.h"

using namespace fh;

namespace {
constexpr float kDt = 1.0f / 60.0f;

// Content must outlive the session (declared first, destroyed last).
struct Fixture {
  std::unique_ptr<ProgressionContent> content;
  SceneDef scene;
  std::unique_ptr<GameSession> s;
};

Fixture session6(std::optional<SystemPosition> start = std::nullopt) {
  Fixture f;
  std::vector<std::string> errors;
  auto m = AssetManifest::load(FH_TEST_CONTENT_DIR, &errors);
  if (!m) return f;
  auto scene = loadSceneDef(*m, "scene.stage2_flight", &errors);
  auto ship = loadShipDef(*m, "ship.sparrow", &errors);
  auto db = loadCommodities(*m, "economy.commodities", &errors);
  if (!scene || !ship || !db) return f;
  auto prog = loadProgression(*m, *scene, *db, &errors);
  if (!prog) {
    for (auto& e : errors) std::fprintf(stderr, "    session6: %s\n", e.c_str());
    return f;
  }
  f.content = std::make_unique<ProgressionContent>(std::move(*prog));
  f.scene = *scene;
  f.s = std::make_unique<GameSession>(buildSpaceWorld(*scene, *ship, {}, start));
  GameSession& s = *f.s;
  s.setShipInterior(ship->interior);
  for (const char* id : {"interior.sparrow", "interior.kestrel_hangar", "interior.gale_ridge"})
    if (auto d = loadInteriorDef(*m, id, &errors)) s.addInterior(id, *d);
  s.setEconomy(*db, scene->startingCredits);
  for (const char* id : {"market.kestrel", "market.gale_ridge"})
    if (auto md = loadMarketDef(*m, id, s.commodities(), &errors)) s.addMarket(id, *md);
  installProgression(s, *scene, *f.content);
  for (auto& e : errors) std::fprintf(stderr, "    session6: %s\n", e.c_str());
  return f;
}

bool runUntil(GameSession& s, float seconds, const std::function<bool()>& done,
              const std::function<void()>& each = {}) {
  for (int i = 0; i < int(seconds / kDt); ++i) {
    if (each) each();
    s.fixedUpdate(kDt);
    const Vec3 p = s.world().player().state.position;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    if (done()) return true;
  }
  return false;
}

void face(GameSession& s, const Vec3& point) {
  auto& st = s.world().player().state;
  const Vec3 f = normalize(point - st.position);
  const Vec3 axis = cross(Vec3{0, 0, -1}, f);
  const float ang = std::acos(std::clamp(dot(Vec3{0, 0, -1}, f), -1.0f, 1.0f));
  st.orientation = length(axis) > 1e-5f ? Quat::fromAxisAngle(normalize(axis), ang) : Quat{};
  st.angularVelocity = {};
}

void teleport(GameSession& s, const Vec3& local) {
  auto& p = s.world().player();
  p.state.position = local;
  p.state.velocity = {};
  p.state.angularVelocity = {};
  p.previous = p.state;
}

const Prop* kestrel(GameSession& s) {
  for (const auto& p : s.world().props())
    if (p.def.docking) return &p;
  return nullptr;
}

bool dockAtKestrel(GameSession& s) {
  const Prop* st = kestrel(s);
  if (!st) return false;
  teleport(s, st->local + Vec3{300, 250, 400});
  if (!s.docking().request(s.world())) return false;
  if (!runUntil(s, 3.0f, [&] { return s.docking().state() == DockState::ApproachAuthorized; })) return false;
  s.docking().toggleAutopilot();
  return runUntil(s, 240.0f, [&] { return s.docking().state() == DockState::Docked; });
}

bool undock(GameSession& s) {
  return s.docking().undock(s.world()) &&
         runUntil(s, 8.0f, [&] { return s.docking().state() == DockState::Undocked; });
}

// Fights with perfect aim at the nearest hostile (tests the combat systems, not the pilot).
void autoFight(GameSession& s) {
  Combat& c = s.combat();
  const auto& t = s.world().target();
  if (!t || t->kind != TargetRef::Kind::Ship || !c.npc(t->index)) c.targetNearestHostile(s.world());
  const auto& t2 = s.world().target();
  if (t2 && t2->kind == TargetRef::Kind::Ship) {
    if (auto lead = c.leadPoint(s.world(), t2->index)) {
      face(s, *lead);
      const float d = length(*lead - s.world().player().state.position);
      s.setFireTriggers(d < 1000.0f && !c.playerArms().overheated, d < 1400.0f);
      return;
    }
  }
  s.setFireTriggers(false, false);
}
}  // namespace

TEST(durability_shields_absorb_then_hull_and_recharge) {
  CombatParams p;
  p.shieldRegen = 10;
  p.shieldDelay = 2;
  Durability d;
  d.reset(100, 50, p);
  CHECK(d.apply(30, 1, 1, 0.0) == 0.0f);
  CHECK(std::fabs(d.shield - 20) < 1e-4f && d.hull == 100);
  const float hullDmg = d.apply(30, 1, 1, 0.5);  // 20 into shields, 10 through to the hull
  CHECK(std::fabs(hullDmg - 10) < 1e-4f);
  CHECK(d.shield == 0 && std::fabs(d.hull - 90) < 1e-4f);
  d.tick(1.0f, 1.0);  // inside the recharge delay
  CHECK(d.shield == 0);
  d.tick(1.0f, 3.0);
  CHECK(std::fabs(d.shield - 10) < 1e-4f);
  d.apply(500, 1, 1, 4.0);
  CHECK(d.dead() && d.hull == 0);
}

TEST(armament_heat_overheats_and_cools) {
  auto f = session6();
  CHECK(f.s != nullptr);
  if (!f.s) return;
  GameSession& s = *f.s;
  Armament& a = s.combat().playerArms();
  CHECK(a.primary.size() == 2);
  CHECK(a.secondary.size() == 1);
  s.setFireTriggers(true, false);
  CHECK(runUntil(s, 20.0f, [&] { return a.overheated; }));
  CHECK(!s.combat().projectiles().empty());
  s.setFireTriggers(false, false);
  CHECK(runUntil(s, 10.0f, [&] { return !a.overheated; }));
  CHECK(a.heat < a.heatCapacity * 0.3f + 1e-3f);
}

TEST(upgrades_modify_the_flown_ship) {
  auto f = session6();
  CHECK(f.s != nullptr);
  if (!f.s) return;
  GameSession& s = *f.s;
  CHECK(f.content->upgrades.all().size() == 6);
  const float cargo0 = s.cargo().capacityTons();
  const float hull0 = s.combat().player().maxHull;
  CHECK(!s.buyUpgrade("upgrade.cargo_rack").ok);  // no shipyard in open space
  CHECK(s.docking().forceDock(s.world()));
  CHECK(s.hasService("upgrades"));
  s.setCredits(5000);
  CHECK(s.buyUpgrade("upgrade.cargo_rack").ok);
  CHECK(s.buyUpgrade("upgrade.hull_plating").ok);
  CHECK(!s.buyUpgrade("upgrade.hull_plating").ok);  // once only
  CHECK(s.cargo().capacityTons() > cargo0 + 7.9f);
  CHECK(s.combat().player().maxHull > hull0 + 59.0f);
  CHECK(s.credits() == 5000 - 900 - 1100);
  CHECK(s.buyUpgrade("upgrade.weapon_capacitors").ok);
  CHECK(std::fabs(s.combat().playerArms().damageScale - 1.25f) < 1e-4f);
  CHECK(s.world().player().def.cargoCapacityTons == s.cargo().capacityTons());
  // Repairs cost credits and restore the hull.
  s.combat().player().hull = 50;
  const auto cost = s.repairCost();
  CHECK(cost > 0);
  const auto before = s.credits();
  CHECK(s.repair().ok);
  CHECK(s.credits() == before - cost);
  CHECK(s.combat().player().hull == s.combat().player().maxHull);
}

TEST(contract_offers_are_deterministic_and_courier_completes) {
  auto f = session6();
  CHECK(f.s != nullptr);
  if (!f.s) return;
  GameSession& s = *f.s;
  CHECK(s.contractOffers().empty());  // not parked
  CHECK(s.docking().forceDock(s.world()));
  const auto a = s.contractOffers();
  const auto b = s.contractOffers();
  CHECK(a.size() == 3);
  CHECK(a.size() == b.size());
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) CHECK(a[i].title == b[i].title && a[i].reward == b[i].reward);
  // Find a courier job (scan a few days if needed).
  std::optional<Contract> courier;
  for (int day = 0; day < 20 && !courier; ++day) {
    s.world().setSimulationTime(day * f.content->contracts.daySeconds + 5.0);
    for (const auto& c : s.contractOffers())
      if (c.kind == ContractKind::Courier) courier = c;
  }
  CHECK(courier.has_value());
  if (!courier) return;
  CHECK(courier->destination == "site.gale_ridge");
  CHECK(s.acceptContract(*courier).ok);
  CHECK(s.cargo().units("cargo.sealed") == courier->units);
  CHECK(!s.acceptContract(*courier).ok);  // taken
  for (const auto& c : s.contractOffers()) CHECK(c.key != courier->key);
  // Arrive at the destination: paid on landing.
  const std::int64_t before = s.credits();
  CHECK(s.docking().undock(s.world()));
  CHECK(runUntil(s, 8.0f, [&] { return s.docking().state() == DockState::Undocked; }));
  s.landing().landAt(s.world(), LandingSiteRef{0, 0});
  CHECK(runUntil(s, 1.0f, [&] { return s.contracts().active().empty(); }));
  CHECK(s.credits() == before + courier->reward);
  CHECK(s.cargo().units("cargo.sealed") == 0);
  CHECK(s.contracts().completedCount() == 1);
}

TEST(player_weapons_destroy_a_ship_and_leave_salvage) {
  auto f = session6();
  CHECK(f.s != nullptr);
  if (!f.s) return;
  GameSession& s = *f.s;
  const Vec3 me = s.world().player().state.position;
  const auto id = s.combat().spawn(s.world(), "ship.pirate_raider", me + Vec3{0, 0, -500}, -1, -1);
  CHECK(id != 0);
  int destroyed = 0;
  bool hitByPlayer = false;
  CHECK(runUntil(
      s, 40.0f,
      [&] {
        for (const auto& e : s.takeCombatEvents()) {
          if (e.type == CombatEvent::Type::ShipDestroyed && e.byPlayer) ++destroyed;
          if (e.type == CombatEvent::Type::NpcHit && e.byPlayer) hitByPlayer = true;
        }
        return destroyed > 0;
      },
      [&] { autoFight(s); }));
  CHECK(hitByPlayer);
  CHECK(s.combat().npc(id) == nullptr);  // (a traffic hauler may be around)
  CHECK(!s.mining().chunks().empty());  // salvage canisters
  CHECK(s.mining().chunks().front().commodity == "goods.parts");
  CHECK(!s.combat().explosions().empty());
  CHECK(!s.world().target().has_value());  // the dead target is dropped
}

TEST(pirate_ai_intercepts_and_attacks) {
  auto f = session6();
  CHECK(f.s != nullptr);
  if (!f.s) return;
  GameSession& s = *f.s;
  const Vec3 me = s.world().player().state.position;
  const auto id = s.combat().spawn(s.world(), "ship.pirate_raider", me + Vec3{1800, 0, -800}, -1, -1);
  bool attacked = false, intercepted = false;
  CHECK(runUntil(s, 60.0f, [&] {
    if (const NpcShip* n = s.combat().npc(id)) {
      intercepted |= n->ai == AiState::Intercept;
      attacked |= n->ai == AiState::Attack;
    }
    const Durability& d = s.combat().player();
    return d.shield < d.maxShield - 10 || d.hull < d.maxHull;
  }));
  CHECK(intercepted);
  CHECK(attacked);
  const NpcShip* n = s.combat().npc(id);
  CHECK(n && n->hostile);
}

TEST(trader_flees_when_shot_and_resumes_route) {
  auto f = session6();
  CHECK(f.s != nullptr);
  if (!f.s) return;
  GameSession& s = *f.s;
  const Vec3 me = s.world().player().state.position;
  const auto id = s.combat().spawn(s.world(), "ship.trader_hauler", me + Vec3{0, 0, -400}, -1, -1);
  NpcShip* n = s.combat().npc(id);
  CHECK(n && n->ai == AiState::Route);
  s.world().setTarget({TargetRef::Kind::Ship, id, 0});
  CHECK(s.world().targetInfo().has_value());
  CHECK(s.world().targetInfo()->marker == "ship");
  CHECK(runUntil(
      s, 10.0f, [&] { return s.combat().npc(id) && s.combat().npc(id)->ai == AiState::Flee; },
      [&] {
        if (auto lead = s.combat().leadPoint(s.world(), id)) face(s, *lead);
        s.setFireTriggers(true, false);
      }));
  s.setFireTriggers(false, false);
  CHECK(runUntil(s, 20.0f, [&] { return s.combat().npc(id) && s.combat().npc(id)->ai == AiState::Route; }));
}

TEST(homing_rocket_tracks_a_crossing_target) {
  auto f = session6();
  CHECK(f.s != nullptr);
  if (!f.s) return;
  GameSession& s = *f.s;
  const Vec3 me = s.world().player().state.position;
  const auto id = s.combat().spawn(s.world(), "ship.trader_hauler", me + Vec3{0, 0, -700}, -1, -1);
  s.world().setTarget({TargetRef::Kind::Ship, id, 0});
  face(s, me + Vec3{0, 0, -700});
  s.combat().npc(id)->state.velocity = {60, 0, 0};  // crossing left to right
  s.setFireTriggers(false, true);
  s.fixedUpdate(kDt);
  s.setFireTriggers(false, false);
  bool hit = false;
  runUntil(s, 6.0f, [&] {
    for (const auto& e : s.takeCombatEvents()) hit |= e.type == CombatEvent::Type::NpcHit && e.byPlayer;
    return hit;
  });
  CHECK(hit);
}

TEST(defeat_respawns_docked_and_charges_insurance) {
  auto f = session6();
  CHECK(f.s != nullptr);
  if (!f.s) return;
  GameSession& s = *f.s;
  s.cargo().add("ore.iron", 4);
  s.combat().player().hull = 3;
  s.combat().player().shield = 0;
  const Vec3 me = s.world().player().state.position;
  s.combat().spawn(s.world(), "ship.pirate_raider", me + Vec3{0, 0, -600}, -1, -1);
  const auto credits = s.credits();
  CHECK(runUntil(s, 60.0f, [&] { return s.defeats() == 1; }));
  CHECK(s.docking().state() == DockState::Docked);
  CHECK(s.cargo().usedTons() == 0);
  CHECK(s.credits() == credits - f.scene.insuranceDeductible);
  CHECK(s.combat().player().hull == s.combat().player().maxHull);
  CHECK(s.combat().npcs().empty());
  // A collision at speed also hurts.
  CHECK(undock(s));
  const float h0 = s.combat().player().hull + s.combat().player().shield;
  s.combat().applyCollisionDamage(40.0f, s.world().simulationTime(), s.world().player().def.combat);
  CHECK(s.combat().player().hull + s.combat().player().shield < h0 - 30.0f);
}

TEST(save_v2_files_migrate_to_v3) {
  const char* v2 = R"({"schemaVersion":2,"sceneId":"scene.stage2_flight","worldTime":12.5,
    "player":{"location":"cockpit","docked":false,"dockedStation":"","dockedBay":0,"landedPlanet":"","landedSite":"",
    "shipPosition":[1,2,3],"shipOrientation":[0,0,0,1],"shipVelocity":[0,0,0],"boostEnergy":50,"credits":700,
    "cargo":{"ore.iron":2}},"markets":{},"asteroids":[]})";
  std::string err;
  auto s = deserializeSave(v2, &err);
  CHECK(s.has_value());
  if (!s) return;
  CHECK(s->hull < 0 && s->upgrades.empty() && s->contracts.empty());
  CHECK(s->credits == 700);
  auto again = deserializeSave(serializeSave(*s), &err);
  CHECK(again.has_value());
}

TEST(stage6_exit_combat_or_trade_contract_then_upgrade) {
  auto f = session6();
  CHECK(f.s != nullptr);
  if (!f.s) return;
  GameSession& s = *f.s;
  SpaceWorld& w = s.world();
  CHECK(s.docking().forceDock(w));
  const std::int64_t startCredits = s.credits();

  // Pick today's jobs at Kestrel: a bounty (combat route) and an ore procurement (trade route).
  std::optional<Contract> bounty, procure;
  for (int day = 0; day < 30 && (!bounty || !procure); ++day) {
    w.setSimulationTime(day * f.content->contracts.daySeconds + 1.0);
    for (const auto& c : s.contractOffers()) {
      if (c.kind == ContractKind::Bounty && !bounty) bounty = c;
      if (c.kind == ContractKind::Procurement && !procure && c.units <= 10) procure = c;
    }
    if (bounty && procure && bounty->key.substr(0, bounty->key.rfind('/')) !=
                                 procure->key.substr(0, procure->key.rfind('/')))
      bounty.reset();  // keep both from the same day so both are on the board together
  }
  CHECK(bounty.has_value() && procure.has_value());
  if (!bounty || !procure) return;
  CHECK(s.acceptContract(*bounty).ok);
  CHECK(s.acceptContract(*procure).ok);
  CHECK(s.contracts().active().size() == 2);

  // ---- Combat route: fly into the deep Cinder Drift, kill raiders until the bounty is met.
  CHECK(undock(s));
  const EncounterDef& enc = f.scene.encounters.at(0);
  const Vec3 zone = w.frame().toLocal(enc.center);
  teleport(s, zone + normalize(w.player().state.position - zone) * 3000.0f);
  int kills = 0;
  const bool bountyDone = runUntil(
      s, 300.0f,
      [&] {
        for (const auto& e : s.takeCombatEvents()) kills += e.type == CombatEvent::Type::ShipDestroyed && e.byPlayer;
        return s.contracts().active().size() == 2 && s.contracts().active()[0].ready() &&
               s.contracts().active()[0].kind == ContractKind::Bounty;
      },
      [&] { autoFight(s); });
  s.setFireTriggers(false, false);
  CHECK(bountyDone);
  CHECK(s.defeats() == 0);
  CHECK(kills >= bounty->killsRequired);
  std::printf("    combat route: %d kill(s), hull %.0f/%.0f\n", kills, s.combat().player().hull,
              s.combat().player().maxHull);
  const std::int64_t beforeTurnIn = s.credits();
  CHECK(dockAtKestrel(s));
  CHECK(s.credits() >= beforeTurnIn + bounty->reward);
  CHECK(s.contracts().active().size() == 1);
  if (s.repairCost() > 0) CHECK(s.repair().ok);

  // ---- Save / reload mid-way: the procurement job and the bounty payout survive.
  const SaveGame saved = captureSave(s, f.scene.id);
  std::string err;
  auto parsed = deserializeSave(serializeSave(saved), &err);
  CHECK(parsed.has_value());
  if (!parsed) return;
  auto f2 = session6(SystemPosition{parsed->shipPosition});
  CHECK(f2.s != nullptr);
  if (!f2.s) return;
  CHECK(applySave(*f2.s, *parsed, &err));
  GameSession& s2 = *f2.s;
  CHECK(s2.credits() == s.credits());
  CHECK(s2.contracts().active().size() == 1);
  CHECK(s2.contracts().completedCount() == 1);
  CHECK(serializeSave(captureSave(s2, f.scene.id)) == serializeSave(saved));

  // ---- Trade route on the reloaded game: mine the requested ore and deliver it.
  SpaceWorld& w2 = s2.world();
  const Contract job = s2.contracts().active().front();
  CHECK(undock(s2));
  std::uint32_t pick = 0;
  int best = 0;
  const Vec3 home = w2.player().state.position;
  for (std::uint32_t i = 0; i < w2.asteroids().size(); ++i) {
    const Asteroid& a = w2.asteroids()[i];
    if (a.resource == job.commodity && a.yield > best && a.radius < 40 && length(a.local - home) < 9000) {
      best = a.yield;
      pick = i;
    }
  }
  CHECK(best >= job.units);
  const Asteroid& rock = w2.asteroids()[pick];
  teleport(s2, rock.local + normalize(home - rock.local) * (rock.radius + 40.0f));
  face(s2, rock.local);
  w2.targetNearestAsteroid();
  s2.setMiningTrigger(true);
  CHECK(runUntil(s2, 90.0f, [&] { return s2.cargo().units(job.commodity) >= job.units; }));
  s2.setMiningTrigger(false);
  const std::int64_t beforeDelivery = s2.credits();
  CHECK(dockAtKestrel(s2));
  CHECK(s2.contracts().active().empty());
  CHECK(s2.credits() == beforeDelivery + job.reward);
  CHECK(s2.contracts().completedCount() == 2);

  // ---- Spend it: a meaningful upgrade.
  std::printf("    credits: start %lld, now %lld\n", (long long)startCredits, (long long)s2.credits());
  CHECK(s2.credits() > startCredits);
  const float hold0 = s2.cargo().capacityTons();
  CHECK(s2.buyUpgrade("upgrade.cargo_rack").ok);
  CHECK(s2.cargo().capacityTons() > hold0);
  // And it persists.
  auto parsed2 = deserializeSave(serializeSave(captureSave(s2, f.scene.id)), &err);
  CHECK(parsed2 && parsed2->upgrades.size() == 1);
}
