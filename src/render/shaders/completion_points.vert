#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in float confidence;
layout(location = 3) in float completion_weight;

layout(binding = 0) uniform sampler2D observed_position_tex;

uniform mat4 view_proj;
uniform mat4 view;
uniform mat4 world;
uniform vec4 depth_intrinsics;  // fx fy cx cy
uniform float support_tolerance_m;
uniform float footprint;
uniform float confidence_min;

out vec3 surfel_normal;
out float surfel_coverage;
out float surfel_confidence;

float observedRejection(vec3 candidate) {
  float depth_m = -candidate.z;
  if (depth_m <= 0.05) return 0.0;

  vec2 pixel = vec2(candidate.x / depth_m * depth_intrinsics.x + depth_intrinsics.z,
                    -candidate.y / depth_m * depth_intrinsics.y + depth_intrinsics.w);
  ivec2 raster_size = textureSize(observed_position_tex, 0);
  ivec2 center = ivec2(floor(pixel));
  if (center.x < 0 || center.y < 0 || center.x >= raster_size.x || center.y >= raster_size.y)
    return 0.0;

  // A candidate at or in front of the measured surface is either already
  // supported or contradicted: if it really occupied empty foreground space,
  // the Kinect would have measured it. A candidate behind the measurement is
  // allowed because that is the self-occlusion completion is meant to reveal.
  vec4 center_observed = texelFetch(observed_position_tex, center, 0);
  if (center_observed.w > 0.5) {
    float observed_depth_m = -center_observed.z;
    if (depth_m <= observed_depth_m + support_tolerance_m) return 1.0;
  }

  float nearest_distance = 1e9;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      ivec2 sample_pixel = clamp(center + ivec2(x, y), ivec2(0), raster_size - 1);
      vec4 observed = texelFetch(observed_position_tex, sample_pixel, 0);
      if (observed.w > 0.5)
        nearest_distance = min(nearest_distance, length(observed.xyz - candidate));
    }
  }
  return 1.0 - smoothstep(support_tolerance_m, support_tolerance_m * 1.75,
                          nearest_distance);
}

void main() {
  float rejection = observedRejection(position);
  surfel_coverage = completion_weight * (1.0 - rejection);
  surfel_confidence = confidence;
  if (surfel_coverage < 0.005 || confidence < confidence_min) {
    gl_Position = vec4(2e9);
    gl_PointSize = 0.0;
    surfel_normal = vec3(0.0, 0.0, 1.0);
    return;
  }

  vec4 world_position = world * vec4(position, 1.0);
  gl_Position = view_proj * world_position;
  surfel_normal = normalize(mat3(world) * normal);
  float camera_distance_m = max(length((view * world_position).xyz), 0.2);
  gl_PointSize = footprint * (1.5 / camera_distance_m);
}
