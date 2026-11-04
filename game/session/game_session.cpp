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
  world_->setShipDescriber([this](std::uint32_t id) { return combat_.describe(*world_, id); });
  lastCollisions_ = world_->player().collisions;
  // Locations for contracts: every dockable station and landing site.
  for (const auto& p : world_->props())
    if (p.def.docking) locations_.push_back({p.def.id, p.def.name});
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
  if (!t || t->kind == TargetRef::Kind::Asteroid) return fail("Select a destination first (N)").ok;
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

const Interactable* GameSession::focusedInteractable() const {
  const InteriorDef* def = activeInterior();
  if (!def) return nullptr;
  const Vec3 eye = character_.eye(characterParams_);
  const Vec3 look = character_.lookDirection();
  const Interactable* best = nullptr;
  float bestScore = -1;
  for (const auto& it : def->interactables) {
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
  combat_.configure(weapons, scene, std::hash<std::string>{}(scene.id));
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
    stepCharacter(character_, characterInput_, characterParams_, colliders_, dt, terrain_.valid() ? &terrain_ : nullptr);
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
