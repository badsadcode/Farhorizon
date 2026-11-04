#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
// Per-instance camera-relative model matrix (uniform scale assumed for normals).
layout(location = 3) in vec4 inModel0;
layout(location = 4) in vec4 inModel1;
layout(location = 5) in vec4 inModel2;
layout(location = 6) in vec4 inModel3;

layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec3 vObjPos;     // object space in metres, for surface detail
layout(location = 4) out vec3 vObjNormal;

// The depth prepass and the shaded pass use this shader, and must produce identical depth.
invariant gl_Position;

void main() {
  mat4 model = mat4(inModel0, inModel1, inModel2, inModel3);
  vec4 world = model * vec4(inPos, 1.0);
  vPos = world.xyz;
  vNormal = normalize(mat3(model) * inNormal);
  vUv = inUv;
  vObjPos = inPos * length(model[0].xyz);
  vObjNormal = inNormal;
  gl_Position = frame.viewProj * world;
}
