#version 460
in vec3 v_color;
in float v_soft;
out vec4 frag;

uniform float opacity;
uniform float soft_edge;  // 0 hard disc .. 1 gaussian

void main() {
  vec2 d = gl_PointCoord * 2.0 - 1.0;
  float r2 = dot(d, d);
  if (r2 > 1.0) discard;
  float gauss = exp(-r2 * 3.5);
  float disc = 1.0 - smoothstep(0.8, 1.0, r2);
  float a = mix(disc, gauss, soft_edge) * opacity * v_soft;
  frag = vec4(v_color * a, a);
}
