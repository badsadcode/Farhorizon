#include "game/ship_controller.h"

#include <algorithm>
#include <cmath>

#include "engine/platform/input.h"

namespace fh {

namespace {
float axis(const InputState& in, const char* neg, const char* pos) {
  return (in.down(pos) ? 1.0f : 0.0f) - (in.down(neg) ? 1.0f : 0.0f);
}
float applyDeadzone(float v, float dz) {
  const float a = std::fabs(v);
  if (a <= dz) return 0.0f;
  return std::copysign((a - dz) / (1.0f - dz), v);
}
}  // namespace

void ShipController::update(const InputState& in, float dt, bool mouseSteering, float /*boostEnergy*/,
                            FlightInput& out) {
  if (in.pressed("ship.toggleAssist")) assist_ = !assist_;
  if (in.pressed("ship.togglePrecision")) mode_ = mode_ == SpeedMode::Cruise ? SpeedMode::Precision : SpeedMode::Cruise;
  if (in.pressed("ship.recenterStick")) recenter();

  const MouseSettings& ms = in.mouse();
  if (mouseSteering) {
    stick_.x += in.mouseDx() * ms.sensitivity;
    stick_.y += in.mouseDy() * ms.sensitivity * (ms.invertY ? -1.0f : 1.0f);
    const float len = std::sqrt(stick_.x * stick_.x + stick_.y * stick_.y);
    if (len > 1.0f) stick_ = {stick_.x / len, stick_.y / len};
  }
  if (ms.recenterPerSecond > 0.0f) {
    const float k = std::exp(-ms.recenterPerSecond * dt);
    stick_ = {stick_.x * k, stick_.y * k};
  }

  // Throttle is a persistent set-speed (assist ON); W/S move it, X zeroes it.
  const float throttleRate = 0.6f;  // full range in ~1.7 s
  throttle_ += axis(in, "ship.throttleDown", "ship.throttleUp") * throttleRate * dt;
  if (in.pressed("ship.throttleZero")) throttle_ = 0.0f;
  throttle_ = std::clamp(throttle_, -0.3f, 1.0f);

  out = FlightInput{};
  out.assist = assist_;
  out.mode = mode_;
  out.throttle = throttle_;
  out.boost = in.down("ship.boost");
  out.thrust.x = axis(in, "ship.strafeLeft", "ship.strafeRight");
  out.thrust.y = axis(in, "ship.strafeDown", "ship.strafeUp");
  // Assist OFF: W/S fire the main engine / retro thrusters directly instead of moving the set-speed.
  out.thrust.z = axis(in, "ship.throttleDown", "ship.throttleUp");
  const float stickYaw = applyDeadzone(stick_.x, ms.deadzone);
  const float stickPitch = applyDeadzone(-stick_.y, ms.deadzone);  // mouse up = nose up
  out.rotation.x = std::clamp(stickPitch + axis(in, "ship.pitchDown", "ship.pitchUp"), -1.0f, 1.0f);
  out.rotation.y = std::clamp(stickYaw + axis(in, "ship.yawLeft", "ship.yawRight"), -1.0f, 1.0f);
  out.rotation.z = axis(in, "ship.rollLeft", "ship.rollRight");

  if (autofly_) {
    out.throttle = autoThrottle_;
    out.rotation = autoRotation_;
    out.thrust = {};
    out.boost = false;
    out.assist = true;
  }
}

}  // namespace fh
