#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

float hash(vec3 p) {
  p = fract(p * 0.3183099 + 0.1);
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash(i + vec3(0, 0, 0)), hash(i + vec3(1, 0, 0)), f.x),
                 mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
             mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),
                 mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm(vec3 p) {
  float v = 0.0, a = 0.5;
  for (int i = 0; i < 5; ++i) { v += a * noise(p); p *= 2.03; a *= 0.5; }
  return v;
}

void main() {
  // Reconstruct the view direction (reversed-Z: ndc z = 1 is the near plane).
  vec4 h = frame.invViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(h.xyz / h.w);

  // Deep background with a faint galactic band and restrained procedural dust.
  float band = 1.0 - abs(dot(dir, galacticNormal()));
  band = pow(band, 6.0);
  float dust = fbm(dir * 3.0);
  float wisps = fbm(dir * 7.0 + vec3(3.1, 1.7, 0.4));
  vec3 col = frame.skyBase.rgb;
  col += band * frame.skyBand.rgb * frame.skyBand.a * (0.55 + 0.9 * dust);
  float neb = smoothstep(0.45, 0.9, wisps) * (0.4 + band);
  col += neb * frame.skyNebula.rgb * frame.skyNebula.a;                   // data-driven nebula tint
  col *= 1.0 - 0.6 * band * smoothstep(0.55, 0.75, fbm(dir * 11.0));     // dark lanes

  // Star disc and glare.
  vec3 sunDir = frame.sunDirCos.xyz;
  float c = dot(dir, sunDir);
  float cosR = frame.sunDirCos.w;
  vec3 sun = frame.sunColorExp.rgb;
  float disc = smoothstep(cosR - 0.00002, cosR + 0.00002, c);
  float glare = frame.skyBase.a;
  col += disc * sun * 60.0 * glare;
  float ang = acos(clamp(c, -1.0, 1.0));
  col += sun * glare * (0.020 * exp(-ang * 9.0) + 0.06 * exp(-ang * 70.0) + 0.6 * exp(-ang * 600.0));

  outColor = vec4(col, 1.0);  // linear HDR radiance
}
