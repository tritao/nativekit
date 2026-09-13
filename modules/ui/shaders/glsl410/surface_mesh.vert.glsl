#version 410

uniform vec4 surface_mesh_vs_params[4];
layout(location = 0) in vec3 position;
layout(location = 0) out vec4 color;
layout(location = 1) in vec4 color0;

void main()
{
    gl_Position = mat4(surface_mesh_vs_params[0], surface_mesh_vs_params[1], surface_mesh_vs_params[2], surface_mesh_vs_params[3]) * vec4(position, 1.0);
    color = color0;
}
