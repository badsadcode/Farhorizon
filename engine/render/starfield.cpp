#include "engine/render/starfield.h"

#include <algorithm>
#include <cmath>

namespace fh {

namespace {
Vec3 randomUnitVector(Rng& rng) {
  const float z = rng.range(-1.0f, 1.0f);
  const float a = rng.range(0.0f, 2.0f * kPi);
  const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  return {r * std::cos(a), r * std::sin(a), z};
}

Vec3 starColor(float t) {
  // t in [0,1]: cool orange -> white -> hot blue-white. Linear RGB.
  const Vec3 orange{1.0f, 0.62f, 0.38f}, white{1.0f, 0.96f, 0.92f}, blue{0.66f, 0.78f, 1.0f};
  return t < 0.5f ? lerp(orange, white, t * 2.0f) : lerp(white, blue, (t - 0.5f) * 2.0f);
}
}  // namespace

std::vector<StarInstance> generateStarfield(const StarfieldParams& params) {
  Rng rng(0xF4A2'0000'0000'0000ull ^ params.seed);
  const Vec3 galacticNormal = normalize(Vec3{0.0f, std::cos(params.galacticPlaneTiltRadians),
                                             std::sin(params.galacticPlaneTiltRadians)});
  std::vector<StarInstance> stars;
  stars.reserve(params.count);
  for (std::uint32_t i = 0; i < params.count; ++i) {
    Vec3 d = randomUnitVector(rng);
    const bool inBand = rng.uniform() < 0.45f;
    if (inBand) {  // squash toward the galactic plane
      const float h = dot(d, galacticNormal);
      d = normalize(d - galacticNormal * (h * 0.85f));
    }
    // Power-law brightness: most stars faint, a few bright.
    const float u = rng.uniform();
    float b = 0.05f + 2.2f * std::pow(u, 9.0f) + 0.25f * std::pow(u, 3.0f);
    if (inBand) b *= 0.75f;
    const float temperature = std::clamp(rng.uniform() * 0.9f + (b > 1.0f ? 0.2f : 0.0f), 0.0f, 1.0f);
    const float size = 1.6f + 2.6f * std::sqrt(std::min(b, 2.5f) / 2.5f);
    StarInstance s;
    s.dirSize = Vec4(d, size);
    s.color = Vec4(starColor(temperature) * (b * params.brightness), 0.0f);
    stars.push_back(s);
  }
  return stars;
}

}  // namespace fh
