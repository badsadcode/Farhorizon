#version 450
// Depth-only: one face of a point light's cube shadow map. The face mapping is written out from the
// Vulkan cube map table (sc, tc, ma), so a direction sampled later lands exactly on this texel.
layout(location = 0) in vec3 inPos;
layout(location = 3) in vec4 inModel0;
layout(location = 4) in vec4 inModel1;
layout(location = 5) in vec4 inModel2;
layout(location = 6) in vec4 inModel3;
layout(push_constant) uniform Push {
  vec4 lightPosNear;  // camera-relative position, near plane (m)
  vec4 farFace;       // x far plane (m), y cube face 0..5
} pc;

void main() {
  vec3 v = (mat4(inModel0, inModel1, inModel2, inModel3) * vec4(inPos, 1.0)).xyz - pc.lightPosNear.xyz;
  int face = int(pc.farFace.y);
  float sc, tc, ma;
  if (face == 0) { sc = -v.z; tc = -v.y; ma = v.x; }
  else if (face == 1) { sc = v.z; tc = -v.y; ma = -v.x; }
  else if (face == 2) { sc = v.x; tc = v.z; ma = v.y; }
  else if (face == 3) { sc = v.x; tc = -v.z; ma = -v.y; }
  else if (face == 4) { sc = v.x; tc = -v.y; ma = v.z; }
  else { sc = -v.x; tc = -v.y; ma = -v.z; }
  float n = pc.lightPosNear.w, f = pc.farFace.x;
  gl_Position = vec4(sc, tc, ma * f / (f - n) - f * n / (f - n), ma);
}
