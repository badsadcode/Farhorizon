#include "game/space/mining.h"

#include <algorithm>
#include <cmath>

namespace fh {

namespace {
// Small deterministic jitter so chunks do not stack on one line.
Vec3 jitter(std::uint32_t n) {
  auto h = [](std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return float(x & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
  };
  return {h(n * 3 + 1), h(n * 3 + 2), h(n * 3 + 3)};
}
}  // namespace

void Mining::startScan(std::uint32_t asteroid, double now) {
  if (scanned_.count(asteroid)) return;
  scanTarget_ = asteroid;
  scanStart_ = now;
}

float Mining::scanProgress(double now) const {
  if (scanTarget_ == kNone) return 0;
  return std::clamp(float((now - scanStart_) / params_.scanSeconds), 0.0f, 1.0f);
}

void Mining::addChunk(const OreChunk& c) {
  if (int(chunks_.size()) >= params_.maxChunks) chunks_.erase(chunks_.begin());
  OreChunk k = c;
  k.asteroid = kNone;
  chunks_.push_back(k);
}

void Mining::applyRebase(const Vec3& delta) {
  for (auto& c : chunks_) c.local += delta;
  beamEnd_ += delta;
}

MiningEvents Mining::fixedUpdate(SpaceWorld& world, bool firing, CargoHold& cargo, float dt) {
  MiningEvents ev;
  const double now = world.simulationTime();
  const PlayerShip& ship = world.player();
  const Vec3 shipPos = ship.state.position;
  beamActive_ = false;
  status_.clear();

  // Scan completes over time while the asteroid stays targeted.
  if (scanTarget_ != kNone) {
    const auto& t = world.target();
    if (!t || t->kind != TargetRef::Kind::Asteroid || t->index != scanTarget_) scanTarget_ = kNone;
    else if (scanProgress(now) >= 1.0f) {
      scanned_.insert(scanTarget_);
      scanTarget_ = kNone;
    }
  }

  if (firing) {
    const auto& t = world.target();
    if (!t || t->kind != TargetRef::Kind::Asteroid) {
      status_ = "No asteroid targeted";
    } else {
      Asteroid& a = world.asteroid(t->index);
      const Vec3 toCentre = a.local - shipPos;
      const float dist = length(toCentre);
      const float surface = dist - a.radius;
      const Vec3 dir = dist > 1e-3f ? toCentre / dist : Vec3{0, 0, -1};
      const float angle = std::acos(std::clamp(dot(ship.state.orientation.forward(), dir), -1.0f, 1.0f));
      const float allowed = radians(params_.coneDeg) + std::atan2(a.radius, std::max(dist, 1.0f));
      if (a.resource.empty()) {
        status_ = "Nothing worth mining here";
      } else if (a.yield <= 0) {
        status_ = "Asteroid depleted";
      } else if (surface > params_.range) {
        status_ = "Out of laser range";
      } else if (angle > allowed) {
        status_ = "Aim at the target";
      } else {
        beamActive_ = true;
        beamEnd_ = a.local - dir * a.radius * 0.85f;  // impact on the facing surface
        if (progressAsteroid_ != t->index) {
          progressAsteroid_ = t->index;
          progress_ = 0;
        }
        progress_ += params_.unitsPerSecond / std::max(0.1f, a.hardness) * dt;
        while (progress_ >= 1.0f && a.yield > 0) {
          progress_ -= 1.0f;
          if (int(chunks_.size()) >= params_.maxChunks) chunks_.erase(chunks_.begin());  // hard cap
          OreChunk c;
          const Vec3 j = jitter(++spawnCounter_);
          c.local = beamEnd_ + j * 1.5f - dir * 1.0f;
          c.velocity = -dir * params_.chunkSpeed + j * 1.2f;
          c.commodity = a.resource;
          c.spawnTime = now;
          c.asteroid = t->index;
          chunks_.push_back(c);
          --a.yield;
          ++ev.spawned;
        }
      }
    }
  }

  // Chunks drift; gently attracted by the scoop field once nearby; scooped or despawned.
  for (size_t i = 0; i < chunks_.size();) {
    OreChunk& c = chunks_[i];
    const Vec3 toShip = shipPos - c.local;
    const float d = length(toShip);
    if (d < params_.scoopRadius * 4.0f && d > 1e-3f) c.velocity += toShip / d * (8.0f * dt);  // scoop field
    c.local += c.velocity * dt;
    bool remove = now - c.spawnTime > params_.chunkLifetime;
    if (!remove && length(shipPos - c.local) <= params_.scoopRadius) {
      if (cargo.add(c.commodity, 1)) {
        ++ev.collected;
        remove = true;
      } else {
        ev.cargoFull = true;
        c.velocity = c.velocity * 0.5f;
      }
    }
    if (remove) chunks_.erase(chunks_.begin() + std::ptrdiff_t(i));
    else ++i;
  }
  return ev;
}

}  // namespace fh
