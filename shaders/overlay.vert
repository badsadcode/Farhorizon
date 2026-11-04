#version 450
layout(location = 0) in vec2 inPos;     // pixels, top-left origin
layout(location = 1) in vec2 inUv;      // UI atlas (glyphs; solid shapes use its white texel)
layout(location = 2) in vec4 inColor;   // sRGB-encoded UNORM
layout(push_constant) uniform Push { vec2 invHalfViewport; } pc;
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;
void main() {
  gl_Position = vec4(inPos * pc.invHalfViewport - 1.0, 0.0, 1.0);
  // Swapchain is sRGB: convert authored sRGB colours to linear before blending.
  vColor = vec4(pow(inColor.rgb, vec3(2.2)), inColor.a);
  vUv = inUv;
}
