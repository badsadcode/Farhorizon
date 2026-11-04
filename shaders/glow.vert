#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Camera-facing additive glow sprites (engine exhaust, running lights) in world space.
layout(location = 0) in vec4 inPosSize;  // camera-relative position, world-space radius (m)
layout(location = 1) in vec4 inColor;    // linear HDR colour
layout(location = 0) out vec2 vCorner;
layout(location = 1) out vec3 vColor;
layout(location = 2) out float vDist;
void main() {
  const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
  vec2 c = corners[gl_VertexIndex];
  vec3 p = inPosSize.xyz + (frame.camRight.xyz * c.x + frame.camUp.xyz * c.y) * inPosSize.w;
  gl_Position = frame.viewProj * vec4(p, 1.0);
  vCorner = c;
  vColor = inColor.rgb;
  vDist = length(inPosSize.xyz);
}
