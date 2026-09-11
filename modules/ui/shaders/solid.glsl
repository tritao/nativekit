// NativeKit solid-color path shader.
//
// This is the minimal path used for flat-color geometry and stencil setup.
// Prepared framebuffer-pixel positions are converted to clip space in the
// vertex stage, while the fragment stage outputs the supplied premultiplied
// color unchanged. Keeping this separate from the path paint shader avoids
// binding a texture for solid fills and gives stencil-only passes a matching
// vertex layout.

@module nkui_solid

@vs vs
layout(binding=0) uniform solid_vs_params {
    vec2 viewport;
};

in vec2 position;

void main() {
    vec2 p = vec2(position.x / viewport.x * 2.0 - 1.0,
                  1.0 - position.y / viewport.y * 2.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
@end

@fs fs
layout(binding=1) uniform solid_fs_params {
    vec4 color;
};

out vec4 frag_color;

void main() {
    frag_color = color;
}
@end

@program solid vs fs
