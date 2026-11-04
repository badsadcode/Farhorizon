#pragma once
// Mining laser, ore chunks and cargo scoop (design doc 6). GPU-free and unit-tested.
//  * The beam needs a targeted, in-range asteroid near the nose (aim cone + angular size).
//  * Sustained exposure accumulates progress at unitsPerSecond / hardness; each whole unit
//    detaches an ore chunk from the surface facing the ship and decrements the asteroid's yield.
//  * Chunks drift toward the ship and are scooped when inside scoopRadius (if the hold has room).
//  * Chunk count and lifetime are hard-capped (no unbounded physics objects).
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "game/economy/economy.h"
#include "game/space/mining_params.h"
#include "game/world/space_world.h"

namespace fh {

struct OreChunk {
  Vec3 local{};
  Vec3 velocity{};
  std::string commodity;
  double spawnTime = 0;
  std::uint32_t asteroid = 0;
};

struct MiningEvents {
  int collected = 0;      // chunks scooped this step
  int spawned = 0;
  bool cargoFull = false; // a chunk could not be scooped for lack of space
};

class Mining {
 public:
  explicit Mining(MiningParams p = {}) : params_(p) {}
  const MiningParams& params() const { return params_; }
  void setParams(const MiningParams& p) { params_ = p; }

  // Steps the beam, chunk motion and scoop. `firing` = trigger held this step.
  MiningEvents fixedUpdate(SpaceWorld& world, bool firing, CargoHold& cargo, float dt);
  void applyRebase(const Vec3& delta);

  // Beam state for rendering/HUD (valid after fixedUpdate).
  bool beamActive() const { return beamActive_; }
  const Vec3& beamEnd() const { return beamEnd_; }       // local frame impact point
  const std::string& status() const { return status_; }  // why the beam is not cutting
  float progress() const { return progress_; }           // fraction toward the next unit
  const std::vector<OreChunk>& chunks() const { return chunks_; }

  // Scanning (reveals composition on the HUD).
  void startScan(std::uint32_t asteroid, double now);
  bool isScanned(std::uint32_t asteroid) const { return scanned_.count(asteroid) != 0; }
  bool scanning() const { return scanTarget_ != kNone; }
  float scanProgress(double now) const;

 private:
  static constexpr std::uint32_t kNone = 0xFFFFFFFFu;
  MiningParams params_;
  std::vector<OreChunk> chunks_;
  float progress_ = 0;
  std::uint32_t progressAsteroid_ = kNone;
  bool beamActive_ = false;
  Vec3 beamEnd_{};
  std::string status_;
  std::set<std::uint32_t> scanned_;
  std::uint32_t scanTarget_ = kNone;
  double scanStart_ = 0;
  std::uint32_t spawnCounter_ = 0;
};

}  // namespace fh
