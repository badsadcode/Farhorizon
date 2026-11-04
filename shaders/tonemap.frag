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
  outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
