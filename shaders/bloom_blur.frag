#version 450
// Separable 9-tap Gaussian (linear-sampling, 5 fetches).
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D src;
layout(push_constant) uniform Push { vec4 params; } pc;  // xy = direction * texel size * radius
void main() {
  const float w0 = 0.2270270270, w1 = 0.3162162162, w2 = 0.0702702703;
  const float o1 = 1.3846153846, o2 = 3.2307692308;
  vec2 d = pc.params.xy;
  vec3 c = texture(src, vUv).rgb * w0;
  c += (texture(src, vUv + d * o1).rgb + texture(src, vUv - d * o1).rgb) * w1;
  c += (texture(src, vUv + d * o2).rgb + texture(src, vUv - d * o2).rgb) * w2;
  outColor = vec4(c, 1.0);
}
