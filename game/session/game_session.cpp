#include "game/session/game_session.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "engine/core/log.h"

namespace fh {

const char* locationName(Location l) {
  switch (l) {
    case Location::Cockpit: return "COCKPIT";
    case Location::ShipInterior: return "SHIP INTERIOR";
    case Location::StationInterior: return "STATION";
  }
  return "?";
}

GameSession::GameSession(std::unique_ptr<SpaceWorld> world) : world_(std::move(world)) {
  baseShip_ = world_->player().def;
  for (FootPlayer& p : footPlayers_) foot_.outfit(p);  // built-in loadout until setFootContent()
  world_->setShipDescriber([this](std::uint32_t id) { return combat_.describe(*world_, id); });
  lastCollisions_ = world_->player().collisions;
  // Locations for contracts: every dockable station and landing site.
  for (const auto& p : world_->props())
    if (p.def.docking && (!p.def.docking->market.empty() || !p.def.docking->services.empty()))  // somewhere with people
      locations_.push_back({p.def.id, p.def.name});
  for (const auto& pl : world_->planets())
    for (const auto& site : pl.def.sites) locations_.push_back({site.id, site.name});
}

const InteriorDef* GameSession::activeInterior() const {
  if (location_ == Location::Cockpit) return nullptr;
  auto it = interiors_.find(activeInteriorId_);
  return it == interiors_.end() ? nullptr : &it->second;
}

ActionResult GameSession::fail(const std::string& msg) {
  notice_ = msg;
  noticeTime_ = world_->simulationTime();
  log::info("session", "{}", msg);
  return {false, msg};
}

ActionResult GameSession::enterInterior(const AssetID& id, const std::string& spawn, Location loc) {
  auto it = interiors_.find(id);
  if (it == interiors_.end()) return fail("Interior '" + id + "' is not loaded");
  auto sp = it->second.spawns.find(spawn);
  if (sp == it->second.spawns.end()) return fail("Interior '" + id + "' has no spawn '" + spawn + "'");
  activeInteriorId_ = id;
  colliders_ = it->second.buildColliders();
  if (it->second.parkedShip) {
    // The parked ship blocks walking: a box around its footprint.
    const auto& ps = *it->second.parkedShip;
    const Vec3 h = it->second.parkedShipColliderHalfExtents;
    if (h.x > 0 && h.y > 0 && h.z > 0) colliders_.push_back({ps.position - h, ps.position + h});
  }
  characterParams_.gravity = it->second.gravity;
  terrain_ = it->second.terrain ? HeightField(*it->second.terrain) : HeightField{};
  character_ = CharacterState{};
  character_.feet = sp->second.position;
  if (terrain_.valid())
    character_.feet.y = std::max(character_.feet.y, terrain_.heightAt(character_.feet.x, character_.feet.z));
  character_.yaw = radians(sp->second.yawDeg);
  location_ = loc;
  beginFoot(it->second);
  notice_ = it->second.name;
  noticeTime_ = world_->simulationTime();
  log::info("session", "entered {} ({})", it->second.name, locationName(loc));
  return {true, it->second.name};
}

const AssetID* GameSession::surfaceSceneId() const {
  if (!landing_.onSurface() || !landing_.site()) return nullptr;
  return &world_->planets().at(landing_.site()->planet).def.sites.at(landing_.site()->site).interior;
}

const InteriorDef* GameSession::surfaceScene() const {
  const AssetID* id = surfaceSceneId();
  if (!id) return nullptr;
  auto it = interiors_.find(*id);
  return it == interiors_.end() ? nullptr : &it->second;
}

bool GameSession::engageTravel() {
  if (!freeFlight()) return fail("Travel drive unavailable right now").ok;
  const auto& t = world_->target();
  if (!t || t->kind == TargetRef::Kind::Asteroid) return fail("Select a destination first").ok;
  return travel_.engage(*world_, *t, world_->player().def.travel);
}

bool GameSession::requestLanding() {
  if (location_ != Location::Cockpit || !freeFlight()) return fail("Cannot begin a landing right now").ok;
  return landing_.request(*world_);
}

bool GameSession::launch() {
  if (location_ != Location::Cockpit) return fail("Take the pilot seat to launch").ok;
  return landing_.launch(*world_);
}

ActionResult GameSession::leaveSeat() {
  if (location_ != Location::Cockpit) return fail("You are not in the pilot seat");
  if (!parked()) return fail("Leaving the seat is only safe while docked or landed");
  if (shipInterior_.empty()) return fail("This ship has no walkable interior");
  return enterInterior(shipInterior_, "seat", Location::ShipInterior);
}

ActionResult GameSession::perform(const std::string& action) {
  if (action.rfind("say:", 0) == 0) {  // a data log in a map
    notify(action.substr(4));
    return {true, notice_};
  }
  if (action.rfind("loot:", 0) == 0) return takeLoot(std::atoi(action.c_str() + 5));
  if (action == "leaveMap") {
    if (docking_.state() == DockState::Docked && docking_.station()) {
      const auto& dock = world_->props().at(*docking_.station()).def.docking;
      if (dock && dock->interior == activeInteriorId_) return enterInterior(shipInterior_, "airlock", Location::ShipInterior);
    }
    mapExitRequested_ = true;  // played straight from Blender: nothing to go back to
    return {true, ""};
  }
  if (action == "takeSeat") {
    if (location_ != Location::ShipInterior) return fail("The pilot seat is aboard your ship");
    location_ = Location::Cockpit;
    activeInteriorId_.clear();
    colliders_.clear();
    notice_ = "Pilot seat. L to undock when ready.";
    noticeTime_ = world_->simulationTime();
    return {true, notice_};
  }
  if (action == "exitShip") {
    if (location_ != Location::ShipInterior) return fail("You are not aboard");
    if (landing_.state() == LandState::Landed) {
      if (const AssetID* id = surfaceSceneId()) return enterInterior(*id, "fromShip", Location::StationInterior);
    }
    if (docking_.state() != DockState::Docked) return fail("Airlock sealed: the ship is not docked");
    const auto station = docking_.station();
    const auto& dock = world_->props().at(*station).def.docking;
    if (!dock) return fail("This station has no walkable interior");
    return enterInterior(dock->interior, "fromShip", Location::StationInterior);
  }
  if (action == "boardShip") {
    if (location_ != Location::StationInterior) return fail("Your ship is not here");
    return enterInterior(shipInterior_, "airlock", Location::ShipInterior);
  }
  if (action == "terminal") {
    if (!dockedMarket() && !hasService("contracts") && !hasService("repair"))
      return fail("Terminal offline: nothing on this terminal");
    marketRequested_ = true;
    return {true, "Market terminal"};
  }
  return fail("Unknown action '" + action + "'");
}

bool GameSession::markerCleared(int marker) const {
  return clearedMarkers_.count(activeInteriorId_ + "#" + std::to_string(marker)) > 0;
}

void GameSession::setFootContent(const FootContent* content) {
  foot_.configure(content);
  for (FootPlayer& p : footPlayers_) foot_.outfit(p);
}

const NavGrid* GameSession::activeNav() const {
  auto it = navs_.find(activeInteriorId_);
  return it == navs_.end() ? nullptr : it->second.get();
}

void GameSession::cycleFootWeapon(int direction) {
  FootPlayer& me = footPlayers_.front();
  const int n = int(me.owned.size());
  if (n == 0) return;
  for (int k = 1; k <= n; ++k) {
    const int w = ((me.weapon + direction * k) % n + n) % n;
    if (me.owned[size_t(w)]) {
      if (w != me.weapon) me.switchRequest = w;
      return;
    }
  }
}

void GameSession::refitFoot() {
  for (FootPlayer& p : footPlayers_) {
    if (p.magazines.size() != foot_.content().weapons.size()) {
      foot_.outfit(p);
      continue;
    }
    p.health = 100;
    p.reloadLeft = p.switchLeft = 0;
    for (size_t w = 0; w < p.owned.size(); ++w)
      if (p.owned[w]) p.magazines[w] = foot_.content().weapons[w].magazine;
    for (const auto& [id, n] : foot_.content().startAmmo)
      if (const int a = foot_.content().ammoIndex(id); a >= 0) p.ammo[size_t(a)] = std::max(p.ammo[size_t(a)], n);
  }
}

void GameSession::beginFoot(const InteriorDef& def) {
  std::vector<FootSpawn> spawns;
  bool hostile = false;
  for (int i = 0; i < int(def.markers.size()); ++i) {
    const InteriorMarker& m = def.markers[size_t(i)];
    if (m.type != "enemy_spawn") continue;
    hostile = true;
    if (!markerCleared(i)) spawns.push_back({i, m.position, m.yawDeg, m.kind});
  }
  footArmed_ = hostile;
  foot_.begin(spawns);
  footEvents_.clear();
  if (hostile && def.meshCollider && !navs_.count(activeInteriorId_)) {
    // Where enemies can walk: baked once per map, the first time it is entered.
    auto nav = std::make_unique<NavGrid>();
    if (nav->build(*def.meshCollider))
      log::info("foot", "nav grid for '{}': {} cells, {} links", activeInteriorId_, nav->nodeCount(), nav->edgeCount());
    navs_[activeInteriorId_] = std::move(nav);
  }
  rebuildDrops();
  if (!hostile) refitFoot();  // back somewhere safe: patched up, and the locker refills the guns
}

void GameSession::rebuildDrops() {
  drops_.clear();
  const std::string prefix = activeInteriorId_ + "#drop";
  for (const auto& [key, pos] : dropPositions_) {
    if (key.rfind(prefix, 0) != 0 || clearedMarkers_.count(key)) continue;
    const InteriorDef* def = activeInterior();
    const int marker = std::atoi(key.c_str() + prefix.size());
    std::string table;
    if (def && marker >= 0 && marker < int(def->markers.size())) {
      const int e = foot_.content().enemyIndex(def->markers[size_t(marker)].kind.empty() ? "crawler" : def->markers[size_t(marker)].kind);
      if (e >= 0) table = foot_.content().enemies[size_t(e)].dropTable;
    }
    if (!table.empty()) drops_.push_back({key, table, pos});
  }
}

void GameSession::pickUpDrops() {
  FootPlayer& me = footPlayers_.front();
  for (size_t i = 0; i < drops_.size();) {
    const FootDrop& d = drops_[i];
    const Vec3 rel = d.position - character_.feet;
    const LootTable* table = foot_.content().lootTable(d.table);
    if (!table || length(Vec3{rel.x, 0, rel.z}) > 1.1f || std::fabs(rel.y) > 1.2f) {
      ++i;
      continue;
    }
    int credits = 0;
    const std::string what = foot_.apply(me, rollLoot(*table, lootSeed(d.key + "#roll")), &credits);
    credits_ += credits;
    clearedMarkers_.insert(d.key);
    dropPositions_.erase(d.key);
    footEvents_.push_back({FootEvent::Type::Pickup, d.position});
    notify("Picked up: " + what);
    drops_.erase(drops_.begin() + std::ptrdiff_t(i));
  }
}

void GameSession::stepFoot(float dt) {
  if (!footArmed_) return;
  const InteriorDef* def = activeInterior();
  if (!def) return;
  FootPlayer& me = footPlayers_.front();
  me.feet = character_.feet;
  me.eye = character_.eye(characterParams_);
  me.look = character_.lookDirection();
  foot_.update(dt, footPlayers_, colliders_, def->meshCollider.get(), activeNav());
  if (length(me.knock) > 0) {  // a charge sends you flying
    character_.velocity += me.knock;
    me.knock = {};
  }
  for (const FootEvent& e : foot_.takeEvents()) {
    footEvents_.push_back(e);
    if (e.type == FootEvent::Type::EnemyDied) {
      const FootEnemy& dead = foot_.enemies()[size_t(e.enemy)];
      const std::string key = activeInteriorId_ + "#" + std::to_string(dead.markerIndex);
      clearedMarkers_.insert(key);
      // A drop or not is decided by the map and marker, so a reload cannot reroll it.
      const FootEnemyDef& d = foot_.enemyDef(dead);
      const std::string dropKey = activeInteriorId_ + "#drop" + std::to_string(dead.markerIndex);
      const float roll = float(lootSeed(dropKey) >> 40) / float(1u << 24);
      if (!d.dropTable.empty() && roll < d.dropChance && foot_.content().lootTable(d.dropTable)) {
        dropPositions_[dropKey] = dead.body.feet + Vec3{0, 0.02f, 0};
        drops_.push_back({dropKey, d.dropTable, dropPositions_[dropKey]});
      }
    }
    if (e.type == FootEvent::Type::PlayerDown) {
      // For now you wake at the way in, hurt, and they lose interest. Nothing is taken from you.
      const auto spawn = def->spawns.count("start") ? def->spawns.find("start") : def->spawns.find("fromShip");
      if (spawn != def->spawns.end()) {
        character_ = CharacterState{};
        character_.feet = spawn->second.position;
        character_.yaw = radians(spawn->second.yawDeg);
      }
      me.health = 60;
      me.trigger = false;
      foot_.returnHome();
      notify("You black out. Your suit drags you back to the way in.");
    }
  }
  pickUpDrops();
}

ActionResult GameSession::takeLoot(int marker) {
  const InteriorDef* def = activeInterior();
  if (!def || marker < 0 || marker >= int(def->markers.size())) return fail("Nothing to take");
  if (markerCleared(marker)) return fail("Already taken");
  const InteriorMarker& m = def->markers[size_t(marker)];
  const LootTable* table = foot_.content().lootTable(m.kind.empty() ? "salvage" : m.kind);
  if (!table) table = foot_.content().lootTable("salvage");
  if (!table) return fail("Nothing useful");
  FootPlayer& me = footPlayers_.front();
  const std::string key = activeInteriorId_ + "#" + std::to_string(marker);
  const LootRoll roll = rollLoot(*table, lootSeed(key));
  int credits = 0;
  const std::string what = foot_.apply(me, roll, &credits);
  credits_ += credits;
  clearedMarkers_.insert(key);
  footEvents_.push_back({FootEvent::Type::Pickup, m.position});
  if (!roll.weapon.empty()) footEvents_.push_back({FootEvent::Type::Unlocked, m.position, {}, 0, -1, me.switchRequest});
  const std::string message = table->mesh == "medical" ? "Medical kit: " + what : "Took: " + what;
  notify(message);
  return {true, message};
}

ActionResult GameSession::enterMap(const AssetID& id) {
  return enterInterior(id, "start", Location::StationInterior);
}

std::string GameSession::promptFor(const Interactable& it) const {
  if (it.action.rfind("loot:", 0) == 0)
    if (const InteriorDef* def = activeInterior()) {
      const int marker = std::atoi(it.action.c_str() + 5);
      if (marker >= 0 && marker < int(def->markers.size())) {
        const std::string& kind = def->markers[size_t(marker)].kind;
        if (const LootTable* t = foot_.content().lootTable(kind.empty() ? "salvage" : kind)) return t->prompt;
      }
    }
  return it.prompt;
}

const Interactable* GameSession::focusedInteractable() const {
  const InteriorDef* def = activeInterior();
  if (!def) return nullptr;
  const Vec3 eye = character_.eye(characterParams_);
  const Vec3 look = character_.lookDirection();
  const Interactable* best = nullptr;
  float bestScore = -1;
  for (const auto& it : def->interactables) {
    if (it.action.rfind("loot:", 0) == 0 && markerCleared(std::atoi(it.action.c_str() + 5))) continue;
    const Vec3 to = it.position - eye;
    const float d = length(to);
    if (d > it.radius + 0.6f) continue;
    const float facing = d > 1e-3f ? dot(to / d, look) : 1.0f;
    if (facing < 0.45f && d > 0.8f) continue;
    const float score = facing - d * 0.1f;
    if (score > bestScore) {
      bestScore = score;
      best = &it;
    }
  }
  return best;
}

ActionResult GameSession::interact() {
  if (location_ == Location::Cockpit) return leaveSeat();
  const Interactable* it = focusedInteractable();
  if (!it) return fail("Nothing to use here");
  return perform(it->action);
}

void GameSession::setEconomy(CommodityDb db, std::int64_t credits) {
  commodities_ = std::move(db);
  credits_ = credits;
  cargo_ = CargoHold(&commodities_, world_->player().def.cargoCapacityTons);
  mining_.setParams(world_->player().def.mining);
}

void GameSession::setProgression(const SceneDef& scene, const WeaponDb* weapons, const UpgradeDb* upgrades,
                                 const ContractDb* contracts) {
  upgradeDb_ = upgrades;
  contractDb_ = contracts;
  deductible_ = scene.insuranceDeductible;
  // Platform-independent seed (std::hash differs between standard libraries).
  std::uint64_t seed = 1469598103934665603ull;
  for (unsigned char ch : scene.id) seed = (seed ^ ch) * 1099511628211ull;
  combat_.configure(weapons, scene, seed);
  applyShip();
  combat_.player().hull = combat_.player().maxHull;
  combat_.player().shield = combat_.player().maxShield;
}

void GameSession::applyShip() {
  float dmg = 1.0f;
  const ShipDef ship = upgradeDb_ ? applyUpgrades(baseShip_, *upgradeDb_, upgrades_, &dmg) : baseShip_;
  PlayerShip& p = world_->player();
  // Keep runtime-only values (flight state lives outside the def).
  p.def = ship;
  cargo_.setCapacity(ship.cargoCapacityTons);
  mining_.setParams(ship.mining);
  combat_.setupPlayer(ship);
  combat_.playerArms().damageScale = dmg;
}

bool GameSession::ownsUpgrade(const std::string& id) const {
  return std::find(upgrades_.begin(), upgrades_.end(), id) != upgrades_.end();
}

void GameSession::setOwnedUpgrades(std::vector<std::string> ids) {
  upgrades_ = std::move(ids);
  applyShip();
}

ActionResult GameSession::buyUpgrade(const std::string& id) {
  if (!hasService("upgrades")) return fail("No shipyard here");
  const UpgradeDef* u = upgradeDb_ ? upgradeDb_->find(id) : nullptr;
  if (!u) return fail("Unknown upgrade");
  if (ownsUpgrade(id)) return fail(u->name + " is already fitted");
  if (credits_ < u->price) return fail(std::format("{} costs {} cr", u->name, u->price));
  credits_ -= u->price;
  upgrades_.push_back(id);
  applyShip();
  log::info("upgrades", "fitted {} for {} cr (balance {})", u->name, u->price, credits_);
  notify("Fitted: " + u->name);
  return {true, notice_};
}

std::int64_t GameSession::repairCost() const {
  const Durability& d = combat_.player();
  const float missing = std::max(0.0f, d.maxHull - d.hull);
  const float price = upgradeDb_ ? upgradeDb_->repairPricePerHull : 3.0f;
  return std::int64_t(std::ceil(missing * price));
}

ActionResult GameSession::repair() {
  if (!hasService("repair")) return fail("No repair service here");
  const std::int64_t cost = repairCost();
  if (cost <= 0) return fail("Hull is already sound");
  Durability& d = combat_.player();
  if (credits_ < cost) {
    // Partial repair with whatever we can afford.
    const float price = upgradeDb_ ? upgradeDb_->repairPricePerHull : 3.0f;
    const float points = float(credits_) / price;
    if (points < 1.0f) return fail(std::format("Repairs cost {} cr", cost));
    d.hull = std::min(d.maxHull, d.hull + points);
    credits_ = 0;
    notify("Partial repair: credits exhausted");
    return {true, notice_};
  }
  credits_ -= cost;
  d.hull = d.maxHull;
  d.shield = d.maxShield;
  log::info("upgrades", "repaired hull for {} cr", cost);
  notify(std::format("Hull repaired ({} cr)", cost));
  return {true, notice_};
}

std::string GameSession::currentLocationId() const {
  if (landing_.state() == LandState::Landed && landing_.site())
    return world_->planets().at(landing_.site()->planet).def.sites.at(landing_.site()->site).id;
  if (docking_.state() == DockState::Docked && docking_.station()) return world_->props().at(*docking_.station()).def.id;
  return {};
}

bool GameSession::hasService(const std::string& s) const {
  const std::vector<std::string>* list = nullptr;
  if (landing_.state() == LandState::Landed && landing_.site())
    list = &world_->planets().at(landing_.site()->planet).def.sites.at(landing_.site()->site).services;
  else if (docking_.state() == DockState::Docked && docking_.station())
    if (const auto& d = world_->props().at(*docking_.station()).def.docking) list = &d->services;
  return list && std::find(list->begin(), list->end(), s) != list->end();
}

std::vector<Contract> GameSession::contractOffers() const {
  if (!contractDb_ || !hasService("contracts")) return {};
  return contracts_.offers(*contractDb_, commodities_, currentLocationId(), locations_, world_->simulationTime());
}

ActionResult GameSession::acceptContract(const Contract& offer) {
  if (!contractDb_ || !hasService("contracts")) return fail("No contract board here");
  if (auto err = contracts_.accept(*contractDb_, offer, cargo_)) return fail(*err);
  notify("Accepted: " + offer.title);
  return {true, notice_};
}

ActionResult GameSession::abandonContract(size_t index) {
  if (index >= contracts_.active().size()) return fail("No such contract");
  const std::string title = contracts_.active()[index].title;
  contracts_.abandon(index, cargo_);
  notify("Abandoned: " + title);
  return {true, notice_};
}

void GameSession::onParked() {
  const std::string loc = currentLocationId();
  if (loc.empty()) return;
  ContractUpdate u = contracts_.onParked(loc, locations_, cargo_, credits_);
  if (!u.messages.empty()) notify(u.messages.back());
}

void GameSession::handleCombatEvents() {
  for (auto& e : combat_.takeEvents()) {
    if (e.type == CombatEvent::Type::ShipDestroyed && e.byPlayer) {
      std::vector<std::string> msgs;
      contracts_.onShipDestroyed(e.faction, &msgs);
      const ShipType* t = combat_.shipType(e.shipDef);
      if (t && t->def.ai.bounty > 0) {
        credits_ += t->def.ai.bounty;
        notify(std::format("Target destroyed. Bounty +{} cr", t->def.ai.bounty));
      }
      if (!msgs.empty()) notify(msgs.back());
      log::info("combat", "player destroyed {} ({})", e.shipDef, e.faction);
    } else if (e.type == CombatEvent::Type::Engaged) {
      notify("Contact: hostile ships closing");
    }
    const bool defeat = e.type == CombatEvent::Type::PlayerDestroyed;
    if (combatEvents_.size() >= 512) combatEvents_.erase(combatEvents_.begin(), combatEvents_.begin() + 256);  // unread
    combatEvents_.push_back(std::move(e));
    if (defeat) {
      handleDefeat();
      break;
    }
  }
  // Drop a target that no longer exists.
  if (const auto& t = world_->target(); t && t->kind == TargetRef::Kind::Ship && !combat_.npc(t->index))
    world_->clearTarget();
}

void GameSession::handleDefeat() {
  ++defeats_;
  combat_.takeEvents();
  combat_.despawnAll();
  mining_.clearChunks();
  cargo_.clear();
  auto failed = contracts_.onShipLost();
  const std::int64_t charge = std::min(credits_, deductible_);
  credits_ -= charge;
  world_->clearTarget();
  travel_.cancel(*world_);
  // The insurer tows what is left of the ship to the nearest station with a shipyard.
  std::optional<std::uint32_t> best;
  float bestD = 1e30f;
  for (std::uint32_t i = 0; i < world_->props().size(); ++i) {
    const Prop& p = world_->props()[i];
    if (!p.def.docking) continue;
    const auto& sv = p.def.docking->services;
    const float d = length(p.local - world_->player().state.position) +
                    (std::find(sv.begin(), sv.end(), "repair") != sv.end() ? 0.0f : 1e9f);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  if (best) docking_.dockAt(*world_, *best, 0, false);
  Durability& d = combat_.player();
  d.hull = d.maxHull;
  d.shield = d.maxShield;
  combat_.playerArms().heat = 0;
  combat_.playerArms().overheated = false;
  wasParked_ = true;  // being towed in does not count as arriving for contracts
  log::warn("session", "ship lost (defeat #{}): deductible {} cr, {} contract(s) failed", defeats_, charge,
            failed.size());
  notify(std::format("HULL BREACH. Insurance recovered your ship: -{} cr, cargo lost", charge));
}

Market* GameSession::dockedMarket() {
  AssetID marketId;
  if (landing_.state() == LandState::Landed && landing_.site()) {
    marketId = world_->planets().at(landing_.site()->planet).def.sites.at(landing_.site()->site).market;
  } else if (docking_.state() == DockState::Docked && docking_.station()) {
    const auto& dock = world_->props().at(*docking_.station()).def.docking;
    if (dock) marketId = dock->market;
  }
  if (marketId.empty()) return nullptr;
  auto it = markets_.find(marketId);
  if (it == markets_.end()) return nullptr;
  it->second.advanceTo(world_->simulationTime());
  return &it->second;
}

TradeResult GameSession::trade(const std::string& commodity, int units) {
  Market* m = dockedMarket();
  if (!m) return {0, 0, "No market: dock at a station first"};
  const int i = m->entryIndex(commodity);
  TradeResult r = units > 0 ? m->buy(commodities_, i, units, credits_, cargo_)
                            : m->sell(commodities_, i, -units, credits_, cargo_);
  if (r.units > 0)
    log::info("economy", "{} {} x {} for {} cr (balance {})", units > 0 ? "bought" : "sold", r.units, commodity,
              r.credits, credits_);
  return r;
}

std::optional<Vec3> GameSession::fixedUpdate(float dt) {
  docking_.preStep(*world_, dt);
  travel_.preStep(*world_, world_->player().def.travel, dt);
  if (travel_.state() == TravelState::Transit || travel_.state() == TravelState::Decelerating)
    world_->player().collisionsEnabled = false;  // the drive's corridor ignores small debris
  landing_.preStep(*world_, dt);
  world_->fixedUpdate(dt);
  docking_.postStep(*world_, dt);
  const bool canMine = location_ == Location::Cockpit && freeFlight();
  miningEvents_ = mining_.fixedUpdate(*world_, miningTrigger_ && canMine, cargo_, dt);
  // Collisions hurt (only in free flight; clamps and pads are gentle).
  PlayerShip& ps = world_->player();
  if (ps.collisions != lastCollisions_) {
    lastCollisions_ = ps.collisions;
    if (freeFlight()) combat_.applyCollisionDamage(ps.lastImpactSpeed, world_->simulationTime(), ps.def.combat);
  }
  const bool active = combatActive();
  combat_.fixedUpdate(*world_, mining_, firePrimary_ && active, fireSecondary_ && active, active, dt);
  handleCombatEvents();  // handles PlayerDestroyed (weapons or collisions) -> handleDefeat()
  if (combat_.player().dead()) handleDefeat();  // safety net outside free flight
  const bool parkedNow = parked();
  if (wasParked_ && !*wasParked_ && parkedNow) onParked();
  wasParked_ = parkedNow;
  if (location_ != Location::Cockpit) {
    {
      const InteriorDef* interior = activeInterior();
      stepCharacter(character_, characterInput_, characterParams_, colliders_, dt, terrain_.valid() ? &terrain_ : nullptr,
                    interior ? interior->meshCollider.get() : nullptr);
    }
    stepFoot(dt);
    characterInput_.yawDelta = characterInput_.pitchDelta = 0;  // consumed
    characterInput_.jump = false;
    // Safety net: never let a bad collider set drop the player into the void.
    if (character_.feet.y < -50.0f) {
      const InteriorDef* def = activeInterior();
      const char* spawn = location_ == Location::ShipInterior ? "airlock" : "fromShip";
      if (def && def->spawns.count(spawn)) {
        character_ = CharacterState{};
        character_.feet = def->spawns.at(spawn).position;
        log::warn("session", "player fell out of '{}'; respawned", activeInteriorId_);
      }
    }
  }
  if (auto delta = world_->rebaseIfNeeded()) {
    mining_.applyRebase(*delta);
    combat_.applyRebase(*delta);
    return delta;
  }
  return std::nullopt;
}

}  // namespace fh
