@module nkui_text

@vs vs
layout(binding=0) uniform text_vs_params {
    vec2 viewport;
};

in vec2 position;
in vec2 uv0;
in vec4 color0;
out vec2 uv;
out vec4 color;

void main() {
    uv = uv0;
    color = color0;
    vec2 p = vec2(position.x / viewport.x * 2.0 - 1.0,
                  1.0 - position.y / viewport.y * 2.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
@end

@fs fs_alpha
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec4 color;
out vec4 frag_color;

void main() {
    float alpha = texture(sampler2D(tex, smp), uv).r;
    frag_color = vec4(color.rgb, color.a * alpha);
}
@end

@fs fs_sdf
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec4 color;
out vec4 frag_color;

void main() {
    float distance = texture(sampler2D(tex, smp), uv).r;
    float width = max(fwidth(distance), 0.001);
    float alpha = smoothstep(0.5 - width, 0.5 + width, distance);
    frag_color = vec4(color.rgb, color.a * alpha);
}
@end

@fs fs_color
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec4 color;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, smp), uv) * color;
}
@end

@program alpha vs fs_alpha
@program sdf vs fs_sdf
@program color vs fs_color
