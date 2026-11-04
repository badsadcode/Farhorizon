#pragma once
// Flight HUD (design doc 11): reticle, velocity vector, speed/throttle, boost, target bracket,
// navigation markers and warnings. Built as 2D overlay geometry each frame.
#include <string>

#include "engine/debug/overlay.h"
#include "game/camera_rig.h"
#include "game/content/visual_profile.h"
#include "game/ship_controller.h"
#include "game/world/space_world.h"

namespace fh {

struct HudContext {
  const SpaceWorld& world;
  const ShipController& controller;
  const FlightInput& input;
  Transform ship;            // interpolated render transform
  CameraView camera;
  CameraMode cameraMode;
  Mat4 viewProj;             // camera-relative (rotation-only view) * projection
  float width = 0, height = 0;
  bool mouseCaptured = false;
  double time = 0;
  HudPalette palette;
  std::string lookName;      // active visual profile, shown briefly after switching
  double lookShownUntil = 0;
};

void drawHud(OverlayBuilder& o, const HudContext& c);
std::string formatDistance(double metres);

}  // namespace fh
