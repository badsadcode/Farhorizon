#version 450
// Removes the per-pixel sample rotation of ssao.frag: a box over the noise pattern's footprint.
layout(location = 0) in vec2 vUv;
layout(location = 0) out float outAo;
layout(set = 0, binding = 0) uniform sampler2D aoTex;
layout(push_constant) uniform Push {
  vec4 texel;  // xy 1/size
} pc;

void main() {
  float sum = 0.0;
  for (int y = -2; y <= 1; ++y)
    for (int x = -2; x <= 1; ++x) sum += texture(aoTex, vUv + (vec2(x, y) + 0.5) * pc.texel.xy).r;
  outAo = sum / 16.0;
}
