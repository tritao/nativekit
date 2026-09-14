// Canonical NativeKit UI surface mesh shaders.

@vs surface_mesh_vs
layout(binding=0) uniform surface_mesh_vs_params {
    vec4 value[4];
};
layout(location=0) in vec3 position;
layout(location=1) in vec4 color0;
layout(location=0) out vec4 color;
void main() {
    mat4 transform = mat4(value[0], value[1], value[2], value[3]);
    gl_Position = transform * vec4(position, 1.0);
    color = color0;
}
@end

@fs surface_mesh_fs
layout(location=0) in vec4 color;
layout(location=0) out vec4 frag_color;
void main() {
    frag_color = color;
}
@end

@program surface_mesh surface_mesh_vs surface_mesh_fs
