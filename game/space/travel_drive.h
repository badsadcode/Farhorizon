#pragma once
// In-system travel drive (design doc 4.2): a controlled state machine
//   Idle -> Aligning -> Spooling -> Transit -> Decelerating -> Idle (normal flight)
// Kinematic and deterministic: during the jump the drive owns the ship's velocity and the speed
// profile is computed from the remaining distance (in double precision), so arrival is exact.
// Refused near obstacles (mass lock), for close destinations, and while docked/landing.
#include <optional>
#include <string>

#include "game/space/travel_params.h"
#include "game/world/space_world.h"

namespace fh {

enum class TravelState { Idle, Aligning, Spooling, Transit, Decelerating };
const char* travelStateName(TravelState s);

class TravelDrive {
 public:
  TravelState state() const { return state_; }
  bool active() const { return state_ != TravelState::Idle; }
  const std::string& message() const { return message_; }
  double messageTime() const { return messageTime_; }
  float speed() const { return speed_; }
  double remaining() const { return remaining_; }
  float spoolProgress() const;
  double etaSeconds() const;
  const std::optional<TargetRef>& destination() const { return dest_; }

  // Engage toward a nav destination. Refusals explain why.
  bool engage(SpaceWorld& world, const TargetRef& dest, const TravelDriveParams& p);
  void cancel(SpaceWorld& world);  // safe abort at any time (drops to normal flight)

  // Call before SpaceWorld::fixedUpdate; owns the ship's kinematics while active.
  void preStep(SpaceWorld& world, const TravelDriveParams& p, float dt);

  // A network client: the host's drive is the real one, and this one only shows what it is doing.
  void netFollow(TravelState state, float speed, double remaining, double stateTime) {
    state_ = state;
    speed_ = speed;
    remaining_ = remaining;
    stateTime_ = stateTime;
  }
  double stateTime() const { return stateTime_; }

  // Name of whatever mass-locks the ship right now (empty = free to jump).
  static std::string massLock(const SpaceWorld& world, const TravelDriveParams& p);

 private:
  void say(const std::string& m, const SpaceWorld& w);
  TravelState state_ = TravelState::Idle;
  std::optional<TargetRef> dest_;
  DVec3 arrival_{};      // system frame
  double stateTime_ = 0;
  float speed_ = 0;
  double remaining_ = 0;
  float spool_ = 4.0f;
  std::string message_;
  double messageTime_ = -100;
};

}  // namespace fh
