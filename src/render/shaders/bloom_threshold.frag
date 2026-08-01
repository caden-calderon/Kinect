#version 460
layout(binding = 0) uniform sampler2D src;
uniform float threshold;
uniform float knee;
in vec2 uv;
out vec4 frag;
void main() {
  vec3 c = texture(src, uv).rgb;
  float l = max(max(c.r, c.g), c.b);
  float gate = smoothstep(threshold - knee, threshold + knee, l);
  frag = vec4(c * gate, 1.0);
}
