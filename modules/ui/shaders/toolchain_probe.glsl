// Build-only sokol-shdc probe.
//
// This shader is never used for rendering. CMake compiles it first to verify
// that the configured shader toolchain can generate a valid backend output
// before compiling the NativeKit path, solid, text, and composite modules.

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
