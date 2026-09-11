@module ui_toolchain_probe

@vs vs
in vec2 position;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
}
@end

@fs fs
out vec4 frag_color;
void main() {
    frag_color = vec4(1.0);
}
@end

@program ui_toolchain_probe vs fs
