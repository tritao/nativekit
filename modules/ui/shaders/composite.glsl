// Canonical NativeKit UI composite shaders.

@vs composite_vs
layout(binding=0) uniform composite_vs_params {
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

@fs composite_fs
layout(binding=1) uniform composite_fs_params {
    vec4 value;
};
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 frag_color;
void main() {
    frag_color = texture(sampler2D(tex, smp), uv) * value;
}
@end

@program composite composite_vs composite_fs
