#include "graphics_device.h"

#include "nativekit_sokol_runtime.h"
#include "sokol_backend.h"

#include "nkui_composite.glsl.h"
#include "nkui_path.glsl.h"
#include "nkui_solid.glsl.h"
#include "nkui_text.glsl.h"

#include <cstddef>
#include <mutex>

namespace nkui {
namespace {

std::mutex device_mutex;
std::weak_ptr<GraphicsDevice> shared_device;

constexpr uint32_t kHandleSlotMask = 0xFFFFu;
constexpr uint32_t kHandleGenerationMask = 0x0FFFu;

uint32_t encode_buffer_handle(size_t slot, uint16_t generation) {
    return (static_cast<uint32_t>(generation) << 16) |
           static_cast<uint32_t>(slot + 1);
}

bool decode_buffer_handle(GpuBufferHandle handle, size_t &slot, uint16_t &generation) {
    const uint32_t encoded_slot = handle.value & kHandleSlotMask;
    const uint32_t encoded_generation = (handle.value >> 16) & kHandleGenerationMask;
    if (!encoded_slot || !encoded_generation)
        return false;
    slot = encoded_slot - 1;
    generation = static_cast<uint16_t>(encoded_generation);
    return true;
}

struct PathVertex {
    float x;
    float y;
    float u;
    float v;
};

struct TextureVertex {
    float x;
    float y;
    float u;
    float v;
};

sg_shader make_solid_shader() {
    const sg_shader_desc *desc = nkui_solid_solid_shader_desc(sg_query_backend());
    return desc ? sg_make_shader(desc) : sg_shader{};
}

sg_shader make_path_shader() {
    const sg_shader_desc *desc = nkui_path_path_shader_desc(sg_query_backend());
    return desc ? sg_make_shader(desc) : sg_shader{};
}

sg_shader make_glyph_shader(GlyphMode mode) {
    const sg_shader_desc *desc = nullptr;
    switch (mode) {
    case GlyphMode::Alpha:
        desc = nkui_text_alpha_shader_desc(sg_query_backend());
        break;
    case GlyphMode::Sdf:
        desc = nkui_text_sdf_shader_desc(sg_query_backend());
        break;
    case GlyphMode::Color:
        desc = nkui_text_color_shader_desc(sg_query_backend());
        break;
    }
    return desc ? sg_make_shader(desc) : sg_shader{};
}

sg_shader make_composite_shader() {
    const sg_shader_desc *desc = nkui_composite_composite_shader_desc(sg_query_backend());
    return desc ? sg_make_shader(desc) : sg_shader{};
}

sg_pipeline make_solid_pipeline(sg_shader shader) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(SolidVertex);
    desc.layout.attrs[0] = {0, offsetof(SolidVertex, x), SG_VERTEXFORMAT_FLOAT2};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    return sg_make_pipeline(&desc);
}

sg_pipeline make_fill_stencil_pipeline(sg_shader shader, bool even_odd = false) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(SolidVertex);
    desc.layout.attrs[0] = {0, offsetof(SolidVertex, x), SG_VERTEXFORMAT_FLOAT2};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].write_mask = SG_COLORMASK_NONE;
    desc.cull_mode = SG_CULLMODE_NONE;
    desc.stencil.enabled = true;
    desc.stencil.front.compare = SG_COMPAREFUNC_ALWAYS;
    desc.stencil.front.pass_op = even_odd ? SG_STENCILOP_INVERT : SG_STENCILOP_INCR_WRAP;
    desc.stencil.back.compare = SG_COMPAREFUNC_ALWAYS;
    desc.stencil.back.pass_op = even_odd ? SG_STENCILOP_INVERT : SG_STENCILOP_DECR_WRAP;
    desc.stencil.read_mask = 0xFF;
    desc.stencil.write_mask = 0xFF;
    return sg_make_pipeline(&desc);
}

sg_pipeline make_fill_cover_pipeline(sg_shader shader) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(SolidVertex);
    desc.layout.attrs[0] = {0, offsetof(SolidVertex, x), SG_VERTEXFORMAT_FLOAT2};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.stencil.enabled = true;
    desc.stencil.front.compare = SG_COMPAREFUNC_NOT_EQUAL;
    desc.stencil.front.fail_op = SG_STENCILOP_ZERO;
    desc.stencil.front.depth_fail_op = SG_STENCILOP_ZERO;
    desc.stencil.front.pass_op = SG_STENCILOP_ZERO;
    desc.stencil.back = desc.stencil.front;
    desc.stencil.read_mask = 0xFF;
    desc.stencil.write_mask = 0xFF;
    return sg_make_pipeline(&desc);
}

sg_pipeline make_paint_pipeline(sg_shader shader, bool stencil_cover,
                                bool stencil_fringe = false) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(PathVertex);
    desc.layout.attrs[0] = {0, offsetof(PathVertex, x), SG_VERTEXFORMAT_FLOAT2};
    desc.layout.attrs[1] = {0, offsetof(PathVertex, u), SG_VERTEXFORMAT_FLOAT2};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    if (stencil_cover) {
        desc.stencil.enabled = true;
        desc.stencil.front.compare = SG_COMPAREFUNC_NOT_EQUAL;
        desc.stencil.front.fail_op = SG_STENCILOP_ZERO;
        desc.stencil.front.depth_fail_op = SG_STENCILOP_ZERO;
        desc.stencil.front.pass_op = SG_STENCILOP_ZERO;
        desc.stencil.back = desc.stencil.front;
        desc.stencil.read_mask = 0xFF;
        desc.stencil.write_mask = 0xFF;
    } else if (stencil_fringe) {
        desc.stencil.enabled = true;
        desc.stencil.front.compare = SG_COMPAREFUNC_EQUAL;
        desc.stencil.back.compare = SG_COMPAREFUNC_EQUAL;
        desc.stencil.read_mask = 0xFF;
        desc.stencil.write_mask = 0xFF;
    }
    return sg_make_pipeline(&desc);
}

sg_pipeline make_glyph_pipeline(sg_shader shader) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(GlyphVertex);
    desc.layout.attrs[0] = {0, offsetof(GlyphVertex, x), SG_VERTEXFORMAT_FLOAT2};
    desc.layout.attrs[1] = {0, offsetof(GlyphVertex, u), SG_VERTEXFORMAT_FLOAT2};
    desc.layout.attrs[2] = {0, offsetof(GlyphVertex, red), SG_VERTEXFORMAT_UBYTE4N};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    return sg_make_pipeline(&desc);
}

sg_pipeline make_composite_pipeline(sg_shader shader) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(TextureVertex);
    desc.layout.attrs[0] = {0, offsetof(TextureVertex, x), SG_VERTEXFORMAT_FLOAT2};
    desc.layout.attrs[1] = {0, offsetof(TextureVertex, u), SG_VERTEXFORMAT_FLOAT2};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    return sg_make_pipeline(&desc);
}

void destroy_resources(GraphicsDeviceResources &resources) {
    sg_destroy_sampler(resources.white_sampler);
    sg_destroy_view(resources.white_view);
    sg_destroy_image(resources.white_image);
    sg_destroy_sampler(resources.surface_sampler);
    sg_destroy_sampler(resources.sampler);
    sg_destroy_pipeline(resources.color_glyph_pipeline);
    sg_destroy_shader(resources.color_glyph_shader);
    sg_destroy_pipeline(resources.sdf_glyph_pipeline);
    sg_destroy_shader(resources.sdf_glyph_shader);
    sg_destroy_pipeline(resources.alpha_glyph_pipeline);
    sg_destroy_shader(resources.alpha_glyph_shader);
    sg_destroy_pipeline(resources.composite_pipeline);
    sg_destroy_shader(resources.composite_shader);
    sg_destroy_pipeline(resources.solid_pipeline);
    sg_destroy_pipeline(resources.fill_cover_pipeline);
    sg_destroy_pipeline(resources.fill_stencil_pipeline);
    sg_destroy_pipeline(resources.fill_stencil_even_odd_pipeline);
    sg_destroy_pipeline(resources.paint_cover_pipeline);
    sg_destroy_pipeline(resources.paint_fringe_pipeline);
    sg_destroy_pipeline(resources.paint_pipeline);
    sg_destroy_shader(resources.paint_shader);
    sg_destroy_shader(resources.solid_shader);
    resources = {};
}

bool resources_valid(const GraphicsDeviceResources &resources) {
    return sg_query_shader_state(resources.solid_shader) == SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(resources.solid_pipeline) == SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(resources.fill_stencil_pipeline) == SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(resources.fill_stencil_even_odd_pipeline) ==
               SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(resources.fill_cover_pipeline) == SG_RESOURCESTATE_VALID &&
           sg_query_shader_state(resources.paint_shader) == SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(resources.paint_pipeline) == SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(resources.paint_cover_pipeline) == SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(resources.paint_fringe_pipeline) == SG_RESOURCESTATE_VALID &&
           sg_query_shader_state(resources.alpha_glyph_shader) == SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(resources.alpha_glyph_pipeline) == SG_RESOURCESTATE_VALID &&
           sg_query_shader_state(resources.sdf_glyph_shader) == SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(resources.sdf_glyph_pipeline) == SG_RESOURCESTATE_VALID &&
           sg_query_shader_state(resources.color_glyph_shader) == SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(resources.color_glyph_pipeline) == SG_RESOURCESTATE_VALID &&
           sg_query_shader_state(resources.composite_shader) == SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(resources.composite_pipeline) == SG_RESOURCESTATE_VALID &&
           sg_query_sampler_state(resources.sampler) == SG_RESOURCESTATE_VALID &&
           sg_query_sampler_state(resources.surface_sampler) == SG_RESOURCESTATE_VALID &&
           sg_query_image_state(resources.white_image) == SG_RESOURCESTATE_VALID &&
           sg_query_view_state(resources.white_view) == SG_RESOURCESTATE_VALID &&
           sg_query_sampler_state(resources.white_sampler) == SG_RESOURCESTATE_VALID;
}

} // namespace

GpuResourceRegistry::~GpuResourceRegistry() {
    clear();
}

GpuBufferHandle GpuResourceRegistry::create_buffer(const sg_buffer_desc &description) {
    size_t slot_index = buffers_.size();
    for (size_t index = 0; index < buffers_.size(); ++index) {
        if (!buffers_[index].active) {
            slot_index = index;
            break;
        }
    }
    if (slot_index >= kHandleSlotMask)
        return {};
    if (slot_index == buffers_.size())
        buffers_.push_back({});
    auto &slot = buffers_[slot_index];
    slot.value = sg_make_buffer(&description);
    if (sg_query_buffer_state(slot.value) != SG_RESOURCESTATE_VALID) {
        slot.value = {};
        return {};
    }
    slot.active = true;
    return {encode_buffer_handle(slot_index, slot.generation)};
}

sg_buffer GpuResourceRegistry::resolve(GpuBufferHandle handle) const {
    size_t slot_index = 0;
    uint16_t generation = 0;
    if (!decode_buffer_handle(handle, slot_index, generation) || slot_index >= buffers_.size())
        return {};
    const auto &slot = buffers_[slot_index];
    return slot.active && slot.generation == generation ? slot.value : sg_buffer{};
}

void GpuResourceRegistry::destroy(GpuBufferHandle handle) {
    size_t slot_index = 0;
    uint16_t generation = 0;
    if (!decode_buffer_handle(handle, slot_index, generation) || slot_index >= buffers_.size())
        return;
    auto &slot = buffers_[slot_index];
    if (!slot.active || slot.generation != generation)
        return;
    sg_destroy_buffer(slot.value);
    slot.value = {};
    slot.active = false;
    slot.generation = static_cast<uint16_t>((slot.generation % kHandleGenerationMask) + 1);
}

void GpuResourceRegistry::clear() {
    for (auto &slot : buffers_) {
        if (slot.active)
            sg_destroy_buffer(slot.value);
        slot = {};
        slot.generation = 1;
    }
}

GraphicsDevice::GraphicsDevice() {
    sg_desc desc{};
    desc.environment.defaults = {SG_PIXELFORMAT_RGBA8, SG_PIXELFORMAT_DEPTH_STENCIL, 1};
    if (!nk_sokol_runtime_acquire(&desc)) {
        error_ = "Sokol graphics runtime acquisition failed";
        return;
    }
    runtime_acquired_ = true;

    resources_.solid_shader = make_solid_shader();
    resources_.paint_shader = make_path_shader();
    resources_.alpha_glyph_shader = make_glyph_shader(GlyphMode::Alpha);
    resources_.sdf_glyph_shader = make_glyph_shader(GlyphMode::Sdf);
    resources_.color_glyph_shader = make_glyph_shader(GlyphMode::Color);
    resources_.composite_shader = make_composite_shader();
    resources_.solid_pipeline = make_solid_pipeline(resources_.solid_shader);
    resources_.fill_stencil_pipeline = make_fill_stencil_pipeline(resources_.solid_shader);
    resources_.fill_stencil_even_odd_pipeline =
        make_fill_stencil_pipeline(resources_.solid_shader, true);
    resources_.fill_cover_pipeline = make_fill_cover_pipeline(resources_.solid_shader);
    resources_.paint_pipeline = make_paint_pipeline(resources_.paint_shader, false);
    resources_.paint_cover_pipeline = make_paint_pipeline(resources_.paint_shader, true);
    resources_.paint_fringe_pipeline = make_paint_pipeline(resources_.paint_shader, false, true);
    resources_.alpha_glyph_pipeline = make_glyph_pipeline(resources_.alpha_glyph_shader);
    resources_.sdf_glyph_pipeline = make_glyph_pipeline(resources_.sdf_glyph_shader);
    resources_.color_glyph_pipeline = make_glyph_pipeline(resources_.color_glyph_shader);
    resources_.composite_pipeline = make_composite_pipeline(resources_.composite_shader);

    sg_sampler_desc sampler_desc{};
    sampler_desc.min_filter = SG_FILTER_NEAREST;
    sampler_desc.mag_filter = SG_FILTER_NEAREST;
    sampler_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sampler_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    resources_.sampler = sg_make_sampler(&sampler_desc);
    sampler_desc.min_filter = SG_FILTER_LINEAR;
    sampler_desc.mag_filter = SG_FILTER_LINEAR;
    resources_.surface_sampler = sg_make_sampler(&sampler_desc);

    const uint32_t white_pixel = UINT32_MAX;
    sg_image_desc white_desc{};
    white_desc.width = 1;
    white_desc.height = 1;
    white_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    white_desc.data.mip_levels[0] = {&white_pixel, sizeof(white_pixel)};
    resources_.white_image = sg_make_image(&white_desc);
    sg_view_desc white_view_desc{};
    white_view_desc.texture.image = resources_.white_image;
    resources_.white_view = sg_make_view(&white_view_desc);
    sg_sampler_desc white_sampler_desc{};
    white_sampler_desc.min_filter = SG_FILTER_NEAREST;
    white_sampler_desc.mag_filter = SG_FILTER_NEAREST;
    white_sampler_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    white_sampler_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    resources_.white_sampler = sg_make_sampler(&white_sampler_desc);

    if (!resources_valid(resources_)) {
        error_ = "UI graphics resource creation failed";
        destroy_resources(resources_);
        nk_sokol_runtime_release();
        runtime_acquired_ = false;
        return;
    }
    valid_ = true;
}

GraphicsDevice::~GraphicsDevice() {
    if (!runtime_acquired_)
        return;
    std::lock_guard<std::mutex> lock(device_mutex);
    gpu_resources_.clear();
    destroy_resources(resources_);
    nk_sokol_runtime_release();
    runtime_acquired_ = false;
}

std::shared_ptr<GraphicsDevice> GraphicsDevice::acquire(std::string *error) {
    std::lock_guard<std::mutex> lock(device_mutex);
    if (auto device = shared_device.lock())
        return device;
    auto device = std::shared_ptr<GraphicsDevice>(new GraphicsDevice);
    if (!device->valid_) {
        if (error)
            *error = device->error_.empty() ? "Sokol graphics device initialization failed"
                                             : device->error_;
        return nullptr;
    }
    shared_device = device;
    return device;
}

} // namespace nkui
