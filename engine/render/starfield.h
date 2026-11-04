#pragma once
// Deterministic starfield generation (CPU only; no GPU dependency, unit-testable).
// Uses its own RNG so the same seed produces identical skies on every compiler/platform.
#include <cstdint>
#include <vector>

#include "engine/math/math.h"

namespace fh {

struct StarInstance {
  Vec4 dirSize;  // xyz unit direction, w = size in pixels
  Vec4 color;    // linear rgb * brightness, w unused
};
static_assert(sizeof(StarInstance) == 32);

struct StarfieldParams {
  std::uint32_t seed = 1;
  std::uint32_t count = 8000;
  float brightness = 1.0f;
  float galacticPlaneTiltRadians = 0.5f;
};

// SplitMix64: tiny, fast, and fully specified (unlike std::*_distribution).
class Rng {
 public:
  explicit Rng(std::uint64_t seed) : state_(seed) {}
  std::uint64_t next() {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  float uniform() { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); }  // [0,1)
  float range(float lo, float hi) { return lo + (hi - lo) * uniform(); }

 private:
  std::uint64_t state_;
};

std::vector<StarInstance> generateStarfield(const StarfieldParams& params);

}  // namespace fh
