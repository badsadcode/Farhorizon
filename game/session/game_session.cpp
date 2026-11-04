#include "game/session/game_session.h"

#include <algorithm>
#include <cmath>

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
    if (!dockedMarket()) return fail("Terminal offline: this station has no market");
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
    return delta;
  }
  return std::nullopt;
}

}  // namespace fh
