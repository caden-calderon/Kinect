#version 460 core

struct Particle {
  vec4 position_age;
  vec4 velocity_lifetime;
  vec4 anchor_depth;
  vec4 source_band_alive;
};

layout(std430, binding = 0) readonly buffer ParticlePool { Particle particles[]; };

uniform mat4 view_proj;
uniform mat4 view;
uniform mat4 world;
uniform float footprint;
uniform float opacity;

out vec3 particle_color;
out float particle_alpha;

float lifeEnvelope(float age_frames, float lifetime_frames) {
  float phase = clamp(age_frames / max(lifetime_frames, 1.0), 0.0, 1.0);
  return smoothstep(0.0, 0.08, phase) * (1.0 - smoothstep(0.72, 1.0, phase));
}

void main() {
  Particle particle = particles[gl_VertexID];
  if (particle.source_band_alive.w < 0.5) {
    gl_Position = vec4(2e9);
    gl_PointSize = 0.0;
    particle_color = vec3(0.0);
    particle_alpha = 0.0;
    return;
  }

  int band = int(round(particle.source_band_alive.z));
  vec4 world_position = world * vec4(particle.position_age.xyz, 1.0);
  gl_Position = view_proj * world_position;
  float camera_distance_m = max(length((view * world_position).xyz), 0.2);
  float band_size = band == 0 ? 0.82 : (band == 1 ? 1.0 : 0.62);
  gl_PointSize = footprint * band_size * (1.5 / camera_distance_m);

  vec3 near_color = vec3(0.58, 0.90, 1.00);
  vec3 volume_color = vec3(0.28, 0.52, 1.00);
  vec3 wisp_color = vec3(0.72, 0.66, 1.00);
  particle_color = band == 0 ? near_color : (band == 1 ? volume_color : wisp_color);
  float band_alpha = band == 0 ? 0.72 : (band == 1 ? 0.42 : 0.26);
  float speed_glow = clamp(length(particle.velocity_lifetime.xyz) * 0.7, 0.0, 0.35);
  particle_color *= 0.70 + speed_glow;
  particle_alpha = opacity * band_alpha *
                   lifeEnvelope(particle.position_age.w, particle.velocity_lifetime.w);
}
