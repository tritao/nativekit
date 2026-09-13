#version 300 es
precision mediump float;
precision highp int;

uniform highp sampler2D tex_smp;

in highp vec2 uv;
layout(location = 0) out highp vec4 frag_color;
in highp vec4 color;

void main()
{
    highp vec4 _24 = texture(tex_smp, uv);
    highp float _27 = _24.x;
    highp float _32 = max(fwidth(_27), 0.001000000047497451305389404296875);
    frag_color = vec4(color.xyz, color.w * smoothstep(0.5 - _32, 0.5 + _32, _27));
}
