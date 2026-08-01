#version 460
// Temporal accumulation / persistence: feedback of the previous accumulated
// frame under the current scene (persistence dial; opt-in, discovery 00).
layout(binding = 0) uniform sampler2D scene;
layout(binding = 1) uniform sampler2D history;
uniform float persistence;  // 0 none .. 0.98 long trails
uniform float decay_floor;  // subtract to avoid never-black smear
in vec2 uv;
out vec4 frag;
void main() {
  vec3 cur = texture(scene, uv).rgb;
  vec3 old = max(texture(history, uv).rgb - decay_floor, 0.0);
  frag = vec4(max(cur, old * persistence), 1.0);
}
