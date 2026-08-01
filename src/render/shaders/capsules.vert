#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in float confidence;
layout(location = 3) in float observed_weight;

uniform mat4 view_proj;
uniform mat4 world;

out vec3 world_normal;
out vec3 world_position;
out float joint_confidence;
out float depth_observed_weight;

void main() {
  vec4 p = world * vec4(position, 1.0);
  world_position = p.xyz;
  world_normal = normalize(mat3(world) * normal);
  joint_confidence = confidence;
  depth_observed_weight = observed_weight;
  gl_Position = view_proj * p;
}
