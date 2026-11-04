#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec2 vCorner;
layout(location = 1) in vec3 vColor;
layout(location = 0) out vec4 outColor;
void main() {
  float r2 = dot(vCorner, vCorner);
  float a = exp(-r2 * 4.0);
  if (a < 0.004) discard;
  // Additive; tone-mapped per star (cheap and good enough until the HDR target lands).
  outColor = vec4(tonemapACES(vColor * a * frame.sunColorExp.w), 1.0);
}
