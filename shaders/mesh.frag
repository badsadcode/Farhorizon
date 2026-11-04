#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D baseColorTex;

layout(push_constant) uniform Push {
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
  // Soft fill light (wrapped diffuse) and rim light keep silhouettes readable on the shadow side.
  float wrap = clamp((dot(N, frame.fillDir.xyz) + 0.4) / 1.4, 0.0, 1.0);
  vec3 fill = frame.fillColor.rgb * wrap * (diffuseColor + f0 * 0.5);
  float rim = pow(1.0 - NoV, max(frame.rimColorPower.a, 0.5));
  vec3 rimLight = frame.rimColorPower.rgb * rim * (1.0 - 0.6 * NoL);
  // Point lights (interior lamps, mining flares): smooth windowed inverse-square falloff.
  vec3 local = vec3(0.0);
  int count = int(frame.lightCount.x);
  for (int i = 0; i < 8; ++i) {
    if (i >= count) break;
    vec3 Lp = frame.pointPosRange[i].xyz - vPos;
    float dist = length(Lp);
    float range = frame.pointPosRange[i].w;
    if (dist >= range) continue;
    vec3 Ld = Lp / max(dist, 1e-3);
    float win = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
    float atten = win * win / (dist * dist + 1.0);
    float nl = max(dot(N, Ld), 0.0);
    vec3 Hp = normalize(Ld + V);
    vec3 Fp = F_Schlick(max(dot(Ld, Hp), 0.0), f0);
    vec3 specP = D_GGX(max(dot(N, Hp), 0.0), a) * V_SmithGGXCorrelated(NoV, nl, a) * Fp;
    local += (diffuseColor / PI * (1.0 - Fp) + specP) * frame.pointColor[i].rgb * nl * atten;
  }
  vec3 color = direct + local + fill + rimLight + ambient * diffuseColor + ambientSpec + pc.emissiveMetallic.rgb;

  outColor = vec4(applyFog(color, length(vPos)), base.a);  // linear HDR radiance
}
