#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec2 vCorner;
layout(location = 1) in vec3 vColor;
layout(location = 2) in float vDist;
layout(location = 0) out vec4 outColor;
void main() {
  float r2 = dot(vCorner, vCorner);
  if (r2 > 1.0) discard;
  float halo = exp(-r2 * 5.0);
  float core = exp(-r2 * 40.0) * 3.0;
  // Lamps keep a hot core through the fog but their halo scatters into it.
  float d = frame.fogColorDensity.a;
  float keep = d > 0.0 ? exp(-d * vDist * 0.6) : 1.0;
  outColor = vec4(vColor * (halo * (0.4 + 0.6 * keep) + core * keep), 1.0);
}
