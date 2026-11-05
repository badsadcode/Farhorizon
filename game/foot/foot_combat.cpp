#include "game/foot/foot_combat.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

#include "game/foot/nav_grid.h"

namespace fh {

namespace {

constexpr float kSightCheckSeconds = 0.2f;
constexpr float kSwitchSeconds = 0.4f;
constexpr float kBeamHitEvery = 0.12f;

float yawTowards(const Vec3& dir) { return std::atan2(dir.x, -dir.z); }
Vec3 facingOf(float yaw) { return {std::sin(yaw), 0, -std::cos(yaw)}; }

float turnTowards(float yaw, float target, float maxStep) {
  const float pi = 3.14159265f;
  float d = std::fmod(target - yaw + pi, 2 * pi);
  if (d < 0) d += 2 * pi;
  d -= pi;
  return yaw + std::clamp(d, -maxStep, maxStep);
}

// Distance along a unit ray to a sphere, or a negative number when it misses.
float raySphere(const Vec3& origin, const Vec3& dir, const Vec3& centre, float radius) {
  const Vec3 to = centre - origin;
  const float along = dot(to, dir);
  const float d2 = dot(to, to) - along * along;
  const float r2 = radius * radius;
  if (d2 > r2) return -1;
  const float t = along - std::sqrt(r2 - d2);
  return t >= 0 ? t : (along >= 0 ? 0.0f : -1.0f);
}

Vec3 flat(Vec3 v) {
  v.y = 0;
  return v;
}

const FootContent& builtin() {
  static const FootContent c = builtinFootContent();
  return c;
}

}  // namespace

const char* enemyStateName(EnemyState s) {
  switch (s) {
    case EnemyState::Asleep: return "asleep";
    case EnemyState::Chasing: return "chasing";
    case EnemyState::WindingUp: return "winding up";
    case EnemyState::Aiming: return "aiming";
    case EnemyState::ChargeWindup: return "charge wind-up";
    case EnemyState::Charging: return "charging";
    case EnemyState::Recovering: return "recovering";
    case EnemyState::Dead: return "dead";
  }
  return "?";
}

bool lineOfSight(const Vec3& from, const Vec3& to, const TriangleCollider* mesh) {
  if (!mesh) return true;
  const Vec3 d = to - from;
  const float len = length(d);
  if (len < 1e-3f) return true;
  float t = 0;
  return !mesh->raycast(from, d / len, len, t);
}

FootCombat::FootCombat() { configure(nullptr); }

void FootCombat::configure(const FootContent* content) {
  content_ = content ? content : &builtin();
  bodies_.clear();
  for (const FootEnemyDef& d : content_->enemies) {
    CharacterParams b;
    b.radius = d.bodyRadius;
    b.height = d.bodyHeight;
    b.eyeHeight = d.bodyHeight * 0.65f;
    b.walkSpeed = d.speed;
    b.runSpeed = d.speed;
    b.groundAccel = 24;
    bodies_.push_back(b);
  }
}

float FootCombat::random01() {
  std::uint64_t z = (rng_ += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  z ^= z >> 31;
  return float(z >> 40) / float(1u << 24);
}

// ---- Loadout --------------------------------------------------------------------------------------

void FootCombat::outfit(FootPlayer& p) const {
  const FootContent& c = *content_;
  p.health = 100;
  p.owned.assign(c.weapons.size(), 0);
  p.magazines.assign(c.weapons.size(), 0);
  p.ammo.assign(c.ammo.size(), 0);
  for (const auto& [id, n] : c.startAmmo)
    if (const int a = c.ammoIndex(id); a >= 0) p.ammo[size_t(a)] = n;
  p.weapon = -1;
  for (const auto& id : c.startWeapons) {
    const int w = c.weaponIndex(id);
    if (w < 0) continue;
    p.owned[size_t(w)] = 1;
    p.magazines[size_t(w)] = c.weapons[size_t(w)].magazine;  // loaded, on top of the starting reserve
    if (p.weapon < 0) p.weapon = w;
  }
  if (p.weapon < 0) p.weapon = 0;
  p.cooldown = p.reloadLeft = p.switchLeft = p.beamCarry = 0;
  p.beamActive = false;
}

const FootWeaponDef& FootCombat::weapon(const FootPlayer& p) const {
  return content_->weapons[size_t(std::clamp(p.weapon, 0, int(content_->weapons.size()) - 1))];
}

int FootCombat::magazine(const FootPlayer& p) const {
  return p.weapon >= 0 && p.weapon < int(p.magazines.size()) ? p.magazines[size_t(p.weapon)] : 0;
}

int FootCombat::reserve(const FootPlayer& p) const {
  const int a = content_->ammoIndex(weapon(p).ammo);
  return a >= 0 && a < int(p.ammo.size()) ? p.ammo[size_t(a)] : 0;
}

int FootCombat::addAmmo(FootPlayer& p, const std::string& ammoId, int amount) const {
  const int a = content_->ammoIndex(ammoId);
  if (a < 0 || a >= int(p.ammo.size()) || amount <= 0) return 0;
  const int room = std::max(0, content_->ammo[size_t(a)].maxReserve - p.ammo[size_t(a)]);
  const int added = std::min(room, amount);
  p.ammo[size_t(a)] += added;
  return added;
}

bool FootCombat::unlock(FootPlayer& p, const std::string& weaponId) const {
  const int w = content_->weaponIndex(weaponId);
  if (w < 0 || w >= int(p.owned.size()) || p.owned[size_t(w)]) return false;
  p.owned[size_t(w)] = 1;
  p.magazines[size_t(w)] = content_->weapons[size_t(w)].magazine;  // comes loaded
  return true;
}

std::string FootCombat::apply(FootPlayer& p, const LootRoll& roll, int* creditsOut) const {
  std::vector<std::string> parts;
  if (!roll.weapon.empty()) {
    const int w = content_->weaponIndex(roll.weapon);
    if (unlock(p, roll.weapon) && w >= 0) {
      parts.push_back(content_->weapons[size_t(w)].name);
      p.switchRequest = w;  // pick it up and hold it
    }
  }
  if (roll.credits > 0) parts.push_back(std::format("{} cr", roll.credits));
  if (creditsOut) *creditsOut = roll.credits;
  for (const auto& [id, n] : roll.ammo) {
    const int got = addAmmo(p, id, n);
    const int a = content_->ammoIndex(id);
    if (got > 0 && a >= 0) parts.push_back(std::format("{} {}", got, content_->ammo[size_t(a)].name));
    else if (a >= 0) parts.push_back(content_->ammo[size_t(a)].name + " full");
  }
  if (roll.heal > 0 && p.alive()) {
    const float before = p.health;
    p.health = std::min(100.0f, p.health + roll.heal);
    parts.push_back(std::format("+{:.0f} health", p.health - before));
  }
  std::string out;
  for (const auto& s : parts) out += (out.empty() ? "" : ", ") + s;
  return out.empty() ? std::string("nothing useful") : out;
}

// ---- Setup ----------------------------------------------------------------------------------------

void FootCombat::begin(const std::vector<FootSpawn>& spawns) {
  enemies_.clear();
  projectiles_.clear();
  events_.clear();
  rng_ = 0x5EEDF007ull + spawns.size() * 7919;
  for (const FootSpawn& s : spawns) {
    FootEnemy e;
    e.def = s.kind.empty() ? 0 : std::max(0, content_->enemyIndex(s.kind));
    e.markerIndex = s.markerIndex;
    e.home = s.position;
    e.homeYaw = radians(s.yawDeg);
    e.body.feet = s.position;
    e.body.yaw = e.homeYaw;
    e.health = content_->enemies[size_t(e.def)].health;
    e.sidestepDir = enemies_.size() % 2 ? 1.0f : -1.0f;
    e.sightCheck = float(enemies_.size()) * 0.05f;  // not all on the same tick
    e.directCheck = float(enemies_.size()) * 0.03f;
    enemies_.push_back(e);
  }
}

void FootCombat::returnHome() {
  projectiles_.clear();
  for (FootEnemy& e : enemies_) {
    if (e.state == EnemyState::Dead) continue;
    e.body = CharacterState{};
    e.body.feet = e.home;
    e.body.yaw = e.homeYaw;
    e.state = EnemyState::Asleep;
    e.health = enemyDef(e).health;
    e.timer = e.cooldown = 0;
    e.sinceSeen = 0;
    e.seesTarget = false;
    e.path.clear();
  }
}

int FootCombat::aliveCount() const {
  return int(std::count_if(enemies_.begin(), enemies_.end(), [](const FootEnemy& e) { return e.state != EnemyState::Dead; }));
}

void FootCombat::update(float dt, std::vector<FootPlayer>& players, const std::vector<Aabb>& colliders,
                        const TriangleCollider* mesh, const NavGrid* nav) {
  for (int i = 0; i < int(players.size()); ++i) {
    FootPlayer& p = players[size_t(i)];
    if (p.magazines.size() != content_->weapons.size() || p.ammo.size() != content_->ammo.size()) {
      const float health = p.health;  // a driver that never outfitted its player: give the start loadout
      outfit(p);
      p.health = health;
    }
    updatePlayer(i, p, dt, mesh);
  }
  for (int i = 0; i < int(enemies_.size()); ++i) updateEnemy(i, enemies_[size_t(i)], dt, players, colliders, mesh, nav);
  updateProjectiles(dt, players, mesh);
}

// ---- Players --------------------------------------------------------------------------------------

void FootCombat::updatePlayer(int index, FootPlayer& p, float dt, const TriangleCollider* mesh) {
  p.cooldown = std::max(0.0f, p.cooldown - dt);
  const bool reloadAsked = std::exchange(p.reloadRequest, false);
  const int switchTo = std::exchange(p.switchRequest, -1);
  const bool wasBeam = p.beamActive;
  p.beamActive = false;
  auto beamStopped = [&] {
    if (wasBeam && !p.beamActive) events_.push_back({FootEvent::Type::BeamStop, p.eye, {}, index, -1, p.weapon});
  };
  if (!p.alive()) {
    beamStopped();
    return;
  }

  // Changing weapons cancels a reload and takes a moment.
  if (switchTo >= 0 && switchTo < int(p.owned.size()) && p.owned[size_t(switchTo)] && switchTo != p.weapon) {
    p.weapon = switchTo;
    p.reloadLeft = 0;
    p.switchLeft = kSwitchSeconds;
    p.beamCarry = 0;
    events_.push_back({FootEvent::Type::WeaponSwitched, p.eye, {}, index, -1, p.weapon});
  }
  if (p.switching()) {
    p.switchLeft = std::max(0.0f, p.switchLeft - dt);
    beamStopped();
    return;
  }

  const FootWeaponDef& w = weapon(p);
  int& mag = p.magazines[size_t(p.weapon)];
  const int ammoIndex = content_->ammoIndex(w.ammo);
  int dummy = 0;
  int& res = ammoIndex >= 0 ? p.ammo[size_t(ammoIndex)] : dummy;

  if (p.reloading()) {
    p.reloadLeft -= dt;
    if (p.reloadLeft <= 0) {
      p.reloadLeft = 0;
      const int taken = std::min(w.magazine - mag, res);
      mag += taken;
      res -= taken;
      events_.push_back({FootEvent::Type::ReloadDone, p.eye, {}, index, -1, p.weapon});
    }
    beamStopped();
    return;
  }
  auto startReload = [&] {
    p.reloadLeft = w.reloadSeconds;
    events_.push_back({FootEvent::Type::ReloadStart, p.eye, {}, index, -1, p.weapon});
  };
  if (reloadAsked && mag < w.magazine && res > 0) {
    startReload();
    beamStopped();
    return;
  }
  if (!p.trigger || p.cooldown > 0) {
    beamStopped();
    return;
  }
  if (mag <= 0) {
    if (res > 0) {
      startReload();
    } else {
      p.cooldown = 0.35f;
      events_.push_back({FootEvent::Type::Dry, p.eye, {}, index, -1, p.weapon});
    }
    beamStopped();
    return;
  }

  if (w.mode == FootFireMode::Beam) {
    updateBeam(index, p, w, dt, mesh);
    if (!wasBeam && p.beamActive) events_.push_back({FootEvent::Type::BeamStart, p.eye, {}, index, -1, p.weapon});
    beamStopped();
    return;
  }

  --mag;
  p.cooldown = w.fireInterval;
  hear(p.eye, w.noiseRadius);
  if (w.mode == FootFireMode::Pellets) {
    // A cone of pellets around the aim, each its own hitscan. Repeatable for the same fight.
    const Vec3 side = normalize(length(cross(p.look, {0, 1, 0})) > 1e-3f ? cross(p.look, {0, 1, 0}) : Vec3{1, 0, 0});
    const Vec3 up = cross(side, p.look);
    const float spread = std::tan(radians(w.spreadDeg));
    for (int i = 0; i < w.pellets; ++i) {
      const float a = random01() * 6.2831853f, r = std::sqrt(random01()) * spread;
      const Vec3 dir = normalize(p.look + side * (std::cos(a) * r) + up * (std::sin(a) * r));
      fireHitscan(index, p, w, dir, mesh, i < 6);
    }
  } else {
    fireHitscan(index, p, w, p.look, mesh, true);
  }
}

void FootCombat::fireHitscan(int index, FootPlayer& p, const FootWeaponDef& w, const Vec3& dir, const TriangleCollider* mesh,
                             bool tracer) {
  // The nearest of the map and every living enemy along the line of fire.
  float wall = w.range;
  const bool hitMap = mesh && mesh->raycast(p.eye, dir, w.range, wall);
  int struck = -1;
  float nearest = wall;
  for (int i = 0; i < int(enemies_.size()); ++i) {
    const FootEnemy& e = enemies_[size_t(i)];
    if (e.state == EnemyState::Dead) continue;
    const FootEnemyDef& d = enemyDef(e);
    const float t = raySphere(p.eye, dir, e.body.feet + Vec3{0, d.hitCentreHeight, 0}, d.hitRadius);
    if (t >= 0 && t < nearest) {
      nearest = t;
      struck = i;
    }
  }
  const Vec3 end = p.eye + dir * nearest;
  if (tracer) events_.push_back({FootEvent::Type::Shot, end, p.eye, index, -1, p.weapon});
  if (struck >= 0) {
    events_.push_back({FootEvent::Type::HitEnemy, end, p.eye, index, struck, p.weapon});
    wake(struck, enemies_[size_t(struck)], p.eye);
    damageEnemy(struck, w.damage, end, flat(dir) * w.knockback);
  } else if (hitMap) {
    events_.push_back({FootEvent::Type::HitWall, end, p.eye, index, -1, p.weapon});
  }
}

void FootCombat::updateBeam(int index, FootPlayer& p, const FootWeaponDef& w, float dt, const TriangleCollider* mesh) {
  int& mag = p.magazines[size_t(p.weapon)];
  p.beamCarry += w.ammoPerSecond * dt;
  while (p.beamCarry >= 1.0f && mag > 0) {
    p.beamCarry -= 1.0f;
    --mag;
  }
  p.beamActive = true;
  float wall = w.range;
  const bool hitMap = mesh && mesh->raycast(p.eye, p.look, w.range, wall);
  int struck = -1;
  float nearest = wall;
  for (int i = 0; i < int(enemies_.size()); ++i) {
    const FootEnemy& e = enemies_[size_t(i)];
    if (e.state == EnemyState::Dead) continue;
    const FootEnemyDef& d = enemyDef(e);
    const float t = raySphere(p.eye, p.look, e.body.feet + Vec3{0, d.hitCentreHeight, 0}, d.hitRadius);
    if (t >= 0 && t < nearest) {
      nearest = t;
      struck = i;
    }
  }
  p.beamEnd = p.eye + p.look * nearest;
  p.beamTick -= dt;
  const bool tick = p.beamTick <= 0;
  if (tick) {
    p.beamTick = kBeamHitEvery;
    hear(p.eye, w.noiseRadius);
  }
  if (struck >= 0) {
    if (tick) events_.push_back({FootEvent::Type::HitEnemy, p.beamEnd, p.eye, index, struck, p.weapon});
    wake(struck, enemies_[size_t(struck)], p.eye);
    damageEnemy(struck, w.damage * dt, p.beamEnd, {});
  } else if (hitMap && tick) {
    events_.push_back({FootEvent::Type::HitWall, p.beamEnd, p.eye, index, -1, p.weapon});
  }
}

// ---- Enemies --------------------------------------------------------------------------------------

void FootCombat::wake(int index, FootEnemy& e, const Vec3& towards) {
  if (e.state == EnemyState::Dead) return;
  e.lastSeen = towards;
  e.sinceSeen = 0;
  if (e.state == EnemyState::Asleep) {
    e.state = EnemyState::Chasing;
    events_.push_back({FootEvent::Type::EnemyWoke, e.body.feet, {}, -1, index});
  }
}

void FootCombat::hear(const Vec3& at, float radius) {
  for (int i = 0; i < int(enemies_.size()); ++i) {
    FootEnemy& e = enemies_[size_t(i)];
    if (e.state == EnemyState::Asleep && length(e.body.feet - at) < radius) wake(i, e, at);
  }
}

void FootCombat::hurtPlayer(FootPlayer& p, int playerIndex, int enemyIndex, float amount, const Vec3& at) {
  if (!p.alive()) return;
  p.health = std::max(0.0f, p.health - amount);
  events_.push_back({FootEvent::Type::PlayerHurt, at, {}, playerIndex, enemyIndex});
  if (!p.alive()) events_.push_back({FootEvent::Type::PlayerDown, p.feet, {}, playerIndex, enemyIndex});
}

void FootCombat::damageEnemy(int index, float amount, const Vec3& at, const Vec3& push) {
  FootEnemy& e = enemies_[size_t(index)];
  if (e.state == EnemyState::Dead) return;
  const FootEnemyDef& d = enemyDef(e);
  if (e.state == EnemyState::Recovering) amount *= d.recoverDamageScale;  // stunned after a charge
  e.health -= amount;
  e.body.velocity += push * (1.0f - std::clamp(d.knockbackResistance, 0.0f, 1.0f));
  if (e.health > 0) return;
  e.health = 0;
  e.state = EnemyState::Dead;
  e.deadTime = 0;
  e.body.velocity = {};
  e.path.clear();
  events_.push_back({FootEvent::Type::EnemyDied, at, {}, -1, index});
}

void FootCombat::stand(FootEnemy& e, float dt, const std::vector<Aabb>& colliders, const TriangleCollider* mesh) {
  stepCharacter(e.body, CharacterInput{}, bodyParams(e), colliders, dt, nullptr, mesh);
}

void FootCombat::moveTowards(FootEnemy& e, const FootEnemyDef& d, const Vec3& goal, float dt, const std::vector<Aabb>& colliders,
                             const TriangleCollider* mesh, const NavGrid* nav, bool away) {
  Vec3 steer = goal;
  if (!away && nav && !nav->empty()) {
    // Straight at it while that works; otherwise along a path, refreshed as the goal moves.
    e.directCheck -= dt;
    if (e.directCheck <= 0) {
      e.directCheck = 0.25f;
      e.direct = length(flat(goal - e.body.feet)) < 1.2f || nav->walkableLine(e.body.feet, goal, d.bodyHeight * 0.9f);
    }
    if (!e.direct) {
      e.repath -= dt;
      if (e.repath <= 0 || length(goal - e.pathGoal) > 1.5f || e.pathIndex >= e.path.size()) {
        e.repath = 0.6f;
        e.pathGoal = goal;
        e.pathIndex = 0;
        if (!nav->findPath(e.body.feet, goal, d.bodyHeight * 0.9f, e.path)) e.path.clear();
      }
      while (e.pathIndex < e.path.size() && length(flat(e.path[e.pathIndex] - e.body.feet)) < 0.45f) ++e.pathIndex;
      if (e.pathIndex < e.path.size()) steer = e.path[e.pathIndex];
    }
  }
  Vec3 to = flat(steer - e.body.feet);
  if (away) to = to * -1.0f;
  CharacterInput in;
  const float distance = length(to);
  if (distance > 0.05f) {
    e.body.yaw = turnTowards(e.body.yaw, yawTowards(away ? to * -1.0f : to), 7.0f * dt);
    // Walk forwards, or backwards while still facing what it is backing away from.
    in.move = away ? Vec2{0, -1} : Vec2{0, 1};
    if (e.sidestepTime > 0) {
      e.sidestepTime -= dt;
      in.move = {e.sidestepDir, away ? -0.3f : 0.3f};
    }
    in.run = true;
  }
  const Vec3 before = e.body.feet;
  stepCharacter(e.body, in, bodyParams(e), colliders, dt, nullptr, mesh);
  const float moved = length(e.body.feet - before);
  e.stride += moved * 3.5f / std::max(0.5f, enemyDef(e).visualScale);
  // Stuck against something the path did not know about: scuttle sideways for a moment.
  if (distance > 0.05f && dt > 0 && moved < d.speed * dt * 0.25f) {
    e.stuckTime += dt;
    if (e.stuckTime > 0.4f) {
      e.stuckTime = 0;
      e.sidestepTime = 0.7f;
      e.sidestepDir = -e.sidestepDir;
      e.repath = 0;
    }
  } else {
    e.stuckTime = 0;
  }
}

void FootCombat::meleeStrike(int index, FootEnemy& e, const FootEnemyDef& d, FootPlayer* target, int targetIndex,
                             float distance) {
  e.state = EnemyState::Chasing;
  e.timer = 0;
  e.cooldown = d.attackCooldown;
  if (target && distance < d.attackReach && e.seesTarget) {
    events_.push_back({FootEvent::Type::EnemyStruck, target->feet, {}, targetIndex, index});
    hurtPlayer(*target, targetIndex, index, d.attackDamage, target->feet);
  }
}

void FootCombat::updateEnemy(int index, FootEnemy& e, float dt, std::vector<FootPlayer>& players,
                             const std::vector<Aabb>& colliders, const TriangleCollider* mesh, const NavGrid* nav) {
  const FootEnemyDef& d = enemyDef(e);
  if (e.state == EnemyState::Dead) {
    e.deadTime += dt;
    stand(e, dt, colliders, mesh);
    return;
  }

  // Who it cares about: the nearest player still standing.
  FootPlayer* target = nullptr;
  float targetDistance = std::numeric_limits<float>::max();
  int targetIndex = -1;
  for (int i = 0; i < int(players.size()); ++i) {
    FootPlayer& p = players[size_t(i)];
    if (!p.alive()) continue;
    const float dist = length(p.feet - e.body.feet);
    if (dist < targetDistance) {
      targetDistance = dist;
      target = &p;
      targetIndex = i;
    }
  }
  const Vec3 head = e.body.feet + Vec3{0, d.bodyHeight * 0.65f, 0};
  e.sightCheck -= dt;
  if (e.sightCheck <= 0) {
    e.sightCheck = kSightCheckSeconds;
    e.seesTarget = target && targetDistance < d.sightRange && lineOfSight(head, target->eye, mesh);
  }
  if (!target) e.seesTarget = false;
  // Running feet carry: close enough and it hears where you are, wall or not.
  const bool hears = target && target->noisy && targetDistance < d.hearRunRange;

  if (e.state == EnemyState::Asleep) {
    if (e.seesTarget) {
      const Vec3 to = target->feet - e.body.feet;
      const bool inFront = dot(to, facingOf(e.body.yaw)) > 0.2f * targetDistance;
      if (inFront || targetDistance < d.closeSenseRange) wake(index, e, target->eye);
    } else if (hears) {
      wake(index, e, target->feet);
    }
    stand(e, dt, colliders, mesh);
    return;
  }

  if (e.seesTarget || hears) {
    e.lastSeen = target->feet;
    e.sinceSeen = 0;
  } else if (e.state != EnemyState::Charging && e.state != EnemyState::Recovering) {
    e.sinceSeen += dt;
    if (e.sinceSeen > d.forgetSeconds) {
      e.state = EnemyState::Asleep;  // lost you: settles where it is
      e.timer = 0;
      e.home = e.body.feet;
      e.path.clear();
      stand(e, dt, colliders, mesh);
      return;
    }
  }

  e.timer = std::max(0.0f, e.timer - dt);
  e.cooldown = std::max(0.0f, e.cooldown - dt);
  auto face = [&](float rate) {
    if (target) e.body.yaw = turnTowards(e.body.yaw, yawTowards(flat(target->feet - e.body.feet)), rate * dt);
  };

  switch (e.state) {
    case EnemyState::WindingUp:  // melee lunge
      face(5.0f);
      if (e.timer <= 0) meleeStrike(index, e, d, target, targetIndex, targetDistance);
      stand(e, dt, colliders, mesh);
      return;

    case EnemyState::Aiming:  // ranged: hold still, then throw
      face(6.0f);
      if (e.timer <= 0) {
        e.state = EnemyState::Chasing;
        e.cooldown = d.fireCooldown;
        if (target && e.seesTarget) {
          FootProjectile pr;
          pr.position = head + facingOf(e.body.yaw) * (d.bodyRadius + 0.15f);
          const Vec3 aim = target->eye - Vec3{0, 0.45f, 0};
          const float dist = length(aim - pr.position);
          const float t = dist / d.projectileSpeed;
          // Aim above the target by the drop over the flight time; no lead, so stepping aside works.
          const Vec3 lofted = aim + Vec3{0, 0.5f * d.projectileGravity * t * t, 0};
          pr.velocity = normalize(lofted - pr.position) * d.projectileSpeed;
          pr.damage = d.projectileDamage;
          pr.radius = d.projectileRadius;
          pr.gravity = d.projectileGravity;
          pr.enemy = index;
          projectiles_.push_back(pr);
          events_.push_back({FootEvent::Type::EnemyFired, pr.position, {}, targetIndex, index});
        }
      }
      stand(e, dt, colliders, mesh);
      return;

    case EnemyState::ChargeWindup:
      face(4.0f);
      if (e.timer <= 0) {
        e.state = EnemyState::Charging;
        e.chargeDir = facingOf(e.body.yaw);
        e.chargeLeft = d.chargeDistance;
        events_.push_back({FootEvent::Type::EnemyCharged, e.body.feet, {}, targetIndex, index});
      }
      stand(e, dt, colliders, mesh);
      return;

    case EnemyState::Charging: {
      CharacterParams fast = bodyParams(e);
      fast.runSpeed = fast.walkSpeed = d.chargeSpeed;
      fast.groundAccel = 60;
      CharacterInput in;
      in.move = {0, 1};
      in.run = true;
      e.body.yaw = yawTowards(e.chargeDir);
      const Vec3 before = e.body.feet;
      stepCharacter(e.body, in, fast, colliders, dt, nullptr, mesh);
      const float moved = length(flat(e.body.feet - before));
      e.chargeLeft -= moved;
      e.stride += moved * 2.5f;
      bool stop = e.chargeLeft <= 0 || (dt > 0 && moved < d.chargeSpeed * dt * 0.3f && e.chargeLeft < d.chargeDistance - 1.0f);
      for (int i = 0; i < int(players.size()) && !stop; ++i) {
        FootPlayer& p = players[size_t(i)];
        if (!p.alive()) continue;
        const Vec3 rel = p.feet - e.body.feet;
        if (length(flat(rel)) < d.bodyRadius + 0.75f && std::fabs(rel.y) < 1.4f) {
          events_.push_back({FootEvent::Type::EnemyStruck, p.feet, {}, i, index});
          hurtPlayer(p, i, index, d.chargeDamage, p.feet);
          p.knock += e.chargeDir * d.chargeKnockback + Vec3{0, 2.5f, 0};
          stop = true;
        }
      }
      if (stop) {
        e.state = EnemyState::Recovering;
        e.timer = d.recoverSeconds;
        e.body.velocity = {};
        events_.push_back({FootEvent::Type::EnemyStunned, e.body.feet, {}, -1, index});
      }
      return;
    }

    case EnemyState::Recovering:
      if (e.timer <= 0) {
        e.state = EnemyState::Chasing;
        e.cooldown = 1.5f;
      }
      stand(e, dt, colliders, mesh);
      return;

    default: break;
  }

  // ---- Chasing ----
  const bool sees = target && e.seesTarget;
  // Up close everyone lunges.
  if (sees && targetDistance < d.attackRange && e.cooldown <= 0) {
    e.state = EnemyState::WindingUp;
    e.timer = d.windupSeconds;
    events_.push_back({FootEvent::Type::EnemyLunged, e.body.feet, {}, targetIndex, index});
    stand(e, dt, colliders, mesh);
    return;
  }
  if (d.behaviour == FootBehaviour::Ranged && sees) {
    if (e.cooldown <= 0 && targetDistance > d.retreatRange * 0.8f) {
      e.state = EnemyState::Aiming;
      e.timer = d.aimSeconds;
      events_.push_back({FootEvent::Type::EnemyLunged, e.body.feet, {}, targetIndex, index});
      stand(e, dt, colliders, mesh);
      return;
    }
    if (targetDistance < d.retreatRange) {
      moveTowards(e, d, target->feet, dt, colliders, mesh, nav, true);  // back off, still facing you
    } else if (targetDistance > d.preferredRange) {
      moveTowards(e, d, target->feet, dt, colliders, mesh, nav);
    } else {
      face(5.0f);
      stand(e, dt, colliders, mesh);
    }
    return;
  }
  if (d.behaviour == FootBehaviour::Charger && sees && e.cooldown <= 0 && targetDistance > d.chargeMinRange &&
      targetDistance < d.chargeMaxRange && (!nav || nav->empty() || nav->walkableLine(e.body.feet, target->feet, d.bodyHeight * 0.9f))) {
    e.state = EnemyState::ChargeWindup;
    e.timer = d.chargeWindup;
    events_.push_back({FootEvent::Type::EnemyLunged, e.body.feet, {}, targetIndex, index});
    stand(e, dt, colliders, mesh);
    return;
  }
  const Vec3 goal = sees ? target->feet : e.lastSeen;
  const float distance = length(flat(goal - e.body.feet));
  const bool closeEnough = distance < (sees ? d.attackRange * 0.8f : 0.6f);
  if (closeEnough) {
    face(7.0f);
    stand(e, dt, colliders, mesh);
  } else {
    moveTowards(e, d, goal, dt, colliders, mesh, nav);
  }
}

void FootCombat::updateProjectiles(float dt, std::vector<FootPlayer>& players, const TriangleCollider* mesh) {
  for (size_t i = 0; i < projectiles_.size();) {
    FootProjectile& pr = projectiles_[i];
    pr.velocity.y -= pr.gravity * dt;
    const Vec3 a = pr.position, step = pr.velocity * dt;
    const float len = length(step);
    pr.life -= dt;
    bool done = pr.life <= 0;
    Vec3 hitAt = a + step;
    int hitPlayer = -1;
    // Players: a capsule from the knees to the head.
    for (int k = 0; k < int(players.size()) && !done; ++k) {
      FootPlayer& p = players[size_t(k)];
      if (!p.alive()) continue;
      const Vec3 lo = p.feet + Vec3{0, 0.35f, 0}, hi = p.eye;
      // Closest points between the projectile's segment and the player's spine (sampled).
      for (int s = 0; s <= 4 && hitPlayer < 0; ++s) {
        const Vec3 q = a + step * (float(s) / 4.0f);
        const Vec3 axis = hi - lo;
        const float t = std::clamp(dot(q - lo, axis) / std::max(1e-4f, dot(axis, axis)), 0.0f, 1.0f);
        if (length(q - (lo + axis * t)) < pr.radius + 0.32f) {
          hitPlayer = k;
          hitAt = q;
        }
      }
      if (hitPlayer >= 0) done = true;
    }
    float wall = 0;
    if (!done && mesh && len > 1e-5f && mesh->raycast(a, step / len, len, wall)) {
      done = true;
      hitAt = a + step / len * wall;
    }
    if (hitPlayer >= 0) hurtPlayer(players[size_t(hitPlayer)], hitPlayer, pr.enemy, pr.damage, hitAt);
    if (done) {
      if (pr.life > 0) events_.push_back({FootEvent::Type::ProjectileHit, hitAt, a, hitPlayer, pr.enemy});
      projectiles_.erase(projectiles_.begin() + std::ptrdiff_t(i));
    } else {
      pr.position = a + step;
      ++i;
    }
  }
}

}  // namespace fh
