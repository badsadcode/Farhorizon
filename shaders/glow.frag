#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec2 vCorner;
layout(location = 1) in vec3 vColor;
layout(location = 2) in float vDist;
layout(location = 3) in vec2 vTurbSeed;
layout(location = 0) out vec4 outColor;

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

void main() {
  float r2 = dot(vCorner, vCorner);
  if (r2 > 1.0) discard;
  float turb = vTurbSeed.x;
  float halo, core;
  if (turb > 0.0) {
    // Engines: the outline breathes and tongues of light wander around it. Noise is sampled on a
    // circle, so there is no seam, and each engine has its own seed so they never pulse in step.
    float t = frame.ambientSkyTime.w;
    float seed = vTurbSeed.y;
    float r = sqrt(r2);
    vec2 dir = vCorner / max(r, 1e-3);
    float shape = noise3(vec3(dir * 1.3 + seed, t * 1.6));
    float tongues = noise3(vec3(dir * 3.6 - seed, t * 3.1));
    float rd = r / (0.62 + 0.62 * mix(0.5, shape, turb));
    float pulse = 1.0 + turb * (0.12 * sin(t * 5.3 + seed) + 0.07 * sin(t * 13.1 + seed * 2.3));
    halo = exp(-rd * rd * 6.5) * (0.22 + 0.42 * mix(0.5, tongues, turb)) * pulse;
    halo *= 1.0 - smoothstep(0.7, 1.0, r);  // never shows the sprite's edge
    core = exp(-r2 * 90.0) * (0.9 + 0.4 * tongues) * pulse;
  } else {
    halo = exp(-r2 * 5.0);
    core = exp(-r2 * 40.0) * 3.0;
  }
  // Lamps keep a hot core through the fog but their halo scatters into it.
  float d = frame.fogColorDensity.a;
  float keep = d > 0.0 ? exp(-d * vDist * 0.6) : 1.0;
  outColor = vec4(vColor * (halo * (0.4 + 0.6 * keep) + core * keep), 1.0);
}
