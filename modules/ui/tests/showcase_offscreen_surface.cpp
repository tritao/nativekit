#include "showcase_offscreen_surface.h"

#include "adapter_internal.h"
#include "core/executor.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

struct Vertex {
    float x;
    float y;
    float z;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
};

using Matrix = std::array<float, 16>;

constexpr std::array<Vertex, 24> cube_vertices = {{
    {-1, -1, 1, 52, 190, 238, 255},   {1, -1, 1, 52, 190, 238, 255},
    {1, 1, 1, 52, 190, 238, 255},     {-1, 1, 1, 52, 190, 238, 255},
    {1, -1, -1, 242, 104, 143, 255},  {-1, -1, -1, 242, 104, 143, 255},
    {-1, 1, -1, 242, 104, 143, 255},  {1, 1, -1, 242, 104, 143, 255},
    {1, -1, 1, 50, 214, 143, 255},    {1, -1, -1, 50, 214, 143, 255},
    {1, 1, -1, 50, 214, 143, 255},    {1, 1, 1, 50, 214, 143, 255},
    {-1, -1, -1, 247, 171, 76, 255},  {-1, -1, 1, 247, 171, 76, 255},
    {-1, 1, 1, 247, 171, 76, 255},    {-1, 1, -1, 247, 171, 76, 255},
    {-1, 1, 1, 143, 105, 245, 255},   {1, 1, 1, 143, 105, 245, 255},
    {1, 1, -1, 143, 105, 245, 255},   {-1, 1, -1, 143, 105, 245, 255},
    {-1, -1, -1, 255, 207, 112, 255}, {1, -1, -1, 255, 207, 112, 255},
    {1, -1, 1, 255, 207, 112, 255},   {-1, -1, 1, 255, 207, 112, 255},
}};

constexpr std::array<uint32_t, 36> cube_indices = [] {
    std::array<uint32_t, 36> indices{};
    for (uint32_t face = 0; face < 6; ++face) {
        const uint32_t vertex = face * 4;
        const uint32_t index = face * 6;
        indices[index] = vertex;
        indices[index + 1] = vertex + 1;
        indices[index + 2] = vertex + 2;
        indices[index + 3] = vertex;
        indices[index + 4] = vertex + 2;
        indices[index + 5] = vertex + 3;
    }
    return indices;
}();

Matrix multiply(const Matrix &left, const Matrix &right) {
    Matrix output{};
    for (size_t column = 0; column < 4; ++column)
        for (size_t row = 0; row < 4; ++row)
            for (size_t inner = 0; inner < 4; ++inner)
                output[column * 4 + row] += left[inner * 4 + row] * right[column * 4 + inner];
    return output;
}

Matrix model_view_projection(float rotation, float aspect) {
    constexpr float near_plane = 0.1f;
    constexpr float far_plane = 20.0f;
    constexpr float field_of_view = 0.7853981633974483f;
    const float focal = 1.0f / std::tan(field_of_view * 0.5f);
    Matrix projection{};
    projection[0] = focal / aspect;
    projection[5] = focal;
    projection[10] = (far_plane + near_plane) / (near_plane - far_plane);
    projection[11] = -1.0f;
    projection[14] = (2.0f * far_plane * near_plane) / (near_plane - far_plane);
    Matrix translation{};
    translation[0] = translation[5] = translation[10] = translation[15] = 1.0f;
    translation[14] = -4.4f;
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    Matrix rotate_x{};
    rotate_x[0] = rotate_x[15] = 1.0f;
    rotate_x[5] = rotate_x[10] = cosine;
    rotate_x[6] = sine;
    rotate_x[9] = -sine;
    Matrix rotate_y{};
    rotate_y[5] = rotate_y[15] = 1.0f;
    rotate_y[0] = rotate_y[10] = cosine;
    rotate_y[2] = -sine;
    rotate_y[8] = sine;
    return multiply(projection, multiply(translation, multiply(rotate_y, rotate_x)));
}

std::array<Vertex, 24> transformed_vertices(float rotation, int width, int height) {
    const Matrix matrix = model_view_projection(rotation, static_cast<float>(width) /
                                                             static_cast<float>(height));
    auto output = cube_vertices;
    for (auto &vertex : output) {
        const float x = vertex.x;
        const float y = vertex.y;
        const float z = vertex.z;
        const float clip_x = matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12];
        const float clip_y = matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13];
        const float clip_z = matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14];
        const float clip_w = matrix[3] * x + matrix[7] * y + matrix[11] * z + matrix[15];
        vertex.x = clip_x / clip_w;
        vertex.y = clip_y / clip_w;
        vertex.z = clip_z / clip_w;
    }
    return output;
}

const char *vertex_source(nk_graphics_api api) {
    if (api == NK_GRAPHICS_OPENGL_ES)
        return "#version 300 es\nprecision mediump float;\nlayout(location=0) in vec3 position;\n"
               "layout(location=1) in vec4 color0; out vec4 color;\n"
               "void main(){ gl_Position=vec4(position,1.0); color=color0; }\n";
    if (api == NK_GRAPHICS_OPENGL)
        return "#version 330\nlayout(location=0) in vec3 position; layout(location=1) in vec4 color0;\n"
               "out vec4 color; void main(){ gl_Position=vec4(position,1.0); color=color0; }\n";
    if (api == NK_GRAPHICS_D3D11)
        return "struct VSIn { float3 position:TEXCOORD0; float4 color0:TEXCOORD1; };\n"
               "struct VSOut { float4 position:SV_Position; float4 color:TEXCOORD0; };\n"
               "VSOut main(VSIn input){ VSOut output; output.position=float4(input.position,1); "
               "output.color=input.color0; return output; }\n";
    return "#include <metal_stdlib>\nusing namespace metal;\nstruct VSIn { float3 position "
           "[[attribute(0)]]; float4 color0 [[attribute(1)]]; }; struct VSOut { float4 position "
           "[[position]]; float4 color [[user(locn0)]]; }; vertex VSOut main(VSIn input "
           "[[stage_in]]) { VSOut output; output.position=float4(input.position,1); "
           "output.color=input.color0; return output; }\n";
}

const char *fragment_source(nk_graphics_api api) {
    if (api == NK_GRAPHICS_OPENGL_ES)
        return "#version 300 es\nprecision mediump float; layout(location=0) out vec4 frag_color; "
               "in vec4 color; void main(){ frag_color=color; }\n";
    if (api == NK_GRAPHICS_OPENGL)
        return "#version 330\nout vec4 frag_color; in vec4 color; void main(){ frag_color=color; }\n";
    if (api == NK_GRAPHICS_D3D11)
        return "struct PSIn { float4 color:TEXCOORD0; }; float4 main(PSIn input):SV_Target "
               "{ return input.color; }\n";
    return "#include <metal_stdlib>\nusing namespace metal; struct PSIn { float4 color "
           "[[user(locn0)]]; }; fragment float4 main(PSIn input [[stage_in]]) { return input.color; }\n";
}

struct RenderTask {
    ShowcaseOffscreenSurface *surface = nullptr;
    nk_surface_frame_target target{};
    float rotation = 0.0f;
    int width = 0;
    int height = 0;
    bool success = false;
};

void render_task(void *data) {
    auto &task = *static_cast<RenderTask *>(data);
    task.success = task.surface->render_on_executor(task.target, task.rotation, task.width,
                                                    task.height);
}

void destroy_task(void *data) {
    auto &task = *static_cast<RenderTask *>(data);
    task.surface->destroy_on_executor();
    task.success = true;
}

} // namespace

ShowcaseOffscreenSurface::ShowcaseOffscreenSurface(nk_surface surface) : surface_(surface) {}

ShowcaseOffscreenSurface::~ShowcaseOffscreenSurface() {
    destroy();
}

bool ShowcaseOffscreenSurface::create(int width, int height) {
    return update(0.0f, width, height);
}

bool ShowcaseOffscreenSurface::render_on_executor(const nk_surface_frame_target &frame_target,
                                                  float rotation, int width, int height) {
    if (renderer_.id) {
        nkgpu_renderer_state state = NKGPU_RENDERER_READY;
        if (nkgpu_renderer_get_state(renderer_, &state) != NKGPU_OK ||
            state == NKGPU_RENDERER_LOST)
            destroy_on_executor();
    }
    if (!renderer_.id) {
        if (nkgpu_renderer_create_for_frame_target(surface_, &frame_target, &renderer_) != NKGPU_OK)
            return false;
        const nkgpu_shader_language language =
            frame_target.api == NK_GRAPHICS_D3D11
                ? NKGPU_SHADERLANGUAGE_HLSL5
                : frame_target.api == NK_GRAPHICS_METAL ? NKGPU_SHADERLANGUAGE_MSL
                                                        : NKGPU_SHADERLANGUAGE_GLSL;
        nkgpu_shader_builder shader_builder{};
        if (nkgpu_shader_begin(renderer_, language, vertex_source(frame_target.api),
                               fragment_source(frame_target.api), &shader_builder) != NKGPU_OK ||
            nkgpu_shader_attribute(shader_builder, 0, "position", "TEXCOORD", 0) != NKGPU_OK ||
            nkgpu_shader_attribute(shader_builder, 1, "color0", "TEXCOORD", 1) != NKGPU_OK ||
            nkgpu_shader_end(shader_builder, &shader_) != NKGPU_OK)
            return false;
        nkgpu_pipeline_builder pipeline_builder{};
        if (nkgpu_pipeline_begin(renderer_, shader_, sizeof(Vertex), &pipeline_builder) != NKGPU_OK ||
            nkgpu_pipeline_attribute(pipeline_builder, 0, 0, offsetof(Vertex, x),
                                     NKGPU_VERTEXFORMAT_FLOAT3) != NKGPU_OK ||
            nkgpu_pipeline_attribute(pipeline_builder, 1, 0, offsetof(Vertex, red),
                                     NKGPU_VERTEXFORMAT_UBYTE4N) != NKGPU_OK ||
            nkgpu_pipeline_depth_stencil(pipeline_builder, 1) != NKGPU_OK ||
            nkgpu_pipeline_cull_mode(pipeline_builder, NKGPU_CULLMODE_BACK,
                                     NKGPU_FACEWINDING_CCW) != NKGPU_OK ||
            nkgpu_pipeline_index_type(pipeline_builder, NKGPU_INDEXTYPE_UINT16) != NKGPU_OK ||
            nkgpu_pipeline_end(pipeline_builder, &pipeline_) != NKGPU_OK)
            return false;
        nkgpu_buffer_builder index_builder{};
        if (nkgpu_buffer_begin_kind(renderer_, static_cast<uint32_t>(cube_indices.size() * 2),
                                    NKGPU_BUFFER_INDEX, &index_builder) != NKGPU_OK)
            return false;
        for (size_t index = 0; index < cube_indices.size(); ++index)
            if (nkgpu_buffer_write_u16(index_builder, static_cast<uint32_t>(index * 2),
                                       cube_indices[index]) != NKGPU_OK)
                return false;
        if (nkgpu_buffer_end(index_builder, &index_buffer_) != NKGPU_OK)
            return false;
    }
    if (target_image_.id && (width_ != width || height_ != height)) {
        if (nkgpu_image_destroy(renderer_, target_image_) != NKGPU_OK ||
            nkgpu_image_destroy(renderer_, depth_image_) != NKGPU_OK)
            return false;
        target_image_ = {};
        depth_image_ = {};
        image_ = {};
    }
    if (!target_image_.id) {
        nkgpu_image_desc color_desc{};
        color_desc.struct_size = sizeof(color_desc);
        color_desc.width = static_cast<uint32_t>(width);
        color_desc.height = static_cast<uint32_t>(height);
        color_desc.format = NKGPU_IMAGEFORMAT_RGBA8;
        color_desc.usage = NKGPU_IMAGE_SAMPLED | NKGPU_IMAGE_RENDER_TARGET;
        color_desc.sample_count = 1;
        color_desc.layer_count = 1;
        nkgpu_image_desc depth_desc = color_desc;
        depth_desc.format = NKGPU_IMAGEFORMAT_DEPTH24_STENCIL8;
        depth_desc.usage = NKGPU_IMAGE_DEPTH_STENCIL;
        if (nkgpu_image_create_desc(renderer_, &color_desc, &target_image_) != NKGPU_OK ||
            nkgpu_image_create_desc(renderer_, &depth_desc, &depth_image_) != NKGPU_OK ||
            nkgpu_image_get_graphics_image(renderer_, target_image_, &image_) != NKGPU_OK)
            return false;
        width_ = width;
        height_ = height;
    }
    const auto vertices = transformed_vertices(rotation, width, height);
    nkgpu_buffer vertex_buffer{};
    if (nkgpu_buffer_create(renderer_, reinterpret_cast<const uint8_t *>(vertices.data()),
                            sizeof(vertices), &vertex_buffer) != NKGPU_OK)
        return false;
    const bool frame_started = nkgpu_frame_begin_with_target(renderer_, &frame_target) == NKGPU_OK;
    bool success = frame_started;
    nkgpu_render_pass_desc pass{};
    pass.struct_size = sizeof(pass);
    pass.color_count = 1;
    pass.colors[0].image = target_image_;
    pass.colors[0].action.load_action = NKGPU_LOADACTION_CLEAR;
    pass.colors[0].action.store_action = NKGPU_STOREACTION_STORE;
    pass.colors[0].action.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
    pass.depth_stencil = depth_image_;
    pass.depth_stencil_action.load_action = NKGPU_LOADACTION_CLEAR;
    pass.depth_stencil_action.store_action = NKGPU_STOREACTION_STORE;
    pass.depth_stencil_action.clear_depth = 1.0f;
    pass.depth_stencil_action.clear_stencil = 0;
    if (success)
        success = nkgpu_begin_render_pass(renderer_, &pass) == NKGPU_OK;
    if (success)
        success = nkgpu_apply_pipeline(renderer_, pipeline_) == NKGPU_OK;
    if (success)
        success = nkgpu_apply_vertex_buffer(renderer_, 0, vertex_buffer, 0) == NKGPU_OK;
    if (success)
        success = nkgpu_apply_index_buffer(renderer_, index_buffer_, 0) == NKGPU_OK;
    if (success)
        success = nkgpu_draw(renderer_, 0, static_cast<uint32_t>(cube_indices.size()), 1) == NKGPU_OK;
    if (success)
        success = nkgpu_end_pass(renderer_) == NKGPU_OK;
    if (frame_started)
        (void)nkgpu_end_frame_deferred_present(renderer_);
    if (vertex_buffer.id)
        (void)nkgpu_buffer_destroy(renderer_, vertex_buffer);
    return success;
}

void ShowcaseOffscreenSurface::destroy_on_executor() {
    if (target_image_.id)
        (void)nkgpu_image_destroy(renderer_, target_image_);
    if (depth_image_.id)
        (void)nkgpu_image_destroy(renderer_, depth_image_);
    if (index_buffer_.id)
        (void)nkgpu_buffer_destroy(renderer_, index_buffer_);
    if (pipeline_.id)
        (void)nkgpu_pipeline_destroy(renderer_, pipeline_);
    if (shader_.id)
        (void)nkgpu_shader_destroy(renderer_, shader_);
    if (renderer_.id)
        (void)nkgpu_renderer_destroy(renderer_);
    target_image_ = {};
    depth_image_ = {};
    image_ = {};
    index_buffer_ = {};
    pipeline_ = {};
    shader_ = {};
    renderer_ = {};
    width_ = height_ = 0;
}

bool ShowcaseOffscreenSurface::update(float rotation, int width, int height) {
    if (width <= 0 || height <= 0 || surface_ == NK_INVALID_HANDLE)
        return false;
    nk_surface_frame frame = NK_INVALID_HANDLE;
    nk_surface_frame_target frame_target{};
    frame_target.struct_size = sizeof(frame_target);
    if (nk_surface_acquire_frame(surface_, &frame, &frame_target) != NK_OK)
        return false;
    RenderTask task{this, frame_target, rotation, width, height, false};
    const nk_result dispatched = nk::core::dispatch_to_render_sync(&render_task, &task, sizeof(task));
    const nk_result closed = nk_surface_cancel_frame(frame);
    if (dispatched != NK_OK || closed != NK_OK || !task.success)
        return false;
    if (!resource_.id) {
        if (nkui_graphics_surface_create(image_, &resource_) != NKUI_OK)
            return false;
    } else if (nkui_graphics_surface_publish_image(resource_, image_) != NKUI_OK) {
        return false;
    }
    return true;
}

void ShowcaseOffscreenSurface::destroy() {
    if (resource_.id) {
        (void)nkui_resource_destroy(resource_);
        resource_ = {};
    }
    if (!renderer_.id)
        return;
    RenderTask task{this, {}, 0.0f, 0, 0, false};
    (void)nk::core::dispatch_to_render_sync(&destroy_task, &task, sizeof(task));
    image_ = {};
}
