#pragma once
// Developer camera for Stage 1: orbit around a target, or free-fly. Positions are local-frame
// floats (relative to the floating origin); the app rebases when the camera drifts too far.
#include "engine/math/math.h"

namespace fh {

class InputState;

class DevCamera {
 public:
  enum class Mode { Orbit, Free };

  void setOrbit(const Vec3& target, float distance, float yawDeg, float pitchDeg);
  void toggleMode();
  Mode mode() const { return mode_; }

  // Per-frame input (mouse look, WASD) with real delta time.
  void handleInput(const InputState& input, float dt);
  // Fixed-step behaviour (auto-rotate).
  void fixedUpdate(float step);

  void setOrbitTarget(const Vec3& t) { target_ = t; }
  void setAutoRotate(float degPerSec) { autoRotateDegPerSec_ = degPerSec; }
  void toggleAutoRotate() { autoRotate_ = !autoRotate_; }
  bool autoRotate() const { return autoRotate_; }

  Vec3 position() const;        // local frame
  Vec3 forward() const;
  Mat4 viewRotation() const;    // rotation-only view matrix
  float freeSpeed() const { return freeSpeed_; }

  // Shifts local positions after a floating-origin rebase.
  void applyRebase(const Vec3& delta) {
    freePos_ += delta;
    target_ += delta;
  }

 private:
  Mode mode_ = Mode::Orbit;
  Vec3 target_{0, 0, 0};
  Vec3 freePos_{0, 0, 0};
  float distance_ = 30.0f;
  float yaw_ = 0.0f, pitch_ = 0.0f;  // radians
  float autoRotateDegPerSec_ = 5.0f;
  bool autoRotate_ = true;
  float freeSpeed_ = 40.0f;          // m/s, adjustable with the mouse wheel in free mode
};

}  // namespace fh
