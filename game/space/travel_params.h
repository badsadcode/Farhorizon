#pragma once
// In-system travel drive tuning from ship data (separate header to avoid include cycles).

namespace fh {

struct TravelDriveParams {
  float maxSpeed = 250000.0f;       // m/s in transit
  float acceleration = 20000.0f;    // m/s^2 (ramp up and braking)
  float spoolSeconds = 4.0f;
  float alignToleranceDeg = 3.0f;
  float alignRateDegPerSec = 30.0f; // drive-assisted turn rate while aligning
  float massLockRange = 3000.0f;    // metres from stations / asteroid field edges
  float exitSpeed = 150.0f;         // m/s when dropping back to normal flight
  float minDistance = 20000.0f;     // destinations closer than this are not worth a jump
};

}  // namespace fh
