#include "game/ships/flight_model.h"

#include <algorithm>
#include <cmath>

namespace fh {

namespace {
float approach(float current, float target, float maxDelta) {
  const float d = target - current;
  return current + clamp(d, -maxDelta, maxDelta);
}
}  // namespace

float flightSpeedCap(const FlightParams& p, const FlightInput& in, bool boosting) {
  float cap = p.maxCruiseSpeed;
  if (in.mode == SpeedMode::Precision) cap *= p.precisionSpeedScale;
  if (boosting) cap = p.maxCruiseSpeed * p.boostSpeedMultiplier;  // boost ignores precision mode
  return cap;
}

void stepFlight(FlightState& s, const FlightInput& in, const FlightParams& p, float dt) {
  if (dt <= 0) return;

  // ---- Boost energy ----
  if (!in.boost) s.boostLockout = false;
  const bool canStart = s.boosting || (s.boostEnergy >= p.boostMinEnergyToStart && !s.boostLockout);
  s.boosting = in.boost && canStart && s.boostEnergy > 0.0f;
  if (s.boosting) {
    s.boostEnergy = std::max(0.0f, s.boostEnergy - p.boostDrainPerSecond * dt);
    if (s.boostEnergy <= 0.0f) s.boostLockout = true;  // must release and re-press to boost again
  } else {
    s.boostEnergy = std::min(p.boostEnergyCapacity, s.boostEnergy + p.boostRegenPerSecond * dt);
  }

  const bool precision = in.mode == SpeedMode::Precision && !s.boosting;
  const float accelScale = (precision ? p.precisionAccelScale : 1.0f) * (s.boosting ? p.boostAccelMultiplier : 1.0f);
  const float angularScale = precision ? p.precisionAngularScale : 1.0f;
  const float cap = flightSpeedCap(p, in, s.boosting);

  // ---- Rotation: rate command with acceleration limit (assist on or off) ----
  const Vec3 r{clamp(in.rotation.x, -1.0f, 1.0f), clamp(in.rotation.y, -1.0f, 1.0f), clamp(in.rotation.z, -1.0f, 1.0f)};
  // Axis-angle body rates: pitch up = +X, yaw right = -Y, roll right = -Z.
  const Vec3 targetRate{r.x * p.maxAngularRate.x * angularScale, -r.y * p.maxAngularRate.y * angularScale,
                        -r.z * p.maxAngularRate.z * angularScale};
  const float maxDw = p.angularAcceleration * angularScale * dt;
  for (int i = 0; i < 3; ++i) s.angularVelocity[i] = approach(s.angularVelocity[i], targetRate[i], maxDw);
  s.orientation = integrateBodyRate(s.orientation, s.angularVelocity, dt);

  // ---- Translation ----
  const Quat q = s.orientation;
  Vec3 vb = q.conjugate().rotate(s.velocity);  // body-frame velocity
  const float lat = p.lateralAcceleration * accelScale * dt;
  const float fwd = p.forwardAcceleration * accelScale * dt;
  const float rev = p.reverseAcceleration * accelScale * dt;

  if (in.assist) {
    const float throttle = clamp(in.throttle, -p.maxReverseFraction, 1.0f);
    const float strafeMax = cap * p.strafeSpeedFraction;
    const Vec3 desired{clamp(in.thrust.x, -1.0f, 1.0f) * strafeMax, clamp(in.thrust.y, -1.0f, 1.0f) * strafeMax,
                       -(s.boosting ? cap : throttle * cap)};
    vb.x = approach(vb.x, desired.x, lat);
    vb.y = approach(vb.y, desired.y, lat);
    // Moving toward more negative z (more forward speed) uses the main engine.
    vb.z = approach(vb.z, desired.z, desired.z < vb.z ? fwd : rev);
  } else {
    const float tz = s.boosting ? 1.0f : clamp(in.thrust.z, -1.0f, 1.0f);
    vb.x += clamp(in.thrust.x, -1.0f, 1.0f) * lat;
    vb.y += clamp(in.thrust.y, -1.0f, 1.0f) * lat;
    vb.z += tz > 0 ? -tz * fwd : -tz * rev;
  }
  s.velocity = q.rotate(vb);

  // Soft speed cap: bleed off excess at the reverse rate instead of snapping.
  const float speed = length(s.velocity);
  if (speed > cap) {
    const float target = std::max(cap, speed - std::max(rev, p.reverseAcceleration * dt) * 2.0f);
    s.velocity *= target / speed;
  }
  s.position += s.velocity * dt;
}

}  // namespace fh
