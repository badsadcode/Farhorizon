#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec4 inDirSize;   // xyz unit direction, w = size in pixels
layout(location = 1) in vec4 inColor;     // linear rgb * brightness, a unused
layout(location = 0) out vec2 vCorner;
layout(location = 1) out vec3 vColor;

void main() {
  const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
  vec2 corner = corners[gl_VertexIndex];
  // w = 0: a direction at infinity. With the reversed-Z infinite projection depth becomes 0.
  vec4 clip = frame.viewProj * vec4(inDirSize.xyz, 0.0);
  clip.xy += corner * inDirSize.w * frame.viewport.zw * clip.w;
  gl_Position = clip;
  vCorner = corner;
  vColor = inColor.rgb;
}
