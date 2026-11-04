// Shared declarations. Rendering is camera-relative: the camera sits at the origin of
// "render space", whose axes are the system axes. viewProj contains rotation + projection only.
layout(set = 0, binding = 0) uniform FrameUniforms {
  mat4 viewProj;
  mat4 invViewProj;
  vec4 sunDirCos;      // xyz: unit direction toward the star, w: cos(star angular radius)
  vec4 sunColorExp;    // rgb: star colour * intensity (linear), w: exposure (applied in tonemap.frag)
  vec4 ambientSkyTime; // rgb: ambient from "sky" hemisphere, w: time in seconds
  vec4 ambientGround;  // rgb: ambient from opposite hemisphere, w: galactic plane tilt (radians)
  vec4 viewport;       // width, height, 1/width, 1/height
  vec4 camRight;       // xyz camera right (render space), for billboards
  vec4 camUp;          // xyz camera up
  vec4 skyBase;        // rgb deep-space background colour
  vec4 skyBand;        // rgb galactic band colour, a band intensity
  vec4 skyNebula;      // rgb nebula tint, a nebula strength
  vec4 fillDir;        // xyz unit direction toward the fill light (bounce/nebula), w unused
  vec4 fillColor;      // rgb fill colour * intensity (soft diffuse only)
  vec4 rimColorPower;  // rgb rim (edge) light colour, a exponent
  vec4 fogColorDensity;  // rgb fog colour (linear), a density per metre (0 = off)
  vec4 lightCount;       // x = number of point lights in use
  vec4 pointPosRange[8]; // xyz camera-relative position, w range (m)
  vec4 pointColor[8];    // rgb colour * intensity
  vec4 atmoZenith;       // rgb zenith colour, a = 1 for atmospheric sky (planet surface)
  vec4 atmoHorizon;      // rgb horizon colour, a haze (sun glow spread)
  vec4 atmoGround;       // rgb below-horizon colour, a star visibility (0..1)
  mat4 sunShadow[3];     // render space -> cascade clip space
  vec4 shadowSplits;     // xyz far view depth of each cascade (m), w > 0 when sun shadows are on
  vec4 shadowInfo;       // xyz world size of a texel per cascade, w number of shadowed point lights
  vec4 lighting;         // x AO on, y reflection strength (0 = off), z surface detail, w env mip count
  vec4 viewForward;      // xyz camera forward in render space
  vec4 skyNebulaB;       // rgb second nebula colour, a: the live probe supplies ambient light
  vec4 skyNebulaC;       // rgb nebula core colour
  vec4 viewOrigin;       // xyz the viewpoint in render space (0 except for probes), w placed probes in use
  vec4 probePos[8];      // placed reflection probes: snapshot position
  vec4 probeMin[8];      // box
  vec4 probeMax[8];
} frame;

vec3 applyFog(vec3 color, float dist) {
  float d = frame.fogColorDensity.a;
  if (d <= 0.0) return color;
  float f = 1.0 - exp(-d * dist);
  return mix(color, frame.fogColorDensity.rgb, f);
}

vec3 tonemapACES(vec3 x) {
  // Narkowicz 2015 ACES fit. Output is linear; the sRGB swapchain encodes it.
  const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 galacticNormal() {
  float t = frame.ambientGround.w;
  return normalize(vec3(0.0, cos(t), sin(t)));
}
