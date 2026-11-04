#include "game/world/asteroid_gen.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "engine/render/starfield.h"  // Rng (platform-independent SplitMix64)

namespace fh {

namespace {

float hash3(int x, int y, int z, std::uint32_t seed) {
  std::uint32_t h = seed * 0x9E3779B1u ^ std::uint32_t(x) * 0x85EBCA77u ^ std::uint32_t(y) * 0xC2B2AE3Du ^
                    std::uint32_t(z) * 0x27D4EB2Fu;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return float(h & 0xFFFFFFu) / float(0xFFFFFF);
}

float valueNoise(const Vec3& p, std::uint32_t seed) {
  const int xi = int(std::floor(p.x)), yi = int(std::floor(p.y)), zi = int(std::floor(p.z));
  const float fx = p.x - float(xi), fy = p.y - float(yi), fz = p.z - float(zi);
  auto s = [](float t) { return t * t * (3.0f - 2.0f * t); };
  const float u = s(fx), v = s(fy), w = s(fz);
  auto h = [&](int dx, int dy, int dz) { return hash3(xi + dx, yi + dy, zi + dz, seed); };
  auto lerpf = [](float a, float b, float t) { return a + (b - a) * t; };
  return lerpf(lerpf(lerpf(h(0, 0, 0), h(1, 0, 0), u), lerpf(h(0, 1, 0), h(1, 1, 0), u), v),
               lerpf(lerpf(h(0, 0, 1), h(1, 0, 1), u), lerpf(h(0, 1, 1), h(1, 1, 1), u), v), w);
}

float fbm(Vec3 p, std::uint32_t seed, int octaves) {
  float sum = 0, amp = 0.5f, norm = 0;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * valueNoise(p, seed + std::uint32_t(i) * 101u);
    norm += amp;
    p = p * 2.07f;
    amp *= 0.5f;
  }
  return sum / norm;  // [0,1]
}

void icosphere(int subdivisions, std::vector<Vec3>& verts, std::vector<std::uint32_t>& tris) {
  const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
  verts = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
           {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  for (auto& v : verts) v = normalize(v);
  tris = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
          3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
  for (int s = 0; s < subdivisions; ++s) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> mid;
    auto midpoint = [&](std::uint32_t a, std::uint32_t b) {
      auto key = std::minmax(a, b);
      auto it = mid.find(key);
      if (it != mid.end()) return it->second;
      verts.push_back(normalize((verts[a] + verts[b]) * 0.5f));
      const auto idx = std::uint32_t(verts.size() - 1);
      mid.emplace(key, idx);
      return idx;
    };
    std::vector<std::uint32_t> next;
    next.reserve(tris.size() * 4);
    for (size_t i = 0; i < tris.size(); i += 3) {
      const std::uint32_t a = tris[i], b = tris[i + 1], c = tris[i + 2];
      const std::uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
      next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
    }
    tris = std::move(next);
  }
}

Vec3 randomUnit(Rng& rng) {
  const float z = rng.range(-1.0f, 1.0f), a = rng.range(0.0f, 2.0f * kPi);
  const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  return {r * std::cos(a), r * std::sin(a), z};
}

}  // namespace

MeshData generateAsteroidMesh(const AsteroidMeshParams& p) {
  Rng rng(0xA57E'0000'0000'0000ull ^ p.seed);
  const auto noiseSeed = std::uint32_t(rng.next());
  // Mild overall elongation so asteroids are not all spheres.
  const Vec3 stretch{rng.range(0.75f, 1.0f), rng.range(0.6f, 0.9f), rng.range(0.85f, 1.15f)};

  std::vector<Vec3> sphere;
  std::vector<std::uint32_t> tris;
  icosphere(std::clamp(p.subdivisions, 0, 4), sphere, tris);

  std::vector<Vec3> displaced(sphere.size());
  float maxR = 0;
  for (size_t i = 0; i < sphere.size(); ++i) {
    const Vec3 d = sphere[i];
    const float big = fbm(d * 1.3f + Vec3{3.7f, 1.1f, 8.2f}, noiseSeed, 3);
    const float small = fbm(d * 4.0f, noiseSeed ^ 0x55u, 2);
    const float r = 1.0f + p.roughness * ((big - 0.5f) * 2.0f + (small - 0.5f) * 0.6f);
    displaced[i] = d * std::max(0.3f, r) * stretch;
    maxR = std::max(maxR, length(displaced[i]));
  }

  MeshData m;
  MaterialData mat;
  mat.name = "asteroid_rock";
  const float v = 1.0f + rng.range(-p.colorVariation, p.colorVariation);
  mat.baseColorFactor = Vec4(p.color * v, 1.0f);
  mat.metallic = 0.0f;
  mat.roughness = 0.92f;
  mat.panels = 0.0f;
  m.materials.push_back(mat);

  // Flat shading: three unique vertices per triangle with the face normal (low-poly look).
  m.vertices.reserve(tris.size());
  m.indices.reserve(tris.size());
  for (size_t i = 0; i < tris.size(); i += 3) {
    const Vec3 a = displaced[tris[i]] / maxR, b = displaced[tris[i + 1]] / maxR, c = displaced[tris[i + 2]] / maxR;
    Vec3 n = normalize(cross(b - a, c - a));
    if (dot(n, a + b + c) < 0) n = -n;  // keep outward-facing
    for (const Vec3* pv : {&a, &b, &c}) {
      m.vertices.push_back({*pv, n, {0, 0}});
      m.indices.push_back(std::uint32_t(m.vertices.size() - 1));
    }
  }
  m.submeshes.push_back({0, std::uint32_t(m.indices.size()), 0});
  // Bounding radius is exactly 1 by construction (normalised by maxR); centre at the origin.
  m.boundsMin = {-1, -1, -1};
  m.boundsMax = {1, 1, 1};
  return m;
}

std::vector<AsteroidInstance> generateAsteroidField(const AsteroidFieldParams& p) {
  Rng rng(0xF1E1'D000'0000'0000ull ^ p.seed);
  std::vector<Vec3> centres;
  const std::uint32_t clusterCount = std::max(1u, p.clusters);
  for (std::uint32_t i = 0; i < clusterCount; ++i) {
    Vec3 c = randomUnit(rng) * (p.radius * std::sqrt(rng.uniform()) * 0.8f);
    c.y *= p.thickness;
    centres.push_back(c);
  }
  const float clusterSpread = p.radius * 0.35f;
  std::vector<AsteroidInstance> out;
  out.reserve(p.count);
  for (std::uint32_t i = 0; i < p.count; ++i) {
    AsteroidInstance a;
    const Vec3& c = centres[rng.next() % clusterCount];
    // Roughly gaussian offset around the cluster centre (sum of uniforms).
    Vec3 o{rng.range(-1, 1) + rng.range(-1, 1) + rng.range(-1, 1), rng.range(-1, 1) + rng.range(-1, 1),
           rng.range(-1, 1) + rng.range(-1, 1) + rng.range(-1, 1)};
    o = o * (clusterSpread / 3.0f);
    o.y *= p.thickness;
    a.offset = c + o;
    // Size distribution skewed toward small rocks.
    const float u = rng.uniform();
    a.radius = p.minScale + (p.maxScale - p.minScale) * u * u * u;
    a.rotation = Quat::fromAxisAngle(randomUnit(rng), rng.range(0.0f, 2.0f * kPi));
    a.spinAxis = randomUnit(rng);
    a.spinRate = rng.range(0.01f, 0.12f) * (10.0f / std::max(10.0f, a.radius));  // big rocks turn slower
    a.variant = std::uint32_t(rng.next() % std::max(1u, p.variants));
    out.push_back(a);
  }
  return out;
}

}  // namespace fh
