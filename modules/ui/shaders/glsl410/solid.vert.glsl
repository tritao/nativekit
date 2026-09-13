#version 410

uniform vec4 solid_vs_params[1];
layout(location = 0) in vec2 position;

void main()
{
    gl_Position = vec4(((position.x / solid_vs_params[0].x) * 2.0) - 1.0, 1.0 - ((position.y / solid_vs_params[0].y) * 2.0), 0.0, 1.0);
}
