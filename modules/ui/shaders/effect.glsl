// Canonical NativeKit UI color-matrix effect shader.

@vs effect_vs
layout(binding=0) uniform effect_vs_params {
    vec4 value;
};
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv0;
layout(location=0) out vec2 uv;
void main() {
    uv = uv0;
    gl_Position = vec4(((position.x / value.x) * 2.0) - 1.0,
                       1.0 - ((position.y / value.y) * 2.0), 0.0, 1.0);
}
@end

@fs effect_fs
layout(binding=1) uniform effect_fs_params {
    vec4 value[5];
};
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 frag_color;
void main() {
    vec4 source = texture(sampler2D(tex, smp), uv);
    frag_color = vec4(dot(source, value[0]) + value[4].x,
                      dot(source, value[1]) + value[4].y,
                      dot(source, value[2]) + value[4].z,
                      dot(source, value[3]) + value[4].w);
}
@end

@program effect effect_vs effect_fs

@vs blur_vs
layout(binding=0) uniform blur_vs_params {
    vec4 value;
};
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv0;
layout(location=0) out vec2 uv;
void main() {
    uv = uv0;
    gl_Position = vec4(((position.x / value.x) * 2.0) - 1.0,
                       1.0 - ((position.y / value.y) * 2.0), 0.0, 1.0);
}
@end

@fs blur_fs
layout(binding=1) uniform blur_fs_params {
    vec4 value;
};
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 frag_color;
void main() {
    vec4 source = texture(sampler2D(tex, smp), uv);
    float sigma = value.x;
    if (sigma <= 0.0001) {
        frag_color = source;
        return;
    }
    vec2 direction = value.y > 0.5 ? vec2(0.0, value.w) : vec2(value.z, 0.0);
    float sample_step = max(1.0, sigma * 0.75);
    vec4 result = source;
    float weight_sum = 1.0;
    for (int index = 1; index <= 4; index++) {
        float normalized = float(index) * 0.75;
        float weight = exp(-0.5 * normalized * normalized);
        vec2 offset = direction * sample_step * float(index);
        result += (texture(sampler2D(tex, smp), uv + offset) +
                   texture(sampler2D(tex, smp), uv - offset)) * weight;
        weight_sum += 2.0 * weight;
    }
    frag_color = result / weight_sum;
}
@end

@program blur blur_vs blur_fs

@vs drop_shadow_vs
layout(binding=0) uniform drop_shadow_vs_params {
    vec4 value;
};
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv0;
layout(location=0) out vec2 uv;
void main() {
    uv = uv0;
    gl_Position = vec4(((position.x / value.x) * 2.0) - 1.0,
                       1.0 - ((position.y / value.y) * 2.0), 0.0, 1.0);
}
@end

@fs drop_shadow_fs
layout(binding=1) uniform drop_shadow_fs_params {
    vec4 value[3];
};
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 frag_color;
void main() {
    vec4 parameters = value[0];
    vec4 color = value[1];
    vec2 texel = value[2].xy;
    vec2 center = uv;
    if (parameters.y > 0.5)
        center -= vec2(parameters.z * texel.x, parameters.w * texel.y);
    float alpha = texture(sampler2D(tex, smp), center).a;
    float weight_sum = 1.0;
    if (parameters.x > 0.0001) {
        float sample_step = max(1.0, parameters.x * 0.75);
        vec2 direction = parameters.y > 0.5 ? vec2(0.0, texel.y) : vec2(texel.x, 0.0);
        for (int index = 1; index <= 4; index++) {
            float normalized = float(index) * 0.75;
            float weight = exp(-0.5 * normalized * normalized);
            vec2 offset = direction * sample_step * float(index);
            alpha += (texture(sampler2D(tex, smp), center + offset).a +
                      texture(sampler2D(tex, smp), center - offset).a) * weight;
            weight_sum += 2.0 * weight;
        }
    }
    alpha /= weight_sum;
    if (parameters.y <= 0.5)
        frag_color = vec4(0.0, 0.0, 0.0, alpha);
    else
        frag_color = vec4(color.rgb * color.a * alpha, color.a * alpha);
}
@end

@program drop_shadow drop_shadow_vs drop_shadow_fs
