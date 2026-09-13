#version 300 es
precision mediump float;
precision highp int;

uniform highp vec4 path_fs_params[7];
uniform highp sampler2D tex_smp;

in highp vec2 fpos;
in highp vec2 ftcoord;
layout(location = 0) out highp vec4 frag_color;

highp float sdroundrect(highp vec2 p, highp vec2 ext, highp float rad)
{
    highp vec2 _25 = abs(p) - (ext - vec2(rad));
    return (min(max(_25.x, _25.y), 0.0) + length(max(_25, vec2(0.0)))) - rad;
}

void main()
{
    highp vec3 _53 = vec3(fpos, 1.0);
    highp vec2 _74 = vec2(dot(_53, path_fs_params[3].xyz), dot(_53, path_fs_params[4].xyz));
    highp vec4 color;
    if (path_fs_params[5].x > 1.5)
    {
        highp vec2 uv = _74 / path_fs_params[2].xy;
        if (path_fs_params[5].z > 0.5)
        {
            highp vec2 _215 = uv;
            _215.y = 1.0 - _215.y;
            uv = _215;
        }
        color = texture(tex_smp, uv);
        if (path_fs_params[5].y > 0.5)
        {
            color = vec4(1.0, 1.0, 1.0, color.x);
        }
        color *= path_fs_params[0];
        if (path_fs_params[5].w < 0.5)
        {
            highp vec4 _219 = color;
            highp vec3 _139 = _219.xyz * _219.w;
            highp vec4 _221 = _219;
            _221.x = _139.x;
            _221.y = _139.y;
            _221.z = _139.z;
            color = _221;
        }
    }
    else
    {
        highp vec2 param = _74;
        highp vec2 param_1 = path_fs_params[2].xy;
        highp float param_2 = path_fs_params[2].z;
        color = mix(path_fs_params[0], path_fs_params[1], vec4(clamp((sdroundrect(param, param_1, param_2) + (path_fs_params[2].w * 0.5)) / max(path_fs_params[2].w, 9.9999997473787516355514526367188e-05), 0.0, 1.0)));
    }
    if (path_fs_params[6].x > 0.5)
    {
        highp float _201 = min(1.0, (1.0 - abs((ftcoord.x * 2.0) - 1.0)) * path_fs_params[6].y) * min(1.0, ftcoord.y);
        if (_201 < path_fs_params[6].z)
        {
            discard;
        }
        color *= _201;
    }
    frag_color = color;
}
