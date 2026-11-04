#include <cstring>

#include "game/ships/flight_model.h"
#include "test_framework.h"

using namespace fh;

namespace {
constexpr float kDt = 1.0f / 60.0f;
void run(FlightState& s, const FlightInput& in, const FlightParams& p, float seconds) {
  const int steps = int(seconds / kDt + 0.5f);
  for (int i = 0; i < steps; ++i) stepFlight(s, in, p, kDt);
}
float forwardSpeed(const FlightState& s) { return dot(s.velocity, s.orientation.forward()); }
}  // namespace

TEST(flight_assist_reaches_and_holds_set_speed) {
  FlightParams p;
  FlightState s;
  FlightInput in;
  in.throttle = 0.5f;
  run(s, in, p, 20.0f);
  CHECK_NEAR(forwardSpeed(s), 0.5 * p.maxCruiseSpeed, 0.01);
  CHECK(s.position.z < -100.0f);  // moved forward (-Z)
  in.throttle = 0.0f;
  run(s, in, p, 20.0f);
  CHECK_NEAR(length(s.velocity), 0.0, 0.01);  // assist brakes to the set speed
}

TEST(flight_assist_damps_released_strafe) {
  FlightParams p;
  FlightState s;
  FlightInput in;
  in.thrust.x = 1.0f;
  run(s, in, p, 5.0f);
  CHECK(s.velocity.x > 10.0f);
  CHECK(s.velocity.x <= p.maxCruiseSpeed * p.strafeSpeedFraction + 0.01f);
  in.thrust.x = 0.0f;
  run(s, in, p, 10.0f);
  CHECK_NEAR(s.velocity.x, 0.0, 0.01);
}

TEST(flight_assist_off_preserves_momentum) {
  FlightParams p;
  FlightState s;
  FlightInput in;
  in.assist = false;
  in.thrust.z = 1.0f;
  run(s, in, p, 2.0f);
  const float v = length(s.velocity);
  CHECK_NEAR(v, p.forwardAcceleration * 2.0f, 0.5);
  in.thrust.z = 0.0f;
  in.rotation.y = 1.0f;  // yaw while coasting: velocity direction must not follow the nose
  const Vec3 before = s.velocity;
  run(s, in, p, 1.0f);
  CHECK_VEC_NEAR(s.velocity, before, 1e-3);
  CHECK(dot(s.orientation.forward(), normalize(before)) < 0.999f);
}

TEST(flight_speed_cap_and_precision_mode) {
  FlightParams p;
  FlightState s;
  FlightInput in;
  in.assist = false;
  in.thrust.z = 1.0f;
  run(s, in, p, 30.0f);
  CHECK(length(s.velocity) <= p.maxCruiseSpeed + 0.01f);
  in.mode = SpeedMode::Precision;
  run(s, in, p, 30.0f);
  CHECK_NEAR(length(s.velocity), p.maxCruiseSpeed * p.precisionSpeedScale, 0.5);
}

TEST(flight_rotation_rate_command_and_damping) {
  FlightParams p;
  FlightState s;
  FlightInput in;
  in.rotation.x = 1.0f;  // pitch up
  run(s, in, p, 3.0f);
  CHECK_NEAR(s.angularVelocity.x, p.maxAngularRate.x, 1e-4);
  CHECK(s.orientation.forward().y > 0.5f);  // nose came up
  in.rotation.x = 0.0f;
  run(s, in, p, 3.0f);
  CHECK_NEAR(length(s.angularVelocity), 0.0, 1e-5);
  // Yaw right turns the nose toward +X.
  FlightState t;
  FlightInput yaw;
  yaw.rotation.y = 1.0f;
  run(t, yaw, p, 0.8f);
  CHECK(t.orientation.forward().x > 0.1f);
  // Roll right drops the right wing.
  FlightState r;
  FlightInput roll;
  roll.rotation.z = 1.0f;
  run(r, roll, p, 0.5f);
  CHECK(r.orientation.right().y < -0.1f);
}

TEST(flight_boost_energy_and_speed) {
  FlightParams p;
  FlightState s;
  FlightInput in;
  in.throttle = 1.0f;
  in.boost = true;
  run(s, in, p, 1.0f);
  CHECK(s.boosting);
  CHECK(s.boostEnergy < p.boostEnergyCapacity);
  run(s, in, p, 10.0f);  // drains completely, boost ends
  CHECK(!s.boosting);
  CHECK(forwardSpeed(s) <= p.maxCruiseSpeed * p.boostSpeedMultiplier + 0.1f);
  // Cannot restart until minimum energy has regenerated.
  stepFlight(s, in, p, kDt);
  CHECK(!s.boosting);
  in.boost = false;
  run(s, in, p, 30.0f);
  CHECK_NEAR(s.boostEnergy, p.boostEnergyCapacity, 1e-3);
  CHECK(forwardSpeed(s) <= p.maxCruiseSpeed + 0.1f);  // excess speed bled off after boost
}

TEST(flight_is_deterministic) {
  FlightParams p;
  FlightState a, b;
  FlightInput in;
  in.throttle = 0.7f;
  in.rotation = {0.3f, -0.5f, 0.2f};
  in.thrust = {0.4f, -0.2f, 0.0f};
  run(a, in, p, 7.0f);
  run(b, in, p, 7.0f);
  // Field-by-field: memcmp would also compare padding bytes, which are unspecified (MSVC differs).
  CHECK(a.position == b.position);
  CHECK(a.velocity == b.velocity);
  CHECK(a.orientation.x == b.orientation.x && a.orientation.y == b.orientation.y &&
        a.orientation.z == b.orientation.z && a.orientation.w == b.orientation.w);
  CHECK(a.angularVelocity == b.angularVelocity);
  CHECK(a.boostEnergy == b.boostEnergy && a.boosting == b.boosting && a.boostLockout == b.boostLockout);
}
