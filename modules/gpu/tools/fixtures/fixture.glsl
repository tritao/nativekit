@include palette.glsl

@vs vs
layout(location=0) in vec2 position;
layout(std140, binding=0) uniform params {
    float shift;
    vec4 weights[2];
};
@include_block palette
void main() { gl_Position = vec4(position * shade_scale(), shift, 1.0) + weights[0] + weights[1]; }
@end

@fs fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;
layout(location=0) out vec4 color;
void main() { color = texture(sampler2D(tex, smp), vec2(0.5)); }
@end

@program fixture vs fs
