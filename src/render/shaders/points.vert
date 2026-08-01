#version 460
// Observed point pass. One vertex per depth texel (stride-decimated);
// geometry from the position texture (observed layer, depth_cam space).

layout(binding = 0) uniform sampler2D position_tex;  // xyz + valid
layout(binding = 1) uniform sampler2D normal_tex;    // xyz + confidence
layout(binding = 2) uniform sampler2D boundary_tex;  // discontinuity, ir_conf
layout(binding = 3) uniform sampler2D color_tex;     // 1080p BGRA (as RGBA)
layout(binding = 4) uniform sampler2D coloruv_tex;   // registered color UVs
layout(binding = 5) uniform sampler2D flow_tex;      // fx, fy, confidence, |v| (color-raster px)

uniform mat4 view_proj;
uniform mat4 view;           // world -> camera, for orbit-correct distance
uniform mat4 world;          // user transform, depth_cam -> world
uniform int stride;          // 1..8
uniform float footprint;     // px at reference depth
uniform float footprint_by_depth;  // 0..1: scale footprint with 1/z
uniform float focus_depth_mm;       // far fog begins here in sensor depth
uniform float fade_range_mm;        // distance over which far fog reaches zero
uniform vec2 jitter;         // sub-texel sampling jitter (breaks grid read)
uniform vec4 crop;           // u0 v0 u1 v1 in 0..1

// color controls
uniform int color_mode;      // 0 mono ramp, 1 rgb, 2 depth ramp, 3 ir/confidence, 4 flow
uniform float flow_gain;     // px of flow that saturates the visualization
uniform float flow_conf_min; // FB-confidence gate (E2: never trust raw flow)
uniform vec3 ramp_lo;
uniform vec3 ramp_hi;
uniform float exposure;
uniform float confidence_to_brightness;  // Luminous Shell fringe dial
uniform vec2 depth_ramp_mm;  // map depth to ramp over this range
uniform float clip_near_mm;
uniform float near_fade_mm;  // dissolve before the sensor/clip floor; 0 disables

out vec3 v_color;
out float v_soft;

void main() {
  int W = 512 / stride;
  ivec2 grid = ivec2(gl_VertexID % W, gl_VertexID / W);
  vec2 uvf = (vec2(grid * stride) + 0.5 + jitter * float(stride)) / vec2(512.0, 424.0);

  if (uvf.x < crop.x || uvf.y < crop.y || uvf.x > crop.z || uvf.y > crop.w) {
    gl_Position = vec4(2e9);
    gl_PointSize = 0.0;
    v_color = vec3(0);
    v_soft = 1.0;
    return;
  }

  vec4 pos = texture(position_tex, uvf);
  if (pos.w < 0.5) {
    gl_Position = vec4(2e9);
    gl_PointSize = 0.0;
    v_color = vec3(0);
    v_soft = 1.0;
    return;
  }

  vec4 world_pos = world * vec4(pos.xyz, 1.0);
  gl_Position = view_proj * world_pos;

  float camera_distance_m = max(length((view * world_pos).xyz), 0.2);
  gl_PointSize =
      footprint * float(stride) * mix(1.0, 1.5 / camera_distance_m, footprint_by_depth);
  float depth_mm = -pos.z * 1000.0;

  vec4 nc = texture(normal_tex, uvf);
  vec2 bc = texture(boundary_tex, uvf).rg;

  vec2 cuv = texture(coloruv_tex, uvf).rg;
  bool cuv_ok = cuv.x > 0.0 && cuv.x < 1.0 && cuv.y > 0.0 && cuv.y < 1.0;
  vec3 rgb = cuv_ok ? texture(color_tex, cuv).rgb : vec3(0.0);
  vec3 base;
  if (color_mode == 1) {
    base = rgb * exposure;
  } else if (color_mode == 2) {
    float t = clamp((depth_mm - depth_ramp_mm.x) / (depth_ramp_mm.y - depth_ramp_mm.x), 0.0, 1.0);
    base = mix(ramp_hi, ramp_lo, t) * exposure;
  } else if (color_mode == 3) {
    base = mix(ramp_lo, ramp_hi, nc.w) * exposure;
  } else if (color_mode == 4) {
    // motion field debug/perform view: hue = direction, energy = magnitude,
    // confidence-gated so untrusted vectors fall back to the quiet ramp floor
    vec4 fl = cuv_ok ? texture(flow_tex, cuv) : vec4(0.0);
    float ang = atan(fl.y, fl.x);
    vec3 dir = 0.5 + 0.5 * vec3(cos(ang), cos(ang - 2.094395), cos(ang + 2.094395));
    float energy = clamp(fl.w * flow_gain, 0.0, 1.0) * step(flow_conf_min, fl.z);
    base = mix(ramp_lo, dir, energy) * exposure;
  } else {
    float lum = dot(rgb, vec3(0.299, 0.587, 0.114));
    base = mix(ramp_lo, ramp_hi, clamp(lum * exposure, 0.0, 1.0));
  }

  // confidence -> brightness: low-confidence silhouette samples become the
  // glowing fringe (honest uncertainty as design language, 04 §1)
  float fringe = mix(1.0, 0.35 + 1.8 * bc.r, confidence_to_brightness);
  float depth_fog = 1.0 - smoothstep(focus_depth_mm, focus_depth_mm + fade_range_mm, depth_mm);
  v_color = base * fringe * depth_fog;
  v_soft = near_fade_mm > 0.0
               ? smoothstep(clip_near_mm, clip_near_mm + near_fade_mm, depth_mm)
               : 1.0;
}
