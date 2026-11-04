#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Turbulent gas along a ribbon: the noise is anchored to distance along the trail, so the billows
// stay with the gas as it leaves the ship, and time makes them churn.
layout(location = 0) in vec4 vColor;
layout(location = 1) in float vU;
layout(location = 2) in float vV;
layout(location = 3) in float vSeed;
layout(location = 4) in float vDist;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D depthTex;
layout(push_constant) uniform Push {
  vec4 params;  // x near plane (m)
} pc;

float hash(vec2 p) {
  vec3 q = fract(vec3(p.xyx) * 0.1031);
  q += dot(q, q.yzx + 33.33);
  return fract((q.x + q.y) * q.z);
}
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
  float v = 0.0, a = 0.5;
  for (int i = 0; i < 4; ++i) {
    v += a * noise(p);
    p = p * 2.07 + vec2(5.3, 1.9);
    a *= 0.5;
  }
  return v / 0.9375;
}

void main() {
  float t = frame.ambientSkyTime.w;
  float x = abs(vU);
  // Billows change the local width, and the edges wander, so the outline is never a straight tube.
  float big = fbm(vec2(vV * 0.09 - t * 1.1 + vSeed, vSeed * 3.0));
  float wander = (noise(vec2(vV * 0.16 - t * 1.7, vSeed + 9.0)) - 0.5) * 0.35;
  float xw = abs(vU + wander);
  float w = 0.34 + 0.46 * big;
  float body = exp(-xw * xw / (w * w) * 2.4);
  float wisps = fbm(vec2(vV * 0.42 - t * 2.4 + vSeed, vU * 1.8 + vSeed));
  body *= 0.55 + 0.9 * wisps;
  float core = exp(-xw * xw * 40.0) * (0.3 + 0.5 * big);  // hotter thread down the middle
  body *= 1.0 - smoothstep(0.72, 1.0, x);                 // always zero at the ribbon's edge

  float d = texelFetch(depthTex, ivec2(gl_FragCoord.xy), 0).r;
  float scene = d > 0.0 ? pc.params.x / d : 1e9;
  float soft = clamp((scene - vDist) / 1.5, 0.0, 1.0);
  vec3 hot = mix(vColor.rgb, vec3(dot(vColor.rgb, vec3(0.45))), 0.55);
  outColor = vec4((vColor.rgb * body + hot * core) * soft, 0.0);  // additive
}
