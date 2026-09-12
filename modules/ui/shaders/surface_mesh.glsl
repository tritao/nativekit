// Shared indexed-mesh path used by NativeKit offscreen surface producers.

@module nkui_surface_mesh

@vs vs
layout(binding=0) uniform surface_mesh_vs_params {
    mat4 model_view_projection;
};

in vec3 position;
in vec4 color0;
out vec4 color;

void main() {
    gl_Position = model_view_projection * vec4(position, 1.0);
    color = color0;
}
@end

@fs fs
in vec4 color;
out vec4 frag_color;

void main() {
    frag_color = color;
}
@end

@program surface_mesh vs fs
