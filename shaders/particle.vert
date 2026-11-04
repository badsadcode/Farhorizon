#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Textured particle quads: camera-facing (gas, flashes) or stretched along an axis (flames, streaks).
layout(location = 0) in vec4 inPosSize;     // camera-relative position, half width (m)
layout(location = 1) in vec4 inColor;       // linear HDR, premultiplied; a 0 additive .. 1 opaque
layout(location = 2) in vec4 inAxisLength;  // camera-relative unit axis, half length (m); 0 = facing
layout(location = 3) in vec4 inFrameSpin;   // x atlas frame, y rotation
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) out float vDist;

void main() {
  const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
  vec2 c = corners[gl_VertexIndex];
  vec3 center = inPosSize.xyz;
  vec3 p;
  if (inAxisLength.w > 0.0) {
    // Rotates around its axis to face the camera as well as it can.
    vec3 axis = inAxisLength.xyz;
    vec3 side = cross(axis, center);
    float len = length(side);
    side = len > 1e-4 ? side / len : frame.camRight.xyz;
    p = center + side * c.x * inPosSize.w - axis * c.y * inAxisLength.w;
  } else {
    float s = sin(inFrameSpin.y), k = cos(inFrameSpin.y);
    vec2 r = vec2(k * c.x - s * c.y, s * c.x + k * c.y);
    p = center + (frame.camRight.xyz * r.x + frame.camUp.xyz * r.y) * inPosSize.w;
  }
  gl_Position = frame.viewProj * vec4(p, 1.0);
  int f = int(inFrameSpin.x + 0.5);
  vec2 cell = vec2(float(f % 2), float(f / 2));
  vUv = (cell + (c * 0.5 + 0.5)) * 0.5;
  vColor = inColor;
  vDist = gl_Position.w;
}
