#version 410

uniform vec4 path_fs_params[7];
uniform sampler2D tex_smp;

layout(location = 0) in vec2 fpos;
layout(location = 1) in vec2 ftcoord;
layout(location = 0) out vec4 frag_color;

float sdroundrect(vec2 p, vec2 ext, float rad)
{
    vec2 _25 = abs(p) - (ext - vec2(rad));
    return (min(max(_25.x, _25.y), 0.0) + length(max(_25, vec2(0.0)))) - rad;
}

void main()
{
    vec3 _53 = vec3(fpos, 1.0);
    vec2 _74 = vec2(dot(_53, path_fs_params[3].xyz), dot(_53, path_fs_params[4].xyz));
    vec4 color;
    if (path_fs_params[5].x > 1.5)
    {
        vec2 uv = _74 / path_fs_params[2].xy;
        if (path_fs_params[5].z > 0.5)
        {
            vec2 _215 = uv;
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
            vec4 _219 = color;
            vec3 _139 = _219.xyz * _219.w;
            vec4 _221 = _219;
            _221.x = _139.x;
            _221.y = _139.y;
            _221.z = _139.z;
            color = _221;
        }
    }
    else
    {
        vec2 param = _74;
        vec2 param_1 = path_fs_params[2].xy;
        float param_2 = path_fs_params[2].z;
        color = mix(path_fs_params[0], path_fs_params[1], vec4(clamp((sdroundrect(param, param_1, param_2) + (path_fs_params[2].w * 0.5)) / max(path_fs_params[2].w, 9.9999997473787516355514526367188e-05), 0.0, 1.0)));
    }
    if (path_fs_params[6].x > 0.5)
    {
        float _201 = min(1.0, (1.0 - abs((ftcoord.x * 2.0) - 1.0)) * path_fs_params[6].y) * min(1.0, ftcoord.y);
        if (_201 < path_fs_params[6].z)
        {
            discard;
        }
        color *= _201;
    }
    frag_color = color;
}
