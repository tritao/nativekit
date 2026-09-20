{{VERSION}}
{{VERTEX_PRECISION}}

layout(location=0) in vec3 position;
layout(location=1) in vec4 transform0;
layout(location=2) in vec4 transform1;
layout(location=3) in vec4 transform2;
layout(location=4) in vec4 transform3;
layout(location=5) in vec4 pick_color;
uniform mat4 view_projection;
out vec4 vertex_pick_color;
out vec3 world_position;

void main() {
    mat4 transform = mat4(transform0, transform1, transform2, transform3);
    vertex_pick_color = pick_color;
    world_position = (transform * vec4(position, 1.0)).xyz;
    gl_Position = view_projection * vec4(world_position, 1.0);
}
