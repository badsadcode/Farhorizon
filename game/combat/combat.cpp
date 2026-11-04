#include "game/combat/combat.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "engine/core/log.h"
#include "game/space/mining.h"

namespace fh {

namespace {
// Closest approach of segment a->b to point c; returns squared distance.
float segmentPointDist2(const Vec3& a, const Vec3& b, const Vec3& c) {
  const Vec3 ab = b - a;
  const float len2 = dot(ab, ab);
  const float t = len2 > 1e-9f ? std::clamp(dot(c - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
  const Vec3 d = a + ab * t - c;
  return dot(d, d);
}

// Converts a desired world direction into pitch/yaw stick commands for a ship.
Vec3 steer(const FlightState& s, const Vec3& desired, float gain = 2.5f) {
  const Vec3 local = s.orientation.conjugate().rotate(normalize(desired));
  const float yaw = std::atan2(local.x, -local.z);
  const float pitch = std::atan2(local.y, std::sqrt(local.x * local.x + local.z * local.z));
  // Roll gently toward the turn so the ship banks like the player's.
  return {std::clamp(pitch * gain, -1.0f, 1.0f), std::clamp(yaw * gain, -1.0f, 1.0f), std::clamp(yaw * 0.5f, -0.4f, 0.4f)};
}

float angleTo(const FlightState& s, const Vec3& dir) {
  return std::acos(std::clamp(dot(s.orientation.forward(), normalize(dir)), -1.0f, 1.0f));
}
}  // namespace

const char* aiStateName(AiState s) {
  switch (s) {
    case AiState::Patrol: return "patrol";
    case AiState::Intercept: return "intercept";
    case AiState::Attack: return "attack";
    case AiState::Evade: return "evade";
    case AiState::Retreat: return "retreat";
    case AiState::Route: return "route";
    case AiState::Flee: return "flee";
  }
  return "?";
}

void Durability::reset(float maxHullIn, float maxShieldIn, const CombatParams& p) {
  maxHull = hull = maxHullIn;
  maxShield = shield = maxShieldIn;
  regen = p.shieldRegen;
  delay = p.shieldDelay;
}

float Durability::apply(float damage, float shieldMul, float hullMul, double now) {
  lastHit = now;
  float toShield = damage * shieldMul;
  float hullDamage = 0;
  if (shield >= toShield) {
    shield -= toShield;
  } else {
    const float remaining = shieldMul > 0 ? (toShield - shield) / shieldMul : damage;
    shield = 0;
    hullDamage = remaining * hullMul;
    hull = std::max(0.0f, hull - hullDamage);
  }
  return hullDamage;
}

void Durability::tick(float dt, double now) {
  if (now - lastHit >= delay) shield = std::min(maxShield, shield + regen * dt);
}

void Armament::setup(const CombatParams& p, const WeaponDb& db) {
  primary.clear();
  secondary.clear();
  for (const auto& m : p.primary)
    if (const WeaponDef* w = db.find(m.weapon)) primary.push_back({m.mount, w, 0});
  for (const auto& m : p.secondary)
    if (const WeaponDef* w = db.find(m.weapon)) secondary.push_back({m.mount, w, 0});
  heatCapacity = p.heatCapacity;
  dissipation = p.heatDissipation;
}

void Armament::tick(float dt) {
  for (auto* group : {&primary, &secondary})
    for (auto& s : *group) s.cooldown = std::max(0.0f, s.cooldown - dt);
  heat = std::max(0.0f, heat - dissipation * dt);
  if (overheated && heat < heatCapacity * 0.3f) overheated = false;
}

float Combat::rand01() {
  rng_ += 0x9E3779B97F4A7C15ull;
  std::uint64_t z = rng_;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  z ^= z >> 31;
  return float(z >> 40) / float(1 << 24);
}

void Combat::configure(const WeaponDb* weapons, const SceneDef& scene, std::uint64_t seed) {
  weapons_ = weapons;
  encounters_ = scene.encounters;
  traffic_ = scene.traffic;
  encounterTimers_.assign(encounters_.size(), 0.0);
  trafficTimers_.assign(traffic_.size(), 0.0);
  rng_ = seed ^ 0xC0BA7ull;
}

const ShipType* Combat::shipType(const AssetID& id) const {
  auto it = types_.find(id);
  return it == types_.end() ? nullptr : &it->second;
}

void Combat::setupPlayer(const ShipDef& def) {
  const float hullFrac = player_.maxHull > 0 ? player_.hull / player_.maxHull : 1.0f;
  const float shieldFrac = player_.maxShield > 0 ? player_.shield / player_.maxShield : 1.0f;
  player_.reset(def.maxHull, def.maxShield, def.combat);
  player_.hull = def.maxHull * std::clamp(hullFrac, 0.0f, 1.0f);
  player_.shield = def.maxShield * std::clamp(shieldFrac, 0.0f, 1.0f);
  if (weapons_) playerArms_.setup(def.combat, *weapons_);
}

NpcShip* Combat::npc(std::uint32_t id) {
  for (auto& n : npcs_)
    if (n.id == id) return &n;
  return nullptr;
}

std::uint32_t Combat::spawn(SpaceWorld& world, const AssetID& shipDef, const Vec3& local, int encounter, int traffic) {
  const ShipType* t = shipType(shipDef);
  if (!t) {
    log::warn("combat", "cannot spawn unknown ship type '{}'", shipDef);
    return 0;
  }
  NpcShip n;
  n.id = nextId_++;
  n.shipDef = shipDef;
  n.state.position = local;
  n.state.orientation = Quat::fromAxisAngle({0, 1, 0}, rand01() * 2.0f * kPi);
  n.state.boostEnergy = t->def.flight.boostEnergyCapacity;
  n.previous = n.state;
  n.dur.reset(t->def.maxHull, t->def.maxShield, t->def.combat);
  if (weapons_) n.arm.setup(t->def.combat, *weapons_);
  n.encounter = encounter;
  n.traffic = traffic;
  n.ai = t->def.ai.role == "pirate" ? AiState::Patrol : AiState::Route;
  n.goal = local;
  (void)world;
  npcs_.push_back(n);
  return n.id;
}

void Combat::updateSpawns(SpaceWorld& world, bool playerActive, float dt) {
  const Vec3 me = world.player().state.position;
  for (size_t e = 0; e < encounters_.size(); ++e) {
    encounterTimers_[e] = std::max(0.0, encounterTimers_[e] - dt);
    const EncounterDef& ed = encounters_[e];
    const Vec3 centre = world.frame().toLocal(ed.center);
    if (!playerActive || length(centre - me) > ed.triggerRange) continue;
    const int alive = int(std::count_if(npcs_.begin(), npcs_.end(), [&](const NpcShip& n) { return n.encounter == int(e); }));
    if (alive >= ed.count || encounterTimers_[e] > 0) continue;
    // Spawn somewhere in the zone, never right next to the player.
    for (int tries = 0; tries < 8; ++tries) {
      const Vec3 off = normalize(Vec3{rand01() - 0.5f, (rand01() - 0.5f) * 0.4f, rand01() - 0.5f}) * (ed.radius * rand01());
      const Vec3 p = centre + off;
      if (length(p - me) >= ed.spawnMinDistance) {
        spawn(world, ed.ship, p, int(e), -1);
        encounterTimers_[e] = ed.respawnSeconds / float(std::max(1, ed.count));
        break;
      }
    }
  }
  for (size_t t = 0; t < traffic_.size(); ++t) {
    trafficTimers_[t] = std::max(0.0, trafficTimers_[t] - dt);
    const TrafficDef& td = traffic_[t];
    if (td.waypoints.empty() || trafficTimers_[t] > 0) continue;
    const Vec3 start = world.frame().toLocal(td.waypoints.front());
    if (length(start - me) > td.triggerRange) continue;
    const int alive = int(std::count_if(npcs_.begin(), npcs_.end(), [&](const NpcShip& n) { return n.traffic == int(t); }));
    if (alive >= td.count) continue;
    const std::uint32_t id = spawn(world, td.ship, start + Vec3{rand01() * 200 - 100, rand01() * 60, rand01() * 200 - 100}, -1, int(t));
    if (NpcShip* n = npc(id)) n->waypoint = td.waypoints.size() > 1 ? 1 : 0;
    trafficTimers_[t] = td.respawnSeconds;
  }
  // Despawn ships the player has left far behind (cheap abstraction, doc 7.3).
  std::erase_if(npcs_, [&](const NpcShip& n) {
    float range = 16000;
    if (n.encounter >= 0) range = encounters_[size_t(n.encounter)].triggerRange * 1.6f;
    if (n.traffic >= 0) range = traffic_[size_t(n.traffic)].triggerRange * 1.6f;
    return length(n.state.position - me) > range;
  });
}

void Combat::think(NpcShip& n, SpaceWorld& world, bool playerActive, float dt) {
  const ShipType& t = types_.at(n.shipDef);
  const AiParams& ai = t.def.ai;
  const PlayerShip& p = world.player();
  const Vec3 toPlayer = p.state.position - n.state.position;
  const float dist = length(toPlayer);
  n.stateTime += dt;
  auto go = [&](AiState s) {
    n.ai = s;
    n.stateTime = 0;
  };
  FlightInput in;
  in.assist = true;

  // Shared: low hull -> run.
  if (n.ai != AiState::Retreat && n.ai != AiState::Flee && n.dur.hullFraction() < ai.fleeHullFraction &&
      ai.role == "pirate") {
    go(AiState::Retreat);
  }
  if (ai.role == "trader" && time_ - n.lastHitByPlayer < 12.0 && n.ai != AiState::Flee) go(AiState::Flee);

  const float projSpeed = n.arm.primary.empty() ? 900.0f : n.arm.primary.front().def->projectileSpeed;
  auto lead = [&]() {
    const Vec3 rel = p.state.velocity - n.state.velocity;
    float tt = dist / projSpeed;
    Vec3 aim = p.state.position + rel * tt;
    tt = length(aim - n.state.position) / projSpeed;
    return p.state.position + rel * tt;
  };

  switch (n.ai) {
    case AiState::Patrol: {
      if (length(n.goal - n.state.position) < 150 || n.stateTime > 25) {
        const EncounterDef* ed = n.encounter >= 0 ? &encounters_[size_t(n.encounter)] : nullptr;
        const Vec3 centre = ed ? world.frame().toLocal(ed->center) : n.state.position;
        const float r = ed ? ed->radius : 1500.0f;
        n.goal = centre + normalize(Vec3{rand01() - 0.5f, (rand01() - 0.5f) * 0.3f, rand01() - 0.5f}) * (r * rand01());
        n.stateTime = 0;
      }
      in.rotation = steer(n.state, n.goal - n.state.position);
      in.throttle = 0.35f;
      if (playerActive && dist < ai.detectionRange) {
        n.hostile = true;
        go(AiState::Intercept);
        if (time_ - n.engagedAnnounced > 60) {
          n.engagedAnnounced = time_;
          events_.push_back({CombatEvent::Type::Engaged, ai.faction, n.shipDef, "", n.state.position, false, 0});
        }
      }
      break;
    }
    case AiState::Intercept:
      in.rotation = steer(n.state, lead() - n.state.position);
      in.throttle = 1.0f;
      in.boost = dist > 2200.0f;
      if (!playerActive && n.stateTime > 5) go(AiState::Patrol);
      else if (dist < ai.attackRange) go(AiState::Attack);
      break;
    case AiState::Attack: {
      const Vec3 aim = lead() - n.state.position;
      in.rotation = steer(n.state, aim, 3.0f);
      in.throttle = dist > ai.preferredRange ? 0.8f : 0.25f;
      const bool aimed = angleTo(n.state, aim) < radians(ai.aimToleranceDeg);
      if (playerActive && aimed && !n.arm.primary.empty() && dist < n.arm.primary.front().def->range * 0.9f) {
        const auto& sockets = socketCache_[n.shipDef];
        fire(n.id, n.arm, false, n.state, sockets, t.radius, kPlayerId);
        if (!n.arm.secondary.empty() && time_ >= n.secondaryReady && dist > 250 && dist < 1400) {
          fire(n.id, n.arm, true, n.state, sockets, t.radius, kPlayerId);
          n.secondaryReady = time_ + 6.0;
        }
      }
      if (!playerActive) go(AiState::Patrol);
      else if (n.stateTime > 7.0 || dist < 120.0f) {
        const Vec3 away = normalize(cross(normalize(toPlayer), Vec3{0, 1, 0}) * (rand01() < 0.5f ? 1.0f : -1.0f) +
                                    Vec3{0, rand01() - 0.5f, 0});
        n.evadeDir = away;
        go(AiState::Evade);
      }
      break;
    }
    case AiState::Evade:
      in.rotation = steer(n.state, n.evadeDir);
      in.throttle = 1.0f;
      in.boost = true;
      if (n.stateTime > 2.5) go(AiState::Intercept);
      break;
    case AiState::Retreat:
      in.rotation = steer(n.state, -toPlayer);
      in.throttle = 1.0f;
      in.boost = true;
      if (dist > 5000.0f) {
        events_.push_back({CombatEvent::Type::Escaped, ai.faction, n.shipDef, "", n.state.position, false, 0});
        n.dur.hull = -1;  // removed below without loot
      }
      break;
    case AiState::Route: {
      const TrafficDef* td = n.traffic >= 0 ? &traffic_[size_t(n.traffic)] : nullptr;
      if (td && !td->waypoints.empty()) {
        const Vec3 wp = world.frame().toLocal(td->waypoints[size_t(n.waypoint) % td->waypoints.size()]);
        if (length(wp - n.state.position) < 300) n.waypoint = (n.waypoint + 1) % int(td->waypoints.size());
        in.rotation = steer(n.state, wp - n.state.position);
        in.throttle = 0.6f;
      }
      break;
    }
    case AiState::Flee:
      in.rotation = steer(n.state, -toPlayer);
      in.throttle = 1.0f;
      in.boost = true;
      if (time_ - n.lastHitByPlayer > 12.0) go(AiState::Route);
      break;
  }
  n.input = in;
}

void Combat::fire(std::uint32_t owner, Armament& arm, bool secondary, const FlightState& pose,
                  const std::map<std::string, Transform>& sockets, float radius, std::optional<std::uint32_t> lock) {
  if (arm.overheated) return;
  auto& group = secondary ? arm.secondary : arm.primary;
  for (auto& slot : group) {
    if (slot.cooldown > 0 || !slot.def) continue;
    Vec3 muzzle = pose.position + pose.orientation.forward() * radius;
    if (auto it = sockets.find(slot.mount); it != sockets.end())
      muzzle = pose.position + pose.orientation.rotate(it->second.position) + pose.orientation.forward() * 2.0f;
    const float spread = radians(slot.def->spreadDeg);
    const Vec3 jitter = pose.orientation.rotate({(rand01() - 0.5f) * spread, (rand01() - 0.5f) * spread, 0});
    const Vec3 dir = normalize(pose.orientation.forward() + jitter);
    Projectile pr;
    pr.pos = muzzle;
    pr.vel = pose.velocity + dir * slot.def->projectileSpeed;
    pr.life = slot.def->range / slot.def->projectileSpeed;
    pr.owner = owner;
    pr.def = slot.def;
    pr.damageScale = arm.damageScale;
    if (slot.def->homing > 0) pr.homingTarget = lock;
    if (projectiles_.size() >= 400) projectiles_.erase(projectiles_.begin());  // hard cap
    projectiles_.push_back(pr);
    slot.cooldown = slot.def->fireInterval;
    arm.heat += slot.def->heatPerShot;
    if (arm.heat >= arm.heatCapacity) arm.overheated = true;
    events_.push_back({CombatEvent::Type::ShotFired, "", "", slot.def->sound, muzzle, owner == kPlayerId, 0});
  }
}

void Combat::moveProjectiles(SpaceWorld& world, Mining& mining, float dt) {
  PlayerShip& p = world.player();
  for (size_t i = 0; i < projectiles_.size();) {
    Projectile& pr = projectiles_[i];
    // Homing: turn toward the lock at a limited rate.
    if (pr.homingTarget && pr.def->homing > 0) {
      std::optional<Vec3> tgt;
      if (*pr.homingTarget == kPlayerId) tgt = p.state.position;
      else if (NpcShip* n = npc(*pr.homingTarget)) tgt = n->state.position;
      if (tgt) {
        const float speed = length(pr.vel);
        const Vec3 cur = pr.vel / std::max(speed, 1e-3f);
        const Vec3 want = normalize(*tgt - pr.pos);
        const float ang = std::acos(std::clamp(dot(cur, want), -1.0f, 1.0f));
        const float step = std::min(ang, pr.def->homing * dt);
        if (ang > 1e-4f) {
          const Vec3 axis = normalize(cross(cur, want));
          pr.vel = Quat::fromAxisAngle(axis, step).rotate(cur) * speed;
        }
      }
    }
    const Vec3 a = pr.pos;
    const Vec3 b = pr.pos + pr.vel * dt;
    pr.pos = b;
    pr.life -= dt;
    bool hit = false;
    // Ships.
    if (pr.owner != kPlayerId && segmentPointDist2(a, b, p.state.position) <= p.collisionRadius * p.collisionRadius) {
      const float hull = player_.apply(pr.def->damage * pr.damageScale, pr.def->shieldMultiplier, pr.def->hullMultiplier, time_);
      events_.push_back({CombatEvent::Type::PlayerHit, "", "", player_.shield > 0 ? "shield_hit" : "hull_hit", b, false, hull});
      explosions_.push_back({b, time_, 2.5f});
      hit = true;
    }
    for (size_t k = 0; !hit && k < npcs_.size(); ++k) {
      NpcShip& n = npcs_[k];
      if (n.id == pr.owner) continue;
      const float r = types_.at(n.shipDef).radius;
      if (segmentPointDist2(a, b, n.state.position) > r * r) continue;
      n.dur.apply(pr.def->damage * pr.damageScale, pr.def->shieldMultiplier, pr.def->hullMultiplier, time_);
      if (pr.owner == kPlayerId) {
        n.lastHitByPlayer = time_;
        if (!n.hostile && types_.at(n.shipDef).def.ai.role == "pirate") n.hostile = true;
      }
      events_.push_back({CombatEvent::Type::NpcHit, types_.at(n.shipDef).def.ai.faction, n.shipDef,
                         n.dur.shield > 0 ? "shield_hit" : "hull_hit", b, pr.owner == kPlayerId, 0});
      explosions_.push_back({b, time_, 2.0f});
      hit = true;
    }
    // Rocks absorb shots.
    for (const auto& ast : world.asteroids()) {
      if (hit) break;
      const float r = ast.radius * 0.9f;
      if (std::fabs(ast.local.x - b.x) > r + 50 || std::fabs(ast.local.z - b.z) > r + 50) continue;
      if (segmentPointDist2(a, b, ast.local) <= r * r) {
        explosions_.push_back({b, time_, 1.5f});
        hit = true;
      }
    }
    if (hit || pr.life <= 0) projectiles_.erase(projectiles_.begin() + std::ptrdiff_t(i));
    else ++i;
  }
  (void)mining;
}

void Combat::destroyNpc(size_t index, SpaceWorld& world, Mining& mining, bool byPlayer) {
  NpcShip n = npcs_[index];
  npcs_.erase(npcs_.begin() + std::ptrdiff_t(index));
  const ShipType& t = types_.at(n.shipDef);
  explosions_.push_back({n.state.position, time_, t.radius * 4.0f});
  events_.push_back({CombatEvent::Type::ShipDestroyed, t.def.ai.faction, n.shipDef, "explosion", n.state.position, byPlayer, 0});
  // Salvage canisters drift out of the wreck; the scoop collects them like ore.
  const int count = t.def.ai.lootMin + int(rand01() * float(std::max(0, t.def.ai.lootMax - t.def.ai.lootMin) + 1));
  for (int k = 0; k < count; ++k) {
    OreChunk c;
    const Vec3 dir = normalize(Vec3{rand01() - 0.5f, rand01() - 0.5f, rand01() - 0.5f});
    c.local = n.state.position + dir * 3.0f;
    c.velocity = n.state.velocity * 0.3f + dir * 4.0f;
    c.commodity = t.def.ai.lootCommodity;
    c.spawnTime = world.simulationTime();
    mining.addChunk(c);
  }
}

void Combat::fixedUpdate(SpaceWorld& world, Mining& mining, bool firePrimary, bool fireSecondary, bool playerActive,
                         float dt) {
  time_ = world.simulationTime();
  updateSpawns(world, playerActive, dt);
  PlayerShip& p = world.player();

  // Player weapons.
  playerArms_.tick(dt);
  player_.tick(dt, time_);
  if (playerActive) {
    std::optional<std::uint32_t> lock;
    if (const auto& t = world.target(); t && t->kind == TargetRef::Kind::Ship) lock = t->index;
    if (firePrimary) fire(kPlayerId, playerArms_, false, p.state, p.sockets, p.collisionRadius, lock);
    if (fireSecondary) fire(kPlayerId, playerArms_, true, p.state, p.sockets, p.collisionRadius, lock);
  }

  // NPCs: think, fly with the shared flight model, bump into rocks, tick systems.
  for (auto& n : npcs_) {
    const ShipType& t = types_.at(n.shipDef);
    if (!socketCache_.count(n.shipDef)) {
      auto& cache = socketCache_[n.shipDef];
      for (const auto& [name, s] : t.def.sockets) cache[name] = {s.position, Quat{}};
    }
    think(n, world, playerActive, dt);
    n.previous = n.state;
    stepFlight(n.state, n.input, t.def.flight, dt);
    for (const auto& a : world.asteroids()) {
      const Vec3 d = n.state.position - a.local;
      const float minD = a.radius * 0.9f + t.radius;
      const float d2 = dot(d, d);
      if (d2 < minD * minD && d2 > 1e-6f) {
        const float dist = std::sqrt(d2);
        n.state.position = a.local + d / dist * minD;
        const float vn = dot(n.state.velocity, d / dist);
        if (vn < 0) n.state.velocity -= d / dist * vn;
      }
    }
    n.arm.tick(dt);
    n.dur.tick(dt, time_);
  }

  moveProjectiles(world, mining, dt);

  for (size_t i = 0; i < npcs_.size();) {
    if (npcs_[i].dur.hull < 0) {  // escaped (retreat) -> vanish quietly
      npcs_.erase(npcs_.begin() + std::ptrdiff_t(i));
    } else if (npcs_[i].dur.dead()) {
      destroyNpc(i, world, mining, time_ - npcs_[i].lastHitByPlayer < 1.0);
    } else {
      ++i;
    }
  }
  if (player_.dead() && playerActive) {
    explosions_.push_back({p.state.position, time_, 40.0f});
    events_.push_back({CombatEvent::Type::PlayerDestroyed, "", "", "explosion", p.state.position, false, 0});
  }
  std::erase_if(explosions_, [&](const Explosion& e) { return time_ - e.time > 3.0; });
}

void Combat::applyRebase(const Vec3& delta) {
  for (auto& n : npcs_) {
    n.state.position += delta;
    n.previous.position += delta;
    n.goal += delta;
  }
  for (auto& pr : projectiles_) pr.pos += delta;
  for (auto& e : explosions_) e.pos += delta;
}

void Combat::applyCollisionDamage(float impactSpeed, double now, const CombatParams& p) {
  if (impactSpeed <= 10.0f) return;
  player_.apply((impactSpeed - 10.0f) * p.collisionDamagePerMs, 1.0f, 1.0f, now);
}

void Combat::despawnAll() {
  npcs_.clear();
  projectiles_.clear();
}

bool Combat::targetNearestHostile(SpaceWorld& world) const {
  const NpcShip* best = nullptr;
  float bestD = 1e30f;
  for (const auto& n : npcs_) {
    const float d = length(n.state.position - world.player().state.position);
    const bool hostile = n.hostile || types_.at(n.shipDef).def.ai.role == "pirate";
    if (d < bestD && (hostile || !best)) {
      bestD = d;
      best = &n;
    }
  }
  if (!best) return false;
  world.setTarget({TargetRef::Kind::Ship, best->id, 0});
  return true;
}

std::optional<TargetInfo> Combat::describe(const SpaceWorld& world, std::uint32_t npcId) const {
  for (const auto& n : npcs_) {
    if (n.id != npcId) continue;
    const ShipType& t = types_.at(n.shipDef);
    TargetInfo info;
    info.name = t.def.name;
    info.marker = n.hostile || t.def.ai.role == "pirate" ? "hostile" : "ship";
    info.local = n.state.position;
    info.radius = t.radius;
    const Vec3 rel = n.state.position - world.player().state.position;
    info.distance = length(rel);
    info.closingSpeed = info.distance > 1e-3 ? dot(world.player().state.velocity - n.state.velocity, rel / float(info.distance)) : 0;
    return info;
  }
  return std::nullopt;
}

std::optional<Vec3> Combat::leadPoint(const SpaceWorld& world, std::uint32_t npcId) const {
  const float speed = playerArms_.primary.empty() ? 900.0f : playerArms_.primary.front().def->projectileSpeed;
  for (const auto& n : npcs_) {
    if (n.id != npcId) continue;
    const PlayerShip& p = world.player();
    const Vec3 rel = n.state.velocity - p.state.velocity;
    float t = length(n.state.position - p.state.position) / speed;
    Vec3 aim = n.state.position + rel * t;
    t = length(aim - p.state.position) / speed;
    return n.state.position + rel * t;
  }
  return std::nullopt;
}

}  // namespace fh
