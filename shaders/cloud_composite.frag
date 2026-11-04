#version 450
// Lays the half-resolution volumetric clouds over the scene. The small box filter removes the
// per-pixel ray offset of cloud.frag, and bilinear upsampling keeps the gas soft.
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D cloudTex;
layout(push_constant) uniform Push {
  vec4 texel;  // xy 1/size of the cloud target
} pc;

void main() {
  vec4 sum = vec4(0.0);
  for (int y = -2; y <= 1; ++y)
    for (int x = -2; x <= 1; ++x) sum += texture(cloudTex, vUv + (vec2(x, y) + 0.5) * pc.texel.xy);
  outColor = sum / 16.0;  // premultiplied
}
