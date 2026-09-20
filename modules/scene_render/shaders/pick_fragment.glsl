{{VERSION}}
{{FRAGMENT_PRECISION}}

in vec4 vertex_pick_color;
in vec3 world_position;
uniform vec4 clip_planes[32];
uniform vec4 clip_plane_count;
out vec4 fragment_color;
layout(location=1) out vec4 fragment_subelement;

void main() {
    for (int index = 0; index < 32; ++index) {
        if (index >= int(clip_plane_count.x))
            break;
        if (dot(clip_planes[index].xyz, world_position) + clip_planes[index].w < 0.0)
            discard;
    }
    uint id = uint(gl_PrimitiveID) + 1u;
    fragment_color = vertex_pick_color;
    fragment_subelement = vec4(float(id & 0xffu) / 255.0,
                               float((id >> 8u) & 0xffu) / 255.0,
                               float((id >> 16u) & 0xffu) / 255.0, 1.0);
}
