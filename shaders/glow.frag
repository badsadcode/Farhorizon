#version 450
layout(location = 0) in vec2 vCorner;
layout(location = 1) in vec3 vColor;
layout(location = 0) out vec4 outColor;
void main() {
  float r2 = dot(vCorner, vCorner);
  if (r2 > 1.0) discard;
  float halo = exp(-r2 * 5.0);
  float core = exp(-r2 * 40.0) * 3.0;
  outColor = vec4(vColor * (halo + core), 1.0);
}
