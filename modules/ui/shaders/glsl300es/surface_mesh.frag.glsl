#version 300 es
precision mediump float;
precision highp int;

layout(location = 0) out highp vec4 frag_color;
in highp vec4 color;

void main()
{
    frag_color = color;
}
