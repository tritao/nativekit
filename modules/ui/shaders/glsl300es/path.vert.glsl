#version 300 es

uniform vec4 path_vs_params[1];
out vec2 fpos;
layout(location = 0) in vec2 position;
out vec2 ftcoord;
layout(location = 1) in vec2 uv0;

void main()
{
    fpos = position;
    ftcoord = uv0;
    gl_Position = vec4(((position.x / path_vs_params[0].x) * 2.0) - 1.0, 1.0 - ((position.y / path_vs_params[0].y) * 2.0), 0.0, 1.0);
}
