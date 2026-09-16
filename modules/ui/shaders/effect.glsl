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
