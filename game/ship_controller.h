#pragma once
// Maps actions + mouse to FlightInput. Mouse steering uses a virtual stick: mouse motion moves
// a point inside a unit circle and its offset is the pitch/yaw command (design doc 4.1:
// accessible mouse-and-keyboard). Thread affinity: main thread.
#include "engine/math/math.h"
#include "game/ships/flight_model.h"

namespace fh {

class InputState;

class ShipController {
 public:
  // Builds the flight input for this frame. `mouseSteering` is true while the mouse is captured.
  void update(const InputState& in, float dt, bool mouseSteering, float boostEnergy, FlightInput& out);
  void recenter() { stick_ = {}; }
  void setThrottle(float t) { throttle_ = t; }
  Vec2 stick() const { return stick_; }
  float throttle() const { return throttle_; }
  bool assist() const { return assist_; }
  SpeedMode mode() const { return mode_; }

  // Scripted input for benchmarks and smoke tests (overrides the player).
  void setAutofly(bool on, float throttle = 0.0f, Vec3 rotation = {}) {
    autofly_ = on;
    autoThrottle_ = throttle;
    autoRotation_ = rotation;
  }
  bool autofly() const { return autofly_; }

 private:
  Vec2 stick_{};
  float throttle_ = 0.0f;
  bool assist_ = true;
  SpeedMode mode_ = SpeedMode::Cruise;
  bool autofly_ = false;
  float autoThrottle_ = 0.0f;
  Vec3 autoRotation_{};
};

}  // namespace fh
