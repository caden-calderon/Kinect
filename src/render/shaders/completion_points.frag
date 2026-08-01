#version 460 core

in vec3 surfel_normal;
in float surfel_coverage;
in float surfel_confidence;

uniform float opacity;
uniform float confidence_min;

layout(location = 0) out vec4 out_color;

void main() {
  vec2 point = gl_PointCoord * 2.0 - 1.0;
  float radius_squared = dot(point, point);
  if (radius_squared > 1.0) discard;

  float disc = exp(-radius_squared * 3.5);
  float confidence = smoothstep(confidence_min, 1.0, surfel_confidence);
  float alpha = opacity * surfel_coverage * confidence * disc;
  if (alpha < 0.004) discard;

  vec3 normal = normalize(surfel_normal);
  vec3 light_direction = normalize(vec3(-0.35, 0.65, 0.7));
  float wrapped_light = 0.45 + 0.55 * max(dot(normal, light_direction), 0.0);
  vec3 uncertain = vec3(0.22, 0.10, 0.56);
  vec3 confident = vec3(0.25, 0.72, 1.0);
  vec3 color = mix(uncertain, confident, surfel_confidence) * wrapped_light;
  out_color = vec4(color * alpha, alpha);
}
