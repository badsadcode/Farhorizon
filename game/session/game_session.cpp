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
  CrewMember me;
  me.id = 0;
  me.name = "Pilot";
  players_.push_back(me);
  footPlayers_.resize(1);
  for (FootPlayer& p : footPlayers_) idleFoot_.outfit(p);  // built-in loadout until setFootContent()
  world_->setShipDescriber([this](std::uint32_t id) { return combat_.describe(*world_, id); });
  lastCollisions_ = world_->player().collisions;
  // Locations for contracts: every dockable station and landing site.
  for (const auto& p : world_->props())
    if (p.def.docking && (!p.def.docking->market.empty() || !p.def.docking->services.empty()))  // somewhere with people
      locations_.push_back({p.def.id, p.def.name});
  for (const auto& pl : world_->planets())
    for (const auto& site : pl.def.sites) locations_.push_back({site.id, site.name});
}

// ---- Crew --------------------------------------------------------------------------------------

int GameSession::slotOf(PlayerId id) const {
  for (size_t i = 0; i < players_.size(); ++i)
    if (players_[i].id == id) return int(i);
  return -1;
}

int GameSession::pilotSlot() const {
  for (size_t i = 0; i < players_.size(); ++i)
    if (players_[i].location == Location::Cockpit && players_[i].pilot) return int(i);
  return -1;
}

CrewMember* GameSession::player(PlayerId id) {
  const int s = slotOf(id);
  return s < 0 ? nullptr : &players_[size_t(s)];
}

const CrewMember* GameSession::player(PlayerId id) const {
  const int s = slotOf(id);
  return s < 0 ? nullptr : &players_[size_t(s)];
}

FootPlayer* GameSession::footPlayer(PlayerId id) {
  const int s = slotOf(id);
  return s < 0 ? nullptr : &footPlayers_[size_t(s)];
}

PlayerId GameSession::addPlayer(const std::string& name) {
  if (int(players_.size()) >= kMaxPlayers + (dedicated_ ? 1 : 0)) return 0;
  CrewMember m;
  m.id = nextPlayerId_++;
  m.name = name.empty() ? std::format("Crew {}", m.id) : name;
  m.location = Location::Cockpit;
  m.pilot = pilotSlot() < 0;  // the helm, if nobody has it
  if (networked_) m.credits = startingCredits_;  // someone new to this ship brings what anyone starts with
  if (auto w = crewWallets_.find(m.name); w != crewWallets_.end()) m.credits = w->second;  // welcome back
  players_.push_back(m);
  FootPlayer f;
  idleFoot_.outfit(f);
  footPlayers_.push_back(f);
  log::info("crew", "{} joined ({} aboard)", players_.back().name, players_.size());
  return m.id;
}

bool GameSession::removePlayer(PlayerId id) {
  const int s = slotOf(id);
  if (s <= 0) return false;  // unknown, or this machine's player
  leavePlace(s);
  log::info("crew", "{} left", players_[size_t(s)].name);
  crewWallets_[players_[size_t(s)].name] = players_[size_t(s)].credits;  // kept for when they come back
  players_.erase(players_.begin() + s);
  footPlayers_.erase(footPlayers_.begin() + s);
  return true;
}

std::map<std::string, std::int64_t> GameSession::allCrewWallets() const {
  auto all = crewWallets_;
  for (size_t i = 1; i < players_.size(); ++i) all[players_[i].name] = players_[i].credits;
  return all;
}

bool GameSession::crewAboard() const {
  return std::all_of(players_.begin(), players_.end(), [](const CrewMember& m) { return m.location != Location::StationInterior; });
}

void GameSession::notifyPlayer(PlayerId who, const std::string& text) {
  if (CrewMember* m = player(who)) {
    m->notice = text;
    m->noticeTime = world_->simulationTime();
  }
}

ActionResult GameSession::failFor(int slot, const std::string& msg) {
  CrewMember& m = players_[size_t(slot)];
  m.notice = msg;
  m.noticeTime = world_->simulationTime();
  log::info("session", "{}{}", slot == 0 ? "" : m.name + ": ", msg);
  return {false, msg};
}

ActionResult GameSession::undock(PlayerId by) {
  const int s = slotOf(by);
  if (s < 0) return {false, "No such player"};
  if (players_[size_t(s)].location != Location::Cockpit || !players_[size_t(s)].pilot)
    return failFor(s, "Only the pilot can undock");
  for (const CrewMember& m : players_)
    if (m.location == Location::StationInterior) {
      log::info("crew", "undock refused: {} is outside the ship", m.name);
      return failFor(s, std::format("Waiting for {}: they are outside the ship", m.name));
    }
  if (!docking_.undock(*world_)) return failFor(s, docking_.message());
  return {true, docking_.message()};
}

// ---- Places ------------------------------------------------------------------------------------

GameSession::Instance* GameSession::instanceOf(int slot) {
  const CrewMember& m = players_[size_t(slot)];
  if (m.location == Location::Cockpit) return nullptr;
  auto it = instances_.find(m.interior);
  return it == instances_.end() ? nullptr : &it->second;
}

const GameSession::Instance* GameSession::instanceOf(int slot) const {
  const CrewMember& m = players_[size_t(slot)];
  if (m.location == Location::Cockpit) return nullptr;
  auto it = instances_.find(m.interior);
  return it == instances_.end() ? nullptr : &it->second;
}

const InteriorDef* GameSession::interiorOf(PlayerId who) const {
  const int s = slotOf(who);
  if (s < 0 || players_[size_t(s)].location == Location::Cockpit) return nullptr;
  auto it = interiors_.find(players_[size_t(s)].interior);
  return it == interiors_.end() ? nullptr : &it->second;
}

const AssetID& GameSession::activeInteriorId() const {
  static const AssetID kNone;
  return players_.front().location == Location::Cockpit ? kNone : players_.front().interior;
}

const std::vector<Aabb>& GameSession::activeColliders() const {
  static const std::vector<Aabb> kNone;
  const Instance* in = instanceOf(0);
  return in ? in->colliders : kNone;
}

const HeightField& GameSession::activeTerrain() const {
  static const HeightField kNone;
  const Instance* in = instanceOf(0);
  return in ? in->terrain : kNone;
}

const FootCombat* GameSession::footCombatAt(const AssetID& interior) const {
  auto it = instances_.find(interior);
  return it == instances_.end() ? nullptr : &it->second.foot;
}

bool GameSession::footArmed() const {
  const Instance* in = instanceOf(0);
  return in && in->armed;
}

const FootCombat& GameSession::foot() const {
  const Instance* in = instanceOf(0);
  return in ? in->foot : idleFoot_;
}

FootCombat& GameSession::foot() {
  Instance* in = instanceOf(0);
  return in ? in->foot : idleFoot_;
}

const std::vector<GameSession::FootDrop>& GameSession::footDrops() const {
  static const std::vector<FootDrop> kNone;
  const Instance* in = instanceOf(0);
  return in ? in->drops : kNone;
}

void GameSession::leavePlace(int slot) {
  CrewMember& m = players_[size_t(slot)];
  if (m.location == Location::Cockpit) return;
  const AssetID was = m.interior;
  m.location = Location::Cockpit;  // provisional: callers set where they actually went
  m.interior.clear();
  footPlayers_[size_t(slot)].trigger = false;
  footPlayers_[size_t(slot)].beamActive = false;
  const bool empty = std::none_of(players_.begin(), players_.end(),
                                  [&](const CrewMember& o) { return o.location != Location::Cockpit && o.interior == was; });
  if (empty) instances_.erase(was);  // nobody left: the place sleeps; the dead stay dead (clearedMarkers_)
}

ActionResult GameSession::enterInterior(int slot, const AssetID& id, const std::string& spawn, Location loc) {
  auto it = interiors_.find(id);
  if (it == interiors_.end()) return failFor(slot, "Interior '" + id + "' is not loaded");
  auto sp = it->second.spawns.find(spawn);
  if (sp == it->second.spawns.end()) return failFor(slot, "Interior '" + id + "' has no spawn '" + spawn + "'");
  leavePlace(slot);
  const InteriorDef& def = it->second;
  auto [found, created] = instances_.try_emplace(id);
  Instance& in = found->second;
  if (created) {
    in.colliders = def.buildColliders();
    if (def.parkedShip) {
      // The parked ship blocks walking: a box around its footprint.
      const auto& ps = *def.parkedShip;
      const Vec3 h = def.parkedShipColliderHalfExtents;
      if (h.x > 0 && h.y > 0 && h.z > 0) in.colliders.push_back({ps.position - h, ps.position + h});
    }
    in.gravity = def.gravity;
    in.terrain = def.terrain ? HeightField(*def.terrain) : HeightField{};
    beginInstance(id, in, def);
  }
  CrewMember& m = players_[size_t(slot)];
  m.character = CharacterState{};
  // Several people at one spawn stand side by side rather than inside each other.
  int already = 0;
  for (size_t i = 0; i < players_.size(); ++i)
    if (int(i) != slot && players_[i].location != Location::Cockpit && players_[i].interior == id) ++already;
  const float yaw = radians(sp->second.yawDeg);
  const Vec3 right{std::cos(yaw), 0, std::sin(yaw)};
  m.character.feet = sp->second.position + right * (0.8f * float((already + 1) / 2) * (already % 2 ? 1.0f : -1.0f));
  if (in.terrain.valid()) m.character.feet.y = std::max(m.character.feet.y, in.terrain.heightAt(m.character.feet.x, m.character.feet.z));
  m.character.yaw = yaw;
  m.location = loc;
  m.pilot = false;
  m.interior = id;
  m.footEvents.clear();
  if (!in.armed) refitFoot(slot);  // somewhere safe: patched up, and the locker refills the guns
  m.notice = def.name;
  m.noticeTime = world_->simulationTime();
  log::info("session", "{}entered {} ({})", slot == 0 ? "" : m.name + " ", def.name, locationName(loc));
  return {true, def.name};
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
  if (networked_) {
    notify("Landing with a crew comes later");
    return false;
  }
  if (location() != Location::Cockpit || !freeFlight()) return fail("Cannot begin a landing right now").ok;
  return landing_.request(*world_);
}

bool GameSession::launch() {
  if (location() != Location::Cockpit) return fail("Take the pilot seat to launch").ok;
  if (!players_.front().pilot) return fail("Only the pilot can launch").ok;
  if (!crewAboard()) return fail("Crew not aboard: everyone must be seated before lift-off").ok;
  return landing_.launch(*world_);
}

ActionResult GameSession::leaveSeat(PlayerId who) {
  const int s = slotOf(who);
  if (s < 0) return {false, "No such player"};
  if (players_[size_t(s)].location != Location::Cockpit) return failFor(s, "You are not in a seat");
  // Docking needs its pilot, but a passenger is free to get up. Everyone stays seated for a landing, which
  // happens in a scene of its own. Otherwise the ship looks after itself: it brakes in open space, and the
  // travel drive holds its course.
  if (!parked() && landing_.busy()) return failFor(s, "Not during a landing");
  if (players_[size_t(s)].pilot && !parked() && docking_.state() != DockState::Undocked)
    return failFor(s, "Not during docking: you have the helm");
  if (shipInterior_.empty()) return failFor(s, "This ship has no walkable interior");
  const bool wasFlying = !parked() && players_[size_t(s)].pilot;
  const bool flying = !parked();
  const bool cruising = travel_.active();
  ActionResult r = enterInterior(s, shipInterior_, "seat", Location::ShipInterior);
  players_[size_t(s)].pilot = false;
  if (r.ok && flying) {
    CrewMember& m = players_[size_t(s)];
    m.notice = !wasFlying              ? "You leave your seat."
               : cruising              ? "You leave the helm. The drive holds its course."
                                       : "You leave the helm. The ship brakes to a stop.";
    m.noticeTime = world_->simulationTime();
    r.message = m.notice;
  }
  return r;
}

ActionResult GameSession::enterMap(PlayerId who, const AssetID& id) {
  const int s = slotOf(who);
  if (s < 0) return {false, "No such player"};
  return enterInterior(s, id, "start", Location::StationInterior);
}

ActionResult GameSession::perform(PlayerId who, const std::string& action) {
  const int s = slotOf(who);
  if (s < 0) return {false, "No such player"};
  CrewMember& m = players_[size_t(s)];
  auto told = [&](const std::string& text) {
    m.notice = text;
    m.noticeTime = world_->simulationTime();
    return ActionResult{true, text};
  };
  if (action.rfind("say:", 0) == 0) return told(action.substr(4));  // a data log in a map
  if (action.rfind("loot:", 0) == 0) return takeLoot(s, std::atoi(action.c_str() + 5));
  if (action == "leaveMap") {
    if (docking_.state() == DockState::Docked && docking_.station()) {
      const auto& dock = world_->props().at(*docking_.station()).def.docking;
      if (dock && dock->interior == m.interior) return enterInterior(s, shipInterior_, "airlock", Location::ShipInterior);
    }
    m.mapExitRequested = true;  // played straight from Blender: nothing to go back to
    return {true, ""};
  }
  if (action == "takeSeat") {
    if (m.location != Location::ShipInterior) return failFor(s, "The pilot seat is aboard your ship");
    leavePlace(s);
    m.location = Location::Cockpit;
    m.pilot = pilotSlot() < 0;
    return told(m.pilot ? "Pilot seat. L to undock when ready." : "Passenger seat. The pilot has the helm.");
  }
  if (action == "exitShip") {
    if (m.location != Location::ShipInterior) return failFor(s, "You are not aboard");
    if (landing_.state() == LandState::Landed) {
      if (const AssetID* id = surfaceSceneId()) return enterInterior(s, *id, "fromShip", Location::StationInterior);
    }
    if (docking_.state() != DockState::Docked) return failFor(s, "Airlock sealed: the ship is not docked");
    const auto station = docking_.station();
    const auto& dock = world_->props().at(*station).def.docking;
    if (!dock) return failFor(s, "This station has no walkable interior");
    if (networked_) {  // enemies are not shared over the network yet
      auto def = interiors_.find(dock->interior);
      const bool hostile = def != interiors_.end() && std::any_of(def->second.markers.begin(), def->second.markers.end(),
                                                                  [](const InteriorMarker& mk) { return mk.type == "enemy_spawn"; });
      if (hostile) return failFor(s, "Fights with a crew come later. The airlock stays shut.");
    }
    return enterInterior(s, dock->interior, "fromShip", Location::StationInterior);
  }
  if (action == "boardShip") {
    if (m.location != Location::StationInterior) return failFor(s, "Your ship is not here");
    return enterInterior(s, shipInterior_, "airlock", Location::ShipInterior);
  }
  if (action == "terminal") {
    if (!dockedMarket() && !hasService("contracts") && !hasService("repair"))
      return failFor(s, "Terminal offline: nothing on this terminal");
    m.marketRequested = true;
    return {true, "Market terminal"};
  }
  return failFor(s, "Unknown action '" + action + "'");
}

bool GameSession::markerCleared(const AssetID& interior, int marker) const {
  return clearedMarkers_.count(interior + "#" + std::to_string(marker)) > 0;
}

void GameSession::setFootContent(const FootContent* content) {
  idleFoot_.configure(content);
  for (auto& [id, in] : instances_) in.foot.configure(content);
  for (FootPlayer& p : footPlayers_) idleFoot_.outfit(p);
}

const NavGrid* GameSession::activeNav() const { return navAt(activeInteriorId()); }

const NavGrid* GameSession::navAt(const AssetID& interior) const {
  auto it = navs_.find(interior);
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

void GameSession::refitFoot(int slot) {
  FootPlayer& p = footPlayers_[size_t(slot)];
  const FootContent& c = idleFoot_.content();
  if (p.magazines.size() != c.weapons.size()) {
    idleFoot_.outfit(p);
    return;
  }
  p.health = 100;
  p.reloadLeft = p.switchLeft = 0;
  for (size_t w = 0; w < p.owned.size(); ++w)
    if (p.owned[w]) p.magazines[w] = c.weapons[w].magazine;
  for (const auto& [id, n] : c.startAmmo)
    if (const int a = c.ammoIndex(id); a >= 0) p.ammo[size_t(a)] = std::max(p.ammo[size_t(a)], n);
}

void GameSession::beginInstance(const AssetID& id, Instance& in, const InteriorDef& def) {
  in.foot.configure(&idleFoot_.content());
  std::vector<FootSpawn> spawns;
  for (int i = 0; i < int(def.markers.size()); ++i) {
    const InteriorMarker& mk = def.markers[size_t(i)];
    if (mk.type != "enemy_spawn") continue;
    in.armed = true;
    if (!markerCleared(id, i)) spawns.push_back({i, mk.position, mk.yawDeg, mk.kind});
  }
  in.foot.begin(spawns);
  if (def.meshCollider && !navs_.count(id)) {
    // Where people and enemies can walk: baked once per map, the first time anyone enters it.
    auto nav = std::make_unique<NavGrid>();
    if (nav->build(*def.meshCollider))
      log::info("foot", "nav grid for '{}': {} cells, {} links", id, nav->nodeCount(), nav->edgeCount());
    navs_[id] = std::move(nav);
  }
  rebuildDrops(id, in);
}

void GameSession::rebuildDrops(const AssetID& id, Instance& in) {
  in.drops.clear();
  const std::string prefix = id + "#drop";
  auto defIt = interiors_.find(id);
  const InteriorDef* def = defIt == interiors_.end() ? nullptr : &defIt->second;
  for (const auto& [key, pos] : dropPositions_) {
    if (key.rfind(prefix, 0) != 0 || clearedMarkers_.count(key)) continue;
    const int marker = std::atoi(key.c_str() + prefix.size());
    std::string table;
    if (def && marker >= 0 && marker < int(def->markers.size())) {
      const std::string& kind = def->markers[size_t(marker)].kind;
      const int e = in.foot.content().enemyIndex(kind.empty() ? "crawler" : kind);
      if (e >= 0) table = in.foot.content().enemies[size_t(e)].dropTable;
    }
    if (!table.empty()) in.drops.push_back({key, table, pos});
  }
}

void GameSession::pickUpDrops(const AssetID& id, Instance& in) {
  (void)id;
  for (size_t i = 0; i < in.drops.size();) {
    const FootDrop& d = in.drops[i];
    const LootTable* table = in.foot.content().lootTable(d.table);
    int taker = -1;
    for (size_t s = 0; s < players_.size() && taker < 0 && table; ++s) {
      if (players_[s].location == Location::Cockpit || players_[s].interior != id) continue;
      const Vec3 rel = d.position - players_[s].character.feet;
      if (length(Vec3{rel.x, 0, rel.z}) <= 1.1f && std::fabs(rel.y) <= 1.2f) taker = int(s);
    }
    if (taker < 0) {
      ++i;
      continue;
    }
    int credits = 0;
    const std::string what = in.foot.apply(footPlayers_[size_t(taker)], rollLoot(*table, lootSeed(d.key + "#roll")), &credits);
    players_[size_t(taker)].credits += credits;
    clearedMarkers_.insert(d.key);
    dropPositions_.erase(d.key);
    for (size_t s = 0; s < players_.size(); ++s)
      if (players_[s].location != Location::Cockpit && players_[s].interior == id)
        players_[s].footEvents.push_back({FootEvent::Type::Pickup, d.position});
    notifyPlayer(players_[size_t(taker)].id, "Picked up: " + what);
    in.drops.erase(in.drops.begin() + std::ptrdiff_t(i));
  }
}

void GameSession::stepInstance(const AssetID& id, Instance& in, float dt) {
  auto defIt = interiors_.find(id);
  if (defIt == interiors_.end()) return;
  const InteriorDef& def = defIt->second;
  std::vector<int> present;
  for (size_t s = 0; s < players_.size(); ++s)
    if (players_[s].location != Location::Cockpit && players_[s].interior == id) present.push_back(int(s));
  // Bodies first: everyone walks, then the fight sees where they ended up.
  CharacterParams params = characterParams_;
  params.gravity = in.gravity;
  for (int s : present) {
    CrewMember& m = players_[size_t(s)];
    stepCharacter(m.character, m.input, params, in.colliders, dt, in.terrain.valid() ? &in.terrain : nullptr,
                  def.meshCollider.get());
    m.input.yawDelta = m.input.pitchDelta = 0;  // consumed
    m.input.jump = false;
    // Safety net: never let a bad collider set drop anyone into the void.
    if (m.character.feet.y < -50.0f) {
      const char* spawn = m.location == Location::ShipInterior ? "airlock" : (def.spawns.count("start") ? "start" : "fromShip");
      if (def.spawns.count(spawn)) {
        m.character = CharacterState{};
        m.character.feet = def.spawns.at(spawn).position;
        log::warn("session", "{} fell out of '{}'; respawned", m.name, id);
      }
    }
  }
  if (!in.armed) return;
  // The fight sees only the people in this place, numbered 0..n-1; events go back in each one's numbering.
  std::vector<FootPlayer> fighters;
  for (int s : present) {
    FootPlayer f = footPlayers_[size_t(s)];
    const CrewMember& m = players_[size_t(s)];
    f.feet = m.character.feet;
    f.eye = m.character.eye(characterParams_);
    f.look = m.character.lookDirection();
    fighters.push_back(std::move(f));
  }
  auto navIt = navs_.find(id);
  in.foot.update(dt, fighters, in.colliders, def.meshCollider.get(), navIt == navs_.end() ? nullptr : navIt->second.get());
  for (size_t k = 0; k < present.size(); ++k) {
    const int s = present[k];
    CrewMember& m = players_[size_t(s)];
    FootPlayer& f = fighters[k];
    if (length(f.knock) > 0) {  // a charge sends you flying
      m.character.velocity += f.knock;
      f.knock = {};
    }
    footPlayers_[size_t(s)] = std::move(f);
  }
  for (FootEvent e : in.foot.takeEvents()) {
    const int fighter = e.player;
    const int who = fighter >= 0 && fighter < int(present.size()) ? present[size_t(fighter)] : -1;
    if (e.type == FootEvent::Type::EnemyDied) {
      const FootEnemy& dead = in.foot.enemies()[size_t(e.enemy)];
      clearedMarkers_.insert(id + "#" + std::to_string(dead.markerIndex));
      // A drop or not is decided by the map and marker, so a reload cannot reroll it.
      const FootEnemyDef& d = in.foot.enemyDef(dead);
      const std::string dropKey = id + "#drop" + std::to_string(dead.markerIndex);
      const float roll = float(lootSeed(dropKey) >> 40) / float(1u << 24);
      if (!d.dropTable.empty() && roll < d.dropChance && in.foot.content().lootTable(d.dropTable)) {
        dropPositions_[dropKey] = dead.body.feet + Vec3{0, 0.02f, 0};
        in.drops.push_back({dropKey, d.dropTable, dropPositions_[dropKey]});
      }
    }
    // Everyone in the place sees it; `player` becomes 0 for "you" and 1+ for someone else.
    for (int s : present) {
      FootEvent mine = e;
      mine.player = who < 0 ? -1 : (who == s ? 0 : 1 + who);
      players_[size_t(s)].footEvents.push_back(mine);
    }
    if (e.type == FootEvent::Type::PlayerDown && who >= 0) {
      // You wake at the way in, hurt, and they lose interest. Nothing is taken from you.
      CrewMember& m = players_[size_t(who)];
      const auto spawn = def.spawns.count("start") ? def.spawns.find("start") : def.spawns.find("fromShip");
      if (spawn != def.spawns.end()) {
        m.character = CharacterState{};
        m.character.feet = spawn->second.position;
        m.character.yaw = radians(spawn->second.yawDeg);
      }
      footPlayers_[size_t(who)].health = 60;
      footPlayers_[size_t(who)].trigger = false;
      // They settle only when nobody is left standing in the place.
      const bool anyoneUp = std::any_of(present.begin(), present.end(), [&](int s) { return footPlayers_[size_t(s)].alive() && s != who; });
      if (!anyoneUp) in.foot.returnHome();
      notifyPlayer(m.id, "You black out. Your suit drags you back to the way in.");
    }
  }
  pickUpDrops(id, in);
}

ActionResult GameSession::takeLoot(int slot, int marker) {
  CrewMember& m = players_[size_t(slot)];
  const InteriorDef* def = interiorOf(m.id);
  Instance* in = instanceOf(slot);
  if (!def || !in || marker < 0 || marker >= int(def->markers.size())) return failFor(slot, "Nothing to take");
  if (markerCleared(m.interior, marker)) return failFor(slot, "Already taken");
  const InteriorMarker& mk = def->markers[size_t(marker)];
  const LootTable* table = in->foot.content().lootTable(mk.kind.empty() ? "salvage" : mk.kind);
  if (!table) table = in->foot.content().lootTable("salvage");
  if (!table) return failFor(slot, "Nothing useful");
  FootPlayer& me = footPlayers_[size_t(slot)];
  const std::string key = m.interior + "#" + std::to_string(marker);
  const LootRoll roll = rollLoot(*table, lootSeed(key));
  int credits = 0;
  const std::string what = in->foot.apply(me, roll, &credits);
  m.credits += credits;  // personal wallets: it is yours
  clearedMarkers_.insert(key);
  m.footEvents.push_back({FootEvent::Type::Pickup, mk.position});
  if (!roll.weapon.empty()) m.footEvents.push_back({FootEvent::Type::Unlocked, mk.position, {}, 0, -1, me.switchRequest});
  const std::string message = table->mesh == "medical" ? "Medical kit: " + what : "Took: " + what;
  m.notice = message;
  m.noticeTime = world_->simulationTime();
  return {true, message};
}

std::string GameSession::promptFor(const Interactable& it) const {
  if (it.action.rfind("loot:", 0) == 0)
    if (const InteriorDef* def = activeInterior()) {
      const int marker = std::atoi(it.action.c_str() + 5);
      if (marker >= 0 && marker < int(def->markers.size())) {
        const std::string& kind = def->markers[size_t(marker)].kind;
        if (const LootTable* t = foot().content().lootTable(kind.empty() ? "salvage" : kind)) return t->prompt;
      }
    }
  return it.prompt;
}

const Interactable* GameSession::focusedInteractable(PlayerId who) const {
  const int s = slotOf(who);
  const InteriorDef* def = interiorOf(who);
  if (s < 0 || !def) return nullptr;
  const CrewMember& m = players_[size_t(s)];
  const Vec3 eye = m.character.eye(characterParams_);
  const Vec3 look = m.character.lookDirection();
  const Interactable* best = nullptr;
  float bestScore = -1;
  for (const auto& it : def->interactables) {
    if (it.action.rfind("loot:", 0) == 0 && markerCleared(m.interior, std::atoi(it.action.c_str() + 5))) continue;
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

ActionResult GameSession::interact(PlayerId who) {
  const int s = slotOf(who);
  if (s < 0) return {false, "No such player"};
  if (players_[size_t(s)].location == Location::Cockpit) return leaveSeat(who);
  const Interactable* it = focusedInteractable(who);
  if (!it) return failFor(s, "Nothing to use here");
  return perform(who, it->action);
}

void GameSession::setEconomy(CommodityDb db, std::int64_t credits) {
  commodities_ = std::move(db);
  players_.front().credits = credits;
  startingCredits_ = credits;
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
  if (players_.front().credits < u->price) return fail(std::format("{} costs {} cr", u->name, u->price));
  players_.front().credits -= u->price;
  upgrades_.push_back(id);
  applyShip();
  log::info("upgrades", "fitted {} for {} cr (balance {})", u->name, u->price, players_.front().credits);
  notify("Fitted: " + u->name);
  return {true, players_.front().notice};
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
  if (players_.front().credits < cost) {
    // Partial repair with whatever we can afford.
    const float price = upgradeDb_ ? upgradeDb_->repairPricePerHull : 3.0f;
    const float points = float(players_.front().credits) / price;
    if (points < 1.0f) return fail(std::format("Repairs cost {} cr", cost));
    d.hull = std::min(d.maxHull, d.hull + points);
    players_.front().credits = 0;
    notify("Partial repair: credits exhausted");
    return {true, players_.front().notice};
  }
  players_.front().credits -= cost;
  d.hull = d.maxHull;
  d.shield = d.maxShield;
  log::info("upgrades", "repaired hull for {} cr", cost);
  notify(std::format("Hull repaired ({} cr)", cost));
  return {true, players_.front().notice};
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
  return {true, players_.front().notice};
}

ActionResult GameSession::abandonContract(size_t index) {
  if (index >= contracts_.active().size()) return fail("No such contract");
  const std::string title = contracts_.active()[index].title;
  contracts_.abandon(index, cargo_);
  notify("Abandoned: " + title);
  return {true, players_.front().notice};
}

void GameSession::onParked() {
  const std::string loc = currentLocationId();
  if (loc.empty()) return;
  ContractUpdate u = contracts_.onParked(loc, locations_, cargo_, players_.front().credits);
  if (!u.messages.empty()) notify(u.messages.back());
}

void GameSession::handleCombatEvents() {
  for (auto& e : combat_.takeEvents()) {
    if (e.type == CombatEvent::Type::ShipDestroyed && e.byPlayer) {
      std::vector<std::string> msgs;
      contracts_.onShipDestroyed(e.faction, &msgs);
      const ShipType* t = combat_.shipType(e.shipDef);
      if (t && t->def.ai.bounty > 0) {
        players_.front().credits += t->def.ai.bounty;
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
  const std::int64_t charge = std::min(players_.front().credits, deductible_);
  players_.front().credits -= charge;
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

void GameSession::netApplyEconomy(std::map<std::string, int> cargo, const std::vector<std::int32_t>& marketStock) {
  cargo_.setContents(std::move(cargo));
  if (Market* m = dockedMarket(); m && !marketStock.empty() && marketStock.size() == m->stocks().size())
    m->restore(std::vector<int>(marketStock.begin(), marketStock.end()), m->lastTick(), m->carry());
}

bool GameSession::tryTakeHelm(PlayerId who) {
  CrewMember* m = player(who);
  if (!m || m->location != Location::Cockpit) return false;
  if (m->pilot) return true;
  if (pilotSlot() >= 0) return false;
  m->pilot = true;
  notifyPlayer(who, "You have the helm");
  log::info("crew", "{} took the empty helm", m->name);
  return true;
}

TradeResult GameSession::trade(PlayerId who, const std::string& commodity, int units) {
  if (netClient_) {  // the host's market decides, and the result arrives as a notice
    netCommands_.push_back(std::format("trade:{}:{}", commodity, units));
    return {0, 0, "Asking the station..."};
  }
  CrewMember* buyer = player(who);
  if (!buyer) return {0, 0, "No such player"};
  Market* m = dockedMarket();
  if (!m) return {0, 0, "No market: dock at a station first"};
  const int i = m->entryIndex(commodity);
  // Personal wallets, shared hold: whoever pays owns the deal, the cargo goes in the ship.
  TradeResult r = units > 0 ? m->buy(commodities_, i, units, buyer->credits, cargo_)
                            : m->sell(commodities_, i, -units, buyer->credits, cargo_);
  if (r.units > 0)
    log::info("economy", "{}{} {} x {} for {} cr (balance {})", who == localPlayer() ? "" : buyer->name + " ",
              units > 0 ? "bought" : "sold", r.units, commodity, r.credits, buyer->credits);
  return r;
}

void GameSession::setDedicated(bool on) {
  dedicated_ = on;
  if (!on) return;
  networked_ = true;
  CrewMember& nobody = players_.front();
  nobody.name = "Server";
  nobody.location = Location::Cockpit;
  nobody.pilot = false;
}

void GameSession::helmCommand(const std::string& name, PlayerId who) {
  if (name == "nav.next") {
    const auto dests = world().navDestinations();
    if (!dests.empty()) {
      size_t next = 0;
      if (const auto& t = world().target())
        for (size_t i = 0; i < dests.size(); ++i)
          if (dests[i] == *t) next = (i + 1) % dests.size();
      world().setTarget(dests[next]);
      if (auto info = world().targetInfo()) notifyPlayer(who, "NAV: " + info->name);
    }
  }
  if (name == "travel.engage") {
    if (travel().active()) travel().cancel(world());
    else engageTravel();
  }
  if (name == "dock.request") {  // L: contextual dock / land / launch / undock
    Docking& d = docking();
    Landing& ld = landing();
    if (ld.state() == LandState::Landed) {
      launch();
    } else if (!ld.busy()) {
      switch (d.state()) {
        case DockState::Undocked: {
          // Prefer a landing when a site's orbit marker is near; otherwise dock.
          bool siteNear = false;
          const DVec3 me = world().frame().toSystem(world().player().state.position).metres;
          for (const auto& pl : world().planets())
            for (std::uint32_t si = 0; si < pl.def.sites.size(); ++si)
              siteNear = siteNear || length(pl.siteOrbit(si) - me) <= Landing::kRequestRange;
          if (siteNear) requestLanding();
          else d.request(world());
          break;
        }
        case DockState::RequestingClearance:
        case DockState::ApproachAuthorized: d.cancel(); break;
        case DockState::Docked: undock(who); break;
        default: break;
      }
    }
  }
  if (name == "dock.autopilot") docking().toggleAutopilot();
  if (who != localPlayer()) {  // they cannot see the host's messages otherwise
    if (!docking().message().empty()) notifyPlayer(who, docking().message());
    if (!travel().message().empty()) notifyPlayer(who, travel().message());
  }
}

void GameSession::setNetClient(bool on) {
  netClient_ = on;
  PlayerShip& ship = world_->player();
  ship.kinematic = on;  // moved by what the host reports
  ship.collisionsEnabled = !on;
  ship.state.velocity = {};
}

void GameSession::netApplyShipStatus(bool docked, std::uint32_t prop, std::uint32_t bay, float hull, float shield) {
  Durability& d = combat_.player();
  d.hull = hull;
  d.shield = shield;
  if (docked && docking_.state() != DockState::Docked) {
    docking_.dockAt(*world_, prop, bay);
    wasParked_ = true;
  } else if (!docked && docking_.state() == DockState::Docked) {
    docking_.clearForNetClient();
    wasParked_ = false;
  }
}

void GameSession::netApplyPlace(PlayerId who, Location where, const AssetID& interior, bool pilot) {
  const int s = slotOf(who);
  if (s < 0) return;
  CrewMember& m = players_[size_t(s)];
  m.pilot = pilot;
  if (where == Location::Cockpit) {
    if (m.location != Location::Cockpit) {
      leavePlace(s);
      m.location = Location::Cockpit;
    }
    return;
  }
  if (m.location == where && m.interior == interior) return;
  auto def = interiors_.find(interior);
  if (def == interiors_.end() || def->second.spawns.empty()) return;  // the caller sets the position
  enterInterior(s, interior, def->second.spawns.begin()->first, where);
}

std::optional<Vec3> GameSession::fixedUpdate(float dt) {
  if (netClient_) {
    world_->fixedUpdate(dt);
    for (auto& [id, in] : instances_) stepInstance(id, in, dt);
    for (CrewMember& m : players_)
      if (m.location == Location::Cockpit) m.input = CharacterInput{};
    return world_->rebaseIfNeeded();
  }
  docking_.preStep(*world_, dt);
  travel_.preStep(*world_, world_->player().def.travel, dt);
  if (travel_.state() == TravelState::Transit || travel_.state() == TravelState::Decelerating)
    world_->player().collisionsEnabled = false;  // the drive's corridor ignores small debris
  landing_.preStep(*world_, dt);
  world_->fixedUpdate(dt);
  docking_.postStep(*world_, dt);
  const bool canMine = pilotSlot() >= 0 && freeFlight() && !networked_;
  miningEvents_ = mining_.fixedUpdate(*world_, miningTrigger_ && canMine, cargo_, dt);
  // Collisions hurt (only in free flight; clamps and pads are gentle).
  PlayerShip& ps = world_->player();
  if (ps.collisions != lastCollisions_) {
    lastCollisions_ = ps.collisions;
    if (freeFlight()) combat_.applyCollisionDamage(ps.lastImpactSpeed, world_->simulationTime(), ps.def.combat);
    // Anyone on their feet is thrown towards the bow.
    const float hit = std::clamp(ps.lastImpactSpeed / 30.0f, 0.0f, 1.0f);
    shipJolt_ = std::max(shipJolt_, hit);
    for (CrewMember& m : players_)
      if (m.location == Location::ShipInterior) m.character.velocity += Vec3{0, 1.5f * hit, -4.5f * hit};
  }
  const bool active = combatActive();
  const bool piloted = pilotSlot() >= 0;
  combat_.fixedUpdate(*world_, mining_, firePrimary_ && active && piloted, fireSecondary_ && active && piloted, active, dt);
  handleCombatEvents();  // handles PlayerDestroyed (weapons or collisions) -> handleDefeat()
  if (combat_.player().dead()) handleDefeat();  // safety net outside free flight
  const bool parkedNow = parked();
  if (wasParked_ && !*wasParked_ && parkedNow) onParked();
  wasParked_ = parkedNow;
  // Everyone on foot, place by place. A place nobody is in does not exist (and does not move).
  for (auto& [id, in] : instances_) stepInstance(id, in, dt);
  for (CrewMember& m : players_)
    if (m.location == Location::Cockpit) m.input = CharacterInput{};
  if (auto delta = world_->rebaseIfNeeded()) {
    mining_.applyRebase(*delta);
    combat_.applyRebase(*delta);
    return delta;
  }
  return std::nullopt;
}

}  // namespace fh
