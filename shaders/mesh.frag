#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D baseColorTex;

layout(push_constant) uniform Push {
  mat4 model;
  vec4 baseColor;
  vec4 emissiveMetallic;
  vec4 params;
} pc;

const float PI = 3.14159265;

float D_GGX(float NoH, float a) {
  float a2 = a * a;
  float d = NoH * NoH * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}
float V_SmithGGXCorrelated(float NoV, float NoL, float a) {
  float a2 = a * a;
  float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
  float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
  return 0.5 / max(gv + gl, 1e-5);
}
vec3 F_Schlick(float u, vec3 f0) { return f0 + (1.0 - f0) * pow(1.0 - u, 5.0); }

void main() {
  vec4 base = pc.baseColor * texture(baseColorTex, vUv);
  float metallic = clamp(pc.emissiveMetallic.a, 0.0, 1.0);
  float rough = clamp(pc.params.x, 0.045, 1.0);
  float a = rough * rough;

  vec3 N = normalize(vNormal);
  vec3 V = normalize(-vPos);          // camera at origin in render space
  if (dot(N, V) < 0.0) N = -N;        // tolerate thin/double-sided geometry
  vec3 L = frame.sunDirCos.xyz;
  vec3 H = normalize(L + V);
  float NoV = max(dot(N, V), 1e-4);
  float NoL = max(dot(N, L), 0.0);
  float NoH = max(dot(N, H), 0.0);
  float LoH = max(dot(L, H), 0.0);

  vec3 diffuseColor = base.rgb * (1.0 - metallic);
  vec3 f0 = mix(vec3(0.04), base.rgb, metallic);
  vec3 F = F_Schlick(LoH, f0);
  vec3 spec = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F;
  vec3 diff = diffuseColor / PI;
  vec3 direct = (diff * (1.0 - F) + spec) * frame.sunColorExp.rgb * NoL;

  // Hemispheric ambient oriented toward the star (lit side vs. shadow side), plus a
  // crude specular ambient so metals do not go black. Real IBL comes in Stage 1 follow-up.
  float hemi = dot(N, L) * 0.5 + 0.5;
  vec3 ambient = mix(frame.ambientGround.rgb, frame.ambientSkyTime.rgb, hemi);
  vec3 ambientSpec = F_Schlick(NoV, f0) * ambient * (1.0 - rough * 0.7);
  vec3 color = direct + ambient * diffuseColor + ambientSpec + pc.emissiveMetallic.rgb;

  outColor = vec4(tonemapACES(color * frame.sunColorExp.w), base.a);
}
