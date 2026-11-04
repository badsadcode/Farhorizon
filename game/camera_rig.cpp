#include "game/camera_rig.h"

#include <algorithm>
#include <cmath>

#include "engine/platform/input.h"

namespace fh {

const char* cameraModeName(CameraMode m) {
  switch (m) {
    case CameraMode::Chase: return "CHASE";
    case CameraMode::Cockpit: return "COCKPIT";
    case CameraMode::Orbit: return "DEV ORBIT";
    case CameraMode::Free: return "DEV FREE";
  }
  return "?";
}

CameraMode cameraModeFromString(const std::string& s) {
  if (s == "cockpit") return CameraMode::Cockpit;
  if (s == "orbit") return CameraMode::Orbit;
  if (s == "free") return CameraMode::Free;
  return CameraMode::Chase;
}

Quat CameraRig::yawPitch(float yaw, float pitch) {
  // yaw > 0 turns right (toward +X), pitch > 0 looks up.
  return normalize(Quat::fromAxisAngle({0, 1, 0}, -yaw) * Quat::fromAxisAngle({1, 0, 0}, pitch));
}

void CameraRig::configureDev(float fovDeg, float orbitDistance, float yawDeg, float pitchDeg,
                             float autoRotateDegPerSec) {
  devFov_ = fovDeg;
  orbitDistance_ = orbitDistance;
  orbitYaw_ = radians(yawDeg);
  orbitPitch_ = radians(-pitchDeg);  // pitchDeg is elevation above the ship
  autoRotateRate_ = radians(autoRotateDegPerSec);
}

void CameraRig::setMode(CameraMode m, const Transform& ship) {
  if (m == CameraMode::Free && mode_ != CameraMode::Free) {
    // Start the free camera where the current orbit camera is.
    const Quat q = yawPitch(orbitYaw_, orbitPitch_);
    freePos_ = ship.position - q.forward() * orbitDistance_;
    freeYaw_ = orbitYaw_;
    freePitch_ = orbitPitch_;
  }
  if (m == CameraMode::Chase || m == CameraMode::Cockpit) lastShipMode_ = m;
  mode_ = m;
  snapNext_ = true;
}

void CameraRig::cycleShipCamera(const Transform& ship) {
  setMode(mode_ == CameraMode::Chase ? CameraMode::Cockpit : CameraMode::Chase, ship);
}

void CameraRig::toggleDev(const Transform& ship) {
  if (mode_ == CameraMode::Chase || mode_ == CameraMode::Cockpit) setMode(CameraMode::Orbit, ship);
  else if (mode_ == CameraMode::Orbit) setMode(CameraMode::Free, ship);
  else setMode(lastShipMode_, ship);
}

void CameraRig::fixedUpdate(float step) {
  if (mode_ == CameraMode::Orbit && autoRotate_) orbitYaw_ += autoRotateRate_ * step;
}

CameraView CameraRig::update(const Transform& ship, const PlayerShip& player, const InputState& in, float dt,
                             bool devInput) {
  CameraView v;
  const ShipCameraDefs& cams = player.def.cameras;
  switch (mode_) {
    case CameraMode::Chase: {
      // Rotation lags the ship (swing on turns); position stays rigidly attached to the ship
      // so there is no lag at speed.
      const float k = 1.0f - std::exp(-cams.chaseRotationLag * dt);
      chaseRot_ = snapNext_ ? ship.rotation : nlerp(chaseRot_, ship.rotation, k);
      const float speed = length(player.state.velocity);
      const float pullback = std::min(speed * 0.015f, 8.0f) + (player.state.boosting ? 4.0f : 0.0f);
      const Vec3 offset{0.0f, cams.chaseHeight, cams.chaseDistance + pullback};
      v.position = ship.position + chaseRot_.rotate(offset);
      // Tilt down so the ship sits in the lower third and the view looks past it.
      const float tilt = std::atan2(cams.chaseHeight * 0.55f, cams.chaseDistance);
      v.rotation = normalize(chaseRot_ * Quat::fromAxisAngle({1, 0, 0}, -tilt));
      v.fovDeg = cams.chaseFovDeg + (player.state.boosting ? 8.0f : 0.0f);
      break;
    }
    case CameraMode::Cockpit: {
      Transform seat{{0, 0, 0}, {}};
      if (auto it = player.sockets.find(cams.cockpitSocket); it != player.sockets.end()) seat = it->second;
      v.position = ship.position + ship.rotation.rotate(seat.position);
      v.rotation = normalize(ship.rotation * seat.rotation);
      v.fovDeg = cams.cockpitFovDeg;
      break;
    }
    case CameraMode::Orbit: {
      if (devInput && (in.mouseLeft() || in.mouseRight())) {
        orbitYaw_ += in.mouseDx() * 0.0035f;
        orbitPitch_ = clamp(orbitPitch_ - in.mouseDy() * 0.0035f, radians(-89.0f), radians(89.0f));
      }
      if (devInput) orbitDistance_ = clamp(orbitDistance_ * std::pow(0.88f, in.wheel()), 3.0f, 20000.0f);
      v.rotation = yawPitch(orbitYaw_, orbitPitch_);
      v.position = ship.position - v.rotation.forward() * orbitDistance_;
      v.fovDeg = devFov_;
      break;
    }
    case CameraMode::Free: {
      if (devInput) {
        if (in.mouseRight()) {
          freeYaw_ += in.mouseDx() * 0.0035f;
          freePitch_ = clamp(freePitch_ - in.mouseDy() * 0.0035f, radians(-89.0f), radians(89.0f));
        }
        freeSpeed_ = clamp(freeSpeed_ * std::pow(1.25f, in.wheel()), 1.0f, 200000.0f);
      }
      v.rotation = yawPitch(freeYaw_, freePitch_);
      if (devInput) {
        Vec3 move{};
        if (in.down("camera.forward")) move += v.rotation.forward();
        if (in.down("camera.back")) move -= v.rotation.forward();
        if (in.down("camera.right")) move += v.rotation.right();
        if (in.down("camera.left")) move -= v.rotation.right();
        if (in.down("camera.up")) move += Vec3{0, 1, 0};
        if (in.down("camera.down")) move -= Vec3{0, 1, 0};
        freePos_ += normalize(move) * (freeSpeed_ * (in.down("camera.boost") ? 10.0f : 1.0f) * dt);
      }
      v.position = freePos_;
      v.fovDeg = devFov_;
      break;
    }
  }
  snapNext_ = false;
  return v;
}

}  // namespace fh
