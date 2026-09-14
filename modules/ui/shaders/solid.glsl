// Canonical NativeKit UI solid shaders.

@vs solid_vs
layout(binding=0) uniform solid_vs_params {
    vec4 value;
};
layout(location=0) in vec2 position;
void main() {
    gl_Position = vec4(((position.x / value.x) * 2.0) - 1.0,
                       1.0 - ((position.y / value.y) * 2.0), 0.0, 1.0);
}
@end

@fs solid_fs
layout(binding=1) uniform solid_fs_params {
    vec4 value;
};
layout(location=0) out vec4 frag_color;
void main() {
    frag_color = value;
}
@end

@program solid solid_vs solid_fs
