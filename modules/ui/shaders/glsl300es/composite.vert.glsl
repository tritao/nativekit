#version 300 es

uniform vec4 composite_vs_params[1];
out vec2 uv;
layout(location = 1) in vec2 uv0;
layout(location = 0) in vec2 position;

void main()
{
    uv = uv0;
    gl_Position = vec4(((position.x / composite_vs_params[0].x) * 2.0) - 1.0, 1.0 - ((position.y / composite_vs_params[0].y) * 2.0), 0.0, 1.0);
}
