#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec3 vObjPos;
layout(location = 4) in vec3 vObjNormal;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D baseColorTex;
layout(set = 1, binding = 1) uniform sampler2D normalTex;
layout(set = 1, binding = 2) uniform sampler2D metalRoughTex;  // G roughness, B metallic
layout(set = 1, binding = 3) uniform sampler2D emissiveTex;
layout(set = 1, binding = 4) uniform sampler2D occlusionTex;   // R
layout(set = 2, binding = 0) uniform sampler2DArrayShadow sunShadowMap;
layout(set = 2, binding = 1) uniform samplerCubeArrayShadow pointShadowMap;
layout(set = 2, binding = 2) uniform sampler2D aoTex;
layout(set = 2, binding = 3) uniform samplerCube envMap;
layout(set = 2, binding = 4) uniform samplerCubeArray probeArray;

layout(push_constant) uniform Push {
  vec4 baseColor;
  vec4 emissiveMetallic;
  vec4 params;  // x roughness, y panel lines 0..1, z has a base colour texture, w has a normal map
} pc;

const float PI = 3.14159265;
const float kPointNear = 0.15;  // matches the shadow pass (Renderer)

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
// Split-sum environment BRDF, analytic approximation (Karis 2014).
vec3 envBrdfApprox(vec3 f0, float rough, float NoV) {
  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
  const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 r = rough * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
  vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
  return f0 * ab.x + ab.y;
}

// Reflections: the live view around the camera, or a placed probe for surfaces inside its box. A probe's
// lookup is corrected for the surface's position: follow the ray to the box wall it hits, and look that
// way from where the snapshot was taken. `blur` is 0 (sharp) to 1 (the room's average colour).
vec3 sampleEnv(vec3 dir, float blur, float liveWeight) {
  vec3 live = textureLod(envMap, dir, blur * (frame.lighting.w - 1.0)).rgb * liveWeight;
  int count = int(frame.viewOrigin.w);
  int best = -1;
  float bestWeight = 0.0;
  for (int i = 0; i < count; ++i) {
    vec3 inside = min(vPos - frame.probeMin[i].xyz, frame.probeMax[i].xyz - vPos);
    // Full strength up to the box, fading over 60 cm beyond it: floors and walls sit right on the box.
    float weight = clamp(min(inside.x, min(inside.y, inside.z)) / 0.6 + 1.0, 0.0, 1.0);
    if (weight > bestWeight) {
      bestWeight = weight;
      best = i;
    }
  }
  if (best < 0) return live;
  vec3 a = (frame.probeMax[best].xyz - vPos) / dir, b = (frame.probeMin[best].xyz - vPos) / dir;
  vec3 far_ = max(a, b);
  vec3 hit = vPos + dir * min(far_.x, min(far_.y, far_.z));
  vec3 placed = textureLod(probeArray, vec4(hit - frame.probePos[best].xyz, float(best)), blur * 9.0).rgb;
  return mix(live, placed, bestWeight);
}

// ---- Procedural surface detail: the models have no textures yet ------------------------------
float hash31(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}
float valueNoise(vec3 p) {
  vec3 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash31(i), hash31(i + vec3(1, 0, 0)), f.x), mix(hash31(i + vec3(0, 1, 0)), hash31(i + vec3(1, 1, 0)), f.x), f.y),
             mix(mix(hash31(i + vec3(0, 0, 1)), hash31(i + vec3(1, 0, 1)), f.x), mix(hash31(i + vec3(0, 1, 1)), hash31(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm(vec3 p) {
  float sum = 0.0, amp = 0.5;
  for (int i = 0; i < 4; ++i) {
    sum += amp * valueNoise(p);
    p = p * 2.03 + vec3(17.1, 3.7, 9.2);
    amp *= 0.5;
  }
  return sum / 0.9375;
}
// Distance to the nearest panel seam, on the two axes across the dominant face direction.
float panelSeam(vec3 p, vec3 n, float cell) {
  vec3 g = abs(fract(p / cell + 0.5) - 0.5) * cell;
  vec3 an = abs(n);
  if (an.x >= an.y && an.x >= an.z) return min(g.y, g.z);
  if (an.y >= an.z) return min(g.x, g.z);
  return min(g.x, g.y);
}

// ---- Shadows ----------------------------------------------------------------------------------
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

const vec2 kDisc[8] = vec2[](vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621),
                             vec2(0.962, -0.195), vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893));

float sunShadow(vec3 N, float NoL, float noise) {
  if (frame.shadowSplits.w <= 0.0) return 1.0;
  float depth = dot(vPos, frame.viewForward.xyz);
  int c = depth < frame.shadowSplits.x ? 0 : depth < frame.shadowSplits.y ? 1 : 2;
  if (depth >= frame.shadowSplits.z) return 1.0;
  float texelWorld = c == 0 ? frame.shadowInfo.x : c == 1 ? frame.shadowInfo.y : frame.shadowInfo.z;
  // Offset along the normal by more on grazing surfaces: it removes acne without detaching shadows.
  vec3 P = vPos + N * texelWorld * (1.2 + 2.5 * (1.0 - NoL));
  vec4 q = frame.sunShadow[c] * vec4(P, 1.0);
  float bias = 0.00002;
  float a = noise * 6.2831853, ca = cos(a), sa = sin(a);
  float texel = 1.0 / 2048.0;
  float sum = 0.0;
  for (int i = 0; i < 8; ++i) {
    vec2 o = vec2(ca * kDisc[i].x - sa * kDisc[i].y, sa * kDisc[i].x + ca * kDisc[i].y) * texel * 1.8;
    sum += texture(sunShadowMap, vec4(q.xy * 0.5 + 0.5 + o, float(c), q.z - bias));
  }
  float lit = sum / 8.0;
  return mix(lit, 1.0, smoothstep(frame.shadowSplits.z * 0.85, frame.shadowSplits.z, depth));
}

float pointShadow(int i, vec3 P, vec3 lightPos, float range, float noise) {
  vec3 v = P - lightPos;
  vec3 av = abs(v);
  float ma = max(av.x, max(av.y, av.z));
  float compareMa = max(ma - 0.03 - 0.006 * ma, kPointNear + 0.001);
  float n = kPointNear, f = range;
  float z = f / (f - n) - f * n / ((f - n) * compareMa);
  // Small jitter across the light direction softens the edge like a lamp with some size.
  vec3 t = normalize(cross(v, abs(v.y) < 0.9 * length(v) ? vec3(0, 1, 0) : vec3(1, 0, 0)));
  vec3 b = cross(normalize(v), t);
  float a = noise * 6.2831853, ca = cos(a), sa = sin(a);
  float spread = 0.012 * ma + 0.008;
  float sum = 0.0;
  for (int k = 0; k < 4; ++k) {
    vec2 d = kDisc[k * 2];
    vec2 o = vec2(ca * d.x - sa * d.y, sa * d.x + ca * d.y) * spread;
    sum += texture(pointShadowMap, vec4(v + t * o.x + b * o.y, float(i)), z);
  }
  return sum / 4.0;
}

void main() {
  vec4 base = pc.baseColor * texture(baseColorTex, vUv);
  vec4 mr = texture(metalRoughTex, vUv);
  float metallic = clamp(pc.emissiveMetallic.a * mr.b, 0.0, 1.0);
  float rough = pc.params.x * mr.g;
  vec3 emissive = pc.emissiveMetallic.rgb * texture(emissiveTex, vUv).rgb;

  vec3 N = normalize(vNormal);
  vec3 V = normalize(frame.viewOrigin.xyz - vPos);  // the camera is at the origin, except for probes
  if (dot(N, V) < 0.0) N = -N;        // tolerate thin/double-sided geometry
  const vec3 Ng = N;                  // before the bump: shadow lookups use the true surface
  if (pc.params.w > 0.5) {
    // Normal map without stored tangents: the frame comes from screen derivatives (Schueler 2013).
    vec3 tn = texture(normalTex, vUv).xyz * 2.0 - 1.0;
    vec3 dp1 = dFdx(vPos), dp2 = dFdy(vPos);
    vec2 duv1 = dFdx(vUv), duv2 = dFdy(vUv);
    vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    if (invmax < 1e12) N = normalize(mat3(T * invmax, B * invmax, N) * tn);
  }

  // Worn, dusty, used: mottled albedo and roughness, panel seams on built surfaces, fine bump.
  // Real textures win: procedural detail is only for models that have none.
  float detail = pc.params.z > 0.5 ? 0.0 : frame.lighting.z * (dot(pc.emissiveMetallic.rgb, vec3(1.0)) > 0.01 ? 0.0 : 1.0);
  float seamShade = 0.0;
  if (detail > 0.0) {
    float px = length(fwidth(vObjPos));
    float large = fbm(vObjPos * 0.23);
    float streak = fbm(vec3(vObjPos.x * 1.7, vObjPos.y * 0.18, vObjPos.z * 1.7));
    float grime = clamp(large * 0.6 + streak * 0.55 - 0.08, 0.0, 1.0);
    float seamA = panelSeam(vObjPos, vObjNormal, 2.4);
    float seamB = panelSeam(vObjPos + vec3(0.6, 1.1, 0.3), vObjNormal, 7.2);
    // Filtered lines: thinner than a pixel means fainter, never dotted.
    float aa = max(px, 0.002);
    float lineA = (1.0 - smoothstep(0.009 - aa, 0.009 + aa, seamA)) * min(1.0, 0.018 / aa);
    float lineB = (1.0 - smoothstep(0.016 - aa, 0.016 + aa, seamB)) * min(1.0, 0.032 / aa);
    float groove = pc.params.y * max(lineA * 0.75, lineB);
    // Each panel gets its own slightly different tone.
    float tone = hash31(floor((vObjPos + 37.0) / vec3(2.4)) * step(abs(vObjNormal), vec3(0.7)));
    float toned = mix(1.0, 0.86 + 0.28 * tone, pc.params.y * detail);
    base.rgb *= toned * mix(1.0, 0.74 + 0.38 * grime, detail) * (1.0 - 0.5 * groove * detail);
    rough = max(rough + detail * (0.42 - grime) * 0.42, 0.3 * detail);  // worn things are never mirrors
    seamShade = groove * detail;
    // Surface-gradient bump (no tangents needed).
    float fine = valueNoise(vObjPos * 6.0) * 0.5 + valueNoise(vObjPos * 19.0) * 0.25;
    float h = detail * (fine * 0.006 * (1.0 - smoothstep(0.01, 0.1, px)) + large * 0.012 * rough * rough - groove * 0.004);
    vec3 dpx = dFdx(vPos), dpy = dFdy(vPos);
    vec3 r1 = cross(dpy, N), r2 = cross(N, dpx);
    float det = dot(dpx, r1);
    if (abs(det) > 1e-12) {
      vec3 g = sign(det) * (dFdx(h) * r1 + dFdy(h) * r2);
      N = normalize(abs(det) * N - g);
    }
  }
  rough = clamp(rough, 0.045, 1.0);
  float a = rough * rough;

  vec3 L = frame.sunDirCos.xyz;
  vec3 H = normalize(L + V);
  float NoV = max(dot(N, V), 1e-4);
  float NoL = max(dot(N, L), 0.0);
  float NoH = max(dot(N, H), 0.0);
  float LoH = max(dot(L, H), 0.0);

  float noise = ign(gl_FragCoord.xy);
  float ao = frame.lighting.x > 0.0 ? texture(aoTex, gl_FragCoord.xy * frame.viewport.zw).r : 1.0;
  ao *= 1.0 - seamShade * 0.35;
  ao *= texture(occlusionTex, vUv).r;

  vec3 diffuseColor = base.rgb * (1.0 - metallic);
  vec3 f0 = mix(vec3(0.04), base.rgb, metallic);
  vec3 F = F_Schlick(LoH, f0);
  vec3 spec = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F;
  vec3 diff = diffuseColor / PI;
  float sun = NoL > 0.0 ? sunShadow(Ng, NoL, noise) : 1.0;
  // A near-mirror's highlight is a tiny point of enormous brightness, which bloom would smear into a
  // white disc, so highlights are capped.
  vec3 direct = (diff * (1.0 - F) * frame.sunColorExp.rgb * NoL + min(spec * frame.sunColorExp.rgb * NoL, vec3(5.0))) * sun;

  // Authored ambient keeps shadow sides from going fully black; the probe adds real surroundings.
  float hemi = dot(N, L) * 0.5 + 0.5;
  vec3 ambient = mix(frame.ambientGround.rgb, frame.ambientSkyTime.rgb, hemi);
  vec3 envDiffuse = vec3(0.0);
  vec3 envSpec;
  if (frame.lighting.y > 0.0) {
    envDiffuse = min(sampleEnv(N, 1.0, frame.skyNebulaB.a), vec3(4.0)) * frame.lighting.y;
    vec3 R = reflect(-V, N);
    vec3 env = min(sampleEnv(R, rough, 1.0), vec3(12.0)) * frame.lighting.y;
    envSpec = env * envBrdfApprox(f0, rough, NoV);
  } else {
    envSpec = F_Schlick(NoV, f0) * ambient * (1.0 - rough * 0.7);
  }
  // Soft fill light (wrapped diffuse) and rim light keep silhouettes readable on the shadow side.
  float wrap = clamp((dot(N, frame.fillDir.xyz) + 0.4) / 1.4, 0.0, 1.0);
  vec3 fill = frame.fillColor.rgb * wrap * (diffuseColor + f0 * 0.5);
  float rim = pow(1.0 - NoV, max(frame.rimColorPower.a, 0.5));
  vec3 rimLight = frame.rimColorPower.rgb * rim * (1.0 - 0.6 * NoL);
  // Point lights (interior lamps, mining flares): smooth windowed inverse-square falloff.
  vec3 local = vec3(0.0);
  int count = int(frame.lightCount.x);
  int shadowed = int(frame.shadowInfo.w);
  for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
    if (i >= count) break;
    vec3 Lp = frame.pointPosRange[i].xyz - vPos;
    float dist = length(Lp);
    float range = frame.pointPosRange[i].w;
    if (dist >= range) continue;
    vec3 Ld = Lp / max(dist, 1e-3);
    float nl = max(dot(N, Ld), 0.0);
    if (nl <= 0.0) continue;
    float win = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
    float atten = win * win / (dist * dist + 1.0);
    if (i < shadowed) {
      // Push further off the surface at grazing angles, where cube texels stretch across it.
      float grazing = 1.0 - max(dot(Ng, Ld), 0.0);
      vec3 Ps = vPos + Ng * (0.015 + (0.02 + 0.02 * dist) * grazing);
      atten *= pointShadow(i, Ps, frame.pointPosRange[i].xyz, range, noise);
    }
    vec3 Hp = normalize(Ld + V);
    vec3 Fp = F_Schlick(max(dot(Ld, Hp), 0.0), f0);
    vec3 specP = D_GGX(max(dot(N, Hp), 0.0), a) * V_SmithGGXCorrelated(NoV, nl, a) * Fp;
    // On a near-mirror the lamp's fixture already shows in the reflection: a highlight would double it.
    if (frame.lighting.y > 0.0) specP *= smoothstep(0.06, 0.3, rough);
    local += diffuseColor / PI * (1.0 - Fp) * frame.pointColor[i].rgb * nl * atten + min(specP * frame.pointColor[i].rgb * nl * atten, vec3(5.0));
  }
  float specAo = clamp(ao * ao + 0.05, 0.0, 1.0);
  vec3 color = direct * mix(1.0, ao, 0.3) + local * mix(1.0, ao, 0.55) + (fill + rimLight) * ao +
               (ambient + envDiffuse) * diffuseColor * ao + envSpec * specAo + emissive;

  outColor = vec4(applyFog(color, length(vPos - frame.viewOrigin.xyz)), base.a);  // linear HDR radiance
}
