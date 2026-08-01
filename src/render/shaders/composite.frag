#version 460
// Final composite: scene + bloom, filmic tonemap, contrast/lift, grain,
// vignette. The post chain v1 of roadmap phase 3.
layout(binding = 0) uniform sampler2D scene;
layout(binding = 1) uniform sampler2D bloom0;
layout(binding = 2) uniform sampler2D bloom1;
layout(binding = 3) uniform sampler2D bloom2;

uniform float bloom_amount;
uniform float grain_amount;
uniform float grain_seed;     // frame-index derived (deterministic, E7)
uniform float vignette;
uniform float contrast;
uniform float lift;           // near-black floor (Luminous Shell wants < 0)
uniform float background;     // background luminance

in vec2 uv;
out vec4 frag;

// ACES approximation (Narkowicz)
vec3 aces(vec3 x) {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float hash(vec2 p) {
  vec3 q = fract(vec3(p.xyx) * 443.8975 + grain_seed);
  q += dot(q, q.yzx + 19.19);
  return fract((q.x + q.y) * q.z);
}

void main() {
  vec3 c = texture(scene, uv).rgb + background;
  vec3 b = texture(bloom0, uv).rgb + texture(bloom1, uv).rgb * 0.7 +
           texture(bloom2, uv).rgb * 0.5;
  c += b * bloom_amount;

  c = aces(c);
  c = pow(c, vec3(1.0 / 2.2));

  // editorial curve: contrast around mid-grey, then lift the floor
  c = (c - 0.5) * contrast + 0.5 + lift;

  // photographic grain (luminance-weighted, deterministic per frame)
  float g = (hash(uv * vec2(1920.0, 1080.0)) - 0.5) * grain_amount;
  c += g * (0.25 + 0.75 * dot(c, vec3(0.333)));

  float d = distance(uv, vec2(0.5)) * 1.4142;
  c *= 1.0 - vignette * d * d;

  frag = vec4(clamp(c, 0.0, 1.0), 1.0);
}
