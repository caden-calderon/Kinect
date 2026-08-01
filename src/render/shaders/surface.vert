#version 460
// Depth-surface mode ("responsive sheet", never "mesh of the person").
// Grid triangulation via a static index buffer; discontinuity rejection by
// NaN-ing vertices on strong edges (any NaN vertex kills the triangle).

layout(binding = 0) uniform sampler2D position_tex;
layout(binding = 1) uniform sampler2D normal_tex;
layout(binding = 2) uniform sampler2D boundary_tex;
layout(binding = 3) uniform sampler2D color_tex;
layout(binding = 4) uniform sampler2D coloruv_tex;

uniform mat4 view_proj;
uniform mat4 world;
uniform float edge_reject;   // 0..1: boundary level that kills geometry
uniform vec4 crop;

out vec3 v_normal;
out vec3 v_color_uvw;  // xy = raster uv (frag samples color at full res), z = confidence

void main() {
  ivec2 grid = ivec2(gl_VertexID % 512, gl_VertexID / 512);
  vec2 uvf = (vec2(grid) + 0.5) / vec2(512.0, 424.0);

  vec4 pos = texture(position_tex, uvf);
  vec2 bc = texture(boundary_tex, uvf).rg;
  bool cropped = uvf.x < crop.x || uvf.y < crop.y || uvf.x > crop.z || uvf.y > crop.w;

  if (pos.w < 0.5 || bc.r > edge_reject || cropped) {
    gl_Position = vec4(intBitsToFloat(0x7fc00000));  // NaN -> triangle culled
    v_normal = vec3(0);
    v_color_uvw = vec3(0);
    return;
  }

  gl_Position = view_proj * (world * vec4(pos.xyz, 1.0));
  vec4 nc = texture(normal_tex, uvf);
  v_normal = nc.xyz;
  v_color_uvw = vec3(texture(coloruv_tex, uvf).rg, nc.w);
}
