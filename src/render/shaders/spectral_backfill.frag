#version 460 core

in vec3 particle_color;
in float particle_alpha;

layout(location = 0) out vec4 out_color;

void main() {
  vec2 point = gl_PointCoord * 2.0 - 1.0;
  float radius_squared = dot(point, point);
  if (radius_squared > 1.0) discard;
  float glow = exp(-radius_squared * 3.8);
  float alpha = particle_alpha * glow;
  if (alpha < 0.001) discard;
  out_color = vec4(particle_color * alpha, alpha);
}
