#version 300 es
precision mediump float;
precision highp int;

uniform highp vec4 composite_fs_params[1];
uniform highp sampler2D tex_smp;

layout(location = 0) out highp vec4 frag_color;
in highp vec2 uv;

void main()
{
    frag_color = texture(tex_smp, uv) * composite_fs_params[0];
}
