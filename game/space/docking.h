#pragma once
// Docking state machine (design doc 5.2):
//   Undocked -> RequestingClearance -> ApproachAuthorized -> Capture -> Docked -> Undocking -> Undocked
// Capture is deterministic: when position, speed and alignment are inside tolerances the ship is
// clamped and settled onto the pad by script, never by chaotic contacts. An optional autopilot
// flies the approach. Invalid approaches produce warnings, never a game over.
// GPU-free and unit-tested; the app only maps keys to request()/toggleAutopilot()/undock().
#include <cstdint>
#include <optional>
#include <string>

#include "game/world/space_world.h"

namespace fh {

enum class DockState { Undocked, RequestingClearance, ApproachAuthorized, Capture, Docked, Undocking };
const char* dockStateName(DockState s);

struct DockApproachInfo {
  float distance = 0;      // ship centre to bay point, metres
  float speed = 0;         // m/s
  float angleError = 0;    // degrees (worst of nose and up alignment)
  bool distanceOk = false, speedOk = false, angleOk = false;
};

class Docking {
 public:
  // Timings (seconds); fixed for now, tunable later from data if needed.
  static constexpr double kClearanceDelay = 1.5;
  static constexpr double kCaptureDuration = 2.5;
  static constexpr double kUndockDuration = 4.0;
  static constexpr float kUndockSpeed = 6.0f;

  DockState state() const { return state_; }
  double stateTime() const { return stateTime_; }
  const std::string& message() const { return message_; }  // last radio/status message
  double messageTime() const { return messageTime_; }
  bool autopilot() const { return autopilot_; }
  std::optional<std::uint32_t> station() const { return prop_; }
  std::uint32_t bay() const { return bay_; }
  const DockApproachInfo& approach() const { return info_; }

  // Player actions. Each returns false (with a message) when not allowed in the current state.
  bool request(SpaceWorld& world);   // ask the nearest station in range for clearance
  bool cancel();                     // abort an approach before capture
  bool undock(SpaceWorld& world);    // leave the pad (only when docked)
  bool toggleAutopilot();            // only while cleared to approach
  // Debug tool (design doc 17 "force dock/land"): clamp to the nearest station's first bay now.
  bool forceDock(SpaceWorld& world);
  // Clamp to a specific station bay immediately (save-game restore).
  bool dockAt(SpaceWorld& world, std::uint32_t prop, std::uint32_t bay, bool announce = false);

  // Bay pose in the world's local frame (valid when a station is assigned).
  Transform bayTransform(const SpaceWorld& world) const;
  // Where the autopilot lines up before descending (above the bay).
  Vec3 approachPoint(const SpaceWorld& world) const;

  // Call once per fixed step *before* SpaceWorld::fixedUpdate (sets kinematic state / velocity)
  // and once *after* it (capture checks, clamping).
  void preStep(SpaceWorld& world, float dt);
  void postStep(SpaceWorld& world, float dt);

 private:
  void setState(DockState s, SpaceWorld& world);
  void say(const std::string& text, const SpaceWorld& world);
  const DockingDef* def(const SpaceWorld& world) const;

  DockState state_ = DockState::Undocked;
  double stateTime_ = 0;
  std::optional<std::uint32_t> prop_;
  std::uint32_t bay_ = 0;
  bool autopilot_ = false;
  std::string message_;
  double messageTime_ = -100;
  DockApproachInfo info_;
  Transform captureStart_;
};

}  // namespace fh
