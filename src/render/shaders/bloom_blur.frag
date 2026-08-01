#version 460
layout(binding = 0) uniform sampler2D src;
uniform vec2 dir;  // texel-scaled direction
in vec2 uv;
out vec4 frag;
void main() {
  vec3 acc = texture(src, uv).rgb * 0.2270270;
  acc += texture(src, uv + dir * 1.3846154).rgb * 0.3162162;
  acc += texture(src, uv - dir * 1.3846154).rgb * 0.3162162;
  acc += texture(src, uv + dir * 3.2307692).rgb * 0.0702703;
  acc += texture(src, uv - dir * 3.2307692).rgb * 0.0702703;
  frag = vec4(acc, 1.0);
}
