#pragma once
// Active local space simulation (design doc 4.3 / 9.2): the player ship, static props (station
// proxy), asteroid fields, simple sphere collisions, targeting and the floating origin.
// GPU-agnostic: visuals are referenced by opaque mesh handles assigned by the app.
// Thread affinity: main thread (fixed-step simulation).
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/math/frames.h"
#include "game/physics/collision.h"
#include "game/content/scene_def.h"
#include "game/content/ship_def.h"
#include "game/ships/flight_model.h"

namespace fh {

using VisualHandle = std::uint32_t;

struct Transform {
  Vec3 position{};
  Quat rotation{};
};

struct PlayerShip {
  ShipDef def;
  FlightState state;
  FlightState previous;          // state at the previous fixed step (for render interpolation)
  FlightInput input;
  VisualHandle visual = 0;
  Mat4 meshFix;                  // mesh space -> ship space (scale, axis fix, recentre)
  float collisionRadius = 5.0f;
  std::map<std::string, Transform> sockets;  // ship space
  std::uint32_t collisions = 0;
  float lastImpactSpeed = 0.0f;
  bool kinematic = false;          // docking/autopilot drives the state directly (no flight model)
  bool collisionsEnabled = true;   // false while clamped to a pad
  double lastImpactTime = -100.0;
};

struct Prop {
  PropDef def;
  SystemPosition system;
  Vec3 local{};
  Quat rotation{};
  VisualHandle visual = 0;
  Mat4 meshFix;
  float collisionRadius = 10.0f;   // bounding sphere; also used when `colliders` is empty
  std::vector<Obb> colliders;      // prop space
};

struct Asteroid {
  SystemPosition system;
  Vec3 local{};
  Quat rotation{};
  Vec3 spinAxis{0, 1, 0};
  float spinRate = 0.0f;
  float radius = 1.0f;
  VisualHandle visual = 0;
  std::uint32_t field = 0;
  std::uint32_t index = 0;
  // Mining (design doc 6): primary resource, remaining/initial yield in units, hardness.
  std::string resource;
  int yield = 0;
  int initialYield = 0;
  float hardness = 1.0f;
};

struct AsteroidFieldRuntime {
  AsteroidFieldDef def;
  std::vector<VisualHandle> variants;
};

struct TargetRef {
  enum class Kind { Prop, Asteroid } kind = Kind::Prop;
  std::uint32_t index = 0;
  bool operator==(const TargetRef&) const = default;
};

struct TargetInfo {
  std::string name;
  std::string marker;     // station, asteroid, ...
  Vec3 local{};
  float radius = 1.0f;
  double distance = 0.0;  // centre to player, metres
  float closingSpeed = 0.0f;  // positive = approaching
};

class SpaceWorld {
 public:
  explicit SpaceWorld(double rebaseDistance = 4096.0) : frame_({}, rebaseDistance) {}

  // Setup (the app creates visuals and passes their handles in).
  void setOrigin(const SystemPosition& origin, double rebaseDistance);
  PlayerShip& player() { return player_; }
  const PlayerShip& player() const { return player_; }
  void addProp(Prop p);
  std::uint32_t addAsteroidField(AsteroidFieldRuntime field);  // returns field index
  void addAsteroid(Asteroid a);

  // Simulation.
  void fixedUpdate(float dt);
  // Moves the floating origin to the player when needed. Returns the delta that was added to
  // every active local position (callers shift their own local-space state by it), or nullopt.
  std::optional<Vec3> rebaseIfNeeded();
  double simulationTime() const { return time_; }

  // Interpolated player transform for rendering (alpha from FixedStepClock).
  Transform playerRenderTransform(float alpha) const;
  Prop& prop(std::uint32_t i) { return props_.at(i); }
  Asteroid& asteroid(std::uint32_t i) { return asteroids_.at(i); }
  void setSimulationTime(double t) { time_ = t; }

  // Targeting.
  void cycleTarget(double maxAsteroidRange = 6000.0);
  void targetNearestAsteroid();
  void clearTarget() { target_.reset(); }
  const std::optional<TargetRef>& target() const { return target_; }
  std::optional<TargetInfo> targetInfo() const;
  TargetInfo describe(const TargetRef& ref) const;

  const LocalFrame& frame() const { return frame_; }
  const std::vector<Prop>& props() const { return props_; }
  const std::vector<Asteroid>& asteroids() const { return asteroids_; }
  const std::vector<AsteroidFieldRuntime>& fields() const { return fields_; }
  // Nearest asteroid surface distance (metres) and its closing speed; for proximity warnings.
  std::optional<std::pair<float, float>> nearestAsteroidSurface() const;

 private:
  void resolveCollision(const Vec3& centre, float radius);
  void resolveContact(const Contact& c);

  LocalFrame frame_;
  PlayerShip player_;
  std::vector<Prop> props_;
  std::vector<AsteroidFieldRuntime> fields_;
  std::vector<Asteroid> asteroids_;
  std::optional<TargetRef> target_;
  double time_ = 0.0;
};

}  // namespace fh
