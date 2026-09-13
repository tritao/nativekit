#version 300 es
precision mediump float;
precision highp int;

uniform highp sampler2D tex_smp;

in highp vec2 uv;
layout(location = 0) out highp vec4 frag_color;
in highp vec4 color;

void main()
{
    frag_color = vec4(color.xyz, color.w * texture(tex_smp, uv).x);
}
