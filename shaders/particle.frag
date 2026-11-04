#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) in float vDist;
layout(location = 0) out vec4 outColor;
layout(set = 1, binding = 0) uniform sampler2D atlas;
layout(set = 2, binding = 0) uniform sampler2D depthTex;
layout(push_constant) uniform Push {
  vec4 params;  // x near plane (m)
} pc;

void main() {
  float shape = texture(atlas, vUv).a;
  if (shape <= 0.002) discard;
  // Soft particles: fade where the quad cuts through geometry, instead of a hard line on the hull.
  float d = texelFetch(depthTex, ivec2(gl_FragCoord.xy), 0).r;
  float scene = d > 0.0 ? pc.params.x / d : 1e9;  // view distance (reversed-Z, infinite far)
  float soft = clamp((scene - vDist) / 1.5, 0.0, 1.0);
  float k = shape * soft;
  outColor = vec4(vColor.rgb * k, vColor.a * k);  // premultiplied; alpha 0 adds light
}
