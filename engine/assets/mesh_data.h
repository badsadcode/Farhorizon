#pragma once
// CPU-side mesh representation produced by importers and consumed by the renderer.
#include <cstdint>
#include <string>
#include <vector>

#include "engine/math/math.h"

namespace fh {

struct MeshVertex {
  Vec3 position;
  Vec3 normal;
  Vec2 uv;
};
static_assert(sizeof(MeshVertex) == 32);

struct ImageRGBA8 {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;  // width*height*4, sRGB-encoded colour
  bool valid() const { return width > 0 && height > 0 && pixels.size() == size_t(width) * height * 4; }
};

struct MaterialData {
  std::string name;
  Vec4 baseColorFactor{1, 1, 1, 1};  // linear
  float metallic = 1.0f;
  float roughness = 1.0f;
  Vec3 emissive{0, 0, 0};            // linear
  int baseColorImage = -1;           // index into MeshData::images, -1 = none
  float panels = 1.0f;               // procedural panel seams: 1 for built surfaces, 0 for rock and ground
};

struct SubMesh {
  std::uint32_t firstIndex = 0;
  std::uint32_t indexCount = 0;
  int material = -1;                 // index into MeshData::materials, -1 = default
};

// Named attachment point (pilot seat, airlock, hardpoints...) in mesh space.
struct MeshSocket {
  std::string name;
  Vec3 position;
  Quat rotation;
};

struct MeshData {
  std::vector<MeshVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<SubMesh> submeshes;
  std::vector<MaterialData> materials;
  std::vector<ImageRGBA8> images;
  std::vector<MeshSocket> sockets;
  Vec3 boundsMin{0, 0, 0};
  Vec3 boundsMax{0, 0, 0};

  Vec3 boundsCenter() const { return (boundsMin + boundsMax) * 0.5f; }
  float boundsRadius() const { return length(boundsMax - boundsMin) * 0.5f; }
  std::size_t triangleCount() const { return indices.size() / 3; }
};

}  // namespace fh
