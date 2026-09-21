{{VERSION}}
{{VERTEX_PRECISION}}

layout(location=0) in vec3 position;
layout(location=1) in vec4 transform0;
layout(location=2) in vec4 transform1;
layout(location=3) in vec4 transform2;
layout(location=4) in vec4 transform3;
layout(location=5) in vec3 normal;
layout(location=6) in vec2 texcoord0;
layout(location=7) in vec4 color0;
uniform mat4 view_projection;
out vec3 world_position;
out vec3 vertex_normal;
out vec2 vertex_texcoord;
out vec4 vertex_color;

void main() {
    mat4 transform = mat4(transform0, transform1, transform2, transform3);
    world_position = (transform * vec4(position, 1.0)).xyz;
    vertex_normal = normalize((transform * vec4(normal, 0.0)).xyz);
    vertex_texcoord = texcoord0;
    vertex_color = color0;
    gl_Position = view_projection * vec4(world_position, 1.0);
}
