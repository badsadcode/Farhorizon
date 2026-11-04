#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(push_constant) uniform Push {
  mat4 model;            // camera-relative model matrix
  vec4 baseColor;        // linear
  vec4 emissiveMetallic; // rgb emissive (linear), a metallic
  vec4 params;           // x roughness
} pc;

layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;

void main() {
  vec4 world = pc.model * vec4(inPos, 1.0);
  vPos = world.xyz;
  vNormal = normalize(transpose(inverse(mat3(pc.model))) * inNormal);
  vUv = inUv;
  gl_Position = frame.viewProj * world;
}
