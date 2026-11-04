#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// One volumetric gas cloud (nebula): ray marched through a sphere in the world, stopping at whatever
// geometry is in front of it, so ships can fly through and asteroids can sit inside. Lit by the sun
// with one shadow sample per step. Noise lives in the cloud's own space, so it never swims.
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;
layout(set = 1, binding = 0) uniform sampler2D depthTex;
layout(push_constant) uniform Push {
  vec4 centerRadius;  // camera-relative centre, radius (m)
  vec4 colorDensity;  // rgb first colour, a optical depth per radius at full density
  vec4 colorSeed;     // rgb second colour, a seed
  vec4 params;        // x near plane (m)
} pc;

const int kSteps = 32;

float hash(vec3 p) {
  p = fract(p * 0.3183099 + 0.1);
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x), mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
             mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x), mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm(vec3 p) {
  float v = 0.0, a = 0.5;
  for (int i = 0; i < 4; ++i) {
    v += a * noise(p);
    p = p * 2.03 + vec3(3.1, 1.7, 4.3);
    a *= 0.5;
  }
  return v / 0.9375;
}
// 0 outside the gas, rising inside. p is in cloud space: the unit sphere.
float gas(vec3 p, float seed) {
  float r2 = dot(p, p);
  if (r2 >= 1.0) return 0.0;
  vec3 q = p * 2.4 + seed;
  vec3 warp = vec3(noise(q * 0.9), noise(q * 0.9 + 5.2), noise(q * 0.9 + 9.7));
  float n = fbm(q + warp * 1.7);
  float edge = 1.0 - r2;  // thins toward the outside so the sphere never shows
  return clamp((n - 0.56 + 0.3 * edge) * 2.6, 0.0, 1.0) * edge;
}
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
  vec4 h = frame.invViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(h.xyz / h.w);
  vec3 c = pc.centerRadius.xyz;
  float R = pc.centerRadius.w;
  float b = dot(dir, c);
  float disc = b * b - dot(c, c) + R * R;
  if (disc <= 0.0) discard;
  float s = sqrt(disc);
  float t0 = max(b - s, 0.0), t1 = b + s;
  if (t1 <= 0.0) discard;
  // Stop at geometry (reversed-Z depth is near / view distance).
  // This pass runs at half resolution; the depth buffer is full size.
  float d = texelFetch(depthTex, ivec2(gl_FragCoord.xy * 2.0), 0).r;
  if (d > 0.0) t1 = min(t1, pc.params.x / d / max(dot(dir, frame.viewForward.xyz), 1e-3));
  if (t1 <= t0) discard;

  float seed = pc.colorSeed.a;
  float dt = (t1 - t0) / float(kSteps);
  float t = t0 + dt * ign(gl_FragCoord.xy);
  float dtUnit = dt / R;
  float sigma = pc.colorDensity.a;
  vec3 L = frame.sunDirCos.xyz;
  float sun = dot(frame.sunColorExp.rgb, vec3(0.33));
  vec3 sunTint = frame.sunColorExp.rgb / max(sun, 1e-3);
  vec3 sum = vec3(0.0);
  float T = 1.0;
  for (int i = 0; i < kSteps; ++i, t += dt) {
    vec3 p = (dir * t - c) / R;
    float g = gas(p, seed);
    if (g <= 0.001) continue;
    float lit = g > 0.04 ? exp(-gas(p + L * 0.18, seed) * sigma * 0.5) : 1.0;  // thin gas is simply lit
    float tone = noise(p * 1.6 + seed * 1.3);
    vec3 albedo = mix(pc.colorDensity.rgb, pc.colorSeed.rgb, smoothstep(0.3, 0.7, tone));
    vec3 light = albedo * (0.3 + 0.7 * lit * sunTint);  // faint glow of its own, plus sunlight
    float step_ = exp(-g * sigma * dtUnit);
    sum += T * (1.0 - step_) * light;
    T *= step_;
    if (T < 0.02) break;
  }
  outColor = vec4(sum, 1.0 - T);  // premultiplied
}
