#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Final composite: bloom, exposure, ACES, colour grade, then lens/film effects.
// All parameters come from a data-driven post profile (content/visuals/post_profiles.json).
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(set = 1, binding = 0) uniform sampler2D hdrColor;
layout(set = 2, binding = 0) uniform sampler2D bloomTex;
layout(push_constant) uniform Post {
  vec4 shadowTintSat;     // rgb shadow tint, a saturation
  vec4 highlightTintCon;  // rgb highlight tint, a contrast
  vec4 lens;              // x vignette, y scanline strength, z scanline period (px), w chromatic aberration
  vec4 misc;              // x grain, y bloom strength, z time, w unused
  vec4 fadeColorAmount;   // rgb fade colour, a amount (scene swaps: cloud deck)
  vec4 heat;              // x re-entry heat 0..1
} post;

float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

vec3 sampleScene(vec2 uv) {
  return texture(hdrColor, uv).rgb + texture(bloomTex, uv).rgb * post.misc.y;
}

void main() {
  vec2 centred = vUv - 0.5;
  float r2 = dot(centred, centred);

  // Chromatic aberration grows toward the edges (lens fringing).
  vec3 hdr;
  float ca = post.lens.w * r2;
  if (ca > 1e-6) {
    hdr.r = sampleScene(vUv + centred * ca).r;
    hdr.g = sampleScene(vUv).g;
    hdr.b = sampleScene(vUv - centred * ca).b;
  } else {
    hdr = sampleScene(vUv);
  }

  vec3 c = tonemapACES(hdr * frame.sunColorExp.w);

  // Grade (display-referred): split-tone shadows/highlights, contrast, saturation.
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c *= mix(post.shadowTintSat.rgb, post.highlightTintCon.rgb, smoothstep(0.0, 0.6, l));
  c = clamp((c - 0.18) * post.highlightTintCon.a + 0.18, 0.0, 1.0);
  l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c = clamp(mix(vec3(l), c, post.shadowTintSat.a), 0.0, 1.0);

  // Re-entry heat: plasma glow creeping in from the edges, flickering.
  float heat = post.heat.x;
  if (heat > 0.0) {
    float flick = 0.85 + 0.15 * sin(post.misc.z * 37.0) * sin(post.misc.z * 13.0 + vUv.y * 9.0);
    float edge = smoothstep(0.05, 0.6, r2 * 2.0 + 0.15 * sin(vUv.x * 23.0 + post.misc.z * 5.0));
    c = mix(c, vec3(1.0, 0.45, 0.12) * flick, heat * edge * 0.85);
    c += vec3(0.25, 0.08, 0.02) * heat * heat;
  }

  // Vignette.
  c *= 1.0 - post.lens.x * smoothstep(0.1, 0.75, r2 * 2.0);

  // Scanlines (screen-space, resolution independent period in pixels).
  if (post.lens.y > 0.0) {
    float line = 0.5 + 0.5 * cos(gl_FragCoord.y * 6.2831853 / max(post.lens.z, 1.0));
    c *= 1.0 - post.lens.y * line;
  }

  // Film grain (animated, applied in display space so it reads in shadows).
  if (post.misc.x > 0.0) {
    float n = hash12(gl_FragCoord.xy + fract(post.misc.z * 7.31) * 1000.0) - 0.5;
    c += n * post.misc.x;
  }
  c = mix(c, post.fadeColorAmount.rgb, clamp(post.fadeColorAmount.a, 0.0, 1.0));
  outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
