#pragma once
// CPU particle simulation for ship effects: engine trails and the travel drive. GPU-free and
// unit-tested. Presentation only, so it never affects the simulation or saves, and its random
// numbers are its own. Particles live in the local frame, or relative to the player's ship
// (`attached`) when the ship moves too fast for world-space particles to stay on screen.
#include <cstdint>
#include <vector>

#include "engine/math/math.h"
#include "engine/render/particle_atlas.h"

namespace fh {

struct Particle {
  Vec3 position{};
  Vec3 velocity{};
  Vec3 color{1, 1, 1};     // linear HDR at full strength
  float age = 0;
  float life = 1;
  float sizeStart = 1, sizeEnd = 1;  // half width (m)
  float stretch = 0;       // half length along the velocity (m); 0 = faces the camera
  float spin = 0;          // rotation (radians)
  float spinRate = 0;      // radians per second
  float fadeIn = 0.02f;    // fraction of life spent fading in
  ParticleFrame frame = ParticleFrame::Puff;
  bool attached = false;   // position is relative to the player's ship
};

// One point of an exhaust ribbon: a little gas left behind by the nozzle.
struct TrailPoint {
  Vec3 position{};
  Vec3 velocity{};
  Vec3 color{1, 1, 1};
  float age = 0, life = 1;
  float strength = 1;   // thrust when it was emitted
  float along = 0;      // metres of trail since the start, so the turbulence stays with the gas
};

// One engine's trail. Gas stays in the world while the ship flies on.
struct TrailEmitter {
  Vec3 last{};
  bool hasLast = false;
  float carry = 0;         // fractional gas puffs owed from the previous frame
  float sincePoint = 1;
  float along = 0;
  float size = 0.7f;
  float seed = 0;
  std::vector<TrailPoint> points;  // oldest first
  void shift(const Vec3& delta);   // floating-origin rebase
};

struct EngineFx {
  Vec3 position{};         // nozzle, local frame
  Vec3 back{0, 0, 1};      // unit direction the exhaust leaves in
  Vec3 shipVelocity{};
  Vec3 color{1, 1, 1};
  float demand = 0;        // 0..1 forward thrust
  bool boosting = false;
  float rate = 320;        // particles per second at full thrust
  float life = 1.4f;       // seconds
  float size = 0.7f;       // half width at the nozzle (m)
  float seed = 0;          // keeps engines' turbulence apart
};

enum class TravelFxPhase { Idle, Spooling, Transit };

struct TravelFx {
  TravelFxPhase phase = TravelFxPhase::Idle;
  float spool = 0;         // 0..1 while spooling
  float speedFraction = 0; // 0..1 of top transit speed
  Vec3 forward{0, 0, -1};
  Vec3 color{0.6f, 0.85f, 2.2f};
  const Vec3* nozzles = nullptr;  // relative to the ship
  int nozzleCount = 0;
};

class ParticleSystem {
 public:
  static constexpr std::size_t kMaxParticles = 8000;

  void update(float dt);
  void clear() { particles_.clear(); }
  void shift(const Vec3& delta);  // floating-origin rebase
  std::size_t size() const { return particles_.size(); }
  const std::vector<Particle>& particles() const { return particles_; }
  void add(const Particle& p);    // dropped when full: effects must never grow without bound

  // Exhaust at the nozzle: a continuous ribbon plus some gas puffs for volume. A jump (teleport)
  // restarts the trail.
  void emitEngine(TrailEmitter& emitter, const EngineFx& fx, float dt);
  // Charging sparks, then hyperspace streaks and long engine plumes while in transit.
  void emitTravel(const TravelFx& fx, float dt);
  // The flash when the drive engages or arrives.
  void emitBurst(const Vec3& forward, const Vec3& color, float strength);

  // Ages a trail whose engine is not running, so it fades out instead of freezing.
  void tickTrail(TrailEmitter& emitter, float dt) const;
  // Camera-facing triangles for one trail, ending at the live nozzle.
  static void buildRibbon(const TrailEmitter& emitter, const Vec3& cameraPosition, std::vector<RibbonVertex>& out);
  // A straight drive plume behind a nozzle, used during transit. `nozzle` is in the local frame.
  static void buildPlume(const Vec3& nozzle, const Vec3& forward, const Vec3& color, float amount, float seed,
                         const Vec3& cameraPosition, std::vector<RibbonVertex>& out);

  // Sprites for rendering, relative to the camera.
  void buildSprites(const Vec3& cameraPosition, const Vec3& shipPosition, std::vector<ParticleSprite>& out) const;

 private:
  float random();                        // 0..1
  Vec3 randomUnit();
  std::uint32_t rng_ = 0x2F6E2B1Du;
  std::vector<Particle> particles_;
  float streakCarry_ = 0, sparkCarry_ = 0;
};

// Dust hanging in space around the ship. It stays put in the world, so it streaks past when you move
// and gives speed something to be measured against. Invisible when stopped, and it hands over to the
// tunnel at travel-drive speeds. Deterministic.
class SpeedDust {
 public:
  static constexpr int kCount = 460;
  static constexpr float kHalfBox = 140.0f;  // metres

  explicit SpeedDust(std::uint32_t seed = 0x51D7A3C9u);
  void shift(const Vec3& delta);
  // Keeps every mote inside the box around the camera. Motes never move on their own.
  void wrap(const Vec3& cameraPosition);
  // How strongly dust shows at this speed, 0..1.
  static float visibility(float speed);
  // Appends sprites; does not clear `out`.
  void buildSprites(const Vec3& cameraPosition, const Vec3& velocity, std::vector<ParticleSprite>& out) const;
  const std::vector<Vec3>& positions() const { return positions_; }

 private:
  std::vector<Vec3> positions_;
  std::vector<float> widths_, brightness_;
};

}  // namespace fh
