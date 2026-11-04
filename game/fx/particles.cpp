#include "game/fx/particles.h"

#include <algorithm>
#include <cmath>

namespace fh {

namespace {
constexpr float kTwoPi = 6.2831853f;
constexpr float kMaxTrailStep = 400.0f;  // metres per frame; beyond this the nozzle jumped
Vec3 perpendicular(const Vec3& n) {
  const Vec3 ref = std::fabs(n.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  return normalize(cross(ref, n));
}
}  // namespace

float ParticleSystem::random() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return float(rng_ & 0xFFFFFFu) / float(0xFFFFFFu);
}

Vec3 ParticleSystem::randomUnit() {
  const float z = random() * 2.0f - 1.0f, a = random() * kTwoPi;
  const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  return {r * std::cos(a), r * std::sin(a), z};
}

void ParticleSystem::add(const Particle& p) {
  if (particles_.size() < kMaxParticles) particles_.push_back(p);
}

void ParticleSystem::update(float dt) {
  for (Particle& p : particles_) {
    p.age += dt;
    p.position += p.velocity * dt;
    p.spin += p.spinRate * dt;
  }
  std::erase_if(particles_, [](const Particle& p) { return p.age >= p.life; });
}

void ParticleSystem::shift(const Vec3& delta) {
  for (Particle& p : particles_)
    if (!p.attached) p.position += delta;
}

void TrailEmitter::shift(const Vec3& delta) {
  last += delta;
  for (TrailPoint& p : points) p.position += delta;
}

namespace {
void ageTrail(TrailEmitter& em, float dt) {
  for (TrailPoint& p : em.points) {
    p.age += dt;
    p.position += p.velocity * dt;
  }
  std::erase_if(em.points, [](const TrailPoint& p) { return p.age >= p.life; });
}
constexpr std::size_t kMaxTrailPoints = 320;

// Appends one quad per segment. Consecutive quads share their edge, so a ribbon cannot have gaps.
struct RibbonNode {
  Vec3 position;
  Vec3 color;   // already faded
  float halfWidth;
  float along;
};
void appendRibbon(const std::vector<RibbonNode>& nodes, float seed, const Vec3& camera, std::vector<RibbonVertex>& out) {
  if (nodes.size() < 2) return;
  std::vector<Vec3> sides(nodes.size());
  Vec3 previous{0, 0, 0};
  for (size_t i = 0; i < nodes.size(); ++i) {
    const Vec3 tangent = nodes[std::min(i + 1, nodes.size() - 1)].position - nodes[i > 0 ? i - 1 : 0].position;
    Vec3 side = cross(tangent, nodes[i].position - camera);
    const float len = length(side);
    if (len > 1e-5f) side = side / len;
    else side = previous;             // looking straight along the trail here
    if (dot(side, previous) < 0) side = -side;  // no twists
    sides[i] = side;
    previous = side;
  }
  auto vertex = [&](size_t i, float u) {
    const RibbonNode& n = nodes[i];
    // Up close the ribbon would fill the screen, so it fades as it reaches the camera.
    const float nearFade = std::clamp((length(n.position - camera) - 1.0f) / 7.0f, 0.0f, 1.0f);
    return RibbonVertex{Vec4(n.position + sides[i] * (u * n.halfWidth) - camera, u), Vec4(n.color * nearFade, 0.0f),
                        Vec4(n.along, seed, 0, 0)};
  };
  for (size_t i = 0; i + 1 < nodes.size(); ++i) {
    const RibbonVertex a = vertex(i, -1), b = vertex(i, 1), c = vertex(i + 1, -1), d = vertex(i + 1, 1);
    out.insert(out.end(), {a, b, d, a, d, c});
  }
}
}  // namespace

void ParticleSystem::emitEngine(TrailEmitter& em, const EngineFx& fx, float dt) {
  const Vec3 from = em.hasLast ? em.last : fx.position;
  const bool jumped = em.hasLast && length(fx.position - from) > kMaxTrailStep;
  em.last = fx.position;
  em.hasLast = true;
  em.size = fx.size;
  em.seed = fx.seed;
  if (jumped) {
    em.carry = 0;
    em.points.clear();
    return;
  }
  ageTrail(em, dt);
  const float demand = std::clamp(fx.demand, 0.0f, 1.0f);
  const float strength = 0.02f + 0.98f * demand;  // idle engines barely breathe
  const float boost = fx.boosting ? 1.6f : 1.0f;
  const float life = fx.life * (0.5f + 0.5f * demand);
  const Vec3 gasVelocity = fx.shipVelocity * 0.82f + fx.back * (10.0f + 30.0f * demand);

  // The ribbon: a point every little while, far more often than the eye can see joints.
  em.sincePoint += dt;
  if (em.sincePoint >= 1.0f / 90.0f && strength > 0.04f) {
    em.sincePoint = 0;
    TrailPoint p;
    p.position = fx.position;
    p.velocity = gasVelocity;
    p.color = fx.color * (0.1f + 0.14f * demand) * boost;
    p.life = life;
    p.strength = strength;
    if (!em.points.empty()) em.along += length(fx.position - em.points.back().position);
    p.along = em.along;
    em.points.push_back(p);
    if (em.points.size() > kMaxTrailPoints) em.points.erase(em.points.begin());
  }

  // Puffs give the ribbon volume when you look along it, which a flat ribbon cannot do alone.
  em.carry += fx.rate * 0.12f * strength * boost * dt;
  const int count = int(em.carry);
  em.carry -= float(count);
  for (int i = 0; i < count; ++i) {
    const float t = (float(i) + random()) / float(count);
    const Vec3 at = from + (fx.position - from) * t;
    const Vec3 jitter = randomUnit();
    const float elapsed = dt * (1.0f - t);  // emitted earlier in the frame: already on its way

    Particle gas;
    gas.position = at + jitter * (fx.size * 0.3f);
    gas.velocity = gasVelocity + jitter * (0.3f + 0.7f * fx.size);
    gas.color = fx.color * (0.03f + 0.06f * demand) * boost;
    gas.life = life * (0.6f + 0.6f * random());
    gas.sizeStart = fx.size * (0.7f + 0.4f * random());
    gas.sizeEnd = fx.size * (2.2f + 1.6f * random());
    gas.spin = random() * kTwoPi;
    gas.spinRate = (random() - 0.5f) * 1.5f;
    gas.fadeIn = 0.05f;
    gas.frame = ParticleFrame::Puff;
    gas.position += gas.velocity * elapsed;
    gas.age = elapsed;
    add(gas);

    // Stray embers under boost.
    if (fx.boosting && random() < 0.3f) {
      Particle spark;
      spark.position = at;
      spark.velocity = fx.shipVelocity * 0.9f + fx.back * 25.0f + jitter * 9.0f;
      spark.color = fx.color * 3.0f;
      spark.life = 0.5f + 0.7f * random();
      spark.sizeStart = 0.18f + 0.2f * random();
      spark.sizeEnd = 0.04f;
      spark.spin = random() * kTwoPi;
      spark.spinRate = (random() - 0.5f) * 6.0f;
      spark.frame = ParticleFrame::Spark;
      add(spark);
    }
  }
}

void ParticleSystem::tickTrail(TrailEmitter& em, float dt) const { ageTrail(em, dt); }

void ParticleSystem::buildRibbon(const TrailEmitter& em, const Vec3& camera, std::vector<RibbonVertex>& out) {
  if (em.points.empty()) return;
  std::vector<RibbonNode> nodes;
  nodes.reserve(em.points.size() + 1);
  for (const TrailPoint& p : em.points) {
    const float t = std::clamp(p.age / std::max(p.life, 1e-4f), 0.0f, 1.0f);
    const float fade = (1.0f - t) * (1.0f - t) * std::min(1.0f, (1.0f - t) * 6.0f);
    nodes.push_back({p.position, p.color * fade, em.size * (1.0f + 2.1f * t) * (0.6f + 0.4f * p.strength), p.along});
  }
  if (em.hasLast) {  // the newest stretch always reaches the nozzle
    const TrailPoint& newest = em.points.back();
    const float gap = length(em.last - newest.position);
    if (gap > 1e-3f) nodes.push_back({em.last, newest.color, em.size * (0.6f + 0.4f * newest.strength), newest.along + gap});
  }
  appendRibbon(nodes, em.seed, camera, out);
}

void ParticleSystem::buildPlume(const Vec3& nozzle, const Vec3& forward, const Vec3& color, float amount, float seed,
                                const Vec3& camera, std::vector<RibbonVertex>& out) {
  const float k = std::clamp(amount, 0.0f, 1.0f);
  if (k <= 0.01f) return;
  const Vec3 back = -normalize(forward);
  const float reach = 50.0f + 150.0f * k;
  constexpr int kNodes = 28;
  std::vector<RibbonNode> nodes;
  nodes.reserve(kNodes);
  for (int i = 0; i < kNodes; ++i) {
    const float t = float(i) / float(kNodes - 1);
    const float fade = (1.0f - t) * (1.0f - t) * std::min(1.0f, t * 12.0f + 0.3f);
    nodes.push_back({nozzle + back * (reach * t), color * (0.32f * k * fade), 0.8f + 2.2f * t, reach * t * 3.0f});
  }
  appendRibbon(nodes, seed, camera, out);
}

void ParticleSystem::emitTravel(const TravelFx& fx, float dt) {
  if (fx.phase == TravelFxPhase::Idle) return;
  const Vec3 f = normalize(fx.forward);
  const Vec3 side = perpendicular(f), up = cross(f, side);

  if (fx.phase == TravelFxPhase::Spooling) {
    // Sparks gather at the nozzles, faster and brighter as the drive charges.
    const float s = std::clamp(fx.spool, 0.0f, 1.0f);
    sparkCarry_ += (30.0f + 260.0f * s * s) * dt;
    const int count = int(sparkCarry_);
    sparkCarry_ -= float(count);
    for (int i = 0; i < count && fx.nozzleCount > 0; ++i) {
      const Vec3 nozzle = fx.nozzles[int(random() * float(fx.nozzleCount)) % fx.nozzleCount];
      const float a = random() * kTwoPi, r = 2.0f + 7.0f * random();
      const Vec3 start = nozzle + (side * std::cos(a) + up * std::sin(a)) * r - f * (1.0f + 3.0f * random());
      Particle p;
      p.attached = true;
      p.position = start;
      p.life = 0.35f + 0.35f * random();
      p.velocity = (nozzle - start) / p.life;
      p.color = fx.color * (1.2f + 3.5f * s);
      p.sizeStart = 0.16f + 0.22f * s;
      p.sizeEnd = 0.05f;
      p.stretch = 0.5f + 1.4f * s;
      p.fadeIn = 0.3f;
      p.frame = ParticleFrame::Streak;
      add(p);
    }
    return;
  }

  // Transit: streaks rush past the ship from ahead.
  const float k = std::clamp(fx.speedFraction, 0.0f, 1.0f);
  if (k <= 0.01f) return;
  streakCarry_ += (220.0f + 330.0f * k) * dt;
  int count = int(streakCarry_);
  streakCarry_ -= float(count);
  const float speed = 500.0f + 1500.0f * k;
  for (int i = 0; i < count; ++i) {
    const float a = random() * kTwoPi;
    const float r = 7.0f + 75.0f * std::sqrt(random());  // clear corridor around the ship
    const float ahead = 120.0f + 520.0f * random();
    Particle p;
    p.attached = true;
    p.position = (side * std::cos(a) + up * std::sin(a)) * r + f * ahead;
    p.velocity = f * -speed;
    p.life = (ahead + 200.0f) / speed;
    p.color = fx.color * (0.25f + 0.5f * k) * (0.3f + 0.7f * random());
    const float w = random();
    p.sizeStart = p.sizeEnd = 0.25f + 2.6f * w * w * w;  // mostly slim, a few broad
    p.stretch = (8.0f + 46.0f * k) * (0.6f + 0.8f * random());
    p.fadeIn = 0.25f;
    p.frame = ParticleFrame::Streak;
    add(p);
  }
}

void ParticleSystem::emitBurst(const Vec3& forward, const Vec3& color, float strength) {
  const Vec3 f = normalize(forward);
  const Vec3 side = perpendicular(f), up = cross(f, side);
  // An expanding ring across the flight path, and a soft flash at its centre.
  const int ringCount = int(140.0f * strength);
  for (int i = 0; i < ringCount; ++i) {
    const float a = kTwoPi * (float(i) + random()) / float(std::max(1, ringCount));
    const Vec3 dir = side * std::cos(a) + up * std::sin(a);
    Particle p;
    p.attached = true;
    p.position = dir * 4.0f;
    p.velocity = dir * (70.0f + 90.0f * random()) * strength - f * (6.0f * random());
    p.color = color * (2.0f + 3.0f * random()) * strength;
    p.life = 0.4f + 0.45f * random();
    p.sizeStart = 0.3f + 0.4f * random();
    p.sizeEnd = 0.05f;
    p.stretch = 3.0f + 7.0f * random();
    p.frame = ParticleFrame::Streak;
    add(p);
  }
  Particle flash;
  flash.attached = true;
  flash.color = color * (5.0f * strength);
  flash.life = 0.32f;
  flash.sizeStart = 9.0f;
  flash.sizeEnd = 60.0f * strength;
  flash.frame = ParticleFrame::Core;
  add(flash);
}

void ParticleSystem::buildSprites(const Vec3& cameraPosition, const Vec3& shipPosition,
                                  std::vector<ParticleSprite>& out) const {
  out.clear();
  out.reserve(particles_.size());
  for (const Particle& p : particles_) {
    const float t = std::clamp(p.age / std::max(p.life, 1e-4f), 0.0f, 1.0f);
    const float in = p.fadeIn > 0 ? std::min(t / p.fadeIn, 1.0f) : 1.0f;
    const float fade = in * (1.0f - t) * (1.0f - t);
    if (fade <= 0.001f) continue;
    const float size = p.sizeStart + (p.sizeEnd - p.sizeStart) * t;
    const Vec3 rel = (p.attached ? shipPosition + p.position : p.position) - cameraPosition;
    Vec3 axis{};
    float halfLength = 0;
    if (p.stretch > 0) {
      const float speed = length(p.velocity);
      if (speed > 1e-3f) {
        axis = p.velocity / speed;
        halfLength = p.stretch;
      }
    }
    out.push_back({Vec4(rel, size), Vec4(p.color * fade, 0.0f), Vec4(axis, halfLength),
                   Vec4(float(int(p.frame)), p.spin, 0, 0)});
  }
}

SpeedDust::SpeedDust(std::uint32_t seed) {
  std::uint32_t rng = seed;
  auto next = [&] {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return float(rng & 0xFFFFFFu) / float(0xFFFFFFu);
  };
  positions_.reserve(kCount);
  for (int i = 0; i < kCount; ++i) {
    positions_.push_back({(next() * 2 - 1) * kHalfBox, (next() * 2 - 1) * kHalfBox, (next() * 2 - 1) * kHalfBox});
    const float w = next();
    widths_.push_back(0.08f + 0.6f * w * w * w);  // mostly fine, a few fat flecks
    brightness_.push_back(0.25f + 0.75f * next());
  }
}

void SpeedDust::shift(const Vec3& delta) {
  for (Vec3& p : positions_) p += delta;
}

void SpeedDust::wrap(const Vec3& camera) {
  const float size = kHalfBox * 2.0f;
  for (Vec3& p : positions_)
    for (int axis = 0; axis < 3; ++axis) {
      const float rel = p[axis] - camera[axis];
      if (std::fabs(rel) > kHalfBox) p[axis] -= std::round(rel / size) * size;
    }
}

float SpeedDust::visibility(float speed) {
  const auto smooth = [](float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  };
  return smooth(4.0f, 70.0f, speed) * (1.0f - smooth(900.0f, 2200.0f, speed));
}

void SpeedDust::buildSprites(const Vec3& camera, const Vec3& velocity, std::vector<ParticleSprite>& out) const {
  const float speed = length(velocity);
  const float vis = visibility(speed);
  if (vis <= 0.001f) return;
  const Vec3 axis = velocity / speed;
  const float halfLength = std::clamp(speed * 0.014f, 0.05f, 16.0f);
  const Vec3 tint{0.62f, 0.72f, 0.92f};
  for (size_t i = 0; i < positions_.size(); ++i) {
    const Vec3 rel = positions_[i] - camera;
    const float d = length(rel);
    const float fade = std::clamp((d - 2.0f) / 10.0f, 0.0f, 1.0f) * std::clamp((kHalfBox - d) / (kHalfBox * 0.4f), 0.0f, 1.0f);
    if (fade <= 0.002f) continue;
    out.push_back({Vec4(rel, widths_[i]), Vec4(tint * (1.5f * vis * fade * brightness_[i]), 0.0f), Vec4(axis, halfLength),
                   Vec4(float(int(ParticleFrame::Streak)), 0, 0, 0)});
  }
}

}  // namespace fh
