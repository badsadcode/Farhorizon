#include "game/camera.h"

#include <algorithm>
#include <cmath>

#include "engine/platform/input.h"

namespace fh {

void DevCamera::setOrbit(const Vec3& target, float distance, float yawDeg, float pitchDeg) {
  target_ = target;
  distance_ = distance;
  yaw_ = radians(yawDeg);
  pitch_ = radians(-pitchDeg);  // pitchDeg is elevation above the target
}

void DevCamera::toggleMode() {
  if (mode_ == Mode::Orbit) {
    freePos_ = position();
    mode_ = Mode::Free;
  } else {
    // Re-centre the orbit at the current distance from the target.
    distance_ = std::max(2.0f, length(freePos_ - target_));
    const Vec3 dir = normalize(target_ - freePos_);
    pitch_ = std::asin(clamp(dir.y, -1.0f, 1.0f));
    yaw_ = std::atan2(dir.x, -dir.z);
    mode_ = Mode::Orbit;
  }
}

Vec3 DevCamera::forward() const {
  return {std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), -std::cos(pitch_) * std::cos(yaw_)};
}

Vec3 DevCamera::position() const { return mode_ == Mode::Orbit ? target_ - forward() * distance_ : freePos_; }

Mat4 DevCamera::viewRotation() const { return Mat4::lookAt({0, 0, 0}, forward(), {0, 1, 0}); }

void DevCamera::handleInput(const InputState& in, float dt) {
  const float lookSpeed = 0.0035f;
  const bool looking = mode_ == Mode::Orbit ? (in.mouseLeft() || in.mouseRight()) : in.mouseRight();
  if (looking) {
    yaw_ += in.mouseDx() * lookSpeed;
    // Orbit: dragging up moves the camera up (looks more downward); free: mouse-look.
    pitch_ += (mode_ == Mode::Orbit ? in.mouseDy() : -in.mouseDy()) * lookSpeed;
    pitch_ = clamp(pitch_, radians(-89.0f), radians(89.0f));
  }
  if (mode_ == Mode::Orbit) {
    distance_ = clamp(distance_ * std::pow(0.88f, in.wheel()), 3.0f, 5000.0f);
    return;
  }
  freeSpeed_ = clamp(freeSpeed_ * std::pow(1.25f, in.wheel()), 1.0f, 200000.0f);
  const Vec3 f = forward();
  const Vec3 r = normalize(cross(f, Vec3{0, 1, 0}));
  const Vec3 u{0, 1, 0};
  Vec3 move{};
  if (in.down("camera.forward")) move += f;
  if (in.down("camera.back")) move -= f;
  if (in.down("camera.right")) move += r;
  if (in.down("camera.left")) move -= r;
  if (in.down("camera.up")) move += u;
  if (in.down("camera.down")) move -= u;
  const float speed = freeSpeed_ * (in.down("camera.boost") ? 10.0f : 1.0f);
  freePos_ += normalize(move) * (speed * dt);
}

void DevCamera::fixedUpdate(float step) {
  if (mode_ == Mode::Orbit && autoRotate_) yaw_ += radians(autoRotateDegPerSec_) * step;
}

}  // namespace fh
