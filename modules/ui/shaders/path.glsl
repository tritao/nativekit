// NativeKit path paint shader.
//
// The vertex stage maps prepared path/paint geometry from framebuffer pixel
// coordinates into clip space and forwards both the position and paint
// coordinates. The fragment stage evaluates either a solid rounded-rect /
// gradient paint or a sampled image paint in the inverse paint-transform
// space. `coverage` applies the analytic fringe used by NanoVG CPU
// tessellation; non-convex fills use the separate stencil passes around this
// shader. Premultiplied image paints are selected through `mode.w`.

@module nkui_path

@vs vs
layout(binding=0) uniform path_vs_params {
    vec2 viewport;
};

in vec2 position;
in vec2 uv0;
out vec2 fpos;
out vec2 ftcoord;

void main() {
    fpos = position;
    ftcoord = uv0;
    vec2 p = vec2(position.x / viewport.x * 2.0 - 1.0,
                  1.0 - position.y / viewport.y * 2.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
@end

@fs fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(binding=1) uniform path_fs_params {
    vec4 inner_color;
    vec4 outer_color;
    vec4 extent_radius_feather;
    vec4 inverse_x;
    vec4 inverse_y;
    vec4 mode;
    vec4 coverage;
};

in vec2 fpos;
in vec2 ftcoord;
out vec4 frag_color;

float sdroundrect(vec2 p, vec2 ext, float rad) {
    vec2 ext2 = ext - vec2(rad);
    vec2 d = abs(p) - ext2;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - rad;
}

void main() {
    vec2 pt = vec2(dot(vec3(fpos, 1.0), inverse_x.xyz),
                   dot(vec3(fpos, 1.0), inverse_y.xyz));
    vec4 color;
    if (mode.x > 1.5) {
        vec2 uv = pt / extent_radius_feather.xy;
        if (mode.z > 0.5)
            uv.y = 1.0 - uv.y;
        color = texture(sampler2D(tex, smp), uv);
        if (mode.y > 0.5)
            color = vec4(1.0, 1.0, 1.0, color.r);
        color *= inner_color;
        if (mode.w < 0.5)
            color.rgb *= color.a;
    } else {
        float distance = sdroundrect(pt, extent_radius_feather.xy,
                                     extent_radius_feather.z);
        float t = clamp((distance + extent_radius_feather.w * 0.5) /
                            max(extent_radius_feather.w, 0.0001),
                        0.0, 1.0);
        color = mix(inner_color, outer_color, t);
    }
    if (coverage.x > 0.5) {
        float alpha = min(1.0, (1.0 - abs(ftcoord.x * 2.0 - 1.0)) * coverage.y) *
                      min(1.0, ftcoord.y);
        if (alpha < coverage.z)
            discard;
        color *= alpha;
    }
    frag_color = color;
}
@end

@program path vs fs
