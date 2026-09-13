#version 410

layout(location = 0) out vec4 frag_color;
layout(location = 0) in vec4 color;

void main()
{
    frag_color = color;
}
