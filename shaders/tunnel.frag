#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Travel drive tunnel: streaks and gas on the inside of a slowly twisting cylinder around the flight
// path, rushing past the camera. Added behind the ship, with the destination left clear at its centre.
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;
layout(set = 1, binding = 0) uniform sampler2D depthTex;
layout(push_constant) uniform Push {
  vec4 forwardAmount;  // xyz flight direction, w strength 0..1
  vec4 colorA;         // rgb main colour
  vec4 colorB;         // rgb second colour
  vec4 unused;
} pc;

float hash1(float n) { return fract(sin(n * 12.9898) * 43758.5453); }
float hash3(vec3 p) {
  p = fract(p * 0.3183099 + 0.1);
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise3(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash3(i), hash3(i + vec3(1, 0, 0)), f.x), mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), f.x), f.y),
             mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), f.x), mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float noise1(float x, float seed) {
  float i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  return mix(hash1(i + seed), hash1(i + 1.0 + seed), f);
}

// One ring of streaks. Each angular cell has its own width, speed and brightness.
float streaks(float ang, float z, float t, float cells, float layer) {
  float x = ang * cells;
  float cell = mod(floor(x), cells);
  float id = cell + layer * 311.0;
  float r = hash1(id + 3.1);
  float halfWidth = mix(0.05, 0.42, r * r * r * r);  // mostly thin, some wide
  float across = 1.0 - smoothstep(halfWidth * 0.35, halfWidth, abs(fract(x) - 0.5));
  float speed = 5.0 + 9.0 * hash1(id + 7.7);
  float length_ = mix(0.05, 0.22, hash1(id + 1.3));
  float along = smoothstep(0.62, 0.86, noise1(z * length_ + t * speed, id * 1.7));
  return across * along * (0.45 + 0.9 * hash1(id + 5.5));
}

void main() {
  float amount = pc.forwardAmount.w;
  float depth = texelFetch(depthTex, ivec2(gl_FragCoord.xy), 0).r;
  if (amount <= 0.001 || depth > 0.0) discard;  // geometry stays in front

  vec4 h = frame.invViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(h.xyz / h.w);
  vec3 f = pc.forwardAmount.xyz;
  vec3 s = normalize(cross(abs(f.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0), f));
  vec3 u = cross(f, s);
  float x = dot(dir, s), y = dot(dir, u);
  float rr = max(length(vec2(x, y)), 1e-3);
  float axial = dot(dir, f);
  float z = axial / rr;            // distance along a cylinder of radius 1
  float t = frame.ambientSkyTime.w;
  float twist = z * 0.035 + t * 0.03;
  float theta = atan(y, x) + twist * 6.2831853;
  float ang = fract(theta / 6.2831853);

  float mask = smoothstep(0.05, 0.42, rr);  // the destination stays clear
  float lines = streaks(ang, z, t, 42.0, 1.0) * 1.2 + streaks(ang, z, t * 1.3, 96.0, 2.0) * 0.8 * smoothstep(0.15, 0.5, rr) +
                streaks(ang, z, t * 1.7, 190.0, 3.0) * 0.5 * smoothstep(0.3, 0.65, rr);
  // Sampled on a circle, so the gas has no seam around the tunnel.
  vec3 q = vec3(cos(theta) * 2.2, sin(theta) * 2.2, z * 0.22 + t * 1.5);
  float gas = noise3(q) * 0.6 + noise3(q * 2.1 + 4.0) * 0.4;
  gas = smoothstep(0.34, 0.8, gas);
  float tone = noise3(vec3(cos(theta) * 1.2, sin(theta) * 1.2, z * 0.06 + t * 0.35) + 9.0);
  vec3 tint = mix(pc.colorA.rgb, pc.colorB.rgb, smoothstep(0.35, 0.65, tone));

  vec3 col = tint * (gas * gas * 0.07 * (0.25 + rr) + lines * 0.2) * mask;
  col += pc.colorA.rgb * exp(-rr * rr * 90.0) * 0.1 * smoothstep(0.0, 0.5, axial);  // light at the end of it
  outColor = vec4(col * amount, 1.0);
}
