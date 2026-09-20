{{VERSION}}
{{FRAGMENT_PRECISION}}

uniform vec4 material_color;
in vec3 world_position;
uniform vec4 clip_planes[32];
uniform vec4 clip_plane_count;
out vec4 fragment_color;

void main() {
    for (int index = 0; index < 32; ++index) {
        if (index >= int(clip_plane_count.x))
            break;
        if (dot(clip_planes[index].xyz, world_position) + clip_planes[index].w < 0.0)
            discard;
    }
    fragment_color = material_color;
}
