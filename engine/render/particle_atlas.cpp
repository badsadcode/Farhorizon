#include "engine/render/particle_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fh {

namespace {
float hash2(int x, int y, std::uint32_t seed) {
  std::uint32_t h = std::uint32_t(x) * 0x8DA6B343u ^ std::uint32_t(y) * 0xD8163841u ^ seed * 0xCB1AB31Fu;
  h ^= h >> 13;
  h *= 0x5BD1E995u;
  h ^= h >> 15;
  return float(h & 0xFFFFFFu) / float(0xFFFFFFu);
}
float valueNoise(float x, float y, std::uint32_t seed) {
  const int ix = int(std::floor(x)), iy = int(std::floor(y));
  float fx = x - float(ix), fy = y - float(iy);
  fx = fx * fx * (3.0f - 2.0f * fx);
  fy = fy * fy * (3.0f - 2.0f * fy);
  const float a = hash2(ix, iy, seed), b = hash2(ix + 1, iy, seed);
  const float c = hash2(ix, iy + 1, seed), d = hash2(ix + 1, iy + 1, seed);
  return (a + (b - a) * fx) * (1.0f - fy) + (c + (d - c) * fx) * fy;
}
float fbm(float x, float y, std::uint32_t seed) {
  float sum = 0, amp = 0.5f;
  for (int i = 0; i < 5; ++i) {
    sum += amp * valueNoise(x, y, seed + std::uint32_t(i));
    x = x * 2.03f + 11.3f;
    y = y * 2.03f + 7.1f;
    amp *= 0.5f;
  }
  return sum / 0.97f;
}
float smooth(float a, float b, float x) {
  const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
// All shapes reach exactly zero at the cell border, so filtering never bleeds between cells.
float shape(ParticleFrame frame, float u, float v) {
  const float x = u * 2.0f - 1.0f, y = v * 2.0f - 1.0f;
  const float r = std::sqrt(x * x + y * y);
  const float edge = 1.0f - smooth(0.82f, 1.0f, r);
  switch (frame) {
    case ParticleFrame::Puff: {  // wispy gas
      const float n = fbm(u * 5.0f, v * 5.0f, 7u);
      const float body = std::exp(-r * r * 3.0f);
      return std::clamp(body * (0.6f + 0.8f * n), 0.0f, 1.0f) * edge;
    }
    case ParticleFrame::Core:  // tight hot centre, soft skirt
      return std::clamp(std::exp(-r * r * 22.0f) + 0.35f * std::exp(-r * r * 5.0f), 0.0f, 1.0f) * edge;
    case ParticleFrame::Streak: {  // bright head at the top of the cell, long fading tail
      const float across = std::exp(-x * x * 12.0f);
      const float along = std::pow(v, 1.6f) * (1.0f - smooth(0.9f, 1.0f, v));
      return std::clamp(across * along * 1.6f, 0.0f, 1.0f) * (1.0f - smooth(0.92f, 1.0f, std::fabs(x)));
    }
    case ParticleFrame::Spark: {  // four-point glint
      const float glint = std::exp(-std::fabs(x) * 14.0f) * std::exp(-y * y * 9.0f) +
                          std::exp(-std::fabs(y) * 14.0f) * std::exp(-x * x * 9.0f);
      return std::clamp(glint + std::exp(-r * r * 50.0f), 0.0f, 1.0f) * edge;
    }
  }
  return 0.0f;
}
}  // namespace

ImageRGBA8 generateParticleAtlas(int cellSize) {
  ImageRGBA8 img;
  img.width = img.height = cellSize * 2;
  img.pixels.assign(size_t(img.width) * size_t(img.height) * 4, 255);
  for (int cell = 0; cell < 4; ++cell) {
    const int ox = (cell % 2) * cellSize, oy = (cell / 2) * cellSize;
    for (int py = 0; py < cellSize; ++py)
      for (int px = 0; px < cellSize; ++px) {
        const float u = (float(px) + 0.5f) / float(cellSize), v = (float(py) + 0.5f) / float(cellSize);
        const float a = shape(ParticleFrame(cell), u, v);
        img.pixels[(size_t(oy + py) * size_t(img.width) + size_t(ox + px)) * 4 + 3] =
            std::uint8_t(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
      }
  }
  return img;
}

}  // namespace fh
