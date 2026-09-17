// Canonical NativeKit UI effect shaders.

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
    // Sample symmetrically at half-step intervals so linear filtering fills
    // the interval between neighboring Gaussian samples. Six pairs provide
    // a smooth 13-tap effective kernel while keeping the pass separable and
    // bounded on every backend.
    float sample_step = max(1.0, sigma * 0.5);
    vec4 result = source;
    float weight_sum = 1.0;
    for (int index = 1; index <= 6; index++) {
        float offset_in_texels = (float(index) - 0.5) * sample_step;
        float normalized = offset_in_texels / sigma;
        float weight = exp(-0.5 * normalized * normalized);
        vec2 offset = direction * offset_in_texels;
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
        float sample_step = max(1.0, parameters.x * 0.5);
        vec2 direction = parameters.y > 0.5 ? vec2(0.0, texel.y) : vec2(texel.x, 0.0);
        for (int index = 1; index <= 6; index++) {
            float offset_in_texels = (float(index) - 0.5) * sample_step;
            float normalized = offset_in_texels / parameters.x;
            float weight = exp(-0.5 * normalized * normalized);
            vec2 offset = direction * offset_in_texels;
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

@vs box_shadow_vs
layout(binding=0) uniform box_shadow_vs_params {
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

@fs box_shadow_fs
layout(binding=1) uniform box_shadow_fs_params {
    vec4 value[4];
};
layout(location=0) in vec2 uv;
layout(location=0) out vec4 frag_color;

float rounded_rect_distance(vec2 point, vec4 rect, vec4 radii) {
    vec2 center = rect.xy + rect.zw * 0.5;
    vec2 local = point - center;
    float radius = local.x < 0.0
                       ? (local.y < 0.0 ? radii.x : radii.w)
                       : (local.y < 0.0 ? radii.y : radii.z);
    radius = max(0.0, min(radius, min(rect.z, rect.w) * 0.5));
    vec2 half_extent = rect.zw * 0.5;
    vec2 q = abs(local) - (half_extent - vec2(radius));
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
    vec4 base = value[0];
    vec4 params = value[1];
    vec4 radii = value[2];
    vec4 color = value[3];
    vec4 shape = vec4(base.x + params.x - params.w,
                      base.y + params.y - params.w,
                      base.z + 2.0 * params.w,
                      base.w + 2.0 * params.w);
    float distance = rounded_rect_distance(uv, shape, radii + vec4(params.w));
    float alpha = 1.0;
    if (distance > 0.0) {
        if (params.z <= 0.0001)
            alpha = 0.0;
        else {
            float normalized = distance / params.z;
            alpha = exp(-0.5 * normalized * normalized);
        }
    }
    frag_color = vec4(color.rgb * color.a * alpha, color.a * alpha);
}
@end

@program box_shadow box_shadow_vs box_shadow_fs

@vs mask_vs
layout(binding=0) uniform mask_vs_params {
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

@fs mask_fs
layout(binding=1) uniform mask_fs_params {
    vec4 value[3];
};
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(binding=1) uniform texture2D mask_tex;
layout(binding=1) uniform sampler mask_smp;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 frag_color;
void main() {
    vec4 source = texture(sampler2D(tex, smp), uv);
    float kind = value[0].x;
    float mask_alpha = 1.0;
    if (kind > 4.5) {
        mask_alpha = texture(sampler2D(mask_tex, mask_smp), uv).a;
    } else if (kind > 3.5) {
        vec2 direction = value[1].xy - value[0].zw;
        float denominator = max(dot(direction, direction), 0.000001);
        float amount = clamp(dot(uv - value[0].zw, direction) / denominator, 0.0, 1.0);
        mask_alpha = mix(value[1].z, value[1].w, amount);
    } else if (kind > 2.5) {
        vec2 position = uv * value[2].xy - value[2].xy * 0.5;
        mask_alpha = step(length(position), value[0].y);
    } else if (kind > 1.5) {
        vec2 position = uv * value[2].xy - value[2].xy * 0.5;
        vec2 extent = value[2].xy * 0.5 - vec2(value[0].y);
        vec2 q = abs(position) - extent;
        float distance = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - value[0].y;
        mask_alpha = step(distance, 0.0);
    }
    frag_color = vec4(source.rgb * mask_alpha, source.a * mask_alpha);
}
@end

@program mask mask_vs mask_fs
