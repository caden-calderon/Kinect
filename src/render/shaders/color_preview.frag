#version 460

in vec2 uv;
out vec4 frag;

layout(binding = 0) uniform sampler2D color_tex;
uniform bool mirror;

void main() {
  // BGRX row zero is the sensor image's top row. OpenGL stores that row at
  // texture y=0, so fullscreen presentation needs a vertical flip.
  vec2 source_uv = vec2(mirror ? 1.0 - uv.x : uv.x, 1.0 - uv.y);
  frag = vec4(texture(color_tex, source_uv).rgb, 1.0);
}
