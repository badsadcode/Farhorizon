// Shared declarations. Rendering is camera-relative: the camera sits at the origin of
// "render space", whose axes are the system axes. viewProj contains rotation + projection only.
layout(set = 0, binding = 0) uniform FrameUniforms {
  mat4 viewProj;
  mat4 invViewProj;
  vec4 sunDirCos;      // xyz: unit direction toward the star, w: cos(star angular radius)
  vec4 sunColorExp;    // rgb: star colour * intensity (linear), w: exposure
  vec4 ambientSkyTime; // rgb: ambient from "sky" hemisphere, w: time in seconds
  vec4 ambientGround;  // rgb: ambient from opposite hemisphere, w: galactic plane tilt (radians)
  vec4 viewport;       // width, height, 1/width, 1/height
} frame;

vec3 tonemapACES(vec3 x) {
  // Narkowicz 2015 ACES fit. Output is linear; the sRGB swapchain encodes it.
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 galacticNormal() {
  float t = frame.ambientGround.w;
  return normalize(vec3(0.0, cos(t), sin(t)));
}
