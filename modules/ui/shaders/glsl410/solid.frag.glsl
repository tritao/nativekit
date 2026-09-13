#version 410

uniform vec4 solid_fs_params[1];
layout(location = 0) out vec4 frag_color;

void main()
{
    frag_color = solid_fs_params[0];
}
