#pragma once
// Procedural asteroid meshes and deterministic field placement (design doc 6 / 10).
// Procedural *placement and asteroid meshes* are explicitly allowed. They are generated on the
// CPU from data and a seed, never hand-tuned in code.
#include <cstdint>
#include <vector>

#include "engine/assets/mesh_data.h"
#include "engine/math/frames.h"

namespace fh {

struct AsteroidMeshParams {
  std::uint64_t seed = 1;
  int subdivisions = 2;     // icosphere detail: 0 = 20 tris, 1 = 80, 2 = 320, 3 = 1280
  float roughness = 0.35f;  // radial displacement amplitude (fraction of radius)
  Vec3 color{0.30f, 0.27f, 0.24f};  // linear base colour
  float colorVariation = 0.15f;
};

// Unit-ish asteroid (bounding radius exactly 1), flat-shaded, one material.
MeshData generateAsteroidMesh(const AsteroidMeshParams& p);

struct AsteroidFieldParams {
  std::uint64_t seed = 1;
  std::uint32_t count = 200;
  float radius = 3000.0f;      // metres, field extent
  float thickness = 0.35f;     // vertical squash of the field ellipsoid
  std::uint32_t clusters = 6;  // asteroids gather around this many cluster centres
  float minScale = 4.0f;       // asteroid radius range in metres
  float maxScale = 60.0f;
  std::uint32_t variants = 4;  // number of distinct meshes to pick from
};

struct AsteroidInstance {
  Vec3 offset;          // from field centre, metres
  Quat rotation;
  Vec3 spinAxis;
  float spinRate = 0;   // rad/s
  float radius = 1;     // metres (scale of the unit mesh)
  std::uint32_t variant = 0;
};

std::vector<AsteroidInstance> generateAsteroidField(const AsteroidFieldParams& p);

}  // namespace fh
