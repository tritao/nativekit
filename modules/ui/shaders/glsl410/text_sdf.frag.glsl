#version 410

uniform sampler2D tex_smp;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 frag_color;
layout(location = 1) in vec4 color;

void main()
{
    vec4 _24 = texture(tex_smp, uv);
    float _27 = _24.x;
    float _32 = max(fwidth(_27), 0.001000000047497451305389404296875);
    frag_color = vec4(color.xyz, color.w * smoothstep(0.5 - _32, 0.5 + _32, _27));
}
