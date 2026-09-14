// Canonical NativeKit UI text shaders.

@vs text_vs
layout(binding=0) uniform text_vs_params {
    vec4 value;
};
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv0;
layout(location=2) in vec4 color0;
layout(location=0) out vec2 uv;
layout(location=1) out vec4 color;
void main() {
    uv = uv0;
    color = color0;
    gl_Position = vec4(((position.x / value.x) * 2.0) - 1.0,
                       1.0 - ((position.y / value.y) * 2.0), 0.0, 1.0);
}
@end

@fs text_alpha_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=0) out vec4 frag_color;
void main() {
    frag_color = vec4(color.xyz, color.w * texture(sampler2D(tex, smp), uv).x);
}
@end

@fs text_sdf_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=0) out vec4 frag_color;
void main() {
    float distance = texture(sampler2D(tex, smp), uv).x;
    float width = max(fwidth(distance), 0.001);
    frag_color = vec4(color.xyz, color.w * smoothstep(0.5 - width, 0.5 + width, distance));
}
@end

@fs text_color_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(location=0) in vec2 uv;
layout(location=1) in vec4 color;
layout(location=0) out vec4 frag_color;
void main() {
    frag_color = texture(sampler2D(tex, smp), uv) * color;
}
@end

@program text_alpha text_vs text_alpha_fs

@program text_sdf text_vs text_sdf_fs

@program text_color text_vs text_color_fs
