#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Continuous exhaust ribbons, built on the CPU facing the camera.
layout(location = 0) in vec4 inPosU;   // camera-relative position, u across the ribbon (-1..1)
layout(location = 1) in vec4 inColor;  // linear HDR
layout(location = 2) in vec4 inVSeed;  // x metres along the trail, y seed
layout(location = 0) out vec4 vColor;
layout(location = 1) out float vU;
layout(location = 2) out float vV;
layout(location = 3) out float vSeed;
layout(location = 4) out float vDist;

void main() {
  gl_Position = frame.viewProj * vec4(inPosU.xyz, 1.0);
  vColor = inColor;
  vU = inPosU.w;
  vV = inVSeed.x;
  vSeed = inVSeed.y;
  vDist = gl_Position.w;
}
