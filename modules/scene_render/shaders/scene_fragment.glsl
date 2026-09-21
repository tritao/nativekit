{{VERSION}}
{{FRAGMENT_PRECISION}}

uniform vec4 base_color;
uniform vec4 material_params;
uniform vec4 emissive;
uniform vec4 lighting;
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
    vec3 light_direction = normalize(lighting.xyz);
    float diffuse = 0.35 + 0.65 * max(dot(normalize(vertex_normal), light_direction), 0.0) *
                    max(lighting.w, 0.0);
    float surface_response = mix(1.0, 0.65, clamp(material_params.x, 0.0, 1.0)) *
                             mix(0.5, 1.0, clamp(material_params.y, 0.0, 1.0));
    vec3 color = base_color.rgb * texture_color.rgb * vertex_color.rgb * diffuse *
                 surface_response + emissive.rgb;
    float alpha = base_color.a * texture_color.a * vertex_color.a;
    if (material_params.w > 1.5 && alpha < material_params.z)
        discard;
    fragment_color = vec4(color, alpha);
}
