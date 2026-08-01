#version 460 core

in vec3 world_normal;
in vec3 world_position;
in float joint_confidence;
in float depth_observed_weight;

uniform float opacity;
uniform float confidence_min;

layout(location = 0) out vec4 out_color;

void main() {
  if (joint_confidence < confidence_min) discard;
  float alpha = opacity * smoothstep(confidence_min, 1.0, joint_confidence);
  if (alpha < 0.005) discard;

  vec3 n = normalize(world_normal);
  vec3 light_dir = normalize(vec3(-0.35, 0.65, 0.7));
  float wrapped_light = 0.3 + 0.7 * max(dot(n, light_dir), 0.0);
  float rim = pow(1.0 - abs(dot(n, normalize(-world_position))), 2.5);
  vec3 inferred_color = vec3(0.42, 0.12, 0.90);
  vec3 depth_color = vec3(0.08, 0.68, 1.0);
  vec3 color = mix(inferred_color, depth_color, depth_observed_weight);
  color *= wrapped_light + rim * 0.55;
  out_color = vec4(color * alpha, alpha);
}
