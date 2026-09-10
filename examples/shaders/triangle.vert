#version 450

layout(location = 0) out vec3 vertex_color;

const vec2 positions[3] = vec2[](
    vec2(0.0, -0.72),
    vec2(0.72, 0.58),
    vec2(-0.72, 0.58)
);

const vec3 colors[3] = vec3[](
    vec3(1.0, 0.30, 0.35),
    vec3(0.30, 0.55, 1.0),
    vec3(0.25, 0.85, 0.55)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    vertex_color = colors[gl_VertexIndex];
}
