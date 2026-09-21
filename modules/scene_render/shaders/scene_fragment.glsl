{{VERSION}}
{{FRAGMENT_PRECISION}}

uniform vec4 base_color;
uniform vec4 material_params;
uniform vec4 emissive;
uniform sampler2D base_color_texture;
in vec3 world_position;
in vec3 vertex_normal;
in vec2 vertex_texcoord;
in vec4 vertex_color;
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
    vec4 texture_color = texture(base_color_texture, vertex_texcoord);
    vec3 light_direction = normalize(vec3(0.35, 0.45, 0.82));
    float diffuse = 0.35 + 0.65 * max(dot(normalize(vertex_normal), light_direction), 0.0);
    vec3 color = base_color.rgb * texture_color.rgb * vertex_color.rgb * diffuse + emissive.rgb;
    fragment_color = vec4(color, base_color.a * texture_color.a * vertex_color.a);
}
