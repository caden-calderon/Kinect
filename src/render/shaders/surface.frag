#version 460
in vec3 v_normal;
in vec3 v_color_uvw;
out vec4 frag;

layout(binding = 3) uniform sampler2D color_tex;

uniform int color_mode;   // 0 mono, 1 rgb
uniform vec3 ramp_lo;
uniform vec3 ramp_hi;
uniform float exposure;
uniform float surface_opacity;
uniform float light_wrap;  // soft headlight shading amount

void main() {
  // 1080p direct color sampling (Dense Veil's sharpness), proportional map
  vec3 rgb = texture(color_tex, v_color_uvw.xy).rgb * exposure;
  vec3 base;
  if (color_mode == 1) {
    base = rgb;
  } else {
    float lum = dot(rgb, vec3(0.299, 0.587, 0.114));
    base = mix(ramp_lo, ramp_hi, clamp(lum, 0.0, 1.0));
  }
  // headlight: light from the camera, wrapped so it never goes black
  float ndl = clamp(v_normal.z, 0.0, 1.0);
  float shade = mix(1.0, 0.25 + 0.75 * ndl, light_wrap);
  frag = vec4(base * shade * surface_opacity, surface_opacity);
}
