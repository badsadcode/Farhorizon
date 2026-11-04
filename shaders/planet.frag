#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Far-field planet impostor: ray/sphere intersection per pixel (scissored to the planet's
// screen bounds). Works in units of the planet radius so float precision holds at any range.
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Planet {
  vec4 centerRadius;  // camera-relative centre (m), radius (m)
  vec4 colorA;        // rgb, seed
  vec4 colorB;        // rgb, crater amount
  vec4 atmo;          // rgb, thickness (fraction of radius)
} pl;

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
  for (int i = 0; i < 6; ++i) { v += a * noise(p); p = p * 2.03 + vec3(1.7, 9.2, 3.1); a *= 0.5; }
  return v;
}
// Crater field: distance to random cell centres gives bowls with raised rims.
float craters(vec3 p) {
  vec3 i = floor(p), f = fract(p);
  float best = 1e9; float r = 0.0;
  for (int z = -1; z <= 1; ++z) for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
    vec3 g = vec3(x, y, z);
    vec3 o = vec3(hash(i + g), hash(i + g + 19.1), hash(i + g + 47.7));
    float d = length(g + o - f);
    if (d < best) { best = d; r = 0.25 + 0.3 * hash(i + g + 7.3); }
  }
  float t = best / r;
  return t < 1.0 ? (t * t - 1.0) * 0.6 + smoothstep(0.8, 1.0, t) * 0.5 : 0.0;
}

void main() {
  vec4 h = frame.invViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(h.xyz / h.w);
  float R = pl.centerRadius.w;
  vec3 c = pl.centerRadius.xyz / R;            // in planet radii
  float b = dot(c, dir);
  float cc = dot(c, c) - 1.0;
  float disc = b * b - cc;
  vec3 sunDir = frame.sunDirCos.xyz;
  vec3 sun = frame.sunColorExp.rgb;
  float thick = pl.atmo.w;

  if (disc >= 0.0 && b > 0.0) {
    float t = b - sqrt(disc);
    vec3 n = normalize(dir * t - c);
    float seed = pl.colorA.w;
    vec3 q = n * 3.0 + seed;
    float land = fbm(q);
    float detail = fbm(n * 24.0 + seed * 1.7);
    float crat = craters(n * 9.0 + seed) * pl.colorB.w + craters(n * 31.0 + seed) * pl.colorB.w * 0.5;
    vec3 albedo = mix(pl.colorB.rgb, pl.colorA.rgb, smoothstep(0.35, 0.65, land + 0.15 * detail));
    albedo *= 0.8 + 0.4 * detail + 0.35 * crat;
    float ndl = dot(n, sunDir);
    float lit = smoothstep(-0.05, 0.25, ndl) * max(ndl, 0.0) + 0.02 * smoothstep(-0.3, 0.0, ndl);
    vec3 col = albedo * sun * lit * 0.6;
    if (thick > 0.0) {  // limb scattering on the day side, a cool fringe at the terminator
      float rim = pow(1.0 - max(dot(n, -dir), 0.0), 3.0);
      col = mix(col, pl.atmo.rgb * sun * 0.6, rim * smoothstep(-0.2, 0.4, ndl) * 0.8);
      col += pl.atmo.rgb * sun * 0.06 * smoothstep(-0.25, 0.1, ndl) * (1.0 - smoothstep(0.1, 0.5, ndl));
    }
    outColor = vec4(col, 1.0);
    return;
  }
  if (thick > 0.0 && b > 0.0) {  // halo just outside the limb
    float d = sqrt(max(dot(c, c) - b * b, 0.0));
    float k = 1.0 - (d - 1.0) / thick;
    if (k > 0.0 && d >= 1.0) {
      vec3 closest = normalize(dir * b - c);
      float day = smoothstep(-0.3, 0.4, dot(closest, sunDir));
      float g = k * k * day;
      outColor = vec4(pl.atmo.rgb * sun * g * 0.5, g * 0.6);
      return;
    }
  }
  discard;
}
