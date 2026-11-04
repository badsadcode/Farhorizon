#pragma once
// Glow lights (running lights, beacons) attached to ships and props. Purely visual for now.
#include <cmath>
#include <cstdint>
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
  float range = 0.0f;      // > 0: also lights surfaces as a point light (metres)
  float intensity = 1.0f;  // point-light strength multiplier (the visible sprite uses `color` only)
  float flicker = 0.0f;    // 0..1: failing-lamp stutter (irregular dropouts)

  // Brightness factor at time t (seconds), with a short fade so blinks are not harsh.
  float intensityAt(double t) const {
    float k = blinkFactor(t);
    if (flicker > 0.0f && k > 0.0f) {
      // Irregular stutter: hash 1/14 s slots; some slots drop out, bursts cluster together.
      auto hash = [](std::uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return float(x & 0xFFFF) / 65535.0f;
      };
      const auto slot = std::uint32_t(std::int64_t(t * 14.0) + std::int64_t(phase * 977.0f));
      const auto burst = std::uint32_t(std::int64_t(t * 0.7) + std::int64_t(phase * 131.0f));
      const bool unstable = hash(burst * 2654435761u) < flicker;  // some seconds are bad seconds
      if (unstable && hash(slot) < 0.55f) k *= 0.08f + 0.25f * hash(slot ^ 0x9E37u);
    }
    return k;
  }

  float blinkFactor(double t) const {
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
