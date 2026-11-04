#pragma once
// Player cameras: chase (default) and cockpit, plus developer orbit/free cameras.
// Positions are local-frame floats; the rig never stores absolute system positions.
#include <string>

#include "engine/math/math.h"
#include "game/world/space_world.h"

namespace fh {

class InputState;

enum class CameraMode { Chase, Cockpit, Orbit, Free };
const char* cameraModeName(CameraMode m);
CameraMode cameraModeFromString(const std::string& s);

struct CameraView {
  Vec3 position{};
  Quat rotation{};
  float fovDeg = 65.0f;
  Mat4 viewRotation() const { return Mat4::rotation(rotation.conjugate()); }
};

class CameraRig {
 public:
  void configureDev(float fovDeg, float orbitDistance, float yawDeg, float pitchDeg, float autoRotateDegPerSec);
  void setMode(CameraMode m, const Transform& ship);
  CameraMode mode() const { return mode_; }
  bool isDev() const { return mode_ == CameraMode::Orbit || mode_ == CameraMode::Free; }
  void cycleShipCamera(const Transform& ship);  // chase <-> cockpit
  void toggleDev(const Transform& ship);        // ship camera -> orbit -> free -> ship camera
  void toggleAutoRotate() { autoRotate_ = !autoRotate_; }

  void fixedUpdate(float step);
  // Per-frame update. Developer cameras read mouse/keys only when `devInput` is true.
  CameraView update(const Transform& ship, const PlayerShip& player, const InputState& in, float dt, bool devInput);
  void applyRebase(const Vec3& delta) { freePos_ += delta; }
  float freeSpeed() const { return freeSpeed_; }
  void snap() { snapNext_ = true; }  // skip smoothing next frame (scene change, teleport)

 private:
  static Quat yawPitch(float yaw, float pitch);
  CameraMode mode_ = CameraMode::Chase;
  CameraMode lastShipMode_ = CameraMode::Chase;
  Quat chaseRot_{};
  bool snapNext_ = true;
  float devFov_ = 62.0f;
  float orbitDistance_ = 25.0f, orbitYaw_ = 0.0f, orbitPitch_ = -0.25f;
  float autoRotateRate_ = 0.1f;
  bool autoRotate_ = true;
  Vec3 freePos_{};
  float freeYaw_ = 0.0f, freePitch_ = 0.0f;
  float freeSpeed_ = 40.0f;
};

}  // namespace fh
