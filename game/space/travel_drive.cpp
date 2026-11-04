#include "game/space/travel_drive.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "engine/core/log.h"

namespace fh {

const char* travelStateName(TravelState s) {
  switch (s) {
    case TravelState::Idle: return "IDLE";
    case TravelState::Aligning: return "ALIGNING";
    case TravelState::Spooling: return "SPOOLING";
    case TravelState::Transit: return "TRANSIT";
    case TravelState::Decelerating: return "DECELERATING";
  }
  return "?";
}

void TravelDrive::say(const std::string& m, const SpaceWorld& w) {
  message_ = m;
  messageTime_ = w.simulationTime();
  log::info("travel", "{}", m);
}

float TravelDrive::spoolProgress() const {
  return state_ == TravelState::Spooling ? std::clamp(float(stateTime_ / spool_), 0.0f, 1.0f) : 0.0f;
}

double TravelDrive::etaSeconds() const {
  if (state_ != TravelState::Transit && state_ != TravelState::Decelerating) return 0;
  return speed_ > 1 ? remaining_ / double(speed_) : 0;
}

std::string TravelDrive::massLock(const SpaceWorld& world, const TravelDriveParams& p) {
  const Vec3 me = world.player().state.position;
  for (const auto& prop : world.props())
    if (prop.def.targetable && length(prop.local - me) < p.massLockRange + prop.collisionRadius) return prop.def.name;
  for (const auto& f : world.fields()) {
    const Vec3 c = world.frame().toLocal(f.def.center);
    if (length(c - me) < f.def.radius + p.massLockRange) return f.def.name;
  }
  for (const auto& pl : world.planets()) {
    const double alt = length(world.frame().toSystem(me).metres - pl.def.center.metres) - pl.def.radius;
    if (alt < pl.def.orbitAltitude * 0.5) return pl.def.name + " gravity well";
  }
  return {};
}

bool TravelDrive::engage(SpaceWorld& world, const TargetRef& dest, const TravelDriveParams& p) {
  if (active()) {
    say("Travel drive already engaged", world);
    return false;
  }
  if (const std::string lock = massLock(world, p); !lock.empty()) {
    say("Mass-locked by " + lock + ". Move clear first.", world);
    return false;
  }
  const DVec3 arrival = world.navArrivalPoint(dest);
  const DVec3 me = world.frame().toSystem(world.player().state.position).metres;
  const double dist = length(arrival - me);
  if (dist < p.minDistance) {
    say("Destination too close for the travel drive", world);
    return false;
  }
  dest_ = dest;
  arrival_ = arrival;
  remaining_ = dist;
  speed_ = length(world.player().state.velocity);
  spool_ = p.spoolSeconds;
  state_ = TravelState::Aligning;
  stateTime_ = 0;
  world.player().kinematic = true;
  say(std::format("Travel drive: plotting course, {:.0f} km", dist / 1000.0), world);
  return true;
}

void TravelDrive::cancel(SpaceWorld& world) {
  if (!active()) return;
  PlayerShip& ship = world.player();
  // Drop to normal flight with a survivable speed along the current heading.
  const Vec3 v = ship.state.velocity;
  const float s = length(v);
  if (s > 300.0f) ship.state.velocity = v / s * 300.0f;
  ship.kinematic = false;
  state_ = TravelState::Idle;
  say("Travel drive disengaged", world);
}

void TravelDrive::preStep(SpaceWorld& world, const TravelDriveParams& p, float dt) {
  if (!active()) return;
  stateTime_ += dt;
  PlayerShip& ship = world.player();
  ship.kinematic = true;
  const DVec3 me = world.frame().toSystem(ship.state.position).metres;
  const DVec3 toArrival = arrival_ - me;
  remaining_ = length(toArrival);
  const Vec3 dir = remaining_ > 1e-3 ? Vec3(toArrival / remaining_) : ship.state.orientation.forward();

  auto rotateToward = [&](float maxAngle) {
    const Vec3 f = ship.state.orientation.forward();
    const float angle = std::acos(std::clamp(dot(f, dir), -1.0f, 1.0f));
    if (angle < 1e-5f) return 0.0f;
    Vec3 axis = cross(f, dir);
    if (length(axis) < 1e-6f) axis = ship.state.orientation.up();
    const float step = std::min(angle, maxAngle);
    ship.state.orientation = normalize(Quat::fromAxisAngle(normalize(axis), step) * ship.state.orientation);
    return angle - step;
  };

  switch (state_) {
    case TravelState::Aligning: {
      // Bleed off normal-flight speed while turning onto the course.
      ship.state.velocity = ship.state.velocity * std::max(0.0f, 1.0f - 2.0f * dt);
      const float left = rotateToward(radians(p.alignRateDegPerSec) * dt);
      if (left <= radians(p.alignToleranceDeg)) {
        state_ = TravelState::Spooling;
        stateTime_ = 0;
        say("Drive spooling...", world);
      }
      break;
    }
    case TravelState::Spooling: {
      rotateToward(radians(p.alignRateDegPerSec) * dt);
      ship.state.velocity = ship.state.velocity * std::max(0.0f, 1.0f - 2.0f * dt);
      if (!massLock(world, p).empty()) {
        cancel(world);
        say("Spool aborted: mass lock", world);
        return;
      }
      if (stateTime_ >= spool_) {
        state_ = TravelState::Transit;
        stateTime_ = 0;
        speed_ = std::max(speed_, 200.0f);
        say("Transit", world);
      }
      break;
    }
    case TravelState::Transit:
    case TravelState::Decelerating: {
      rotateToward(radians(p.alignRateDegPerSec) * dt);
      // Speed profile: accelerate, cruise, then brake so we reach exitSpeed at the arrival point.
      const double brake = std::sqrt(double(p.exitSpeed) * p.exitSpeed + 2.0 * p.acceleration * remaining_);
      const float target = float(std::min<double>(p.maxSpeed, brake));
      speed_ = std::min(speed_ + p.acceleration * dt, target);
      if (state_ == TravelState::Transit && target < p.maxSpeed && speed_ >= target - 1.0f) {
        state_ = TravelState::Decelerating;
        say("Decelerating", world);
      }
      const double stepLen = double(speed_) * dt;
      if (stepLen >= remaining_ || remaining_ < 5.0) {
        // Arrive exactly (computed in double), then hand back to normal flight.
        ship.state.position = world.frame().toLocal(SystemPosition{arrival_});
        ship.previous.position = ship.state.position;
        ship.state.velocity = dir * p.exitSpeed;
        ship.kinematic = false;
        state_ = TravelState::Idle;
        remaining_ = 0;
        speed_ = p.exitSpeed;
        say("Arrived. Normal flight.", world);
        return;
      }
      ship.state.velocity = dir * speed_;
      break;
    }
    case TravelState::Idle: break;
  }
}

}  // namespace fh
