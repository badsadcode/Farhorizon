#version 450
// Depth-only: one sun shadow cascade. Instance matrices are camera-relative.
layout(location = 0) in vec3 inPos;
layout(location = 3) in vec4 inModel0;
layout(location = 4) in vec4 inModel1;
layout(location = 5) in vec4 inModel2;
layout(location = 6) in vec4 inModel3;
layout(push_constant) uniform Push {
  mat4 lightViewProj;
} pc;

void main() { gl_Position = pc.lightViewProj * mat4(inModel0, inModel1, inModel2, inModel3) * vec4(inPos, 1.0); }
