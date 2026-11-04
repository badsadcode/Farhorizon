#pragma once
// Arcade-friendly 6DOF flight model (design doc 4.1). Deterministic, GPU-free, unit-tested.
//
// Body axes: +X right, +Y up, -Z forward.
// Flight assist ON : throttle is a set-speed; strafe and rotation are rate commands, and released
//                    axes are actively damped to zero ("approachable docking and combat").
// Flight assist OFF: thrusters accelerate directly and linear momentum is preserved. Rotation
//                    keeps rate damping (full-Newtonian rotation is a later option).
// Speeds are capped game-friendly values, not orbital mechanics. Above the cap (for example
// after a boost ends) speed bleeds off at the reverse acceleration rather than snapping.
#include "engine/math/math.h"

namespace fh {

enum class SpeedMode { Precision, Cruise };

struct FlightParams {
  float forwardAcceleration = 18.0f;   // m/s^2
  float reverseAcceleration = 9.0f;    // m/s^2
  float lateralAcceleration = 9.0f;    // m/s^2 (strafe x/y)
  float angularAcceleration = 1.8f;    // rad/s^2
  Vec3 maxAngularRate{0.9f, 0.7f, 1.4f};  // rad/s: pitch, yaw, roll
  float maxCruiseSpeed = 170.0f;       // m/s
  float strafeSpeedFraction = 0.35f;   // max strafe speed as a fraction of the current max speed
  float maxReverseFraction = 0.3f;     // negative throttle range
  // Precision mode (docking): scales speed, acceleration and turn rate down.
  float precisionSpeedScale = 0.25f;
  float precisionAccelScale = 0.6f;
  float precisionAngularScale = 0.6f;
  // Boost: short burst, energy limited.
  float boostSpeedMultiplier = 2.0f;
  float boostAccelMultiplier = 2.5f;
  float boostEnergyCapacity = 100.0f;
  float boostDrainPerSecond = 30.0f;
  float boostRegenPerSecond = 10.0f;
  float boostMinEnergyToStart = 20.0f;
};

struct FlightInput {
  float throttle = 0.0f;   // assist ON set-speed in [-maxReverseFraction, 1]
  Vec3 thrust{};           // body-axis thrust commands in [-1,1]: x right, y up, z forward (z: assist OFF only)
  Vec3 rotation{};         // [-1,1]: x pitch up, y yaw right, z roll right
  bool boost = false;
  bool assist = true;
  SpeedMode mode = SpeedMode::Cruise;
};

struct FlightState {
  Vec3 position{};         // local frame (floating origin)
  Vec3 velocity{};         // local frame, world axes, m/s
  Quat orientation{};      // body -> world
  Vec3 angularVelocity{};  // body frame axis-angle rate, rad/s
  float boostEnergy = 100.0f;
  bool boosting = false;
  bool boostLockout = false;  // set when energy runs out; cleared when the boost input is released
};

// Current speed cap for the given state/input (includes mode and boost).
float flightSpeedCap(const FlightParams& p, const FlightInput& in, bool boosting);
// Advances the state by dt seconds. Call at the fixed simulation rate.
void stepFlight(FlightState& s, const FlightInput& in, const FlightParams& p, float dt);

}  // namespace fh
