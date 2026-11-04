#include "game/space/docking.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "engine/core/log.h"

namespace fh {

namespace {
Quat eulerDeg(const Vec3& d) {
  return normalize(Quat::fromAxisAngle({0, 0, 1}, radians(d.z)) * Quat::fromAxisAngle({0, 1, 0}, radians(d.y)) *
                   Quat::fromAxisAngle({1, 0, 0}, radians(d.x)));
}
float angleDeg(const Vec3& a, const Vec3& b) {
  return std::acos(std::clamp(dot(normalize(a), normalize(b)), -1.0f, 1.0f)) * 180.0f / kPi;
}
Vec3 approachVec(const Vec3& cur, const Vec3& target, float maxDelta) {
  const Vec3 d = target - cur;
  const float l = length(d);
  return l <= maxDelta ? target : cur + d * (maxDelta / l);
}
// Rotates `from` toward `to` by at most maxAngle radians.
Quat rotateToward(const Quat& from, const Quat& to, float maxAngle) {
  Quat t = to;
  if (dot(from, t) < 0) t = {-t.x, -t.y, -t.z, -t.w};
  const float angle = 2.0f * std::acos(std::clamp(std::fabs(dot(from, t)), -1.0f, 1.0f));
  if (angle <= maxAngle || angle < 1e-5f) return t;
  return nlerp(from, t, maxAngle / angle);
}
}  // namespace

const char* dockStateName(DockState s) {
  switch (s) {
    case DockState::Undocked: return "UNDOCKED";
    case DockState::RequestingClearance: return "REQUESTING CLEARANCE";
    case DockState::ApproachAuthorized: return "CLEARED TO APPROACH";
    case DockState::Capture: return "CAPTURE";
    case DockState::Docked: return "DOCKED";
    case DockState::Undocking: return "UNDOCKING";
  }
  return "?";
}

const DockingDef* Docking::def(const SpaceWorld& world) const {
  if (!prop_ || *prop_ >= world.props().size()) return nullptr;
  const auto& d = world.props()[*prop_].def.docking;
  return d ? &*d : nullptr;
}

void Docking::say(const std::string& text, const SpaceWorld& world) {
  message_ = text;
  messageTime_ = world.simulationTime();
  log::info("docking", "{}", text);
}

void Docking::setState(DockState s, SpaceWorld& world) {
  state_ = s;
  stateTime_ = 0;
  PlayerShip& p = world.player();
  const bool clamped = s == DockState::Capture || s == DockState::Docked || s == DockState::Undocking;
  p.kinematic = clamped || (s == DockState::ApproachAuthorized && autopilot_);
  p.collisionsEnabled = !clamped;
  if (s == DockState::Undocked || s == DockState::RequestingClearance) autopilot_ = false;
}

Transform Docking::bayTransform(const SpaceWorld& world) const {
  const DockingDef* d = def(world);
  if (!d || bay_ >= d->bays.size()) return {};
  const Prop& p = world.props()[*prop_];
  const DockingBayDef& b = d->bays[bay_];
  return {p.local + p.rotation.rotate(b.position), normalize(p.rotation * eulerDeg(b.rotationDeg))};
}

Vec3 Docking::approachPoint(const SpaceWorld& world) const {
  const DockingDef* d = def(world);
  const Transform bay = bayTransform(world);
  return bay.position + bay.rotation.up() * (d ? d->approachHeight : 80.0f);
}

bool Docking::request(SpaceWorld& world) {
  if (state_ != DockState::Undocked) {
    say("Request ignored: already " + std::string(dockStateName(state_)), world);
    return false;
  }
  // Nearest dockable prop within its clearance range.
  std::optional<std::uint32_t> best;
  float bestDist = 1e30f;
  for (std::uint32_t i = 0; i < world.props().size(); ++i) {
    const Prop& p = world.props()[i];
    if (!p.def.docking) continue;
    const float d = length(p.local - world.player().state.position);
    if (d <= p.def.docking->clearanceRange && d < bestDist) {
      bestDist = d;
      best = i;
    }
  }
  if (!best) {
    say("No station within docking range", world);
    return false;
  }
  prop_ = best;
  bay_ = 0;
  setState(DockState::RequestingClearance, world);
  say(std::format("{} Control, requesting docking clearance.", world.props()[*best].def.name), world);
  return true;
}

bool Docking::cancel() {
  if (state_ != DockState::RequestingClearance && state_ != DockState::ApproachAuthorized) return false;
  state_ = DockState::Undocked;
  stateTime_ = 0;
  autopilot_ = false;
  message_ = "Docking request cancelled.";
  return true;
}

bool Docking::dockAt(SpaceWorld& world, std::uint32_t prop, std::uint32_t bay, bool announce) {
  if (prop >= world.props().size() || !world.props()[prop].def.docking) return false;
  if (bay >= world.props()[prop].def.docking->bays.size()) return false;
  prop_ = prop;
  bay_ = bay;
  autopilot_ = false;
  setState(DockState::Docked, world);
  const Transform t = bayTransform(world);
  world.player().state.position = t.position;
  world.player().state.orientation = t.rotation;
  world.player().state.velocity = {};
  world.player().previous = world.player().state;
  if (announce) say("[debug] Force-docked at " + world.props()[prop].def.name, world);
  return true;
}

bool Docking::forceDock(SpaceWorld& world) {
  std::optional<std::uint32_t> best;
  float bestDist = 1e30f;
  for (std::uint32_t i = 0; i < world.props().size(); ++i) {
    const Prop& p = world.props()[i];
    const float d = length(p.local - world.player().state.position);
    if (p.def.docking && d < bestDist) {
      bestDist = d;
      best = i;
    }
  }
  return best && dockAt(world, *best, 0, true);
}

bool Docking::toggleAutopilot() {
  if (state_ != DockState::ApproachAuthorized) return false;
  autopilot_ = !autopilot_;
  return true;
}

bool Docking::undock(SpaceWorld& world) {
  if (state_ != DockState::Docked) {
    say("Cannot undock: not docked", world);
    return false;
  }
  setState(DockState::Undocking, world);
  say("Clamps released. Clear the pad, pilot.", world);
  return true;
}

void Docking::preStep(SpaceWorld& world, float dt) {
  stateTime_ += dt;
  PlayerShip& p = world.player();
  const DockingDef* d = def(world);
  // Keep the kinematic flag in sync (autopilot can be toggled at any time).
  const bool clamped = state_ == DockState::Capture || state_ == DockState::Docked || state_ == DockState::Undocking;
  p.kinematic = clamped || (state_ == DockState::ApproachAuthorized && autopilot_);
  p.collisionsEnabled = !clamped;

  if (state_ == DockState::ApproachAuthorized && autopilot_ && d) {
    // Kinematic guidance: line up in the approach column above the bay, then descend.
    const Transform bay = bayTransform(world);
    const Vec3 column = approachPoint(world);
    const Vec3 up = bay.rotation.up();
    const Vec3 rel = p.state.position - bay.position;
    const float height = dot(rel, up);
    const Vec3 lateral = rel - up * height;
    const bool inColumn = length(lateral) < 6.0f && height > 0;
    // Climb to approach height first, traverse above the station, then descend the column.
    Vec3 target = column;
    if (inColumn) target = bay.position;
    else if (height < d->approachHeight * 0.9f) target = p.state.position + up * (d->approachHeight - height);
    const Vec3 toTarget = target - p.state.position;
    const float dist = length(toTarget);
    const float accel = p.def.flight.lateralAcceleration;
    // Speed profile: brake in time, slow to well under the capture limit at the bay.
    float speed = std::min({p.def.flight.maxCruiseSpeed * 0.6f, std::sqrt(2.0f * accel * 0.6f * dist)});
    if (inColumn) speed = std::min(speed, std::max(d->captureSpeed * 0.5f, height * 0.25f));
    const Vec3 desired = dist > 0.01f ? toTarget / dist * speed : Vec3{};
    p.state.velocity = approachVec(p.state.velocity, desired, accel * dt);
    // Align with the bay orientation while approaching.
    p.state.orientation = rotateToward(p.state.orientation, bay.rotation, p.def.flight.maxAngularRate.y * 0.8f * dt);
    p.state.angularVelocity = {};
  } else if (state_ == DockState::Undocking && d) {
    const Transform bay = bayTransform(world);
    p.state.velocity = bay.rotation.up() * kUndockSpeed;
    p.state.angularVelocity = {};
  } else if (state_ == DockState::Capture || state_ == DockState::Docked) {
    p.state.velocity = {};
    p.state.angularVelocity = {};
  }
}

void Docking::postStep(SpaceWorld& world, float /*dt*/) {
  PlayerShip& p = world.player();
  const DockingDef* d = def(world);
  info_ = {};
  if (!d && state_ != DockState::Undocked) {
    state_ = DockState::Undocked;  // station vanished (e.g. content reload)
    p.kinematic = false;
    p.collisionsEnabled = true;
    return;
  }
  switch (state_) {
    case DockState::Undocked: break;
    case DockState::RequestingClearance: {
      if (length(world.props()[*prop_].local - p.state.position) > d->clearanceRange * 1.2f) {
        say("Clearance request dropped: out of range.", world);
        setState(DockState::Undocked, world);
      } else if (stateTime_ >= kClearanceDelay) {
        setState(DockState::ApproachAuthorized, world);
        say(std::format("{} Control: cleared to {}. Watch your speed on final.", world.props()[*prop_].def.name,
                        d->bays[bay_].name),
            world);
      }
      break;
    }
    case DockState::ApproachAuthorized: {
      const Transform bay = bayTransform(world);
      info_.distance = length(p.state.position - bay.position);
      info_.speed = length(p.state.velocity);
      info_.angleError = std::max(angleDeg(p.state.orientation.forward(), bay.rotation.forward()),
                                  angleDeg(p.state.orientation.up(), bay.rotation.up()));
      // Tolerances: capture radius, speed, and alignment (up axis gets 1.5x leeway).
      info_.distanceOk = info_.distance <= d->captureDistance;
      info_.speedOk = info_.speed <= d->captureSpeed;
      info_.angleOk = angleDeg(p.state.orientation.forward(), bay.rotation.forward()) <= d->captureAngleDeg &&
                      angleDeg(p.state.orientation.up(), bay.rotation.up()) <= d->captureAngleDeg * 1.5f;
      if (info_.distanceOk && info_.speedOk && info_.angleOk) {
        captureStart_ = {p.state.position, p.state.orientation};
        setState(DockState::Capture, world);
        say("Contact. Docking clamps engaging.", world);
      } else if (length(world.props()[*prop_].local - p.state.position) > d->clearanceRange * 1.2f) {
        say("Docking clearance expired: you left the control zone.", world);
        setState(DockState::Undocked, world);
      }
      break;
    }
    case DockState::Capture: {
      const Transform bay = bayTransform(world);
      float t = float(std::min(1.0, stateTime_ / kCaptureDuration));
      t = t * t * (3.0f - 2.0f * t);  // smoothstep settle
      p.state.position = lerp(captureStart_.position, bay.position, t);
      p.state.orientation = nlerp(captureStart_.rotation, bay.rotation, t);
      if (stateTime_ >= kCaptureDuration) {
        setState(DockState::Docked, world);
        say(std::format("Welcome to {}. You are docked at {}.", world.props()[*prop_].def.name, d->bays[bay_].name),
            world);
      }
      break;
    }
    case DockState::Docked: {
      const Transform bay = bayTransform(world);  // follows a spinning station
      p.state.position = bay.position;
      p.state.orientation = bay.rotation;
      p.input = FlightInput{};
      break;
    }
    case DockState::Undocking: {
      if (stateTime_ >= kUndockDuration) {
        setState(DockState::Undocked, world);
        say("You are clear. Fly safe.", world);
      }
      break;
    }
  }
}

}  // namespace fh
