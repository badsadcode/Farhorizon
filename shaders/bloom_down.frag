#version 450
// Bright pass + 4x downsample into the bloom chain. Soft-knee threshold keeps it restrained.
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D src;
layout(push_constant) uniform Push { vec4 params; } pc;  // x threshold, y knee, zw source texel size
void main() {
  vec2 t = pc.params.zw;
  vec3 c = texture(src, vUv + t * vec2(-1, -1)).rgb + texture(src, vUv + t * vec2(1, -1)).rgb +
           texture(src, vUv + t * vec2(-1, 1)).rgb + texture(src, vUv + t * vec2(1, 1)).rgb;
  c *= 0.25;
  float lum = max(c.r, max(c.g, c.b));
  float knee = pc.params.y;
  float soft = clamp(lum - pc.params.x + knee, 0.0, 2.0 * knee);
  soft = soft * soft / (4.0 * knee + 1e-4);
  float contrib = max(soft, lum - pc.params.x) / max(lum, 1e-4);
  outColor = vec4(min(c * contrib, vec3(16.0)), 1.0);  // clamp fireflies (sun disc)
}
