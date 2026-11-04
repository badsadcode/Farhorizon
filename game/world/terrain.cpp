#include "game/world/terrain.h"

#include <algorithm>
#include <cmath>

namespace fh {

namespace {
float hash2(int x, int z, std::uint32_t seed) {
  std::uint32_t h = seed * 0x9E3779B1u ^ std::uint32_t(x) * 0x85EBCA77u ^ std::uint32_t(z) * 0xC2B2AE3Du;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return float(h & 0xFFFFFFu) / float(0xFFFFFF);
}
float noise2(float x, float z, std::uint32_t seed) {
  const int xi = int(std::floor(x)), zi = int(std::floor(z));
  const float fx = x - float(xi), fz = z - float(zi);
  const float u = fx * fx * (3 - 2 * fx), v = fz * fz * (3 - 2 * fz);
  const float a = hash2(xi, zi, seed), b = hash2(xi + 1, zi, seed), c = hash2(xi, zi + 1, seed),
              d = hash2(xi + 1, zi + 1, seed);
  return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}
float smoothstep01(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}
}  // namespace

HeightField::HeightField(const TerrainParams& p) : p_(p) {
  n_ = std::max(2, int(p.size / p.cellSize) + 1);
  origin_ = -p.size * 0.5f;
  h_.resize(size_t(n_) * size_t(n_));
  for (int iz = 0; iz < n_; ++iz)
    for (int ix = 0; ix < n_; ++ix) {
      const float x = origin_ + float(ix) * p.cellSize, z = origin_ + float(iz) * p.cellSize;
      float h = 0, amp = 1, freq = 1.0f / 60.0f, norm = 0;
      for (int o = 0; o < 4; ++o) {
        h += (noise2(x * freq, z * freq, p.seed + std::uint32_t(o) * 31u) - 0.5f) * amp;
        norm += amp * 0.5f;
        amp *= 0.5f;
        freq *= 2.1f;
      }
      h = h / norm * p.roughness;
      // Cliffs: rise sharply within rimWidth of the edge (with a noisy crest).
      const float edge = std::min({x - origin_, -origin_ - x, z - origin_, -origin_ - z});
      const float rim = smoothstep01(1.0f - edge / p.rimWidth);
      h += rim * rim * p.rimHeight * (0.75f + 0.5f * noise2(x / 25.0f, z / 25.0f, p.seed ^ 0xABCu));
      // Flatten pads and building plots.
      for (const auto& fz : p.flatZones) {
        const float d = std::sqrt((x - fz.center.x) * (x - fz.center.x) + (z - fz.center.y) * (z - fz.center.y));
        const float w = 1.0f - smoothstep01((d - fz.radius) / std::max(0.01f, fz.blend));
        h = h + (fz.height - h) * w;
      }
      h_[size_t(iz) * size_t(n_) + size_t(ix)] = h;
    }
}

float HeightField::heightAt(float x, float z) const {
  if (!valid()) return 0;
  const float gx = std::clamp((x - origin_) / p_.cellSize, 0.0f, float(n_ - 1) - 1e-3f);
  const float gz = std::clamp((z - origin_) / p_.cellSize, 0.0f, float(n_ - 1) - 1e-3f);
  const int ix = int(gx), iz = int(gz);
  const float fx = gx - float(ix), fz = gz - float(iz);
  const float a = sample(ix, iz), b = sample(ix + 1, iz), c = sample(ix, iz + 1), d = sample(ix + 1, iz + 1);
  return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fz;
}

Vec3 HeightField::normalAt(float x, float z) const {
  const float e = p_.cellSize * 0.5f;
  const float dx = heightAt(x + e, z) - heightAt(x - e, z);
  const float dz = heightAt(x, z + e) - heightAt(x, z - e);
  return normalize(Vec3{-dx, 2 * e, -dz});
}

MeshData HeightField::buildMesh() const {
  MeshData m;
  MaterialData mat;
  mat.name = "terrain";
  mat.baseColorFactor = Vec4(p_.color, 1.0f);
  mat.metallic = 0.0f;
  mat.roughness = 0.95f;
  m.materials.push_back(mat);
  m.vertices.reserve(size_t(n_) * size_t(n_));
  for (int iz = 0; iz < n_; ++iz)
    for (int ix = 0; ix < n_; ++ix) {
      const float x = origin_ + float(ix) * p_.cellSize, z = origin_ + float(iz) * p_.cellSize;
      m.vertices.push_back({{x, sample(ix, iz), z}, normalAt(x, z), {x / 8.0f, z / 8.0f}});
    }
  for (int iz = 0; iz + 1 < n_; ++iz)
    for (int ix = 0; ix + 1 < n_; ++ix) {
      const auto i = std::uint32_t(iz * n_ + ix);
      const auto n = std::uint32_t(n_);
      m.indices.insert(m.indices.end(), {i, i + n, i + 1, i + 1, i + n, i + n + 1});  // CCW from above
    }
  m.submeshes.push_back({0, std::uint32_t(m.indices.size()), 0});
  m.boundsMin = {origin_, -p_.rimHeight, origin_};
  m.boundsMax = {-origin_, p_.rimHeight * 2, -origin_};
  return m;
}

}  // namespace fh
