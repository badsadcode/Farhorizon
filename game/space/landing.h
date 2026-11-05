#pragma once
// Planetary landing (design doc 5.3): orbit -> guided descent corridor (re-entry heat) ->
// fade through the cloud deck -> surface scene approach -> touchdown on the pad -> Landed.
// Launch reverses it: surface ascent -> fade -> back in space at the site's orbit marker.
// The scene swap is hidden by a short fade; we never pretend this is seamless travel.
// While on the surface the space ship is parked (kinematic, hidden) at the orbit marker, which
// is also the guaranteed return destination. GPU-free and unit-tested.
#include <cmath>
#include <optional>
#include <string>

#include "game/world/space_world.h"

namespace fh {

enum class LandState { None, Descent, SurfaceApproach, Landed, SurfaceAscent, Ascent };
const char* landStateName(LandState s);

struct LandingSiteRef {
  std::uint32_t planet = 0;
  std::uint32_t site = 0;
  bool operator==(const LandingSiteRef&) const = default;
};

class Landing {
 public:
  static constexpr double kDescentSeconds = 9.0;
  static constexpr double kApproachSeconds = 9.0;
  static constexpr double kSurfaceAscentSeconds = 7.0;
  static constexpr double kAscentSeconds = 3.0;
  static constexpr double kFadeSeconds = 1.2;
  static constexpr double kRequestRange = 600000.0;  // metres from the site's orbit marker

  LandState state() const { return state_; }
  bool onSurface() const {
    return state_ == LandState::SurfaceApproach || state_ == LandState::Landed || state_ == LandState::SurfaceAscent;
  }
  bool busy() const { return state_ != LandState::None; }
  const std::optional<LandingSiteRef>& site() const { return site_; }
  double stateTime() const { return stateTime_; }
  const std::string& message() const { return message_; }
  double messageTime() const { return messageTime_; }

  // Nearest landing site in range (or the targeted one). Returns false with a message otherwise.
  bool request(SpaceWorld& world);
  bool launch(SpaceWorld& world);
  // Immediately landed at a site (save restore, debug).
  bool landAt(SpaceWorld& world, LandingSiteRef ref);

  void preStep(SpaceWorld& world, float dt);
  // A network client: the host's landing is the real one, and this one only shows what it is doing.
  void netFollow(LandState state, LandingSiteRef site, double stateTime) {
    if (state != state_ || std::abs(stateTime - stateTime_) > 0.25) stateTime_ = stateTime;
    state_ = state;
    if (state == LandState::None) site_.reset();
    else site_ = site;
  }
  void netTick(double dt) { stateTime_ += dt; }

  // Ship pose in the surface scene, relative to the pad pose (feet of the parked ship).
  Transform surfaceOffset() const;
  // 0..1 screen fade (cloud deck) and re-entry heat for post-processing.
  float fade() const;
  float heat() const;

 private:
  void say(const std::string& m, const SpaceWorld& w);
  void park(SpaceWorld& world);
  std::optional<LandingSiteRef> nearestSite(const SpaceWorld& world) const;

  LandState state_ = LandState::None;
  std::optional<LandingSiteRef> site_;
  double stateTime_ = 0;
  DVec3 descentStart_{};
  Quat descentStartRot_{};
  std::string message_;
  double messageTime_ = -100;
};

}  // namespace fh
