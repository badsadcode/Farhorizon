#pragma once
// Mining tuning from ship data (kept separate to avoid an include cycle with ship_def.h).

namespace fh {

struct MiningParams {
  float range = 450.0f;          // metres from ship to asteroid surface
  float unitsPerSecond = 1.2f;   // at hardness 1
  float coneDeg = 6.0f;          // aim tolerance beyond the asteroid's angular radius
  float scoopRadius = 14.0f;     // metres from the ship centre
  int maxChunks = 40;
  float chunkLifetime = 90.0f;   // seconds
  float chunkSpeed = 6.0f;       // m/s initial drift toward the ship
  float scanSeconds = 1.5f;
};

}  // namespace fh
