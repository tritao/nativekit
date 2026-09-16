// Canonical NativeKit UI path shaders.

@vs path_vs
layout(binding=0) uniform path_vs_params {
    vec4 value;
};
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv0;
layout(location=0) out vec2 fpos;
layout(location=1) out vec2 ftcoord;
void main() {
    fpos = position;
    ftcoord = uv0;
    gl_Position = vec4(((position.x / value.x) * 2.0) - 1.0,
                       1.0 - ((position.y / value.y) * 2.0), 0.0, 1.0);
}
@end

@fs path_fs
layout(binding=1) uniform path_fs_params {
    vec4 value[24];
};
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(location=0) in vec2 fpos;
layout(location=1) in vec2 ftcoord;
layout(location=0) out vec4 frag_color;
float sdroundrect(vec2 p, vec2 ext, float rad) {
    vec2 q = abs(p) - (ext - vec2(rad));
    return (min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0)))) - rad;
}
vec4 gradient_color(float position) {
    int stop_count = int(value[5].y + 0.5);
    vec4 result = value[8];
    for (int index = 1; index < 8; ++index) {
        if (index >= stop_count) {
            break;
        }
        float start = value[16 + index - 1].x;
        float end = value[16 + index].x;
        if (position <= end) {
            float amount = clamp((position - start) / max(end - start, 0.000001), 0.0, 1.0);
            return mix(value[8 + index - 1], value[8 + index], amount);
        }
        result = value[8 + index];
    }
    return result;
}
void main() {
    vec3 homogeneous_position = vec3(fpos, 1.0);
    vec2 paint_position = vec2(dot(homogeneous_position, value[3].xyz),
                               dot(homogeneous_position, value[4].xyz));
    vec4 color;
    if (value[5].x > 1.5) {
        vec2 uv = paint_position / value[2].xy;
        if (value[5].z > 0.5) {
            uv.y = 1.0 - uv.y;
        }
        color = texture(sampler2D(tex, smp), uv);
        if (value[5].y > 0.5) {
            color = vec4(1.0, 1.0, 1.0, color.x);
        }
        color *= value[0];
        if (value[5].w < 0.5) {
            color.rgb *= color.a;
        }
    } else if (value[5].x > 0.5) {
        vec2 gradient_start = value[7].xy;
        vec2 gradient_direction = value[7].zw - gradient_start;
        float position = clamp(dot(paint_position - gradient_start, gradient_direction) /
                                   max(dot(gradient_direction, gradient_direction), 0.000001),
                               0.0, 1.0);
        color = gradient_color(position);
    } else {
        float distance = sdroundrect(paint_position, value[2].xy, value[2].z);
        float coverage = clamp((distance + (value[2].w * 0.5)) /
                               max(value[2].w, 0.0001), 0.0, 1.0);
        color = mix(value[0], value[1], coverage);
    }
    float fringe_coverage = min(1.0, (1.0 - abs((ftcoord.x * 2.0) - 1.0)) * value[6].y) *
                            min(1.0, ftcoord.y);
    if (value[6].x > 0.5) {
        if (fringe_coverage < value[6].z) {
            discard;
        }
        color *= fringe_coverage;
    }
    frag_color = color;
}
@end

@program path path_vs path_fs
