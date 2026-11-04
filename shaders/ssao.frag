#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Ambient occlusion at half resolution from the depth prepass (screen-space obscurance after
// McGuire et al. 2011). Only depth is needed, so forward shading can use the result directly.
layout(location = 0) in vec2 vUv;
layout(location = 0) out float outAo;
layout(set = 1, binding = 0) uniform sampler2D depthTex;
layout(push_constant) uniform Push {
  vec4 params;  // x radius (m), y intensity, z world radius growth per metre of distance
} pc;

const int kSamples = 14;
const float kTau = 6.2831853;

vec3 positionAt(vec2 uv) {
  float d = texture(depthTex, uv).r;
  vec4 p = frame.invViewProj * vec4(uv * 2.0 - 1.0, d, 1.0);
  return p.xyz / p.w;
}

float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
  float d0 = texture(depthTex, vUv).r;
  if (d0 <= 0.0) {  // sky (reversed-Z: 0 is infinitely far)
    outAo = 1.0;
    return;
  }
  vec2 texel = frame.viewport.zw * 2.0;
  vec3 P = positionAt(vUv);
  // The shorter difference on each axis avoids false normals along depth edges.
  vec3 dx0 = positionAt(vUv + vec2(texel.x, 0)) - P, dx1 = P - positionAt(vUv - vec2(texel.x, 0));
  vec3 dy0 = positionAt(vUv + vec2(0, texel.y)) - P, dy1 = P - positionAt(vUv - vec2(0, texel.y));
  vec3 dx = dot(dx0, dx0) < dot(dx1, dx1) ? dx0 : dx1;
  vec3 dy = dot(dy0, dy0) < dot(dy1, dy1) ? dy0 : dy1;
  vec3 N = normalize(cross(dx, dy));
  float dist = length(P);
  if (dot(N, P) > 0.0) N = -N;

  float radius = pc.params.x + pc.params.z * dist;
  // Projected radius in UV units (viewProj[1][1] is the vertical focal scale).
  float uvRadius = radius * abs(frame.viewProj[1][1]) * 0.5 / max(dist, 0.05);
  uvRadius = min(uvRadius, 0.12);
  float angle = ign(gl_FragCoord.xy) * kTau;
  float sum = 0.0;
  for (int i = 0; i < kSamples; ++i) {
    float t = (float(i) + 0.5) / float(kSamples);
    float a = angle + t * 7.0 * kTau;
    vec2 offset = vec2(cos(a), sin(a)) * t * uvRadius;
    offset.x *= frame.viewport.y * frame.viewport.z;  // keep the disc round on screen
    vec3 v = positionAt(vUv + offset) - P;
    float vv = dot(v, v);
    float falloff = clamp(1.0 - vv / (radius * radius), 0.0, 1.0);
    sum += falloff * max(dot(v, N) - 0.015 * dist * 0.1 - 0.004, 0.0) / (vv + 0.0005);
  }
  float ao = 1.0 - 2.0 * pc.params.y * sum / float(kSamples);
  outAo = clamp(ao, 0.0, 1.0);
}
