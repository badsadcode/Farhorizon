#pragma once
// Procedural particle textures (CPU only, deterministic, unit-testable): a 2x2 atlas generated at
// startup, so effects are original and need no art files. The colour is white; shapes are in alpha.
#include "engine/assets/mesh_data.h"
#include "engine/math/math.h"

namespace fh {

enum class ParticleFrame : int { Puff = 0, Core = 1, Streak = 2, Spark = 3 };

// One camera-facing or velocity-stretched textured quad. Must match shaders/particle.vert.
struct ParticleSprite {
  Vec4 positionSize;  // camera-relative position, half width (m)
  Vec4 color;         // linear HDR colour (premultiplied), a: 0 additive .. 1 opaque
  Vec4 axisLength;    // camera-relative unit axis, half length (m); 0 = faces the camera
  Vec4 frameSpin;     // x atlas frame, y rotation (radians), zw unused
};
static_assert(sizeof(ParticleSprite) == 64);

// One vertex of a continuous exhaust ribbon. Must match shaders/ribbon.vert.
struct RibbonVertex {
  Vec4 posU;    // camera-relative position, u across the ribbon (-1..1)
  Vec4 color;   // linear HDR
  Vec4 vSeed;   // x metres along the trail, y seed
};
static_assert(sizeof(RibbonVertex) == 48);

ImageRGBA8 generateParticleAtlas(int cellSize = 128);

}  // namespace fh
