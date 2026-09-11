// NativeKit render-target compositor shader.
//
// This shader draws a previously rendered offscreen target as a textured quad.
// The vertex stage converts framebuffer-pixel positions to clip space and
// forwards UVs; the fragment stage samples the target with the selected
// surface sampler and applies the caller's premultiplied tint/opacity. It is
// intentionally separate from path image paints because target composition
// has its own filtering and pass/layer lifetime semantics.

@module nkui_composite

@vs vs
layout(binding=0) uniform composite_vs_params {
    vec2 viewport;
};

in vec2 position;
in vec2 uv0;
out vec2 uv;

void main() {
    uv = uv0;
    vec2 p = vec2(position.x / viewport.x * 2.0 - 1.0,
                  1.0 - position.y / viewport.y * 2.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
@end

@fs fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(binding=1) uniform composite_fs_params {
    vec4 tint;
};

in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, smp), uv) * tint;
}
@end

@program composite vs fs
