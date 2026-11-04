#pragma once
// Glow lights (running lights, beacons) attached to ships and props. Purely visual for now.
#include <cmath>
#include <string>
#include <vector>

#include "engine/math/math.h"

namespace fh {

struct LightDef {
  Vec3 position{};         // owner-local metres (ship space / prop space after scale)
  Vec3 color{1, 1, 1};     // linear HDR colour (values > 1 bloom)
  float size = 1.0f;       // sprite radius in metres
  float blinkHz = 0.0f;    // 0 = steady
  float phase = 0.0f;      // 0..1 offset in the blink cycle
  float duty = 0.5f;       // fraction of the cycle the light is on

  // Brightness factor at time t (seconds), with a short fade so blinks are not harsh.
  float intensityAt(double t) const {
    if (blinkHz <= 0.0f) return 1.0f;
    double cycle = t * blinkHz + phase;
    const float f = float(cycle - std::floor(cycle));
    const float fade = 0.04f;
    if (f < duty) return std::min(1.0f, std::min(f, duty - f) / fade + 0.2f);
    return 0.0f;
  }
};

struct EngineGlowDef {
  std::vector<std::string> sockets;  // ship sockets where exhaust glows are drawn
  Vec3 color{0.3f, 0.8f, 1.6f};
  Vec3 boostColor{1.6f, 0.4f, 1.4f};
  float idleSize = 1.0f;
  float thrustSize = 3.0f;
  float idleIntensity = 0.4f;
  float thrustIntensity = 3.0f;
};

}  // namespace fh
