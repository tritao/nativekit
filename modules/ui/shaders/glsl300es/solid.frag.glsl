#version 300 es
precision mediump float;
precision highp int;

uniform highp vec4 solid_fs_params[1];
layout(location = 0) out highp vec4 frag_color;

void main()
{
    frag_color = solid_fs_params[0];
}
