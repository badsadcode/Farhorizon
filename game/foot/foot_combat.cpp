#include "game/foot/foot_combat.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace fh {

namespace {

constexpr float kSightCheckSeconds = 0.2f;

float yawTowards(const Vec3& dir) { return std::atan2(dir.x, -dir.z); }

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

}  // namespace

bool lineOfSight(const Vec3& from, const Vec3& to, const TriangleCollider* mesh) {
  if (!mesh) return true;
  const Vec3 d = to - from;
  const float len = length(d);
  if (len < 1e-3f) return true;
  float t = 0;
  return !mesh->raycast(from, d / len, len, t);
}

void FootCombat::begin(const std::vector<FootSpawn>& spawns) {
  crawlerBody_ = CharacterParams{};
  crawlerBody_.radius = 0.38f;
  crawlerBody_.height = 0.8f;
  crawlerBody_.eyeHeight = 0.5f;
  crawlerBody_.walkSpeed = crawler_.speed;
  crawlerBody_.runSpeed = crawler_.speed;
  crawlerBody_.groundAccel = 24;
  enemies_.clear();
  events_.clear();
  for (const FootSpawn& s : spawns) {
    FootEnemy e;
    e.markerIndex = s.markerIndex;
    e.home = s.position;
    e.homeYaw = radians(s.yawDeg);
    e.body.feet = s.position;
    e.body.yaw = e.homeYaw;
    e.health = crawler_.health;
    e.sidestepDir = enemies_.size() % 2 ? 1.0f : -1.0f;
    e.sightCheck = float(enemies_.size()) * 0.05f;  // not all on the same tick
    enemies_.push_back(e);
  }
}

void FootCombat::returnHome() {
  for (FootEnemy& e : enemies_) {
    if (e.state == CrawlerState::Dead) continue;
    e.body = CharacterState{};
    e.body.feet = e.home;
    e.body.yaw = e.homeYaw;
    e.state = CrawlerState::Asleep;
    e.health = crawler_.health;
    e.timer = 0;
    e.sinceSeen = 0;
    e.seesTarget = false;
  }
}

int FootCombat::aliveCount() const {
  return int(std::count_if(enemies_.begin(), enemies_.end(), [](const FootEnemy& e) { return e.state != CrawlerState::Dead; }));
}

void FootCombat::update(float dt, std::vector<FootPlayer>& players, const std::vector<Aabb>& colliders,
                        const TriangleCollider* mesh) {
  for (int i = 0; i < int(players.size()); ++i) updatePlayer(i, players[size_t(i)], dt, mesh);
  for (int i = 0; i < int(enemies_.size()); ++i) updateEnemy(i, enemies_[size_t(i)], dt, players, colliders, mesh);
}

void FootCombat::updatePlayer(int index, FootPlayer& p, float dt, const TriangleCollider* mesh) {
  p.cooldown = std::max(0.0f, p.cooldown - dt);
  const bool reloadAsked = std::exchange(p.reloadRequest, false);
  if (!p.alive()) return;
  if (p.reloading()) {
    p.reloadLeft -= dt;
    if (p.reloadLeft <= 0) {
      p.reloadLeft = 0;
      const int taken = std::min(gun_.magazine - p.magazine, p.reserve);
      p.magazine += taken;
      p.reserve -= taken;
      events_.push_back({FootEvent::Type::ReloadDone, p.eye, {}, index});
    }
    return;
  }
  auto startReload = [&] {
    p.reloadLeft = gun_.reloadSeconds;
    events_.push_back({FootEvent::Type::ReloadStart, p.eye, {}, index});
  };
  if (reloadAsked && p.magazine < gun_.magazine && p.reserve > 0) {
    startReload();
    return;
  }
  if (!p.trigger || p.cooldown > 0) return;
  if (p.magazine <= 0) {
    if (p.reserve > 0) {
      startReload();
    } else {
      p.cooldown = 0.35f;
      events_.push_back({FootEvent::Type::Dry, p.eye, {}, index});
    }
    return;
  }
  --p.magazine;
  p.cooldown = gun_.fireInterval;

  // The nearest of the map and every living enemy along the line of fire.
  float wall = gun_.range;
  const bool hitMap = mesh && mesh->raycast(p.eye, p.look, gun_.range, wall);
  int struck = -1;
  float nearest = wall;
  for (int i = 0; i < int(enemies_.size()); ++i) {
    const FootEnemy& e = enemies_[size_t(i)];
    if (e.state == CrawlerState::Dead) continue;
    const float t = raySphere(p.eye, p.look, e.body.feet + Vec3{0, crawler_.hitCentreHeight, 0}, crawler_.hitRadius);
    if (t >= 0 && t < nearest) {
      nearest = t;
      struck = i;
    }
  }
  const Vec3 end = p.eye + p.look * nearest;
  FootEvent shot{FootEvent::Type::Shot, end, p.eye, index};
  events_.push_back(shot);
  hear(p.eye, gun_.noiseRadius);
  if (struck >= 0) {
    FootEvent hit{FootEvent::Type::HitEnemy, end, p.eye, index, struck};
    events_.push_back(hit);
    wake(struck, enemies_[size_t(struck)], p.eye);
    damageEnemy(struck, gun_.damage, end);
  } else if (hitMap) {
    events_.push_back({FootEvent::Type::HitWall, end, p.eye, index});
  }
}

void FootCombat::wake(int index, FootEnemy& e, const Vec3& towards) {
  if (e.state == CrawlerState::Dead) return;
  e.lastSeen = towards;
  e.sinceSeen = 0;
  if (e.state == CrawlerState::Asleep) {
    e.state = CrawlerState::Chasing;
    events_.push_back({FootEvent::Type::EnemyWoke, e.body.feet, {}, -1, index});
  }
}

void FootCombat::hear(const Vec3& at, float radius) {
  for (int i = 0; i < int(enemies_.size()); ++i) {
    FootEnemy& e = enemies_[size_t(i)];
    if (e.state == CrawlerState::Asleep && length(e.body.feet - at) < radius) wake(i, e, at);
  }
}

void FootCombat::damageEnemy(int index, float amount, const Vec3& at) {
  FootEnemy& e = enemies_[size_t(index)];
  e.health -= amount;
  if (e.health > 0) return;
  e.health = 0;
  e.state = CrawlerState::Dead;
  e.deadTime = 0;
  e.body.velocity = {};
  events_.push_back({FootEvent::Type::EnemyDied, at, {}, -1, index});
}

void FootCombat::updateEnemy(int index, FootEnemy& e, float dt, std::vector<FootPlayer>& players,
                             const std::vector<Aabb>& colliders, const TriangleCollider* mesh) {
  CharacterInput in;
  if (e.state == CrawlerState::Dead) {
    e.deadTime += dt;
    stepCharacter(e.body, in, crawlerBody_, colliders, dt, nullptr, mesh);
    return;
  }

  // Who it cares about: the nearest player still standing.
  FootPlayer* target = nullptr;
  float targetDistance = std::numeric_limits<float>::max();
  int targetIndex = -1;
  for (int i = 0; i < int(players.size()); ++i) {
    FootPlayer& p = players[size_t(i)];
    if (!p.alive()) continue;
    const float d = length(p.feet - e.body.feet);
    if (d < targetDistance) {
      targetDistance = d;
      target = &p;
      targetIndex = i;
    }
  }
  const Vec3 head = e.body.feet + Vec3{0, 0.5f, 0};
  e.sightCheck -= dt;
  if (e.sightCheck <= 0) {
    e.sightCheck = kSightCheckSeconds;
    e.seesTarget = target && targetDistance < crawler_.sightRange && lineOfSight(head, target->eye, mesh);
  }
  if (!target) e.seesTarget = false;

  if (e.state == CrawlerState::Asleep) {
    if (e.seesTarget) {
      const Vec3 to = target->feet - e.body.feet;
      const Vec3 facing{std::sin(e.body.yaw), 0, -std::cos(e.body.yaw)};
      const bool inFront = dot(to, facing) > 0.2f * targetDistance;
      if (inFront || targetDistance < crawler_.closeSenseRange) wake(index, e, target->eye);
    }
    stepCharacter(e.body, in, crawlerBody_, colliders, dt, nullptr, mesh);
    return;
  }

  if (e.seesTarget) {
    e.lastSeen = target->feet;
    e.sinceSeen = 0;
  } else {
    e.sinceSeen += dt;
    if (e.sinceSeen > crawler_.forgetSeconds) {
      e.state = CrawlerState::Asleep;  // lost you: settles where it is
      e.timer = 0;
      e.home = e.body.feet;
      stepCharacter(e.body, in, crawlerBody_, colliders, dt, nullptr, mesh);
      return;
    }
  }

  e.timer = std::max(0.0f, e.timer - dt);
  if (e.state == CrawlerState::WindingUp) {
    if (target) e.body.yaw = turnTowards(e.body.yaw, yawTowards(target->feet - e.body.feet), 5.0f * dt);
    if (e.timer <= 0) {
      e.state = CrawlerState::Chasing;
      e.timer = crawler_.attackCooldown;
      if (target && targetDistance < crawler_.attackReach && e.seesTarget) {
        target->health = std::max(0.0f, target->health - crawler_.attackDamage);
        events_.push_back({FootEvent::Type::EnemyStruck, target->feet, {}, targetIndex, index});
        events_.push_back({FootEvent::Type::PlayerHurt, target->feet, {}, targetIndex, index});
        if (!target->alive()) events_.push_back({FootEvent::Type::PlayerDown, target->feet, {}, targetIndex, index});
      }
    }
    stepCharacter(e.body, in, crawlerBody_, colliders, dt, nullptr, mesh);
    return;
  }

  const Vec3 goal = e.seesTarget && target ? target->feet : e.lastSeen;
  Vec3 to = goal - e.body.feet;
  to.y = 0;
  const float distance = length(to);
  if (target && e.seesTarget && targetDistance < crawler_.attackRange && e.timer <= 0) {
    e.state = CrawlerState::WindingUp;
    e.timer = crawler_.windupSeconds;
    events_.push_back({FootEvent::Type::EnemyLunged, e.body.feet, {}, targetIndex, index});
    stepCharacter(e.body, in, crawlerBody_, colliders, dt, nullptr, mesh);
    return;
  }

  // Straight at the goal, sliding along walls. When that stops working, scuttle sideways for a moment.
  // This is not pathfinding: it can lose you around a long detour, and then it gives up.
  const bool closeEnough = distance < (e.seesTarget ? crawler_.attackRange * 0.8f : 0.6f);
  if (!closeEnough) {
    e.body.yaw = turnTowards(e.body.yaw, yawTowards(to), 7.0f * dt);
    in.move = {0, 1};
    if (e.sidestepTime > 0) {
      e.sidestepTime -= dt;
      in.move = {e.sidestepDir, 0.3f};
    }
    in.run = true;
  }
  const Vec3 before = e.body.feet;
  stepCharacter(e.body, in, crawlerBody_, colliders, dt, nullptr, mesh);
  const float moved = length(e.body.feet - before);
  e.stride += moved * 3.5f;
  if (!closeEnough && dt > 0 && moved < crawler_.speed * dt * 0.25f) {
    e.stuckTime += dt;
    if (e.stuckTime > 0.4f) {
      e.stuckTime = 0;
      e.sidestepTime = 0.7f;
      e.sidestepDir = -e.sidestepDir;
    }
  } else {
    e.stuckTime = 0;
  }
}

}  // namespace fh
