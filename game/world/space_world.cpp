#include "game/world/space_world.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "engine/core/log.h"

namespace fh {

void SpaceWorld::setOrigin(const SystemPosition& origin, double rebaseDistance) {
  frame_ = LocalFrame(origin, rebaseDistance);
}

void SpaceWorld::addProp(Prop p) {
  p.local = frame_.toLocal(p.system);
  props_.push_back(std::move(p));
}

std::uint32_t SpaceWorld::addAsteroidField(AsteroidFieldRuntime field) {
  fields_.push_back(std::move(field));
  return std::uint32_t(fields_.size() - 1);
}

void SpaceWorld::addAsteroid(Asteroid a) {
  a.local = frame_.toLocal(a.system);
  asteroids_.push_back(a);
}

void SpaceWorld::resolveCollision(const Vec3& centre, float radius) {
  FlightState& s = player_.state;
  const Vec3 d = s.position - centre;
  const float minDist = radius + player_.collisionRadius;
  const float dist2 = dot(d, d);
  if (dist2 >= minDist * minDist) return;
  const float dist = std::sqrt(dist2);
  const Vec3 n = dist > 1e-4f ? d / dist : Vec3{0, 1, 0};
  s.position = centre + n * minDist;  // push out
  const float vn = dot(s.velocity, n);
  if (vn < 0) {
    constexpr float restitution = 0.35f;
    constexpr float friction = 0.85f;
    const Vec3 tangential = s.velocity - n * vn;
    s.velocity = tangential * friction - n * (vn * restitution);
    if (-vn > 2.0f) {  // ignore resting contact jitter
      ++player_.collisions;
      player_.lastImpactSpeed = -vn;
      player_.lastImpactTime = time_;
      log::info("world", "impact at {:.1f} m/s", -vn);
    }
  }
}

void SpaceWorld::fixedUpdate(float dt) {
  time_ += dt;
  player_.previous = player_.state;
  stepFlight(player_.state, player_.input, player_.def.flight, dt);

  for (auto& p : props_)
    if (p.def.spinDegPerSec != 0.0f)
      p.rotation = normalize(Quat::fromAxisAngle(p.def.spinAxis, radians(p.def.spinDegPerSec) * dt) * p.rotation);
  for (auto& a : asteroids_) a.rotation = normalize(Quat::fromAxisAngle(a.spinAxis, a.spinRate * dt) * a.rotation);

  // Sphere collisions against nearby static bodies (brute force: a few hundred bodies at most here).
  for (const auto& p : props_) resolveCollision(p.local, p.collisionRadius);
  for (const auto& a : asteroids_) resolveCollision(a.local, a.radius * 0.9f);  // rocks are not full spheres
}

std::optional<Vec3> SpaceWorld::rebaseIfNeeded() {
  if (!frame_.needsRebase(player_.state.position)) return std::nullopt;
  const Vec3 delta = frame_.rebase(frame_.toSystem(player_.state.position));
  player_.state.position += delta;
  player_.previous.position += delta;
  // Static bodies are recomputed from their double-precision system positions (no accumulated drift).
  for (auto& p : props_) p.local = frame_.toLocal(p.system);
  for (auto& a : asteroids_) a.local = frame_.toLocal(a.system);
  return delta;
}

Transform SpaceWorld::playerRenderTransform(float alpha) const {
  return {lerp(player_.previous.position, player_.state.position, alpha),
          nlerp(player_.previous.orientation, player_.state.orientation, alpha)};
}

TargetInfo SpaceWorld::describe(const TargetRef& ref) const {
  TargetInfo t;
  if (ref.kind == TargetRef::Kind::Prop) {
    const Prop& p = props_.at(ref.index);
    t.name = p.def.name;
    t.marker = p.def.marker;
    t.local = p.local;
    t.radius = p.collisionRadius;
  } else {
    const Asteroid& a = asteroids_.at(ref.index);
    const auto& field = fields_.at(a.field);
    t.name = std::format("{} #{:03}", field.def.name, a.index);
    t.marker = "asteroid";
    t.local = a.local;
    t.radius = a.radius;
  }
  const Vec3 rel = t.local - player_.state.position;
  t.distance = length(rel);
  const Vec3 dir = t.distance > 1e-3 ? rel / float(t.distance) : Vec3{0, 0, -1};
  t.closingSpeed = dot(player_.state.velocity, dir);  // targets are static in Stage 2
  return t;
}

std::optional<TargetInfo> SpaceWorld::targetInfo() const {
  if (!target_) return std::nullopt;
  return describe(*target_);
}

void SpaceWorld::cycleTarget(double maxAsteroidRange) {
  struct Candidate {
    TargetRef ref;
    double distance;
  };
  std::vector<Candidate> c;
  const Vec3 me = player_.state.position;
  for (std::uint32_t i = 0; i < props_.size(); ++i)
    if (props_[i].def.targetable) c.push_back({{TargetRef::Kind::Prop, i}, length(props_[i].local - me)});
  for (std::uint32_t i = 0; i < asteroids_.size(); ++i) {
    const double d = length(asteroids_[i].local - me);
    if (d <= maxAsteroidRange) c.push_back({{TargetRef::Kind::Asteroid, i}, d});
  }
  if (c.empty()) {
    target_.reset();
    return;
  }
  std::sort(c.begin(), c.end(), [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
  // Props first (navigation), then asteroids by distance.
  std::stable_partition(c.begin(), c.end(), [](const Candidate& x) { return x.ref.kind == TargetRef::Kind::Prop; });
  size_t next = 0;
  if (target_) {
    for (size_t i = 0; i < c.size(); ++i)
      if (c[i].ref == *target_) {
        next = (i + 1) % c.size();
        break;
      }
  }
  target_ = c[next].ref;
}

void SpaceWorld::targetNearestAsteroid() {
  double best = 1e30;
  std::optional<TargetRef> found;
  for (std::uint32_t i = 0; i < asteroids_.size(); ++i) {
    const double d = length(asteroids_[i].local - player_.state.position) - asteroids_[i].radius;
    if (d < best) {
      best = d;
      found = TargetRef{TargetRef::Kind::Asteroid, i};
    }
  }
  if (found) target_ = found;
}

std::optional<std::pair<float, float>> SpaceWorld::nearestAsteroidSurface() const {
  float best = 1e30f, closing = 0;
  bool any = false;
  for (const auto& a : asteroids_) {
    const Vec3 rel = a.local - player_.state.position;
    const float centre = length(rel);
    const float surface = centre - a.radius - player_.collisionRadius;
    if (surface < best) {
      best = surface;
      closing = centre > 1e-3f ? dot(player_.state.velocity, rel / centre) : 0.0f;
      any = true;
    }
  }
  if (!any) return std::nullopt;
  return std::make_pair(best, closing);
}

}  // namespace fh
