#pragma once
// Local surface terrain for landing sites (design doc 5.3): a deterministic procedural heightfield
// around the pad. Flat zones are blended in for the pad and buildings; the edges rise into cliffs
// that double as the natural walkable boundary. Procedural terrain *data* is allowed by doc 1.4.
#include <cstdint>
#include <vector>

#include "engine/assets/mesh_data.h"
#include "engine/math/math.h"

namespace fh {

struct TerrainFlatZone {
  Vec2 center{};      // x, z
  float radius = 20;  // fully flat inside
  float blend = 15;   // smooth transition width
  float height = 0;
};

struct TerrainParams {
  std::uint32_t seed = 1;
  float size = 400.0f;        // square side, centred on the origin (metres)
  float cellSize = 4.0f;
  float roughness = 6.0f;     // rolling height amplitude (metres)
  float rimHeight = 60.0f;    // cliff height at the edges
  float rimWidth = 50.0f;     // how far in from the edge the cliffs start
  Vec3 color{0.30f, 0.22f, 0.16f};  // linear albedo
  std::vector<TerrainFlatZone> flatZones;
};

class HeightField {
 public:
  HeightField() = default;
  explicit HeightField(const TerrainParams& p);
  float heightAt(float x, float z) const;  // bilinear; clamps outside
  Vec3 normalAt(float x, float z) const;
  bool valid() const { return n_ > 1; }
  MeshData buildMesh() const;
  const TerrainParams& params() const { return p_; }

 private:
  float sample(int ix, int iz) const { return h_[size_t(iz) * size_t(n_) + size_t(ix)]; }
  TerrainParams p_;
  int n_ = 0;          // vertices per side
  float origin_ = 0;   // -size/2
  std::vector<float> h_;
};

}  // namespace fh
