#include "nativekit_gpu.h"
#include "nativekit_graphics.h"
#include "nativekit_sokol_api.h"
#include "adapter_internal.h"
#include "core/graphics_image_registry.h"
#include "core/executor.hpp"
#include "core/frame_backend.hpp"
#if defined(NKGPU_TESTING)
#include "testing.h"
#endif

#include "sokol_gfx.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>
#include <string>
#include <utility>
#include <vector>

enum Kind : uint32_t {
    RendererKind = 1,
    BufferKind,
    ShaderKind,
    PipelineKind,
    BufferBuilderKind,
    PipelineBuilderKind,
    ShaderBuilderKind,
    UniformBuilderKind,
    ImageKind,
    ImageBuilderKind,
    SamplerKind,
    RenderTargetKind,
    BatchKind
};
using Handle = uint32_t;

enum class RendererState : uint8_t {
    Ready,
    FrameActive,
    RenderTargetActive,
    Lost,
};

template <class T, Kind K, size_t N, bool Expandable = false> struct Pool {
    struct Slot {
        uint16_t generation = 1;
        bool active = false;
        /* Retained by at least one batch. A pinned slot keeps its value and its
           generation so retained handles still resolve, and is never reused. */
        uint32_t pins = 0;
        bool retired = false;
        /* Generation space is finite. Once the last representable generation
           has been destroyed, the slot must never identify a new resource. */
        bool generation_exhausted = false;
        T value{};
    };
    std::vector<Slot> slots;

    Pool() : slots(N) {}

    template <class U> Handle add(U &&value) {
        for (uint32_t i = 0; i < slots.size(); ++i)
            /* A retired slot still holds backend objects awaiting release. */
            if (!slots[i].active && !slots[i].pins && !slots[i].retired &&
                !slots[i].generation_exhausted) {
                slots[i].value = std::forward<U>(value);
                slots[i].active = true;
                slots[i].retired = false;
                return (uint32_t(K) << 28) | (uint32_t(slots[i].generation) << 16) | (i + 1);
            }
        if constexpr (Expandable) {
            /* Handles have a 16-bit slot field; grow while another
             * representable slot is available. */
            if (slots.size() < 0xFFFFu) {
                slots.emplace_back();
                auto &slot = slots.back();
                slot.value = std::forward<U>(value);
                slot.active = true;
                return (uint32_t(K) << 28) | (uint32_t(slot.generation) << 16) |
                       static_cast<uint32_t>(slots.size());
            }
        }
        return 0;
    }
    Slot *get(Handle h) {
        uint32_t encoded = h & 0xFFFF, generation = (h >> 16) & 0xFFF;
        if ((h >> 28) != K || !encoded || encoded > slots.size() || !generation)
            return nullptr;
        Slot &s = slots[encoded - 1];
        return s.active && s.generation == generation ? &s : nullptr;
    }
    Slot *get_retained(Handle h) {
        uint32_t encoded = h & 0xFFFF, generation = (h >> 16) & 0xFFF;
        if ((h >> 28) != K || !encoded || encoded > slots.size() || !generation)
            return nullptr;
        Slot &s = slots[encoded - 1];
        return (s.active || s.pins) && s.generation == generation ? &s : nullptr;
    }
    void advance_generation(Slot &s) {
        if (s.generation == 0xFFF) {
            s.generation_exhausted = true;
            return;
        }
        ++s.generation;
    }
    void remove(Slot &s) {
        s.active = false;
        if (s.pins) {
            /* The caller's handle is dead, but retained handles keep resolving. */
            s.retired = true;
            return;
        }
        s.retired = false;
        advance_generation(s);
        s.value = T{};
    }
    /* Final release of a retired slot once its last pin is gone. */
    void release(Slot &s) {
        s.active = false;
        s.retired = false;
        s.pins = 0;
        advance_generation(s);
        s.value = T{};
    }
};

struct Renderer {
    nk_surface surface = 0;
    const nk_sokol_api *api = nullptr;
    nk_graphics_api graphics_api = 0;
    nk_graphics_device device{};
    uint64_t native_device = 0;
    nk_surface_frame_target context_target{};
    bool has_context_target = false;
    sg_bindings bindings{};
    RendererState state = RendererState::Ready;
    bool in_pass = false;
    Handle active_target = 0;
    int32_t pass_width = 0;
    int32_t pass_height = 0;
    sg_pixel_format surface_color_format = SG_PIXELFORMAT_RGBA8;
    sg_pixel_format surface_depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    uint64_t frames = 0;
    uint64_t passes = 0;
    uint64_t draw_calls = 0;
    uint64_t upload_bytes = 0;
    uint64_t resource_creations = 0;
    uint64_t resource_destructions = 0;
    uint64_t surface_recreations = 0;
    uint64_t device_losses = 0;
    uint64_t failed_allocations = 0;
    nk_surface_frame_target frame_target{};
    bool has_frame_target = false;
#if defined(NKGPU_TESTING)
    uint64_t test_frames_before_loss = UINT64_MAX;
#endif
};
struct Buffer {
    Handle owner = 0;
    sg_buffer object{};
    uint32_t size = 0;
    bool stream = false;
};
struct Shader {
    Handle owner = 0;
    sg_shader object{};
    nkgpu_shader_language language = NKGPU_SHADERLANGUAGE_GLSL;
};
struct Pipeline {
    Handle owner = 0;
    sg_pipeline object{};
};
struct BufferBuilder {
    Handle owner = 0;
    uint8_t *data = nullptr;
    uint32_t size = 0;
    bool index = false;
};
struct PipelineBuilder {
    Handle owner = 0;
    sg_pipeline_desc desc{};
};
struct ShaderBuilder {
    Handle owner = 0;
    nkgpu_shader_language language = NKGPU_SHADERLANGUAGE_GLSL;
    sg_shader_desc desc{};
    std::string vertex_source;
    std::string fragment_source;
    std::array<std::array<std::string, SG_MAX_UNIFORMBLOCK_MEMBERS>, SG_MAX_UNIFORMBLOCK_BINDSLOTS>
        uniform_names;
    std::array<std::string, SG_MAX_TEXTURE_SAMPLER_PAIRS> texture_names;
    std::array<std::string, SG_MAX_VERTEX_ATTRIBUTES> glsl_attribute_names;
    std::array<std::string, SG_MAX_VERTEX_ATTRIBUTES> hlsl_semantic_names;
    std::array<uint32_t, SG_MAX_VERTEX_ATTRIBUTES> hlsl_semantic_indices{};
};
struct UniformBuilder {
    Handle owner = 0;
    uint8_t *data = nullptr;
    uint32_t size = 0;
};
struct Image {
    Handle owner = 0;
    sg_image object{};
    sg_view view{};
    uint32_t width = 0;
    uint32_t height = 0;
    nkgpu_image_format format = NKGPU_IMAGEFORMAT_RGBA8;
    bool dynamic_update = false;
    bool external = false;
    std::vector<uint8_t> pixels;
};
struct ImageBuilder {
    Handle owner = 0;
    uint8_t *data = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};
struct Sampler {
    Handle owner = 0;
    sg_sampler object{};
};
struct RenderTarget {
    Handle owner = 0;
    nk_graphics_image image{};
    sg_image color{};
    sg_view color_attachment{};
    sg_image depth{};
    sg_view depth_attachment{};
    int32_t width = 0;
    int32_t height = 0;
};
/* One recorded render pass: a target, its load action, and the packed command
   records that run inside it. */
struct BatchPass {
    uint32_t kind = 0;
    Handle target = 0;
    uint32_t clear = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> commands;
};
/* A resource handle pinned by a batch, addressed by pool kind and slot. */
struct RetainedResource {
    uint32_t kind = 0;
    uint32_t slot = 0;
};
struct Batch {
    Handle owner = 0;
    std::vector<BatchPass> passes;
    std::vector<RetainedResource> retained;
    /* External graphics images are retained through the core handle. */
    std::vector<uint32_t> retained_images;
    bool sealed = false;
};

static Pool<Renderer, RendererKind, 8> renderer_pool;
static Pool<Buffer, BufferKind, 256> buffer_pool;
static Pool<Shader, ShaderKind, 256> shader_pool;
static Pool<Pipeline, PipelineKind, 256> pipeline_pool;
static Pool<BufferBuilder, BufferBuilderKind, 16> buffer_builder_pool;
static Pool<PipelineBuilder, PipelineBuilderKind, 16> pipeline_builder_pool;
static Pool<ShaderBuilder, ShaderBuilderKind, 16> shader_builder_pool;
static Pool<UniformBuilder, UniformBuilderKind, 16, true> uniform_builder_pool;
static Pool<Image, ImageKind, 256> image_pool;
static Pool<ImageBuilder, ImageBuilderKind, 16> image_builder_pool;
static Pool<Sampler, SamplerKind, 256> sampler_pool;
static Pool<RenderTarget, RenderTargetKind, 128> render_target_pool;
static Pool<Batch, BatchKind, 64> batch_pool;
static Handle active_renderer = 0;
static Handle selected_renderer = 0;
static const nk_sokol_api *selected_api = nullptr;
static char error_message[256];
static nkgpu_result fail(nkgpu_result code, const char *format, ...);

#if defined(NKGPU_TESTING)
static bool fail_next_image_creation = false;
static bool fail_next_buffer_creation = false;
static bool fail_next_present = false;
static std::atomic_bool forbid_surface_target_queries = false;
#endif

static bool renderer_is_active(const Renderer &renderer) {
    return renderer.state == RendererState::FrameActive ||
           renderer.state == RendererState::RenderTargetActive;
}

static bool target_matches_renderer(const Renderer &renderer,
                                    const nk_surface_frame_target &target) {
    if (target.api != renderer.graphics_api)
        return false;
    if (target.native_device && renderer.native_device)
        return target.native_device == renderer.native_device;
    return target.device.id == renderer.device.id;
}

static void mark_renderer_lost(Handle handle, Renderer &renderer) {
    if (renderer.state == RendererState::Lost)
        return;
    renderer.state = RendererState::Lost;
    renderer.in_pass = false;
    renderer.active_target = 0;
    renderer.pass_width = 0;
    renderer.pass_height = 0;
    renderer.bindings = {};
    ++renderer.device_losses;
    if (active_renderer == handle)
        active_renderer = 0;
}

static nkgpu_result renderer_live(Handle, Renderer *renderer) {
    if (!renderer)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (renderer->state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    return NKGPU_OK;
}

static void record_resource_created(Handle owner, uint64_t uploaded = 0) {
    if (auto *renderer = renderer_pool.get(owner)) {
        ++renderer->value.resource_creations;
        renderer->value.upload_bytes += uploaded;
    }
}

static void record_resource_destroyed(Handle owner) {
    if (auto *renderer = renderer_pool.get(owner))
        ++renderer->value.resource_destructions;
}

static void record_allocation_failure(Handle owner) {
    if (auto *renderer = renderer_pool.get(owner))
        ++renderer->value.failed_allocations;
}

#if defined(NKGPU_TESTING)
static bool consume_image_creation_failure(Handle owner) {
    if (!fail_next_image_creation)
        return false;
    fail_next_image_creation = false;
    record_allocation_failure(owner);
    return true;
}

static bool consume_buffer_creation_failure(Handle owner) {
    if (!fail_next_buffer_creation)
        return false;
    fail_next_buffer_creation = false;
    record_allocation_failure(owner);
    return true;
}
#else
static bool consume_image_creation_failure(Handle) {
    return false;
}
static bool consume_buffer_creation_failure(Handle) {
    return false;
}
#endif

static const sg_api *runtime_gfx() {
    return selected_api ? selected_api->gfx : nullptr;
}

static nk_result get_surface_frame_target(nk_surface surface, nk_surface_frame_target *target) {
#if defined(NKGPU_TESTING)
    if (forbid_surface_target_queries.load(std::memory_order_acquire))
        return NK_ERROR_UNKNOWN;
#endif
    return nk_surface_get_frame_target(surface, target);
}

static bool make_renderer_surface_current(const Renderer &renderer) {
    if (nk::core::render_executor_physical() && nk_executor_is_current(NK_EXECUTOR_RENDER) &&
        renderer.has_context_target)
        return nkgpu_bind_frame_target(&renderer.context_target) == NKGPU_OK;
    if (nk_surface_make_current(renderer.surface) != NK_OK)
        return false;
    nk_surface_frame_target target{};
    target.struct_size = sizeof(target);
    return get_surface_frame_target(renderer.surface, &target) == NK_OK &&
           target.api == renderer.graphics_api && target.device.id == renderer.device.id;
}

static nkgpu_result activate_renderer(Handle handle,
                                      const nk_surface_frame_target *provided_target = nullptr) {
    auto *slot = renderer_pool.get(handle);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (slot->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    if (active_renderer && active_renderer != handle)
        return fail(NKGPU_ERROR_WRONG_STATE, "another renderer has an active frame");
    const bool context_backend = slot->value.graphics_api == NK_GRAPHICS_OPENGL ||
                                 slot->value.graphics_api == NK_GRAPHICS_OPENGL_ES;
    const bool render_owned_context =
        nk::core::render_executor_physical() && nk_executor_is_current(NK_EXECUTOR_RENDER);
    if (!provided_target && render_owned_context && context_backend &&
        !renderer_is_active(slot->value) && slot->value.has_context_target) {
        const nkgpu_result bound = nkgpu_bind_frame_target(&slot->value.context_target);
        if (bound != NKGPU_OK)
            return bound;
    }
    if (!provided_target && !renderer_is_active(slot->value) && context_backend &&
        !render_owned_context && nk_surface_make_current(slot->value.surface) != NK_OK)
        return fail(NKGPU_ERROR_UNKNOWN, "current: %s", nk_last_error());
    nk_surface_frame_target target =
        provided_target
            ? *provided_target
            : (slot->value.has_frame_target ? slot->value.frame_target : nk_surface_frame_target{});
    if (!provided_target && !slot->value.has_frame_target)
        target.struct_size = sizeof(target);
    const bool target_available =
        provided_target || slot->value.has_frame_target ||
        (!render_owned_context && get_surface_frame_target(slot->value.surface, &target) == NK_OK);
    if (target_available && target.device.id && !target_matches_renderer(slot->value, target)) {
        ++slot->value.surface_recreations;
        mark_renderer_lost(handle, slot->value);
        return fail(NKGPU_ERROR_DEVICE_LOST,
                    "surface graphics device changed; recreate the GPU renderer");
    }
    selected_renderer = handle;
    selected_api = slot->value.api;
    return NKGPU_OK;
}

static nkgpu_result begin_frame_with_target(Handle handle, const nk_surface_frame_target &target) {
    auto *slot = renderer_pool.get(handle);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (slot->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    if (slot->value.state != RendererState::Ready || active_renderer)
        return fail(NKGPU_ERROR_WRONG_STATE, "a renderer frame is already active");
    if (target.width <= 0 || target.height <= 0 || !target_matches_renderer(slot->value, target))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "frame target does not belong to the renderer");
    const bool context_backend =
        target.api == NK_GRAPHICS_OPENGL || target.api == NK_GRAPHICS_OPENGL_ES;
    if (context_backend) {
        const nkgpu_result bound = nkgpu_bind_frame_target(&target);
        if (bound != NKGPU_OK)
            return bound;
    }
    const nkgpu_result activated = activate_renderer(handle, &target);
    if (activated != NKGPU_OK)
        return activated;
    sg_reset_state_cache();
    slot->value.frame_target = target;
    slot->value.has_frame_target = true;
    slot->value.context_target = target;
    slot->value.context_target.native_target = 0;
    slot->value.context_target.native_depth_stencil_target = 0;
    slot->value.has_context_target = target.native_context != 0;
    slot->value.state = RendererState::FrameActive;
    slot->value.in_pass = false;
    slot->value.active_target = 0;
    slot->value.bindings = {};
    active_renderer = handle;
    return NKGPU_OK;
}

static nkgpu_result require_idle_renderer(Handle handle) {
    auto *slot = renderer_pool.get(handle);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (slot->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    if (slot->value.in_pass || slot->value.state == RendererState::RenderTargetActive ||
        (active_renderer && active_renderer != handle))
        return fail(NKGPU_ERROR_WRONG_STATE, "GPU resources can only change between render passes");
    return NKGPU_OK;
}

static nkgpu_result require_streaming_resource_access(Handle handle) {
    auto *slot = renderer_pool.get(handle);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (slot->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    if (active_renderer && active_renderer != handle)
        return fail(NKGPU_ERROR_WRONG_STATE, "another renderer has an active frame");
    return NKGPU_OK;
}

static nkgpu_result require_active_pass(Handle handle) {
    auto *slot = renderer_pool.get(handle);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (slot->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    if (!renderer_is_active(slot->value) || !slot->value.in_pass || active_renderer != handle)
        return fail(NKGPU_ERROR_WRONG_STATE, "operation requires an active render pass");
    return NKGPU_OK;
}

static nkgpu_result prepare_resource_destroy(Handle handle, bool &backend_available) {
    auto *slot = renderer_pool.get(handle);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (active_renderer && active_renderer != handle)
        return fail(NKGPU_ERROR_WRONG_STATE, "another renderer has an active frame");
    if (slot->value.state == RendererState::Lost) {
        backend_available = make_renderer_surface_current(slot->value);
        if (backend_available) {
            selected_renderer = handle;
            selected_api = slot->value.api;
        }
        return NKGPU_OK;
    }
    const nkgpu_result idle = require_idle_renderer(handle);
    if (idle != NKGPU_OK)
        return idle;
    const nkgpu_result activated = activate_renderer(handle);
    if (activated != NKGPU_OK) {
        backend_available = false;
        return NKGPU_OK;
    }
    backend_available = true;
    return NKGPU_OK;
}

#define sg_apply_bindings(...) (runtime_gfx()->apply_bindings(__VA_ARGS__))
#define sg_apply_pipeline(...) (runtime_gfx()->apply_pipeline(__VA_ARGS__))
#define sg_apply_scissor_rect(...) (runtime_gfx()->apply_scissor_rect(__VA_ARGS__))
#define sg_apply_uniforms(...) (runtime_gfx()->apply_uniforms(__VA_ARGS__))
#define sg_append_buffer(...) (runtime_gfx()->append_buffer(__VA_ARGS__))
#define sg_begin_pass(...) (runtime_gfx()->begin_pass(__VA_ARGS__))
#define sg_commit(...) (runtime_gfx()->commit(__VA_ARGS__))
#define sg_destroy_buffer(...) (runtime_gfx()->destroy_buffer(__VA_ARGS__))
#define sg_destroy_image(...) (runtime_gfx()->destroy_image(__VA_ARGS__))
#define sg_destroy_pipeline(...) (runtime_gfx()->destroy_pipeline(__VA_ARGS__))
#define sg_destroy_sampler(...) (runtime_gfx()->destroy_sampler(__VA_ARGS__))
#define sg_destroy_shader(...) (runtime_gfx()->destroy_shader(__VA_ARGS__))
#define sg_destroy_view(...) (runtime_gfx()->destroy_view(__VA_ARGS__))
#define sg_draw(...) (runtime_gfx()->draw(__VA_ARGS__))
#define sg_end_pass(...) (runtime_gfx()->end_pass(__VA_ARGS__))
#define sg_make_buffer(...) (runtime_gfx()->make_buffer(__VA_ARGS__))
#define sg_make_image(...) (runtime_gfx()->make_image(__VA_ARGS__))
#define sg_make_pipeline(...) (runtime_gfx()->make_pipeline(__VA_ARGS__))
#define sg_make_sampler(...) (runtime_gfx()->make_sampler(__VA_ARGS__))
#define sg_make_shader(...) (runtime_gfx()->make_shader(__VA_ARGS__))
#define sg_make_view(...) (runtime_gfx()->make_view(__VA_ARGS__))
#define sg_query_buffer_state(...) (runtime_gfx()->query_buffer_state(__VA_ARGS__))
#define sg_query_image_state(...) (runtime_gfx()->query_image_state(__VA_ARGS__))
#define sg_query_pipeline_state(...) (runtime_gfx()->query_pipeline_state(__VA_ARGS__))
#define sg_query_sampler_state(...) (runtime_gfx()->query_sampler_state(__VA_ARGS__))
#define sg_query_shader_state(...) (runtime_gfx()->query_shader_state(__VA_ARGS__))
#define sg_query_view_state(...) (runtime_gfx()->query_view_state(__VA_ARGS__))
#define sg_reset_state_cache(...) (runtime_gfx()->reset_state_cache(__VA_ARGS__))

static const nk_sokol_api *api_for_graphics_api(nk_graphics_api api,
                                                nk_surface surface = NK_INVALID_HANDLE) {
#if defined(NK_SOKOL_MULTI_CONTEXT)
    static nk_surface primary_runtime_surface = NK_INVALID_HANDLE;
#endif
    const nk_sokol_api *runtime = nullptr;
#if defined(NK_SOKOL_RUNTIME_MATRIX)
    switch (api) {
    case NK_GRAPHICS_OPENGL:
        runtime = nk_sokol_glcore_get_api();
        break;
    case NK_GRAPHICS_OPENGL_ES:
        runtime = nk_sokol_gles3_get_api();
        break;
    default:
        return nullptr;
    }
#else
    runtime = nk_sokol_get_api();
#if defined(NK_SOKOL_BACKEND_GLES3)
#if defined(__EMSCRIPTEN__)
    if (api != NK_GRAPHICS_OPENGL && api != NK_GRAPHICS_OPENGL_ES)
        return nullptr;
#else
    if (api != NK_GRAPHICS_OPENGL_ES)
        return nullptr;
#endif
#else
#if defined(NK_SOKOL_BACKEND_D3D11)
    if (api != NK_GRAPHICS_D3D11)
        return nullptr;
#elif defined(NK_SOKOL_BACKEND_METAL)
    if (api != NK_GRAPHICS_METAL)
        return nullptr;
#else
    if (api != NK_GRAPHICS_OPENGL)
        return nullptr;
#endif
#endif
#endif
    if (!runtime || !runtime->gfx)
        return nullptr;
#if defined(NK_SOKOL_MULTI_CONTEXT)
    if (surface != NK_INVALID_HANDLE) {
        if (primary_runtime_surface == NK_INVALID_HANDLE)
            primary_runtime_surface = surface;
        else if (surface != primary_runtime_surface) {
#if defined(NK_SOKOL_RUNTIME_MATRIX)
            runtime = api == NK_GRAPHICS_OPENGL      ? nk_sokol_glcore_secondary_get_api()
                      : api == NK_GRAPHICS_OPENGL_ES ? nk_sokol_gles3_secondary_get_api()
                                                     : nullptr;
#else
            runtime = nk_sokol_secondary_get_api();
#endif
        }
    }
#endif
    return runtime;
}

static nkgpu_backend convert_backend(const nk_sokol_api *api) {
    if (!api || !api->gfx)
        return 0;
    switch (api->gfx->query_backend()) {
    case SG_BACKEND_GLCORE:
        return NKGPU_BACKEND_GLCORE;
    case SG_BACKEND_GLES3:
        return NKGPU_BACKEND_GLES3;
    case SG_BACKEND_D3D11:
        return NKGPU_BACKEND_D3D11;
    case SG_BACKEND_METAL_IOS:
    case SG_BACKEND_METAL_MACOS:
    case SG_BACKEND_METAL_SIMULATOR:
        return NKGPU_BACKEND_METAL;
    default:
        return 0;
    }
}

static int release_graphics_image(nk_graphics_api graphics_api, const void *runtime,
                                  nk_graphics_device device, uint64_t backend_image) {
    auto *api = static_cast<const nk_sokol_api *>(runtime);
    if (!api || !api->external_image_release || !device.id || !backend_image)
        return 0;
    if ((graphics_api == NK_GRAPHICS_OPENGL || graphics_api == NK_GRAPHICS_OPENGL_ES) &&
        !nk_executor_is_current(NK_EXECUTOR_RENDER) && nk_surface_make_current(device.id) != NK_OK)
        return 0;
    api->external_image_release(static_cast<uint32_t>(backend_image));
    return 1;
}

static nkgpu_result fail(nkgpu_result code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(error_message, sizeof(error_message), format, args);
    va_end(args);
    return code;
}

static nkgpu_shader_language shader_language_for(nk_graphics_api api) {
    switch (api) {
    case NK_GRAPHICS_OPENGL:
    case NK_GRAPHICS_OPENGL_ES:
        return NKGPU_SHADERLANGUAGE_GLSL;
    case NK_GRAPHICS_D3D11:
        return NKGPU_SHADERLANGUAGE_HLSL5;
    case NK_GRAPHICS_METAL:
        return NKGPU_SHADERLANGUAGE_MSL;
    default:
        return 0;
    }
}

static bool shader_language_matches_renderer(nkgpu_renderer renderer,
                                             nkgpu_shader_language language) {
    const auto *slot = renderer_pool.get(renderer);
    return slot && shader_language_for(slot->value.graphics_api) == language;
}
static sg_vertex_format convert_format(nkgpu_vertex_format f) {
    switch (f) {
    case 1:
        return SG_VERTEXFORMAT_FLOAT;
    case 2:
        return SG_VERTEXFORMAT_FLOAT2;
    case 3:
        return SG_VERTEXFORMAT_FLOAT3;
    case 4:
        return SG_VERTEXFORMAT_FLOAT4;
    case NKGPU_VERTEXFORMAT_UBYTE4N:
        return SG_VERTEXFORMAT_UBYTE4N;
    default:
        return SG_VERTEXFORMAT_INVALID;
    }
}

static sg_blend_factor convert_blend_factor(nkgpu_blend_factor value) {
    switch (value) {
    case NKGPU_BLENDFACTOR_ZERO:
        return SG_BLENDFACTOR_ZERO;
    case NKGPU_BLENDFACTOR_ONE:
        return SG_BLENDFACTOR_ONE;
    case NKGPU_BLENDFACTOR_SRC_ALPHA:
        return SG_BLENDFACTOR_SRC_ALPHA;
    case NKGPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:
        return SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    default:
        return _SG_BLENDFACTOR_DEFAULT;
    }
}

static sg_compare_func convert_compare(nkgpu_compare_func value) {
    switch (value) {
    case NKGPU_COMPAREFUNC_ALWAYS:
        return SG_COMPAREFUNC_ALWAYS;
    case NKGPU_COMPAREFUNC_LESS_EQUAL:
        return SG_COMPAREFUNC_LESS_EQUAL;
    case NKGPU_COMPAREFUNC_EQUAL:
        return SG_COMPAREFUNC_EQUAL;
    case NKGPU_COMPAREFUNC_NOT_EQUAL:
        return SG_COMPAREFUNC_NOT_EQUAL;
    default:
        return _SG_COMPAREFUNC_DEFAULT;
    }
}

static sg_stencil_op convert_stencil_op(nkgpu_stencil_op value) {
    switch (value) {
    case NKGPU_STENCILOP_KEEP:
        return SG_STENCILOP_KEEP;
    case NKGPU_STENCILOP_ZERO:
        return SG_STENCILOP_ZERO;
    case NKGPU_STENCILOP_INVERT:
        return SG_STENCILOP_INVERT;
    case NKGPU_STENCILOP_INCREMENT_WRAP:
        return SG_STENCILOP_INCR_WRAP;
    case NKGPU_STENCILOP_DECREMENT_WRAP:
        return SG_STENCILOP_DECR_WRAP;
    default:
        return _SG_STENCILOP_DEFAULT;
    }
}

static sg_stencil_face_state convert_stencil_face(const nkgpu_stencil_face_state &value) {
    return {convert_compare(value.compare), convert_stencil_op(value.fail_op),
            convert_stencil_op(value.depth_fail_op), convert_stencil_op(value.pass_op)};
}
static sg_uniform_type convert_uniform(nkgpu_uniform_type t) {
    switch (t) {
    case 1:
        return SG_UNIFORMTYPE_FLOAT;
    case 2:
        return SG_UNIFORMTYPE_FLOAT2;
    case 3:
        return SG_UNIFORMTYPE_FLOAT3;
    case 4:
        return SG_UNIFORMTYPE_FLOAT4;
    case 5:
        return SG_UNIFORMTYPE_INT;
    case 6:
        return SG_UNIFORMTYPE_INT2;
    case 7:
        return SG_UNIFORMTYPE_INT3;
    case 8:
        return SG_UNIFORMTYPE_INT4;
    case 9:
        return SG_UNIFORMTYPE_MAT4;
    default:
        return SG_UNIFORMTYPE_INVALID;
    }
}
extern "C" {
const char *nkgpu_last_error(void) {
    return error_message;
}

nk_graphics_api nkgpu_default_graphics_api(void) {
#if defined(NK_SOKOL_BACKEND_GLES3)
    return NK_GRAPHICS_OPENGL_ES;
#elif defined(NK_SOKOL_BACKEND_D3D11)
    return NK_GRAPHICS_D3D11;
#elif defined(NK_SOKOL_BACKEND_METAL)
    return NK_GRAPHICS_METAL;
#else
    return NK_GRAPHICS_OPENGL;
#endif
}

nkgpu_backend nkgpu_query_backend(nkgpu_renderer renderer) {
    auto *slot = renderer_pool.get(renderer);
    return slot ? convert_backend(slot->value.api) : 0;
}

nk_graphics_api nkgpu_query_graphics_api(nkgpu_renderer renderer) {
    auto *slot = renderer_pool.get(renderer);
    return slot ? slot->value.graphics_api : static_cast<nk_graphics_api>(0);
}

nkgpu_result nkgpu_renderer_get_state(nkgpu_renderer renderer, nkgpu_renderer_state *out_state) {
    auto *slot = renderer_pool.get(renderer);
    if (!slot || !out_state)
        return fail(!out_state ? NKGPU_ERROR_INVALID_ARGUMENT : NKGPU_ERROR_INVALID_HANDLE,
                    "invalid renderer state query");
    switch (slot->value.state) {
    case RendererState::Ready:
        *out_state = NKGPU_RENDERER_READY;
        break;
    case RendererState::FrameActive:
        *out_state = NKGPU_RENDERER_FRAME_ACTIVE;
        break;
    case RendererState::RenderTargetActive:
        *out_state = NKGPU_RENDERER_RENDER_TARGET_ACTIVE;
        break;
    case RendererState::Lost:
        *out_state = NKGPU_RENDERER_LOST;
        break;
    }
    return NKGPU_OK;
}

nkgpu_result nkgpu_renderer_get_stats(nkgpu_renderer renderer, nkgpu_renderer_stats *out_stats) {
    auto *slot = renderer_pool.get(renderer);
    if (!slot || !out_stats)
        return fail(!out_stats ? NKGPU_ERROR_INVALID_ARGUMENT : NKGPU_ERROR_INVALID_HANDLE,
                    "invalid renderer statistics query");
    const Renderer &owner = slot->value;
    nkgpu_renderer_stats stats{};
    stats.struct_size = sizeof(stats);
    stats.frames = owner.frames;
    stats.passes = owner.passes;
    stats.draw_calls = owner.draw_calls;
    stats.upload_bytes = owner.upload_bytes;
    stats.resource_creations = owner.resource_creations;
    stats.resource_destructions = owner.resource_destructions;
    stats.surface_recreations = owner.surface_recreations;
    stats.device_losses = owner.device_losses;
    stats.failed_allocations = owner.failed_allocations;
    for (const auto &resource : buffer_pool.slots) {
        if (resource.active && resource.value.owner == renderer) {
            ++stats.buffers_live;
            stats.buffer_bytes += resource.value.size;
        }
    }
    for (const auto &resource : image_pool.slots) {
        if (resource.active && resource.value.owner == renderer) {
            ++stats.images_live;
            stats.image_bytes += resource.value.pixels.size();
        }
    }
    for (const auto &resource : sampler_pool.slots)
        if (resource.active && resource.value.owner == renderer)
            ++stats.samplers_live;
    for (const auto &resource : shader_pool.slots)
        if (resource.active && resource.value.owner == renderer)
            ++stats.shaders_live;
    for (const auto &resource : pipeline_pool.slots)
        if (resource.active && resource.value.owner == renderer)
            ++stats.pipelines_live;
    for (const auto &resource : render_target_pool.slots) {
        if (resource.active && resource.value.owner == renderer) {
            ++stats.render_targets_live;
            const uint64_t pixels = static_cast<uint64_t>(resource.value.width) *
                                    static_cast<uint64_t>(resource.value.height);
            stats.render_target_bytes += pixels * (resource.value.depth.id ? 8u : 4u);
        }
    }
    *out_stats = stats;
    return NKGPU_OK;
}

#if defined(NKGPU_TESTING)
void nkgpu_test_fail_next_image_creation(void) {
    fail_next_image_creation = true;
}
void nkgpu_test_fail_next_buffer_creation(void) {
    fail_next_buffer_creation = true;
}
void nkgpu_test_fail_next_present(void) {
    fail_next_present = true;
}

int32_t nkgpu_test_generation_exhaustion(void) {
    using TestPool = Pool<uint32_t, BufferKind, 1>;

    TestPool immediate;
    const auto stale = immediate.add(1);
    auto *stale_slot = immediate.get(stale);
    if (!stale || !stale_slot)
        return 0;
    immediate.remove(*stale_slot);
    for (uint32_t cycle = 0; cycle != 4094; ++cycle) {
        const auto current = immediate.add(cycle);
        auto *current_slot = immediate.get(current);
        if (!current || current == stale || !current_slot)
            return 0;
        immediate.remove(*current_slot);
    }
    if (immediate.get(stale) || immediate.add(2) != 0)
        return 0;

    TestPool pinned;
    const auto retained = pinned.add(1);
    auto *retained_slot = pinned.get(retained);
    if (!retained || !retained_slot)
        return 0;
    retained_slot->pins = 1;
    pinned.remove(*retained_slot);
    if (!pinned.get_retained(retained))
        return 0;
    pinned.release(*retained_slot);
    if (pinned.get_retained(retained))
        return 0;
    for (uint32_t cycle = 0; cycle != 4094; ++cycle) {
        const auto current = pinned.add(cycle);
        auto *current_slot = pinned.get(current);
        if (!current || !current_slot)
            return 0;
        pinned.remove(*current_slot);
    }
    if (pinned.add(2) != 0)
        return 0;

    using ExpandablePool = Pool<uint32_t, UniformBuilderKind, 16, true>;
    ExpandablePool transient;
    for (uint32_t cycle = 0; cycle != 65520; ++cycle) {
        const auto current = transient.add(cycle);
        auto *current_slot = transient.get(current);
        if (!current || !current_slot)
            return 0;
        transient.remove(*current_slot);
    }
    return transient.add(65520) != 0 ? 1 : 0;
}

void nkgpu_test_forbid_surface_target_queries(void) {
    forbid_surface_target_queries.store(true, std::memory_order_release);
}

void nkgpu_test_allow_surface_target_queries(void) {
    forbid_surface_target_queries.store(false, std::memory_order_release);
}

nkgpu_result nkgpu_test_lose_after_frames(nkgpu_renderer renderer, uint32_t frames) {
    auto *slot = renderer_pool.get(renderer);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (slot->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    slot->value.test_frames_before_loss = frames;
    return NKGPU_OK;
}

void nkgpu_test_lose_all_after_frames(uint32_t frames) {
    for (auto &slot : renderer_pool.slots)
        if (slot.active && slot.value.state != RendererState::Lost)
            slot.value.test_frames_before_loss = frames;
}

void nkgpu_test_invalidate_all(void) {
    for (uint32_t index = 0; index < renderer_pool.slots.size(); ++index) {
        auto &slot = renderer_pool.slots[index];
        if (slot.active) {
            const Handle handle =
                (uint32_t(RendererKind) << 28) | (uint32_t(slot.generation) << 16) | (index + 1);
            mark_renderer_lost(handle, slot.value);
        }
    }
}

nkgpu_result nkgpu_test_invalidate_surface(nkgpu_renderer renderer) {
    auto *slot = renderer_pool.get(renderer);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    mark_renderer_lost(renderer, slot->value);
    return NKGPU_OK;
}
#endif

nkgpu_result nkgpu_surface_create(nk_window window, int32_t width, int32_t height,
                                  nk_surface *out) {
    return nkgpu_surface_create_for_api(window, nkgpu_default_graphics_api(), width, height, out);
}

nkgpu_result nkgpu_surface_create_for_api(nk_window window, nk_graphics_api api, int32_t width,
                                          int32_t height, nk_surface *out) {
    if (!window || width <= 0 || height <= 0 || !out)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid surface arguments");
    if (api != NK_GRAPHICS_OPENGL && api != NK_GRAPHICS_OPENGL_ES && api != NK_GRAPHICS_D3D11 &&
        api != NK_GRAPHICS_METAL)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "unsupported graphics API request");
    if (!api_for_graphics_api(api))
        return fail(NKGPU_ERROR_UNKNOWN, "requested Sokol runtime backend is unavailable");
    nk_surface_options o{};
    o.struct_size = sizeof(o);
    o.flags = api == NK_GRAPHICS_OPENGL ? NK_SURFACE_FORWARD_COMPATIBLE : 0;
    if (api == NK_GRAPHICS_D3D11 || api == NK_GRAPHICS_METAL)
        o.flags |= NK_SURFACE_DEPTH | NK_SURFACE_STENCIL;
    o.api = api;
    o.major_version = (api == NK_GRAPHICS_OPENGL || api == NK_GRAPHICS_OPENGL_ES) ? 3 : 0;
    o.minor_version = api == NK_GRAPHICS_OPENGL ? 3 : 0;
    o.width = width;
    o.height = height;
    return nk_surface_create(window, &o, out) == NK_OK
               ? NKGPU_OK
               : fail(NKGPU_ERROR_UNKNOWN, "surface: %s", nk_last_error());
}
nkgpu_result nkgpu_surface_resize(nk_surface s, int32_t w, int32_t h) {
    return nk_surface_set_bounds(s, 0, 0, w, h) == NK_OK
               ? NKGPU_OK
               : fail(NKGPU_ERROR_UNKNOWN, "resize: %s", nk_last_error());
}
nkgpu_result nkgpu_surface_destroy(nk_surface s) {
    return nk_surface_destroy(s) == NK_OK
               ? NKGPU_OK
               : fail(NKGPU_ERROR_UNKNOWN, "destroy surface: %s", nk_last_error());
}
static nkgpu_result create_renderer_from_target(nk_surface surface,
                                                const nk_surface_frame_target &target,
                                                nkgpu_renderer *out) {
    if (!surface || !out)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid renderer arguments");
    if (active_renderer)
        return fail(NKGPU_ERROR_WRONG_STATE, "cannot create a renderer during an active frame");
    if (target.struct_size < sizeof(nk_surface_frame_target) || target.api == 0 ||
        target.width <= 0 || target.height <= 0)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid frame target");
    if (!target.device.id)
        return fail(NKGPU_ERROR_UNKNOWN, "surface has no graphics-device identity");
    if ((target.api == NK_GRAPHICS_D3D11 && (!target.native_device || !target.native_context)) ||
        (target.api == NK_GRAPHICS_METAL && (!target.native_device || !target.native_context)))
        return fail(NKGPU_ERROR_UNKNOWN, "explicit surface target is missing native tokens");
    const nk_sokol_api *api = api_for_graphics_api(target.api, surface);
    if (!api || !api->runtime_acquire || !api->runtime_release || !api->gfx)
        return fail(NKGPU_ERROR_UNKNOWN, "surface graphics backend is unavailable");
    sg_desc desc{};
    const bool explicit_backend =
        target.api == NK_GRAPHICS_D3D11 || target.api == NK_GRAPHICS_METAL;
    const sg_pixel_format color_format =
        explicit_backend ? SG_PIXELFORMAT_BGRA8 : SG_PIXELFORMAT_RGBA8;
    const sg_pixel_format depth_format = explicit_backend && !target.native_depth_stencil_target
                                             ? SG_PIXELFORMAT_NONE
                                             : SG_PIXELFORMAT_DEPTH_STENCIL;
    desc.environment.defaults = {
        .color_format = color_format, .depth_format = depth_format, .sample_count = 1};
    if (target.api == NK_GRAPHICS_D3D11) {
        desc.environment.d3d11.device =
            reinterpret_cast<const void *>(static_cast<uintptr_t>(target.native_device));
        desc.environment.d3d11.device_context =
            reinterpret_cast<const void *>(static_cast<uintptr_t>(target.native_context));
    } else if (target.api == NK_GRAPHICS_METAL) {
        desc.environment.metal.device =
            reinterpret_cast<const void *>(static_cast<uintptr_t>(target.native_device));
    }
    if (nk_graphics_device_retain(target.device) != NK_OK)
        return fail(NKGPU_ERROR_UNKNOWN, "graphics device retention failed");
    if (api->runtime_is_compatible &&
        !api->runtime_is_compatible(&desc, target.device, target.native_device)) {
        nk_graphics_device_release(target.device);
        return fail(NKGPU_ERROR_WRONG_STATE,
                    "surface graphics device is incompatible with the active Sokol runtime");
    }
    if (!api->runtime_acquire(&desc, target.device, target.native_device)) {
        nk_graphics_device_release(target.device);
        return fail(NKGPU_ERROR_UNKNOWN, "Sokol graphics runtime acquisition failed");
    }
    Renderer renderer_state{};
    renderer_state.surface = surface;
    renderer_state.api = api;
    renderer_state.graphics_api = target.api;
    renderer_state.device = target.device;
    renderer_state.native_device = target.native_device;
    renderer_state.context_target = target;
    renderer_state.context_target.native_target = 0;
    renderer_state.context_target.native_depth_stencil_target = 0;
    renderer_state.has_context_target = target.native_context != 0;
    renderer_state.surface_color_format = color_format;
    renderer_state.surface_depth_format = depth_format;
    Handle h = renderer_pool.add(renderer_state);
    if (!h) {
        api->runtime_release();
        nk_graphics_device_release(target.device);
        return fail(NKGPU_ERROR_UNKNOWN, "renderer pool full");
    }
    selected_renderer = h;
    selected_api = api;
    *out = h;
    return NKGPU_OK;
}
nkgpu_result nkgpu_renderer_create(nk_surface surface, nkgpu_renderer *out) {
    if (!surface || !out)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid renderer arguments");
    /* Explicit APIs initialize against the surface-owned device without
       acquiring a presentation image. The first frame prepares its target. */
    nk_surface_frame_target target{};
    target.struct_size = sizeof(target);
    if (nk_surface_get_frame_target(surface, &target) != NK_OK)
        return fail(NKGPU_ERROR_UNKNOWN, "surface target query: %s", nk_last_error());
    const bool context_backend =
        target.api == NK_GRAPHICS_OPENGL || target.api == NK_GRAPHICS_OPENGL_ES;
    if (context_backend && nk_surface_make_current(surface) != NK_OK)
        return fail(NKGPU_ERROR_UNKNOWN, "current: %s", nk_last_error());
    if (context_backend && nk_surface_get_frame_target(surface, &target) != NK_OK)
        return fail(NKGPU_ERROR_UNKNOWN, "surface target query: %s", nk_last_error());
    return create_renderer_from_target(surface, target, out);
}

nkgpu_result nkgpu_renderer_create_for_frame_target(nk_surface surface,
                                                    const nk_surface_frame_target *frame_target,
                                                    nkgpu_renderer *out_renderer) {
    if (!nk_executor_is_current(NK_EXECUTOR_RENDER))
        return fail(NKGPU_ERROR_WRONG_THREAD, "renderer creation requires the render executor");
    if (!frame_target)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "frame target is null");
    const nkgpu_result bound = nkgpu_bind_frame_target(frame_target);
    if (bound != NKGPU_OK)
        return bound;
    return create_renderer_from_target(surface, *frame_target, out_renderer);
}
static void destroy_render_target(Pool<RenderTarget, RenderTargetKind, 128>::Slot &slot,
                                  bool backend_available) {
    auto &target = slot.value;
    /*
     * A batch may still hold this target; keep the backend objects and the
     * imported image alive until the last pin is released.
     */
    if (!slot.pins) {
        if (backend_available && target.depth_attachment.id)
            sg_destroy_view(target.depth_attachment);
        if (backend_available && target.color_attachment.id)
            sg_destroy_view(target.color_attachment);
        if (backend_available && target.depth.id)
            sg_destroy_image(target.depth);
        if (target.image.id)
            nk_graphics_image_release(target.image);
    }
    record_resource_destroyed(target.owner);
    render_target_pool.remove(slot);
}
/*
 * Releases one retained pin. When the slot was retired by its owner and no
 * other batch holds it, the deferred backend objects are destroyed here.
 */
static void unpin_resource(uint32_t kind, uint32_t slot_index, bool backend_available) {
    if (!slot_index)
        return;
    switch (kind) {
    case RenderTargetKind: {
        if (slot_index > render_target_pool.slots.size())
            return;
        auto &s = render_target_pool.slots[slot_index - 1];
        if (s.pins)
            --s.pins;
        /* Without a reachable backend the slot stays retired and is
           released when the renderer is torn down. */
        if (!s.pins && s.retired && backend_available) {
            auto &target = s.value;
            if (target.depth_attachment.id)
                sg_destroy_view(target.depth_attachment);
            if (target.color_attachment.id)
                sg_destroy_view(target.color_attachment);
            if (target.depth.id)
                sg_destroy_image(target.depth);
            if (target.image.id)
                nk_graphics_image_release(target.image);
            render_target_pool.release(s);
        }
        break;
    }
    case SamplerKind: {
        if (slot_index > sampler_pool.slots.size())
            return;
        auto &s = sampler_pool.slots[slot_index - 1];
        if (s.pins)
            --s.pins;
        /* Without a reachable backend the slot stays retired and is
           released when the renderer is torn down. */
        if (!s.pins && s.retired && backend_available) {
            sg_destroy_sampler(s.value.object);
            sampler_pool.release(s);
        }
        break;
    }
    case ImageKind: {
        if (slot_index > image_pool.slots.size())
            return;
        auto &s = image_pool.slots[slot_index - 1];
        if (s.pins)
            --s.pins;
        /* Without a reachable backend the slot stays retired and is
           released when the renderer is torn down. */
        if (!s.pins && s.retired && backend_available) {
            sg_destroy_view(s.value.view);
            sg_destroy_image(s.value.object);
            image_pool.release(s);
        }
        break;
    }
    case PipelineKind: {
        if (slot_index > pipeline_pool.slots.size())
            return;
        auto &s = pipeline_pool.slots[slot_index - 1];
        if (s.pins)
            --s.pins;
        /* Without a reachable backend the slot stays retired and is
           released when the renderer is torn down. */
        if (!s.pins && s.retired && backend_available) {
            sg_destroy_pipeline(s.value.object);
            pipeline_pool.release(s);
        }
        break;
    }
    case BufferKind: {
        if (slot_index > buffer_pool.slots.size())
            return;
        auto &s = buffer_pool.slots[slot_index - 1];
        if (s.pins)
            --s.pins;
        /* Without a reachable backend the slot stays retired and is
           released when the renderer is torn down. */
        if (!s.pins && s.retired && backend_available) {
            sg_destroy_buffer(s.value.object);
            buffer_pool.release(s);
        }
        break;
    }
    default:
        break;
    }
}
/* Releases every retained graphics image recorded from `from` onward. */
static void release_batch_images(Batch &batch, size_t from) {
    while (batch.retained_images.size() > from) {
        const uint32_t image = batch.retained_images.back();
        batch.retained_images.pop_back();
        nk_graphics_image_release(nk_graphics_image{image});
    }
}
static void destroy_owned(Handle owner, bool backend_available) {
    /*
     * Retired-but-pinned slots keep their backend objects for batch replay, so
     * they are hidden from the loops below. Releasing the batches first drops
     * every pin; `retired` keeps the slot visible here so nothing leaks.
     */
    for (auto &s : batch_pool.slots)
        if (s.active && s.value.owner == owner) {
            for (const auto &retained : s.value.retained)
                unpin_resource(retained.kind, retained.slot, backend_available);
            release_batch_images(s.value, 0);
            batch_pool.remove(s);
        }
    for (auto &s : render_target_pool.slots)
        if ((s.active || s.retired || s.pins) && s.value.owner == owner) {
            s.pins = 0;
            destroy_render_target(s, backend_available);
        }
    for (auto &s : sampler_pool.slots)
        if ((s.active || s.retired || s.pins) && s.value.owner == owner) {
            s.pins = 0;
            if (backend_available)
                sg_destroy_sampler(s.value.object);
            record_resource_destroyed(owner);
            sampler_pool.remove(s);
        }
    for (auto &s : image_pool.slots)
        if ((s.active || s.retired || s.pins) && s.value.owner == owner) {
            s.pins = 0;
            if (backend_available) {
                sg_destroy_view(s.value.view);
                sg_destroy_image(s.value.object);
            }
            record_resource_destroyed(owner);
            image_pool.remove(s);
        }
    for (auto &s : pipeline_pool.slots)
        if ((s.active || s.retired || s.pins) && s.value.owner == owner) {
            s.pins = 0;
            if (backend_available)
                sg_destroy_pipeline(s.value.object);
            record_resource_destroyed(owner);
            pipeline_pool.remove(s);
        }
    for (auto &s : shader_pool.slots)
        if (s.active && s.value.owner == owner) {
            if (backend_available)
                sg_destroy_shader(s.value.object);
            record_resource_destroyed(owner);
            shader_pool.remove(s);
        }
    for (auto &s : buffer_pool.slots)
        if ((s.active || s.retired || s.pins) && s.value.owner == owner) {
            s.pins = 0;
            if (backend_available)
                sg_destroy_buffer(s.value.object);
            record_resource_destroyed(owner);
            buffer_pool.remove(s);
        }
    for (auto &s : buffer_builder_pool.slots)
        if (s.active && s.value.owner == owner) {
            free(s.value.data);
            buffer_builder_pool.remove(s);
        }
    for (auto &s : pipeline_builder_pool.slots)
        if (s.active && s.value.owner == owner)
            pipeline_builder_pool.remove(s);
    for (auto &s : shader_builder_pool.slots)
        if (s.active && s.value.owner == owner)
            shader_builder_pool.remove(s);
    for (auto &s : uniform_builder_pool.slots)
        if (s.active && s.value.owner == owner) {
            free(s.value.data);
            uniform_builder_pool.remove(s);
        }
    for (auto &s : image_builder_pool.slots)
        if (s.active && s.value.owner == owner) {
            free(s.value.data);
            image_builder_pool.remove(s);
        }
}
nkgpu_result nkgpu_renderer_destroy(nkgpu_renderer h) {
    auto *s = renderer_pool.get(h);
    if (!s)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (renderer_is_active(s->value))
        return fail(NKGPU_ERROR_WRONG_STATE, "renderer has active frame");
    if (active_renderer && active_renderer != h)
        return fail(NKGPU_ERROR_WRONG_STATE, "another renderer has an active frame");
    bool backend_available = false;
    if (s->value.state == RendererState::Lost) {
        backend_available = make_renderer_surface_current(s->value);
        if (backend_available) {
            selected_renderer = h;
            selected_api = s->value.api;
        }
    } else {
        backend_available = activate_renderer(h) == NKGPU_OK;
    }
    const nk_sokol_api *api = s->value.api;
    const nk_graphics_device device = s->value.device;
    destroy_owned(h, backend_available);
    renderer_pool.remove(*s);
    api->runtime_release();
    nk_graphics_device_release(device);
    if (selected_renderer == h) {
        selected_renderer = 0;
        selected_api = nullptr;
    }
    return NKGPU_OK;
}
nkgpu_result nkgpu_render_target_create(nkgpu_renderer renderer, uint32_t width, uint32_t height,
                                        uint32_t depth_stencil, nkgpu_render_target *out) {
    auto *owner = renderer_pool.get(renderer);
    if (!owner)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (!width || !height || !out || depth_stencil > 1 ||
        width > static_cast<uint32_t>(INT32_MAX) || height > static_cast<uint32_t>(INT32_MAX))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid render-target arguments");
    const nkgpu_result idle = require_idle_renderer(renderer);
    if (idle != NKGPU_OK)
        return idle;
    const nkgpu_result activated = activate_renderer(renderer);
    if (activated != NKGPU_OK)
        return activated;

    RenderTarget target{};
    target.owner = renderer;
    target.width = static_cast<int32_t>(width);
    target.height = static_cast<int32_t>(height);
    if (consume_image_creation_failure(renderer))
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "injected image allocation failure");
    sg_image_desc color_desc{};
    color_desc.width = static_cast<int>(width);
    color_desc.height = static_cast<int>(height);
    color_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    color_desc.usage.color_attachment = true;
    target.color = sg_make_image(&color_desc);
    if (sg_query_image_state(target.color) != SG_RESOURCESTATE_VALID) {
        record_allocation_failure(renderer);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "render-target color image creation failed");
    }

    sg_view_desc sampled_view_desc{};
    sampled_view_desc.texture.image = target.color;
    const sg_view sampled_view = sg_make_view(&sampled_view_desc);
    if (sg_query_view_state(sampled_view) != SG_RESOURCESTATE_VALID) {
        sg_destroy_image(target.color);
        return fail(NKGPU_ERROR_UNKNOWN, "render-target sampled view creation failed");
    }

    sg_view_desc color_attachment_desc{};
    color_attachment_desc.color_attachment.image = target.color;
    target.color_attachment = sg_make_view(&color_attachment_desc);
    if (sg_query_view_state(target.color_attachment) != SG_RESOURCESTATE_VALID) {
        sg_destroy_view(sampled_view);
        sg_destroy_image(target.color);
        return fail(NKGPU_ERROR_UNKNOWN, "render-target color attachment creation failed");
    }

    if (depth_stencil) {
        sg_image_desc depth_desc{};
        depth_desc.width = static_cast<int>(width);
        depth_desc.height = static_cast<int>(height);
        depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        depth_desc.usage.depth_stencil_attachment = true;
        if (consume_image_creation_failure(renderer)) {
            sg_destroy_view(target.color_attachment);
            sg_destroy_view(sampled_view);
            sg_destroy_image(target.color);
            return fail(NKGPU_ERROR_OUT_OF_MEMORY, "injected image allocation failure");
        }
        target.depth = sg_make_image(&depth_desc);
        if (sg_query_image_state(target.depth) == SG_RESOURCESTATE_VALID) {
            sg_view_desc depth_attachment_desc{};
            depth_attachment_desc.depth_stencil_attachment.image = target.depth;
            target.depth_attachment = sg_make_view(&depth_attachment_desc);
        }
        if (sg_query_image_state(target.depth) != SG_RESOURCESTATE_VALID ||
            sg_query_view_state(target.depth_attachment) != SG_RESOURCESTATE_VALID) {
            if (target.depth_attachment.id)
                sg_destroy_view(target.depth_attachment);
            if (target.depth.id)
                sg_destroy_image(target.depth);
            sg_destroy_view(target.color_attachment);
            sg_destroy_view(sampled_view);
            sg_destroy_image(target.color);
            record_allocation_failure(renderer);
            return fail(NKGPU_ERROR_OUT_OF_MEMORY,
                        "render-target depth attachment creation failed");
        }
    }

    const uint32_t backend_image = owner->value.api->external_image_create(
        target.color, sampled_view, static_cast<int32_t>(width), static_cast<int32_t>(height));
    if (!backend_image) {
        if (target.depth_attachment.id)
            sg_destroy_view(target.depth_attachment);
        if (target.depth.id)
            sg_destroy_image(target.depth);
        sg_destroy_view(target.color_attachment);
        sg_destroy_view(sampled_view);
        sg_destroy_image(target.color);
        return fail(NKGPU_ERROR_UNKNOWN, "Sokol sampled image registry is full");
    }
    const nk_result image_result = nk_core_graphics_image_register(
        owner->value.graphics_api, owner->value.device, static_cast<int32_t>(width),
        static_cast<int32_t>(height), owner->value.api, backend_image, release_graphics_image,
        &target.image);
    if (image_result != NK_OK) {
        owner->value.api->external_image_release(backend_image);
        if (target.depth_attachment.id)
            sg_destroy_view(target.depth_attachment);
        if (target.depth.id)
            sg_destroy_image(target.depth);
        sg_destroy_view(target.color_attachment);
        return fail(NKGPU_ERROR_UNKNOWN, "NativeKit graphics image registration failed");
    }

    const Handle handle = render_target_pool.add(target);
    if (!handle) {
        if (target.depth_attachment.id)
            sg_destroy_view(target.depth_attachment);
        if (target.depth.id)
            sg_destroy_image(target.depth);
        sg_destroy_view(target.color_attachment);
        nk_graphics_image_release(target.image);
        record_allocation_failure(renderer);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "render-target pool full");
    }
    record_resource_created(renderer);
    *out = handle;
    return NKGPU_OK;
}
nkgpu_result nkgpu_render_target_get_image(nkgpu_renderer renderer, nkgpu_render_target handle,
                                           nk_graphics_image *out) {
    auto *owner = renderer_pool.get(renderer);
    auto *target = render_target_pool.get(handle);
    if (!owner || !target || target->value.owner != renderer || !out)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "invalid render-target image request");
    const nkgpu_result idle = require_idle_renderer(renderer);
    if (idle != NKGPU_OK)
        return idle;
    *out = target->value.image;
    return NKGPU_OK;
}
nkgpu_result nkgpu_render_target_destroy(nkgpu_renderer renderer, nkgpu_render_target handle) {
    auto *owner = renderer_pool.get(renderer);
    auto *target = render_target_pool.get(handle);
    if (!owner || !target || target->value.owner != renderer)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "invalid render-target handle/state");
    bool backend_available = false;
    const nkgpu_result ready = prepare_resource_destroy(renderer, backend_available);
    if (ready != NKGPU_OK)
        return ready;
    destroy_render_target(*target, backend_available);
    return NKGPU_OK;
}
nkgpu_result nkgpu_begin_render_target(nkgpu_renderer renderer, nkgpu_render_target handle,
                                       uint32_t clear) {
    auto *owner = renderer_pool.get(renderer);
    auto *target = render_target_pool.get(handle);
    if (!owner || !target || target->value.owner != renderer)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "invalid or foreign render target");
    if (clear > 1)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid render-target clear flag");
    if (owner->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    if (owner->value.state != RendererState::Ready || active_renderer)
        return fail(NKGPU_ERROR_WRONG_STATE, "invalid render-target frame state");
    const nkgpu_result activated = activate_renderer(renderer);
    if (activated != NKGPU_OK)
        return activated;
    sg_reset_state_cache();
    sg_pass pass{};
    pass.action.colors[0].load_action = clear ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
    if (target->value.depth.id) {
        pass.action.depth = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, 1.0f};
        pass.action.stencil = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, 0};
        pass.attachments.depth_stencil = target->value.depth_attachment;
    }
    pass.attachments.colors[0] = target->value.color_attachment;
    sg_begin_pass(&pass);
    owner->value.state = RendererState::RenderTargetActive;
    owner->value.in_pass = true;
    ++owner->value.passes;
    owner->value.active_target = handle;
    owner->value.pass_width = target->value.width;
    owner->value.pass_height = target->value.height;
    owner->value.bindings = {};
    active_renderer = renderer;
    return NKGPU_OK;
}
nkgpu_result nkgpu_end_render_target(nkgpu_renderer renderer) {
    auto *owner = renderer_pool.get(renderer);
    if (!owner)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (owner->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    if (owner->value.state != RendererState::RenderTargetActive || !owner->value.in_pass ||
        !owner->value.active_target || active_renderer != renderer)
        return fail(NKGPU_ERROR_WRONG_STATE, "no active render-target pass");
    const nkgpu_result activated = activate_renderer(renderer);
    if (activated != NKGPU_OK)
        return activated;
    sg_end_pass();
    sg_commit();
    owner->value.state = RendererState::Ready;
    owner->value.in_pass = false;
    owner->value.active_target = 0;
    active_renderer = 0;
    ++owner->value.frames;
    return NKGPU_OK;
}
static nkgpu_result save_buffer(Handle owner, sg_buffer object, uint32_t size, nkgpu_buffer *out) {
    Handle h = buffer_pool.add(Buffer{owner, object, size, false});
    if (!h) {
        sg_destroy_buffer(object);
        record_allocation_failure(owner);
        return fail(NKGPU_ERROR_UNKNOWN, "buffer pool full");
    }
    record_resource_created(owner, size);
    *out = h;
    return NKGPU_OK;
}
nkgpu_result nkgpu_buffer_create_stream(nkgpu_renderer r, uint32_t capacity,
                                        nkgpu_buffer_usage usage, nkgpu_buffer *out) {
    if (!capacity || capacity > static_cast<uint32_t>(INT32_MAX) || !out ||
        (usage != NKGPU_BUFFER_VERTEX && usage != NKGPU_BUFFER_INDEX))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid stream buffer arguments");
    const nkgpu_result idle = require_idle_renderer(r);
    if (idle != NKGPU_OK)
        return idle;
    const nkgpu_result activated = activate_renderer(r);
    if (activated != NKGPU_OK)
        return activated;
    if (consume_buffer_creation_failure(r))
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "injected buffer allocation failure");
    sg_buffer_desc desc{};
    desc.size = capacity;
    desc.usage.vertex_buffer = usage == NKGPU_BUFFER_VERTEX;
    desc.usage.index_buffer = usage == NKGPU_BUFFER_INDEX;
    desc.usage.immutable = false;
    desc.usage.dynamic_update = true;
    const sg_buffer object = sg_make_buffer(&desc);
    if (sg_query_buffer_state(object) != SG_RESOURCESTATE_VALID) {
        record_allocation_failure(r);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "stream buffer creation failed");
    }
    const Handle handle = buffer_pool.add(Buffer{r, object, capacity, true});
    if (!handle) {
        sg_destroy_buffer(object);
        record_allocation_failure(r);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "buffer pool full");
    }
    record_resource_created(r);
    *out = handle;
    return NKGPU_OK;
}
nkgpu_result nkgpu_buffer_append(nkgpu_renderer r, nkgpu_buffer h, const uint8_t *data,
                                 uint32_t size, uint32_t *out_offset) {
    auto *renderer = renderer_pool.get(r);
    auto *buffer = buffer_pool.get(h);
    if (!renderer || !buffer || buffer->value.owner != r)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale or foreign stream buffer");
    /*
     * Appends are valid inside a pass or between frames: a recorded frame
     * fills its stream buffers before the batch opens any pass. Sokol rewinds
     * the append cursor when the frame index changes, so the data stays valid
     * until the next commit either way.
     */
    const nkgpu_result access = require_streaming_resource_access(r);
    if (access != NKGPU_OK)
        return access;
    if (!buffer->value.stream || !data || !size || !out_offset)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid stream append arguments");
    const sg_range range{data, size};
    const int offset = sg_append_buffer(buffer->value.object, &range);
    if (offset < 0 || runtime_gfx()->query_buffer_overflow(buffer->value.object))
        return fail(NKGPU_ERROR_UNKNOWN, "stream buffer capacity exceeded");
    renderer->value.upload_bytes += size;
    *out_offset = static_cast<uint32_t>(offset);
    return NKGPU_OK;
}
nkgpu_result nkgpu_buffer_create(nkgpu_renderer r, const uint8_t *data, uint32_t size,
                                 nkgpu_buffer *out) {
    if (!data || !size || !out)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid buffer arguments");
    const nkgpu_result idle = require_idle_renderer(r);
    if (idle != NKGPU_OK)
        return idle;
    const nkgpu_result activated = activate_renderer(r);
    if (activated != NKGPU_OK)
        return activated;
    if (consume_buffer_creation_failure(r))
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "injected buffer allocation failure");
    sg_buffer_desc desc{};
    desc.data = {data, size};
    sg_buffer b = sg_make_buffer(&desc);
    return sg_query_buffer_state(b) == SG_RESOURCESTATE_VALID
               ? save_buffer(r, b, size, out)
               : (record_allocation_failure(r),
                  fail(NKGPU_ERROR_OUT_OF_MEMORY, "buffer creation failed"));
}
nkgpu_result nkgpu_buffer_begin(nkgpu_renderer r, uint32_t size, nkgpu_buffer_builder *out) {
    return nkgpu_buffer_begin_kind(r, size, NKGPU_BUFFER_VERTEX, out);
}
nkgpu_result nkgpu_buffer_begin_kind(nkgpu_renderer r, uint32_t size, nkgpu_buffer_usage usage,
                                     nkgpu_buffer_builder *out) {
    if (!size || !out)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid buffer builder");
    if (usage != NKGPU_BUFFER_VERTEX && usage != NKGPU_BUFFER_INDEX)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid buffer usage");
    const nkgpu_result idle = require_idle_renderer(r);
    if (idle != NKGPU_OK)
        return idle;
    uint8_t *data = (uint8_t *)calloc(1, size);
    if (!data)
        return fail(NKGPU_ERROR_UNKNOWN, "allocation failed");
    Handle h = buffer_builder_pool.add(BufferBuilder{r, data, size, usage == NKGPU_BUFFER_INDEX});
    if (!h) {
        free(data);
        return fail(NKGPU_ERROR_UNKNOWN, "builder pool full");
    }
    *out = h;
    return NKGPU_OK;
}
nkgpu_result nkgpu_buffer_write_u16(nkgpu_buffer_builder h, uint32_t offset, uint32_t value) {
    auto *s = buffer_builder_pool.get(h);
    if (!s || value > UINT16_MAX || offset > s->value.size || s->value.size - offset < 2)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale/out-of-range buffer builder");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    uint16_t v = (uint16_t)value;
    memcpy(s->value.data + offset, &v, 2);
    return NKGPU_OK;
}
nkgpu_result nkgpu_buffer_write_f32(nkgpu_buffer_builder h, uint32_t offset, float value) {
    auto *s = buffer_builder_pool.get(h);
    if (!s || offset > s->value.size || s->value.size - offset < sizeof(value))
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale/out-of-range buffer builder");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    memcpy(s->value.data + offset, &value, sizeof(value));
    return NKGPU_OK;
}
nkgpu_result nkgpu_buffer_end(nkgpu_buffer_builder h, nkgpu_buffer *out) {
    auto *s = buffer_builder_pool.get(h);
    if (!s || !out)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale buffer builder");
    auto owner = s->value.owner;
    const nkgpu_result idle = require_idle_renderer(owner);
    if (idle != NKGPU_OK)
        return idle;
    const nkgpu_result activated = activate_renderer(owner);
    if (activated != NKGPU_OK)
        return activated;
    const uint32_t size = s->value.size;
    if (consume_buffer_creation_failure(owner)) {
        free(s->value.data);
        buffer_builder_pool.remove(*s);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "injected buffer allocation failure");
    }
    sg_buffer_desc desc{};
    desc.data = {s->value.data, s->value.size};
    desc.usage.index_buffer = s->value.index;
    desc.usage.vertex_buffer = !s->value.index;
    sg_buffer b = sg_make_buffer(&desc);
    free(s->value.data);
    buffer_builder_pool.remove(*s);
    return sg_query_buffer_state(b) == SG_RESOURCESTATE_VALID
               ? save_buffer(owner, b, size, out)
               : (record_allocation_failure(owner),
                  fail(NKGPU_ERROR_OUT_OF_MEMORY, "buffer creation failed"));
}
nkgpu_result nkgpu_buffer_destroy(nkgpu_renderer r, nkgpu_buffer h) {
    auto *s = buffer_pool.get(h);
    if (!renderer_pool.get(r) || !s || s->value.owner != r)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale/foreign buffer");
    bool backend_available = false;
    const nkgpu_result ready = prepare_resource_destroy(r, backend_available);
    if (ready != NKGPU_OK)
        return ready;
    /* A batch may still reference this buffer; defer its backend destruction. */
    if (backend_available && !s->pins)
        sg_destroy_buffer(s->value.object);
    record_resource_destroyed(r);
    buffer_pool.remove(*s);
    return NKGPU_OK;
}
nkgpu_result nkgpu_shader_create(nkgpu_renderer r, nkgpu_shader_language language, const char *vs,
                                 const char *fs, nkgpu_shader *out) {
    if (!renderer_pool.get(r))
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (!vs || !fs || !out || !shader_language_matches_renderer(r, language))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid shader arguments");
    const nkgpu_result idle = require_idle_renderer(r);
    if (idle != NKGPU_OK)
        return idle;
    const nkgpu_result activated = activate_renderer(r);
    if (activated != NKGPU_OK)
        return activated;
    sg_shader_desc desc{};
    desc.vertex_func.source = vs;
    desc.fragment_func.source = fs;
    if (language == NKGPU_SHADERLANGUAGE_HLSL5) {
        desc.vertex_func.d3d11_target = "vs_5_0";
        desc.fragment_func.d3d11_target = "ps_5_0";
    }
    if (language == NKGPU_SHADERLANGUAGE_HLSL5) {
        desc.vertex_func.entry = "main";
        desc.fragment_func.entry = "main";
    } else if (language == NKGPU_SHADERLANGUAGE_MSL) {
        desc.vertex_func.entry = "main0";
        desc.fragment_func.entry = "main0";
    }
    sg_shader object = sg_make_shader(&desc);
    if (sg_query_shader_state(object) != SG_RESOURCESTATE_VALID) {
        record_allocation_failure(r);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "shader creation failed");
    }
    Handle h = shader_pool.add(Shader{r, object, language});
    if (!h) {
        sg_destroy_shader(object);
        record_allocation_failure(r);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "shader pool full");
    }
    record_resource_created(r);
    *out = h;
    return NKGPU_OK;
}
nkgpu_result nkgpu_shader_destroy(nkgpu_renderer r, nkgpu_shader h) {
    auto *s = shader_pool.get(h);
    if (!renderer_pool.get(r) || !s || s->value.owner != r)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale/foreign shader");
    bool backend_available = false;
    const nkgpu_result ready = prepare_resource_destroy(r, backend_available);
    if (ready != NKGPU_OK)
        return ready;
    if (backend_available)
        sg_destroy_shader(s->value.object);
    record_resource_destroyed(r);
    shader_pool.remove(*s);
    return NKGPU_OK;
}
nkgpu_result nkgpu_shader_begin(nkgpu_renderer r, nkgpu_shader_language language, const char *vs,
                                const char *fs, nkgpu_shader_builder *out) {
    if (!renderer_pool.get(r))
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (!vs || !fs || !out || !shader_language_matches_renderer(r, language))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid shader builder");
    const nkgpu_result idle = require_idle_renderer(r);
    if (idle != NKGPU_OK)
        return idle;
    Handle h = shader_builder_pool.add(ShaderBuilder{});
    if (!h)
        return fail(NKGPU_ERROR_UNKNOWN, "shader builder pool full");
    auto *s = shader_builder_pool.get(h);
    s->value.owner = r;
    s->value.language = language;
    s->value.vertex_source = vs;
    s->value.fragment_source = fs;
    s->value.desc.vertex_func.source = s->value.vertex_source.c_str();
    s->value.desc.fragment_func.source = s->value.fragment_source.c_str();
    if (language == NKGPU_SHADERLANGUAGE_HLSL5) {
        s->value.desc.vertex_func.d3d11_target = "vs_5_0";
        s->value.desc.fragment_func.d3d11_target = "ps_5_0";
    }
    if (language == NKGPU_SHADERLANGUAGE_HLSL5) {
        s->value.desc.vertex_func.entry = "main";
        s->value.desc.fragment_func.entry = "main";
    } else if (language == NKGPU_SHADERLANGUAGE_MSL) {
        s->value.desc.vertex_func.entry = "main0";
        s->value.desc.fragment_func.entry = "main0";
    }
    *out = h;
    return NKGPU_OK;
}

nkgpu_result nkgpu_shader_attribute(nkgpu_shader_builder h, uint32_t location,
                                    const char *glsl_name, const char *hlsl_semantic,
                                    uint32_t hlsl_semantic_index) {
    auto *s = shader_builder_pool.get(h);
    if (!s || location >= SG_MAX_VERTEX_ATTRIBUTES || hlsl_semantic_index > UINT8_MAX ||
        (s->value.language == NKGPU_SHADERLANGUAGE_GLSL && (!glsl_name || !*glsl_name)) ||
        (s->value.language == NKGPU_SHADERLANGUAGE_HLSL5 && (!hlsl_semantic || !*hlsl_semantic)))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid shader vertex attribute");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    auto &attribute = s->value.desc.attrs[location];
    if (glsl_name) {
        s->value.glsl_attribute_names[location] = glsl_name;
        attribute.glsl_name = s->value.glsl_attribute_names[location].c_str();
    }
    if (hlsl_semantic) {
        s->value.hlsl_semantic_names[location] = hlsl_semantic;
        attribute.hlsl_sem_name = s->value.hlsl_semantic_names[location].c_str();
        attribute.hlsl_sem_index = static_cast<uint8_t>(hlsl_semantic_index);
    }
    return NKGPU_OK;
}

nkgpu_result nkgpu_shader_uniform_block(nkgpu_shader_builder h, uint32_t slot,
                                        nkgpu_shader_stage stage, uint32_t size) {
    auto *s = shader_builder_pool.get(h);
    if (!s || slot >= SG_MAX_UNIFORMBLOCK_BINDSLOTS || !size ||
        (stage != NKGPU_SHADERSTAGE_VERTEX && stage != NKGPU_SHADERSTAGE_FRAGMENT))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid uniform block");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    auto &b = s->value.desc.uniform_blocks[slot];
    b.stage = stage == NKGPU_SHADERSTAGE_VERTEX ? SG_SHADERSTAGE_VERTEX : SG_SHADERSTAGE_FRAGMENT;
    b.size = size;
    b.layout = SG_UNIFORMLAYOUT_STD140;
    b.hlsl_register_b_n = static_cast<uint8_t>(slot);
    b.msl_buffer_n = static_cast<uint8_t>(slot);
    return NKGPU_OK;
}
nkgpu_result nkgpu_shader_uniform(nkgpu_shader_builder h, uint32_t block, uint32_t member,
                                  const char *name, nkgpu_uniform_type type, uint32_t count) {
    auto *s = shader_builder_pool.get(h);
    auto converted = convert_uniform(type);
    if (!s || block >= SG_MAX_UNIFORMBLOCK_BINDSLOTS || member >= SG_MAX_UNIFORMBLOCK_MEMBERS ||
        !name || converted == SG_UNIFORMTYPE_INVALID)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid shader uniform");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    s->value.uniform_names[block][member] = name;
    auto &u = s->value.desc.uniform_blocks[block].glsl_uniforms[member];
    u.glsl_name = s->value.uniform_names[block][member].c_str();
    u.type = converted;
    u.array_count = count ? count : 1;
    return NKGPU_OK;
}
nkgpu_result nkgpu_shader_texture(nkgpu_shader_builder h, uint32_t view_slot, uint32_t sampler_slot,
                                  nkgpu_shader_stage stage, const char *name) {
    auto *s = shader_builder_pool.get(h);
    if (!s || view_slot >= SG_MAX_VIEW_BINDSLOTS || sampler_slot >= SG_MAX_SAMPLER_BINDSLOTS ||
        view_slot >= SG_MAX_TEXTURE_SAMPLER_PAIRS || !name ||
        (stage != NKGPU_SHADERSTAGE_VERTEX && stage != NKGPU_SHADERSTAGE_FRAGMENT))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid shader texture binding");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    const sg_shader_stage converted =
        stage == NKGPU_SHADERSTAGE_VERTEX ? SG_SHADERSTAGE_VERTEX : SG_SHADERSTAGE_FRAGMENT;
    s->value.desc.views[view_slot].texture.stage = converted;
    s->value.desc.views[view_slot].texture.image_type = SG_IMAGETYPE_2D;
    s->value.desc.views[view_slot].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    s->value.desc.views[view_slot].texture.hlsl_register_t_n = static_cast<uint8_t>(view_slot);
    s->value.desc.views[view_slot].texture.msl_texture_n = static_cast<uint8_t>(view_slot);
    s->value.desc.samplers[sampler_slot].stage = converted;
    s->value.desc.samplers[sampler_slot].sampler_type = SG_SAMPLERTYPE_FILTERING;
    s->value.desc.samplers[sampler_slot].hlsl_register_s_n = static_cast<uint8_t>(sampler_slot);
    s->value.desc.samplers[sampler_slot].msl_sampler_n = static_cast<uint8_t>(sampler_slot);
    s->value.texture_names[view_slot] = name;
    auto &pair = s->value.desc.texture_sampler_pairs[view_slot];
    pair.stage = converted;
    pair.view_slot = (uint8_t)view_slot;
    pair.sampler_slot = (uint8_t)sampler_slot;
    pair.glsl_name = s->value.texture_names[view_slot].c_str();
    return NKGPU_OK;
}
nkgpu_result nkgpu_shader_end(nkgpu_shader_builder h, nkgpu_shader *out) {
    auto *s = shader_builder_pool.get(h);
    if (!s || !out)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale shader builder");
    Handle owner = s->value.owner;
    const nkgpu_result idle = require_idle_renderer(owner);
    if (idle != NKGPU_OK)
        return idle;
    const nkgpu_result activated = activate_renderer(owner);
    if (activated != NKGPU_OK)
        return activated;
    sg_shader object = sg_make_shader(&s->value.desc);
    const nkgpu_shader_language language = s->value.language;
    shader_builder_pool.remove(*s);
    if (sg_query_shader_state(object) != SG_RESOURCESTATE_VALID) {
        record_allocation_failure(owner);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "shader creation failed");
    }
    Handle result = shader_pool.add(Shader{owner, object, language});
    if (!result) {
        sg_destroy_shader(object);
        record_allocation_failure(owner);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "shader pool full");
    }
    record_resource_created(owner);
    *out = result;
    return NKGPU_OK;
}
nkgpu_result nkgpu_pipeline_begin(nkgpu_renderer r, nkgpu_shader shader, uint32_t stride,
                                  nkgpu_pipeline_builder *out) {
    auto *sh = shader_pool.get(shader);
    if (!renderer_pool.get(r))
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (!sh || sh->value.owner != r)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale or foreign shader");
    if (!stride || !out)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid pipeline builder");
    const nkgpu_result idle = require_idle_renderer(r);
    if (idle != NKGPU_OK)
        return idle;
    PipelineBuilder b{};
    b.owner = r;
    b.desc.shader = sh->value.object;
    b.desc.layout.buffers[0].stride = (int)stride;
    b.desc.depth.pixel_format = renderer_pool.get(r)->value.surface_depth_format;
    Handle h = pipeline_builder_pool.add(b);
    if (!h)
        return fail(NKGPU_ERROR_UNKNOWN, "builder pool full");
    *out = h;
    return NKGPU_OK;
}
nkgpu_result nkgpu_pipeline_attribute(nkgpu_pipeline_builder h, uint32_t location,
                                      uint32_t buffer_index, uint32_t offset,
                                      nkgpu_vertex_format format) {
    auto *s = pipeline_builder_pool.get(h);
    auto f = convert_format(format);
    if (!s || location >= SG_MAX_VERTEX_ATTRIBUTES ||
        buffer_index >= SG_MAX_VERTEXBUFFER_BINDSLOTS || f == SG_VERTEXFORMAT_INVALID)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "invalid pipeline attribute");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    auto &a = s->value.desc.layout.attrs[location];
    a.buffer_index = (int)buffer_index;
    a.offset = (int)offset;
    a.format = f;
    return NKGPU_OK;
}
nkgpu_result nkgpu_pipeline_index_type(nkgpu_pipeline_builder h, nkgpu_index_type type) {
    auto *s = pipeline_builder_pool.get(h);
    if (!s || type > NKGPU_INDEXTYPE_UINT32)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid index type");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    s->value.desc.index_type = type == NKGPU_INDEXTYPE_UINT16   ? SG_INDEXTYPE_UINT16
                               : type == NKGPU_INDEXTYPE_UINT32 ? SG_INDEXTYPE_UINT32
                                                                : SG_INDEXTYPE_NONE;
    return NKGPU_OK;
}
nkgpu_result nkgpu_pipeline_depth_stencil(nkgpu_pipeline_builder h, uint32_t enabled) {
    auto *s = pipeline_builder_pool.get(h);
    if (!s || enabled > 1)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid pipeline depth/stencil state");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    const auto *renderer = renderer_pool.get(s->value.owner);
    if (enabled && renderer->value.surface_depth_format == SG_PIXELFORMAT_NONE)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT,
                    "depth/stencil testing requires a surface depth target");
    s->value.desc.depth.pixel_format = enabled ? SG_PIXELFORMAT_DEPTH_STENCIL : SG_PIXELFORMAT_NONE;
    s->value.desc.depth.write_enabled = enabled != 0;
    s->value.desc.depth.compare = enabled ? SG_COMPAREFUNC_LESS_EQUAL : SG_COMPAREFUNC_ALWAYS;
    return NKGPU_OK;
}
nkgpu_result nkgpu_pipeline_blend(nkgpu_pipeline_builder h, const nkgpu_blend_state *state) {
    auto *s = pipeline_builder_pool.get(h);
    if (!s || !state || state->enabled > 1 || state->op_rgb != NKGPU_BLENDOP_ADD ||
        state->op_alpha != NKGPU_BLENDOP_ADD ||
        convert_blend_factor(state->src_rgb) == _SG_BLENDFACTOR_DEFAULT ||
        convert_blend_factor(state->dst_rgb) == _SG_BLENDFACTOR_DEFAULT ||
        convert_blend_factor(state->src_alpha) == _SG_BLENDFACTOR_DEFAULT ||
        convert_blend_factor(state->dst_alpha) == _SG_BLENDFACTOR_DEFAULT)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid pipeline blend state");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    auto &blend = s->value.desc.colors[0].blend;
    blend.enabled = state->enabled != 0;
    blend.src_factor_rgb = convert_blend_factor(state->src_rgb);
    blend.dst_factor_rgb = convert_blend_factor(state->dst_rgb);
    blend.op_rgb = SG_BLENDOP_ADD;
    blend.src_factor_alpha = convert_blend_factor(state->src_alpha);
    blend.dst_factor_alpha = convert_blend_factor(state->dst_alpha);
    blend.op_alpha = SG_BLENDOP_ADD;
    return NKGPU_OK;
}
nkgpu_result nkgpu_pipeline_stencil(nkgpu_pipeline_builder h, const nkgpu_stencil_state *state) {
    auto *s = pipeline_builder_pool.get(h);
    if (!s || !state || state->enabled > 1)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid pipeline stencil state");
    const auto valid_face = [](const nkgpu_stencil_face_state &face) {
        return convert_compare(face.compare) != _SG_COMPAREFUNC_DEFAULT &&
               convert_stencil_op(face.fail_op) != _SG_STENCILOP_DEFAULT &&
               convert_stencil_op(face.depth_fail_op) != _SG_STENCILOP_DEFAULT &&
               convert_stencil_op(face.pass_op) != _SG_STENCILOP_DEFAULT;
    };
    if (state->enabled && (!valid_face(state->front) || !valid_face(state->back)))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid stencil face state");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    auto &stencil = s->value.desc.stencil;
    stencil.enabled = state->enabled != 0;
    stencil.read_mask = state->read_mask;
    stencil.write_mask = state->write_mask;
    stencil.front = convert_stencil_face(state->front);
    stencil.back = convert_stencil_face(state->back);
    if (state->enabled && s->value.desc.depth.pixel_format == SG_PIXELFORMAT_NONE)
        s->value.desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    return NKGPU_OK;
}
nkgpu_result nkgpu_pipeline_cull_mode(nkgpu_pipeline_builder h, nkgpu_cull_mode mode,
                                      nkgpu_face_winding winding) {
    auto *s = pipeline_builder_pool.get(h);
    if (!s || (mode != NKGPU_CULLMODE_NONE && mode != NKGPU_CULLMODE_BACK) ||
        (winding != NKGPU_FACEWINDING_CCW && winding != NKGPU_FACEWINDING_CW))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid pipeline cull state");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    s->value.desc.cull_mode = mode == NKGPU_CULLMODE_BACK ? SG_CULLMODE_BACK : SG_CULLMODE_NONE;
    s->value.desc.face_winding =
        winding == NKGPU_FACEWINDING_CCW ? SG_FACEWINDING_CCW : SG_FACEWINDING_CW;
    return NKGPU_OK;
}
nkgpu_result nkgpu_pipeline_color_write_mask(nkgpu_pipeline_builder h,
                                             nkgpu_color_write_mask mask) {
    auto *s = pipeline_builder_pool.get(h);
    if (!s || (mask & ~NKGPU_COLORMASK_RGBA))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid pipeline color write mask");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    uint32_t native_mask = SG_COLORMASK_NONE;
    if (mask & NKGPU_COLORMASK_R)
        native_mask |= SG_COLORMASK_R;
    if (mask & NKGPU_COLORMASK_G)
        native_mask |= SG_COLORMASK_G;
    if (mask & NKGPU_COLORMASK_B)
        native_mask |= SG_COLORMASK_B;
    if (mask & NKGPU_COLORMASK_A)
        native_mask |= SG_COLORMASK_A;
    s->value.desc.colors[0].write_mask = static_cast<sg_color_mask>(native_mask);
    return NKGPU_OK;
}
nkgpu_result nkgpu_pipeline_end(nkgpu_pipeline_builder h, nkgpu_pipeline *out) {
    auto *s = pipeline_builder_pool.get(h);
    if (!s || !out)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale pipeline builder");
    auto owner = s->value.owner;
    const nkgpu_result idle = require_idle_renderer(owner);
    if (idle != NKGPU_OK)
        return idle;
    const nkgpu_result activated = activate_renderer(owner);
    if (activated != NKGPU_OK)
        return activated;
    sg_pipeline object = sg_make_pipeline(&s->value.desc);
    pipeline_builder_pool.remove(*s);
    if (sg_query_pipeline_state(object) != SG_RESOURCESTATE_VALID) {
        record_allocation_failure(owner);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "pipeline creation failed");
    }
    Handle result = pipeline_pool.add(Pipeline{owner, object});
    if (!result) {
        sg_destroy_pipeline(object);
        record_allocation_failure(owner);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "pipeline pool full");
    }
    record_resource_created(owner);
    *out = result;
    return NKGPU_OK;
}
nkgpu_result nkgpu_pipeline_destroy(nkgpu_renderer r, nkgpu_pipeline h) {
    auto *s = pipeline_pool.get(h);
    if (!renderer_pool.get(r) || !s || s->value.owner != r)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale/foreign pipeline");
    bool backend_available = false;
    const nkgpu_result ready = prepare_resource_destroy(r, backend_available);
    if (ready != NKGPU_OK)
        return ready;
    /* A batch may still reference this pipeline; defer its backend destruction. */
    if (backend_available && !s->pins)
        sg_destroy_pipeline(s->value.object);
    record_resource_destroyed(r);
    pipeline_pool.remove(*s);
    return NKGPU_OK;
}
nkgpu_result nkgpu_begin_frame(nkgpu_renderer h) {
    const nkgpu_result result = nkgpu_frame_begin(h);
    if (result != NKGPU_OK)
        return result;
    auto *s = renderer_pool.get(h);
    nk_surface_frame_target target{};
    target.struct_size = sizeof(target);
    if (nk_surface_get_frame_target(s->value.surface, &target) != NK_OK || target.width <= 0 ||
        target.height <= 0) {
        s->value.state = RendererState::Ready;
        active_renderer = 0;
        return fail(NKGPU_ERROR_UNKNOWN, "surface framebuffer is unavailable");
    }
    return nkgpu_begin_window_pass(h, static_cast<uint32_t>(target.width),
                                   static_cast<uint32_t>(target.height), 1);
}
nkgpu_result nkgpu_frame_begin(nkgpu_renderer h) {
    auto *s = renderer_pool.get(h);
    if (!s)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (s->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    if (s->value.state != RendererState::Ready || active_renderer)
        return fail(NKGPU_ERROR_WRONG_STATE, "a renderer frame is already active");
    const bool context_backend = s->value.graphics_api == NK_GRAPHICS_OPENGL ||
                                 s->value.graphics_api == NK_GRAPHICS_OPENGL_ES;
    if (!context_backend && nk_surface_make_current(s->value.surface) != NK_OK)
        return fail(NKGPU_ERROR_UNKNOWN, "current: %s", nk_last_error());
    const nkgpu_result activated = activate_renderer(h);
    if (activated != NKGPU_OK)
        return activated;
    sg_reset_state_cache();
    s->value.state = RendererState::FrameActive;
    s->value.has_frame_target = false;
    s->value.frame_target = {};
    s->value.in_pass = false;
    s->value.active_target = 0;
    s->value.bindings = {};
    active_renderer = h;
    return NKGPU_OK;
}

nkgpu_result nkgpu_frame_begin_with_target(nkgpu_renderer h,
                                           const nk_surface_frame_target *frame_target) {
    if (!frame_target)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "frame target is null");
    return begin_frame_with_target(h, *frame_target);
}

nkgpu_result nkgpu_begin_window_pass(nkgpu_renderer h, uint32_t width, uint32_t height,
                                     uint32_t clear) {
    auto *s = renderer_pool.get(h);
    if (!s || !width || !height || width > INT32_MAX || height > INT32_MAX || clear > 1)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid window-pass arguments");
    if (s->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    if (s->value.state != RendererState::FrameActive || s->value.in_pass || active_renderer != h)
        return fail(NKGPU_ERROR_WRONG_STATE, "window pass requires a frame with no active pass");
    const nkgpu_result activated = activate_renderer(h);
    if (activated != NKGPU_OK)
        return activated;
    nk_surface_frame_target target =
        s->value.has_frame_target ? s->value.frame_target : nk_surface_frame_target{};
    if (!s->value.has_frame_target) {
        target.struct_size = sizeof(target);
        if (nk_surface_get_frame_target(s->value.surface, &target) != NK_OK)
            return fail(NKGPU_ERROR_UNKNOWN, "surface framebuffer is unavailable");
    }
    if (target.width <= 0 || target.height <= 0)
        return fail(NKGPU_ERROR_UNKNOWN, "surface framebuffer is unavailable");
    if (!target_matches_renderer(s->value, target)) {
        ++s->value.surface_recreations;
        mark_renderer_lost(h, s->value);
        return fail(NKGPU_ERROR_DEVICE_LOST,
                    "surface graphics device changed; recreate the GPU renderer");
    }
    s->value.frame_target = target;
    s->value.has_frame_target = true;
    sg_pass pass{};
    pass.action.colors[0].load_action = clear ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.colors[0].clear_value = {.025f, .035f, .07f, 1};
    if (s->value.surface_depth_format != SG_PIXELFORMAT_NONE) {
        pass.action.depth = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, 1.0f};
        pass.action.stencil = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, 0};
    }
    pass.swapchain = {.width = static_cast<int>(width),
                      .height = static_cast<int>(height),
                      .sample_count = 1,
                      .color_format = s->value.surface_color_format,
                      .depth_format = s->value.surface_depth_format};
    if (target.api == NK_GRAPHICS_D3D11) {
        pass.swapchain.d3d11.render_view =
            reinterpret_cast<const void *>(static_cast<uintptr_t>(target.native_target));
        pass.swapchain.d3d11.depth_stencil_view = reinterpret_cast<const void *>(
            static_cast<uintptr_t>(target.native_depth_stencil_target));
    } else if (target.api == NK_GRAPHICS_METAL) {
        pass.swapchain.metal.current_drawable =
            reinterpret_cast<const void *>(static_cast<uintptr_t>(target.native_present_target));
        pass.swapchain.metal.depth_stencil_texture = reinterpret_cast<const void *>(
            static_cast<uintptr_t>(target.native_depth_stencil_target));
    } else {
        pass.swapchain.gl.framebuffer = static_cast<uint32_t>(target.native_target);
    }
    sg_begin_pass(&pass);
    s->value.in_pass = true;
    ++s->value.passes;
    s->value.active_target = 0;
    s->value.pass_width = static_cast<int32_t>(width);
    s->value.pass_height = static_cast<int32_t>(height);
    s->value.bindings = {};
    return NKGPU_OK;
}
nkgpu_result nkgpu_begin_target_pass(nkgpu_renderer h, nkgpu_render_target target_handle,
                                     uint32_t clear) {
    auto *renderer = renderer_pool.get(h);
    auto *target = render_target_pool.get(target_handle);
    if (!renderer || !target || target->value.owner != h)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale or foreign render target");
    if (clear > 1)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid target clear flag");
    if (renderer->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    if (renderer->value.state != RendererState::FrameActive || renderer->value.in_pass ||
        active_renderer != h)
        return fail(NKGPU_ERROR_WRONG_STATE, "target pass requires a frame with no active pass");
    const nkgpu_result activated = activate_renderer(h);
    if (activated != NKGPU_OK)
        return activated;
    sg_pass pass{};
    pass.action.colors[0].load_action = clear ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
    if (target->value.depth.id) {
        pass.action.depth = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, 1.0f};
        pass.action.stencil = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, 0};
        pass.attachments.depth_stencil = target->value.depth_attachment;
    }
    pass.attachments.colors[0] = target->value.color_attachment;
    sg_begin_pass(&pass);
    renderer->value.in_pass = true;
    ++renderer->value.passes;
    renderer->value.active_target = target_handle;
    renderer->value.pass_width = target->value.width;
    renderer->value.pass_height = target->value.height;
    renderer->value.bindings = {};
    return NKGPU_OK;
}
nkgpu_result nkgpu_end_pass(nkgpu_renderer h) {
    auto *renderer = renderer_pool.get(h);
    if (!renderer)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (renderer->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    if (renderer->value.state != RendererState::FrameActive || !renderer->value.in_pass ||
        active_renderer != h)
        return fail(NKGPU_ERROR_WRONG_STATE, "no active pass");
    const nkgpu_result activated = activate_renderer(h);
    if (activated != NKGPU_OK)
        return activated;
    sg_end_pass();
    renderer->value.in_pass = false;
    renderer->value.active_target = 0;
    renderer->value.bindings = {};
    return NKGPU_OK;
}
nkgpu_result nkgpu_apply_scissor(nkgpu_renderer h, uint32_t enabled, int32_t x, int32_t y,
                                 int32_t width, int32_t height) {
    auto *renderer = renderer_pool.get(h);
    const nkgpu_result pass = require_active_pass(h);
    if (pass != NKGPU_OK)
        return pass;
    if (enabled > 1 || (enabled && (width < 0 || height < 0)))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid scissor rectangle");
    if (!enabled) {
        sg_apply_scissor_rect(0, 0, renderer->value.pass_width, renderer->value.pass_height, true);
        return NKGPU_OK;
    }
    const int64_t right = static_cast<int64_t>(x) + width;
    const int64_t bottom = static_cast<int64_t>(y) + height;
    const int32_t left_clamped = std::clamp(x, 0, renderer->value.pass_width);
    const int32_t top_clamped = std::clamp(y, 0, renderer->value.pass_height);
    const int32_t right_clamped =
        static_cast<int32_t>(std::clamp<int64_t>(right, 0, renderer->value.pass_width));
    const int32_t bottom_clamped =
        static_cast<int32_t>(std::clamp<int64_t>(bottom, 0, renderer->value.pass_height));
    sg_apply_scissor_rect(left_clamped, top_clamped, std::max(0, right_clamped - left_clamped),
                          std::max(0, bottom_clamped - top_clamped), true);
    return NKGPU_OK;
}
nkgpu_result nkgpu_apply_pipeline(nkgpu_renderer r, nkgpu_pipeline h) {
    auto *rs = renderer_pool.get(r);
    auto *p = pipeline_pool.get(h);
    const nkgpu_result pass = require_active_pass(r);
    if (pass != NKGPU_OK)
        return pass;
    if (!rs || !p || p->value.owner != r)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "invalid pipeline/frame");
    sg_apply_pipeline(p->value.object);
    return NKGPU_OK;
}
nkgpu_result nkgpu_apply_vertex_buffer(nkgpu_renderer r, uint32_t slot, nkgpu_buffer h,
                                       uint32_t offset) {
    auto *rs = renderer_pool.get(r);
    auto *b = buffer_pool.get(h);
    const nkgpu_result pass = require_active_pass(r);
    if (pass != NKGPU_OK)
        return pass;
    if (!rs || !b || b->value.owner != r || slot >= SG_MAX_VERTEXBUFFER_BINDSLOTS)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "invalid buffer/frame");
    rs->value.bindings.vertex_buffers[slot] = b->value.object;
    rs->value.bindings.vertex_buffer_offsets[slot] = (int)offset;
    return NKGPU_OK;
}
nkgpu_result nkgpu_apply_index_buffer(nkgpu_renderer r, nkgpu_buffer h, uint32_t offset) {
    auto *rs = renderer_pool.get(r);
    auto *b = buffer_pool.get(h);
    const nkgpu_result pass = require_active_pass(r);
    if (pass != NKGPU_OK)
        return pass;
    if (!rs || !b || b->value.owner != r)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "invalid index buffer/frame");
    rs->value.bindings.index_buffer = b->value.object;
    rs->value.bindings.index_buffer_offset = (int)offset;
    return NKGPU_OK;
}
nkgpu_result nkgpu_uniforms_begin(nkgpu_renderer r, uint32_t size, nkgpu_uniform_builder *out) {
    auto *renderer = renderer_pool.get(r);
    const nkgpu_result live = renderer_live(r, renderer ? &renderer->value : nullptr);
    if (live != NKGPU_OK)
        return live;
    if (!size || !out)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid uniform builder");
    *out = 0;
    uint8_t *data = (uint8_t *)calloc(1, size);
    if (!data)
        return fail(NKGPU_ERROR_UNKNOWN, "allocation failed");
    Handle h = 0;
#if NK_ENABLE_NO_EXCEPTIONS
    h = uniform_builder_pool.add(UniformBuilder{r, data, size});
#else
    try {
        h = uniform_builder_pool.add(UniformBuilder{r, data, size});
    } catch (const std::bad_alloc &) {
        free(data);
        return fail(NKGPU_ERROR_UNKNOWN, "uniform builder allocation failed");
    } catch (...) {
        free(data);
        return fail(NKGPU_ERROR_UNKNOWN, "uniform builder allocation failed");
    }
#endif
    if (!h) {
        free(data);
        return fail(NKGPU_ERROR_UNKNOWN, "uniform builder pool full");
    }
    *out = h;
    return NKGPU_OK;
}
nkgpu_result nkgpu_uniforms_write_f32(nkgpu_uniform_builder h, uint32_t offset, float value) {
    auto *s = uniform_builder_pool.get(h);
    if (!s || offset > s->value.size || s->value.size - offset < sizeof(value))
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale/out-of-range uniform builder");
    memcpy(s->value.data + offset, &value, sizeof(value));
    return NKGPU_OK;
}
nkgpu_result nkgpu_apply_uniform_data(nkgpu_renderer r, uint32_t slot, const uint8_t *data,
                                      uint32_t size) {
    auto *renderer = renderer_pool.get(r);
    const nkgpu_result pass = require_active_pass(r);
    if (pass != NKGPU_OK)
        return pass;
    if (!data || !size)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "uniform data is empty");
    const sg_range range{data, size};
    sg_apply_uniforms(static_cast<int>(slot), &range);
    return NKGPU_OK;
}
nkgpu_result nkgpu_apply_uniforms(nkgpu_renderer r, uint32_t slot, nkgpu_uniform_builder h) {
    auto *rs = renderer_pool.get(r);
    auto *u = uniform_builder_pool.get(h);
    const nkgpu_result pass = require_active_pass(r);
    if (pass != NKGPU_OK)
        return pass;
    if (!rs || !u || u->value.owner != r)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "invalid uniforms/frame");
    sg_range range{u->value.data, u->value.size};
    sg_apply_uniforms((int)slot, &range);
    free(u->value.data);
    uniform_builder_pool.remove(*u);
    return NKGPU_OK;
}
nkgpu_result nkgpu_image_begin(nkgpu_renderer r, uint32_t width, uint32_t height,
                               nkgpu_image_builder *out) {
    if (!width || !height || !out || width > UINT32_MAX / height / 4)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid image builder");
    const nkgpu_result idle = require_idle_renderer(r);
    if (idle != NKGPU_OK)
        return idle;
    uint8_t *data = (uint8_t *)calloc((size_t)width * height, 4);
    if (!data)
        return fail(NKGPU_ERROR_UNKNOWN, "image allocation failed");
    Handle h = image_builder_pool.add(ImageBuilder{r, data, width, height});
    if (!h) {
        free(data);
        return fail(NKGPU_ERROR_UNKNOWN, "image builder pool full");
    }
    *out = h;
    return NKGPU_OK;
}
nkgpu_result nkgpu_image_write_rgba8(nkgpu_image_builder h, uint32_t x, uint32_t y, uint32_t red,
                                     uint32_t green, uint32_t blue, uint32_t alpha) {
    auto *s = image_builder_pool.get(h);
    if (!s || x >= s->value.width || y >= s->value.height || red > 255 || green > 255 ||
        blue > 255 || alpha > 255)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid image pixel write");
    const nkgpu_result idle = require_idle_renderer(s->value.owner);
    if (idle != NKGPU_OK)
        return idle;
    uint8_t *pixel = s->value.data + ((size_t)y * s->value.width + x) * 4;
    pixel[0] = (uint8_t)red;
    pixel[1] = (uint8_t)green;
    pixel[2] = (uint8_t)blue;
    pixel[3] = (uint8_t)alpha;
    return NKGPU_OK;
}
nkgpu_result nkgpu_image_end(nkgpu_image_builder h, nkgpu_image *out) {
    auto *s = image_builder_pool.get(h);
    if (!s || !out)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale image builder");
    const Handle owner = s->value.owner;
    const nkgpu_result idle = require_idle_renderer(owner);
    if (idle != NKGPU_OK)
        return idle;
    const nkgpu_result activated = activate_renderer(owner);
    if (activated != NKGPU_OK)
        return activated;
    if (consume_image_creation_failure(owner)) {
        free(s->value.data);
        image_builder_pool.remove(*s);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "injected image allocation failure");
    }
    sg_image_desc desc{};
    desc.width = (int)s->value.width;
    desc.height = (int)s->value.height;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0] = {s->value.data, (size_t)s->value.width * s->value.height * 4};
    const sg_image image = sg_make_image(&desc);
    Image image_value{};
    image_value.owner = owner;
    image_value.object = image;
    image_value.width = s->value.width;
    image_value.height = s->value.height;
    image_value.format = NKGPU_IMAGEFORMAT_RGBA8;
    image_value.dynamic_update = false;
    image_value.pixels.assign(s->value.data, s->value.data + static_cast<size_t>(s->value.width) *
                                                                 s->value.height * 4);
    free(s->value.data);
    image_builder_pool.remove(*s);
    if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID) {
        record_allocation_failure(owner);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "image creation failed");
    }
    sg_view_desc view_desc{};
    view_desc.texture.image = image;
    const sg_view view = sg_make_view(&view_desc);
    if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID) {
        sg_destroy_image(image);
        record_allocation_failure(owner);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "texture view creation failed");
    }
    image_value.view = view;
    Handle result = image_pool.add(image_value);
    if (!result) {
        sg_destroy_view(view);
        sg_destroy_image(image);
        record_allocation_failure(owner);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "image pool full");
    }
    record_resource_created(owner, image_value.pixels.size());
    *out = result;
    return NKGPU_OK;
}
nkgpu_result nkgpu_image_create(nkgpu_renderer r, uint32_t width, uint32_t height,
                                nkgpu_image_format format, const uint8_t *pixels, uint32_t size,
                                uint32_t dynamic_update, nkgpu_image *out) {
    {
        const uint32_t bytes_per_pixel = format == NKGPU_IMAGEFORMAT_R8      ? 1
                                         : format == NKGPU_IMAGEFORMAT_RGBA8 ? 4
                                                                             : 0;
        if (!width || !height || width > INT32_MAX || height > INT32_MAX || !bytes_per_pixel ||
            !pixels || !out || dynamic_update > 1 ||
            static_cast<uint64_t>(width) * height * bytes_per_pixel != size)
            return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid image creation arguments");
        *out = 0;
        const nkgpu_result access = require_streaming_resource_access(r);
        if (access != NKGPU_OK)
            return access;
        const nkgpu_result activated = activate_renderer(r);
        if (activated != NKGPU_OK)
            return activated;
        if (consume_image_creation_failure(r))
            return fail(NKGPU_ERROR_OUT_OF_MEMORY, "injected image allocation failure");
        sg_image_desc desc{};
        desc.width = static_cast<int>(width);
        desc.height = static_cast<int>(height);
        desc.pixel_format =
            format == NKGPU_IMAGEFORMAT_R8 ? SG_PIXELFORMAT_R8 : SG_PIXELFORMAT_RGBA8;
        // Keep the public update contract independent of Sokol's per-frame
        // dynamic-image update restriction. Mutable images are replaced from
        // their CPU mirror when updated; every Sokol image starts with valid data.
        desc.data.mip_levels[0] = {pixels, size};
        sg_image object = sg_make_image(&desc);
        struct ImageGuard {
            sg_image object{};
            ~ImageGuard() {
                if (object.id)
                    sg_destroy_image(object);
            }
        } image_guard{object};
        if (sg_query_image_state(object) != SG_RESOURCESTATE_VALID) {
            record_allocation_failure(r);
            return fail(NKGPU_ERROR_OUT_OF_MEMORY, "image creation failed");
        }
        sg_view_desc view_desc{};
        view_desc.texture.image = object;
        sg_view view = sg_make_view(&view_desc);
        struct ViewGuard {
            sg_view view{};
            ~ViewGuard() {
                if (view.id)
                    sg_destroy_view(view);
            }
        } view_guard{view};
        if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID) {
            record_allocation_failure(r);
            return fail(NKGPU_ERROR_OUT_OF_MEMORY, "texture view creation failed");
        }
        Image image_value{};
        image_value.owner = r;
        image_value.object = object;
        image_value.view = view;
        image_value.width = width;
        image_value.height = height;
        image_value.format = format;
        image_value.dynamic_update = dynamic_update != 0;
        image_value.pixels.assign(pixels, pixels + size);
        const Handle handle = image_pool.add(std::move(image_value));
        if (!handle) {
            record_allocation_failure(r);
            return fail(NKGPU_ERROR_OUT_OF_MEMORY, "image pool full");
        }
        image_guard.object = {};
        view_guard.view = {};
        record_resource_created(r, size);
        *out = handle;
        return NKGPU_OK;
    }
}
nkgpu_result nkgpu_image_update(nkgpu_renderer r, nkgpu_image h, uint32_t x, uint32_t y,
                                uint32_t width, uint32_t height, const uint8_t *pixels,
                                uint32_t row_pitch) {
    auto *image = image_pool.get(h);
    if (!renderer_pool.get(r) || !image || image->value.owner != r)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale or foreign image");
    const uint32_t bytes_per_pixel = image->value.format == NKGPU_IMAGEFORMAT_R8 ? 1 : 4;
    if (!image->value.dynamic_update || !pixels || !width || !height || x > image->value.width ||
        y > image->value.height || width > image->value.width - x ||
        height > image->value.height - y ||
        row_pitch < static_cast<uint64_t>(width) * bytes_per_pixel ||
        row_pitch % bytes_per_pixel != 0 || row_pitch > INT32_MAX)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid image update region");
    const uint64_t source_size = static_cast<uint64_t>(row_pitch) * (height - 1) +
                                 static_cast<uint64_t>(width) * bytes_per_pixel;
    if (source_size > SIZE_MAX)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "image update size overflow");
    const nkgpu_result access = require_streaming_resource_access(r);
    if (access != NKGPU_OK)
        return access;
    const nkgpu_result activated = activate_renderer(r);
    if (activated != NKGPU_OK)
        return activated;
    std::vector<uint8_t> next_pixels;
    next_pixels = image->value.pixels;
    for (uint32_t row = 0; row < height; ++row) {
        const auto *source = pixels + static_cast<size_t>(row) * row_pitch;
        auto *destination =
            next_pixels.data() +
            (static_cast<size_t>(y + row) * image->value.width + x) * bytes_per_pixel;
        memcpy(destination, source, static_cast<size_t>(width) * bytes_per_pixel);
    }
    sg_image_desc desc{};
    desc.width = static_cast<int>(image->value.width);
    desc.height = static_cast<int>(image->value.height);
    desc.pixel_format =
        image->value.format == NKGPU_IMAGEFORMAT_R8 ? SG_PIXELFORMAT_R8 : SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0] = {next_pixels.data(), next_pixels.size()};
    if (consume_image_creation_failure(r))
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "injected image allocation failure");
    const sg_image object = sg_make_image(&desc);
    if (sg_query_image_state(object) != SG_RESOURCESTATE_VALID) {
        record_allocation_failure(r);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "updated image creation failed");
    }
    sg_view_desc view_desc{};
    view_desc.texture.image = object;
    const sg_view view = sg_make_view(&view_desc);
    if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID) {
        sg_destroy_image(object);
        record_allocation_failure(r);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "updated image view creation failed");
    }
    sg_destroy_view(image->value.view);
    sg_destroy_image(image->value.object);
    image->value.object = object;
    image->value.view = view;
    image->value.pixels = std::move(next_pixels);
    renderer_pool.get(r)->value.upload_bytes +=
        static_cast<uint64_t>(width) * height * bytes_per_pixel;
    return NKGPU_OK;
}
nkgpu_result nkgpu_image_destroy(nkgpu_renderer r, nkgpu_image h) {
    auto *s = image_pool.get(h);
    if (!renderer_pool.get(r) || !s || s->value.owner != r)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale/foreign image");
    bool backend_available = false;
    const nkgpu_result ready = prepare_resource_destroy(r, backend_available);
    if (ready != NKGPU_OK)
        return ready;
    /* A batch may still reference this image; defer its backend destruction. */
    if (backend_available && !s->pins) {
        sg_destroy_view(s->value.view);
        sg_destroy_image(s->value.object);
    }
    record_resource_destroyed(r);
    image_pool.remove(*s);
    return NKGPU_OK;
}
nkgpu_result nkgpu_sampler_create(nkgpu_renderer r, nkgpu_filter min_filter,
                                  nkgpu_filter mag_filter, nkgpu_wrap wrap_u, nkgpu_wrap wrap_v,
                                  nkgpu_sampler *out) {
    if (!out || min_filter < 1 || min_filter > 2 || mag_filter < 1 || mag_filter > 2 ||
        wrap_u < 1 || wrap_u > 2 || wrap_v < 1 || wrap_v > 2)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid sampler arguments");
    const nkgpu_result access = require_streaming_resource_access(r);
    if (access != NKGPU_OK)
        return access;
    const nkgpu_result activated = activate_renderer(r);
    if (activated != NKGPU_OK)
        return activated;
    sg_sampler_desc desc{};
    desc.min_filter = min_filter == NKGPU_FILTER_LINEAR ? SG_FILTER_LINEAR : SG_FILTER_NEAREST;
    desc.mag_filter = mag_filter == NKGPU_FILTER_LINEAR ? SG_FILTER_LINEAR : SG_FILTER_NEAREST;
    desc.wrap_u = wrap_u == NKGPU_WRAP_CLAMP_TO_EDGE ? SG_WRAP_CLAMP_TO_EDGE : SG_WRAP_REPEAT;
    desc.wrap_v = wrap_v == NKGPU_WRAP_CLAMP_TO_EDGE ? SG_WRAP_CLAMP_TO_EDGE : SG_WRAP_REPEAT;
    const sg_sampler sampler = sg_make_sampler(&desc);
    if (sg_query_sampler_state(sampler) != SG_RESOURCESTATE_VALID) {
        record_allocation_failure(r);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "sampler creation failed");
    }
    Handle result = sampler_pool.add(Sampler{r, sampler});
    if (!result) {
        sg_destroy_sampler(sampler);
        record_allocation_failure(r);
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "sampler pool full");
    }
    record_resource_created(r);
    *out = result;
    return NKGPU_OK;
}
nkgpu_result nkgpu_sampler_destroy(nkgpu_renderer r, nkgpu_sampler h) {
    auto *s = sampler_pool.get(h);
    if (!renderer_pool.get(r) || !s || s->value.owner != r)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale/foreign sampler");
    bool backend_available = false;
    const nkgpu_result ready = prepare_resource_destroy(r, backend_available);
    if (ready != NKGPU_OK)
        return ready;
    /* A batch may still reference this sampler; defer its backend destruction. */
    if (backend_available && !s->pins)
        sg_destroy_sampler(s->value.object);
    record_resource_destroyed(r);
    sampler_pool.remove(*s);
    return NKGPU_OK;
}
nkgpu_result nkgpu_apply_image(nkgpu_renderer r, uint32_t slot, nkgpu_image h) {
    auto *rs = renderer_pool.get(r);
    auto *image = image_pool.get(h);
    const nkgpu_result pass = require_active_pass(r);
    if (pass != NKGPU_OK)
        return pass;
    if (!rs || !image || image->value.owner != r || slot >= SG_MAX_VIEW_BINDSLOTS)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "invalid image/frame");
    rs->value.bindings.views[slot] = image->value.view;
    return NKGPU_OK;
}
/*
 * Resolves an external graphics image to a backend view, rejecting images that
 * belong to another runtime or device. Shared by the immediate and batch paths
 * so both apply the same validation.
 */
static nkgpu_result resolve_graphics_image(Renderer &renderer, uint32_t slot,
                                           nk_graphics_image image, sg_view &out_view) {
    if (!image.id || slot >= SG_MAX_VIEW_BINDSLOTS)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid graphics-image binding");
    if (!renderer.api->external_image_resolve)
        return fail(NKGPU_ERROR_UNKNOWN, "external graphics images are unavailable");
    nk_graphics_image_info info{};
    info.struct_size = sizeof(info);
    const void *runtime = nullptr;
    uint64_t backend_image = 0;
    sg_view view{};
    int32_t width = 0;
    int32_t height = 0;
    if (nk_core_graphics_image_get_backend(image, &info, &runtime, &backend_image) != NK_OK ||
        runtime != renderer.api || info.api != renderer.graphics_api ||
        info.device.id != renderer.device.id || !backend_image ||
        !renderer.api->external_image_resolve(static_cast<uint32_t>(backend_image), &view, &width,
                                              &height) ||
        !view.id || width <= 0 || height <= 0)
        return fail(NKGPU_ERROR_INVALID_HANDLE,
                    "graphics image belongs to another runtime or device");
    out_view = view;
    return NKGPU_OK;
}
nkgpu_result nkgpu_apply_graphics_image(nkgpu_renderer r, uint32_t slot, nk_graphics_image image) {
    auto *renderer = renderer_pool.get(r);
    const nkgpu_result pass = require_active_pass(r);
    if (pass != NKGPU_OK)
        return pass;
    if (!renderer)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    sg_view view{};
    const nkgpu_result resolved = resolve_graphics_image(renderer->value, slot, image, view);
    if (resolved != NKGPU_OK)
        return resolved;
    renderer->value.bindings.views[slot] = view;
    return NKGPU_OK;
}
nkgpu_result nkgpu_apply_sampler(nkgpu_renderer r, uint32_t slot, nkgpu_sampler h) {
    auto *rs = renderer_pool.get(r);
    auto *sampler = sampler_pool.get(h);
    const nkgpu_result pass = require_active_pass(r);
    if (pass != NKGPU_OK)
        return pass;
    if (!rs || !sampler || sampler->value.owner != r || slot >= SG_MAX_SAMPLER_BINDSLOTS)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "invalid sampler/frame");
    rs->value.bindings.samplers[slot] = sampler->value.object;
    return NKGPU_OK;
}
nkgpu_result nkgpu_draw(nkgpu_renderer r, uint32_t base, uint32_t count, uint32_t instances) {
    auto *rs = renderer_pool.get(r);
    const nkgpu_result pass = require_active_pass(r);
    if (pass != NKGPU_OK)
        return pass;
    if (!rs || !count || !instances)
        return fail(NKGPU_ERROR_WRONG_STATE, "invalid draw/frame");
    sg_apply_bindings(&rs->value.bindings);
    sg_draw((int)base, (int)count, (int)instances);
    ++rs->value.draw_calls;
    return NKGPU_OK;
}
static uint32_t read_u32(const uint8_t *data) {
    return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) |
           (uint32_t(data[3]) << 24);
}
static nkgpu_result submit_command(nkgpu_renderer r, uint32_t opcode, const uint8_t *payload,
                                   uint32_t size) {
    switch (opcode) {
    case NKGPU_COMMAND_APPLY_PIPELINE:
        return size == 4 ? nkgpu_apply_pipeline(r, nkgpu_pipeline{read_u32(payload)})
                         : NKGPU_ERROR_INVALID_ARGUMENT;
    case NKGPU_COMMAND_APPLY_VERTEX_BUFFER:
        return size == 12 ? nkgpu_apply_vertex_buffer(r, read_u32(payload),
                                                      nkgpu_buffer{read_u32(payload + 4)},
                                                      read_u32(payload + 8))
                          : NKGPU_ERROR_INVALID_ARGUMENT;
    case NKGPU_COMMAND_APPLY_INDEX_BUFFER:
        return size == 8 ? nkgpu_apply_index_buffer(r, nkgpu_buffer{read_u32(payload)},
                                                    read_u32(payload + 4))
                         : NKGPU_ERROR_INVALID_ARGUMENT;
    case NKGPU_COMMAND_APPLY_IMAGE:
        return size == 8
                   ? nkgpu_apply_image(r, read_u32(payload), nkgpu_image{read_u32(payload + 4)})
                   : NKGPU_ERROR_INVALID_ARGUMENT;
    case NKGPU_COMMAND_APPLY_SAMPLER:
        return size == 8
                   ? nkgpu_apply_sampler(r, read_u32(payload), nkgpu_sampler{read_u32(payload + 4)})
                   : NKGPU_ERROR_INVALID_ARGUMENT;
    case NKGPU_COMMAND_APPLY_UNIFORMS: {
        if (size < 8 || size - 8 != read_u32(payload + 4))
            return NKGPU_ERROR_INVALID_ARGUMENT;
        auto *renderer = renderer_pool.get(r);
        const nkgpu_result pass = require_active_pass(r);
        if (pass != NKGPU_OK)
            return pass;
        sg_range range{payload + 8, size - 8};
        sg_apply_uniforms(read_u32(payload), &range);
        return NKGPU_OK;
    }
    case NKGPU_COMMAND_DRAW:
        return size == 12
                   ? nkgpu_draw(r, read_u32(payload), read_u32(payload + 4), read_u32(payload + 8))
                   : NKGPU_ERROR_INVALID_ARGUMENT;
    case NKGPU_COMMAND_APPLY_SCISSOR:
        return size == 20 ? nkgpu_apply_scissor(r, read_u32(payload),
                                                static_cast<int32_t>(read_u32(payload + 4)),
                                                static_cast<int32_t>(read_u32(payload + 8)),
                                                static_cast<int32_t>(read_u32(payload + 12)),
                                                static_cast<int32_t>(read_u32(payload + 16)))
                          : NKGPU_ERROR_INVALID_ARGUMENT;
    case NKGPU_COMMAND_APPLY_GRAPHICS_IMAGE:
        return size == 8 ? nkgpu_apply_graphics_image(r, read_u32(payload),
                                                      nk_graphics_image{read_u32(payload + 4)})
                         : NKGPU_ERROR_INVALID_ARGUMENT;
    default:
        return NKGPU_ERROR_INVALID_ARGUMENT;
    }
}
nkgpu_result nkgpu_submit_commands(nkgpu_renderer r, const uint8_t *commands, uint32_t size) {
    auto *renderer = renderer_pool.get(r);
    const nkgpu_result pass = require_active_pass(r);
    if (pass != NKGPU_OK)
        return pass;
    if (!commands || !size)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "empty command stream");
    uint32_t offset = 0;
    while (offset < size) {
        if (size - offset < 8)
            return fail(NKGPU_ERROR_INVALID_ARGUMENT, "truncated command header at %u", offset);
        const uint32_t opcode = read_u32(commands + offset);
        const uint32_t record_size = read_u32(commands + offset + 4);
        if (record_size < 8 || record_size > size - offset)
            return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid command size at %u", offset);
        const nkgpu_result result =
            submit_command(r, opcode, commands + offset + 8, record_size - 8);
        if (result != NKGPU_OK)
            return fail(result, "invalid command %u at %u", opcode, offset);
        offset += record_size;
    }
    return NKGPU_OK;
}

/* ------------------------------------------------------------------------- */
/* Sealed submission batches                                                 */
/* ------------------------------------------------------------------------- */

/*
 * Pins one resource handle for a batch. The handle must still resolve, and a
 * second reference to the same slot shares the existing pin instead of
 * stacking a new one.
 */
static bool retain_batch_resource(Batch &batch, uint32_t kind, Handle handle) {
    const uint32_t slot_index = handle & 0xFFFFu;
    if (!slot_index)
        return false;
    /* A batch may only describe work on its own renderer's resources. */
    bool resolvable = false;
    switch (kind) {
    case BufferKind: {
        auto *slot = buffer_pool.get_retained(handle);
        resolvable = slot && slot->value.owner == batch.owner;
        break;
    }
    case PipelineKind: {
        auto *slot = pipeline_pool.get_retained(handle);
        resolvable = slot && slot->value.owner == batch.owner;
        break;
    }
    case ImageKind: {
        auto *slot = image_pool.get_retained(handle);
        resolvable = slot && slot->value.owner == batch.owner;
        break;
    }
    case SamplerKind: {
        auto *slot = sampler_pool.get_retained(handle);
        resolvable = slot && slot->value.owner == batch.owner;
        break;
    }
    case RenderTargetKind: {
        auto *slot = render_target_pool.get_retained(handle);
        resolvable = slot && slot->value.owner == batch.owner;
        break;
    }
    default:
        return false;
    }
    if (!resolvable)
        return false;
    for (const auto &retained : batch.retained)
        if (retained.kind == kind && retained.slot == slot_index)
            return true;
    switch (kind) {
    case BufferKind:
        ++buffer_pool.slots[slot_index - 1].pins;
        break;
    case PipelineKind:
        ++pipeline_pool.slots[slot_index - 1].pins;
        break;
    case ImageKind:
        ++image_pool.slots[slot_index - 1].pins;
        break;
    case SamplerKind:
        ++sampler_pool.slots[slot_index - 1].pins;
        break;
    case RenderTargetKind:
        ++render_target_pool.slots[slot_index - 1].pins;
        break;
    default:
        return false;
    }
    batch.retained.push_back(RetainedResource{kind, slot_index});
    return true;
}

/*
 * Retains an external graphics image for a batch. The core handle keeps the
 * image alive, and the same runtime/device validation the immediate path
 * applies runs here so a foreign image is rejected when it is recorded.
 */
static bool retain_batch_graphics_image(Batch &batch, uint32_t slot, uint32_t image_id) {
    auto *renderer = renderer_pool.get(batch.owner);
    if (!renderer)
        return false;
    const nk_graphics_image image{image_id};
    sg_view view{};
    if (resolve_graphics_image(renderer->value, slot, image, view) != NKGPU_OK)
        return false;
    for (const uint32_t retained : batch.retained_images)
        if (retained == image_id)
            return true;
    if (nk_graphics_image_retain(image) != NK_OK)
        return false;
    batch.retained_images.push_back(image_id);
    return true;
}

/* Reports the record that made a batch append fail and returns false. */
static bool invalid_batch_record(uint32_t opcode, uint32_t offset, const char *reason) {
    fail(NKGPU_ERROR_INVALID_ARGUMENT, "batch record %u at %u: %s", opcode, offset, reason);
    return false;
}

/*
 * Validates a packed command stream and retains everything it references. The
 * stream uses the same record format as nkgpu_submit_commands(), so a batch
 * rejects malformed or stale records at append time instead of at submit.
 */
static bool retain_batch_records(Batch &batch, const uint8_t *commands, uint32_t size) {
    uint32_t offset = 0;
    while (offset < size) {
        if (size - offset < 8)
            return invalid_batch_record(0, offset, "truncated header");
        const uint32_t opcode = read_u32(commands + offset);
        const uint32_t record_size = read_u32(commands + offset + 4);
        if (record_size < 8 || record_size > size - offset)
            return invalid_batch_record(opcode, offset, "invalid record size");
        const uint8_t *payload = commands + offset + 8;
        const uint32_t payload_size = record_size - 8;
        switch (opcode) {
        case NKGPU_COMMAND_APPLY_PIPELINE:
            if (payload_size != 4)
                return invalid_batch_record(opcode, offset, "bad pipeline payload");
            if (!retain_batch_resource(batch, PipelineKind, read_u32(payload)))
                return invalid_batch_record(opcode, offset, "unusable pipeline handle");
            break;
        case NKGPU_COMMAND_APPLY_VERTEX_BUFFER:
            if (payload_size != 12 || read_u32(payload) >= SG_MAX_VERTEXBUFFER_BINDSLOTS)
                return invalid_batch_record(opcode, offset, "bad vertex-buffer payload");
            if (!retain_batch_resource(batch, BufferKind, read_u32(payload + 4)))
                return invalid_batch_record(opcode, offset, "unusable vertex buffer");
            break;
        case NKGPU_COMMAND_APPLY_INDEX_BUFFER:
            if (payload_size != 8)
                return invalid_batch_record(opcode, offset, "bad index-buffer payload");
            if (!retain_batch_resource(batch, BufferKind, read_u32(payload)))
                return invalid_batch_record(opcode, offset, "unusable index buffer");
            break;
        case NKGPU_COMMAND_APPLY_IMAGE:
            if (payload_size != 8 || read_u32(payload) >= SG_MAX_VIEW_BINDSLOTS)
                return invalid_batch_record(opcode, offset, "bad image payload");
            if (!retain_batch_resource(batch, ImageKind, read_u32(payload + 4)))
                return invalid_batch_record(opcode, offset, "unusable image");
            break;
        case NKGPU_COMMAND_APPLY_SAMPLER:
            if (payload_size != 8 || read_u32(payload) >= SG_MAX_SAMPLER_BINDSLOTS)
                return invalid_batch_record(opcode, offset, "bad sampler payload");
            if (!retain_batch_resource(batch, SamplerKind, read_u32(payload + 4)))
                return invalid_batch_record(opcode, offset, "unusable sampler");
            break;
        case NKGPU_COMMAND_APPLY_UNIFORMS:
            if (payload_size < 8 || payload_size - 8 != read_u32(payload + 4))
                return invalid_batch_record(opcode, offset, "bad uniform payload");
            break;
        case NKGPU_COMMAND_APPLY_SCISSOR:
            if (payload_size != 20)
                return invalid_batch_record(opcode, offset, "bad scissor payload");
            break;
        case NKGPU_COMMAND_APPLY_GRAPHICS_IMAGE:
            if (payload_size != 8)
                return invalid_batch_record(opcode, offset, "bad graphics-image payload");
            if (!retain_batch_graphics_image(batch, read_u32(payload), read_u32(payload + 4)))
                return invalid_batch_record(opcode, offset, "unusable graphics image");
            break;
        case NKGPU_COMMAND_DRAW:
            if (payload_size != 12)
                return invalid_batch_record(opcode, offset, "bad draw payload");
            break;
        default:
            return invalid_batch_record(opcode, offset, "unknown opcode");
        }
        offset += record_size;
    }
    return offset == size;
}

/* Resolves the backend availability a deferred destroy would need, once. */
static bool batch_backend_available(Handle owner) {
    bool available = false;
    if (prepare_resource_destroy(owner, available) != NKGPU_OK)
        return false;
    return available;
}

/* Releases every pin recorded from `from` onward. */
static void release_batch_retention(Batch &batch, size_t from) {
    bool backend_available = false;
    bool availability_known = false;
    while (batch.retained.size() > from) {
        const RetainedResource retained = batch.retained.back();
        batch.retained.pop_back();
        if (!availability_known) {
            backend_available = batch_backend_available(batch.owner);
            availability_known = true;
        }
        unpin_resource(retained.kind, retained.slot, backend_available);
    }
}

/*
 * Retired slots stay hidden from the registry so their owner's handle is
 * invalid, but the shared interpreter resolves handles through it during
 * replay. Re-activating a retired slot for the duration of a submission keeps
 * one implementation of command replay instead of two.
 */
static void set_retained_active(uint32_t kind, uint32_t slot_index, bool active) {
    if (!slot_index)
        return;
    switch (kind) {
    case RenderTargetKind:
        if (slot_index <= render_target_pool.slots.size()) {
            auto &s = render_target_pool.slots[slot_index - 1];
            if (s.retired)
                s.active = active;
        }
        break;
    case SamplerKind:
        if (slot_index <= sampler_pool.slots.size()) {
            auto &s = sampler_pool.slots[slot_index - 1];
            if (s.retired)
                s.active = active;
        }
        break;
    case ImageKind:
        if (slot_index <= image_pool.slots.size()) {
            auto &s = image_pool.slots[slot_index - 1];
            if (s.retired)
                s.active = active;
        }
        break;
    case PipelineKind:
        if (slot_index <= pipeline_pool.slots.size()) {
            auto &s = pipeline_pool.slots[slot_index - 1];
            if (s.retired)
                s.active = active;
        }
        break;
    case BufferKind:
        if (slot_index <= buffer_pool.slots.size()) {
            auto &s = buffer_pool.slots[slot_index - 1];
            if (s.retired)
                s.active = active;
        }
        break;
    default:
        break;
    }
}

nkgpu_result nkgpu_batch_begin(nkgpu_renderer renderer, nkgpu_batch *out_batch) {
    if (!out_batch)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid batch output");
    *out_batch = 0;
    auto *slot = renderer_pool.get(renderer);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (slot->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    Batch batch{};
    batch.owner = renderer;
    const Handle handle = batch_pool.add(batch);
    if (!handle)
        return fail(NKGPU_ERROR_OUT_OF_MEMORY, "batch pool full");
    *out_batch = handle;
    return NKGPU_OK;
}

nkgpu_result nkgpu_batch_append_pass(nkgpu_batch batch, const nkgpu_batch_pass *pass) {
    auto *slot = batch_pool.get(batch);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale batch");
    if (slot->value.sealed)
        return fail(NKGPU_ERROR_WRONG_STATE, "batch is sealed");
    if (!pass || pass->struct_size < sizeof(nkgpu_batch_pass))
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid batch pass");
    if (pass->kind != NKGPU_BATCH_PASS_WINDOW && pass->kind != NKGPU_BATCH_PASS_TARGET)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid batch pass kind");
    if (pass->clear > 1)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid batch pass clear flag");
    BatchPass recorded{};
    recorded.kind = pass->kind;
    recorded.clear = pass->clear;
    if (pass->kind == NKGPU_BATCH_PASS_WINDOW) {
        if (!pass->width || !pass->height || pass->width > INT32_MAX || pass->height > INT32_MAX)
            return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid batch window-pass extent");
        recorded.width = pass->width;
        recorded.height = pass->height;
    } else {
        auto *target = render_target_pool.get(pass->target);
        if (!target || target->value.owner != slot->value.owner)
            return fail(NKGPU_ERROR_INVALID_HANDLE, "invalid or foreign render target");
        const size_t before = slot->value.retained.size();
        if (!retain_batch_resource(slot->value, RenderTargetKind, pass->target)) {
            release_batch_retention(slot->value, before);
            return fail(NKGPU_ERROR_INVALID_HANDLE, "render target cannot be retained");
        }
        recorded.target = pass->target;
    }
    slot->value.passes.push_back(std::move(recorded));
    return NKGPU_OK;
}

nkgpu_result nkgpu_batch_append_command(nkgpu_batch batch, const uint8_t *commands, uint32_t size) {
    auto *slot = batch_pool.get(batch);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale batch");
    if (slot->value.sealed)
        return fail(NKGPU_ERROR_WRONG_STATE, "batch is sealed");
    if (!commands || !size)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "empty command stream");
    if (slot->value.passes.empty())
        return fail(NKGPU_ERROR_WRONG_STATE, "append a pass before its commands");
    const size_t before = slot->value.retained.size();
    const size_t images_before = slot->value.retained_images.size();
    if (!retain_batch_records(slot->value, commands, size)) {
        /* Roll back any pins taken before the invalid record. */
        release_batch_retention(slot->value, before);
        release_batch_images(slot->value, images_before);
        /* Keep the specific reason from the failing record. */
        const std::string detail = nkgpu_last_error();
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid batch command stream: %s",
                    detail.c_str());
    }
    auto &pass = slot->value.passes.back();
    pass.commands.insert(pass.commands.end(), commands, commands + size);
    return NKGPU_OK;
}

nkgpu_result nkgpu_batch_seal(nkgpu_batch batch) {
    auto *slot = batch_pool.get(batch);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale batch");
    if (slot->value.sealed)
        return NKGPU_OK;
    if (slot->value.passes.empty())
        return fail(NKGPU_ERROR_WRONG_STATE, "batch has no passes");
    slot->value.sealed = true;
    return NKGPU_OK;
}

nkgpu_result nkgpu_batch_submit(nkgpu_renderer renderer, nkgpu_batch batch,
                                const nk_surface_frame_target *frame_target) {
    auto *rs = renderer_pool.get(renderer);
    auto *bs = batch_pool.get(batch);
    if (!rs || !bs)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer or batch");
    if (rs->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    /*
     * Submission is render-executor work: acquisition and presentation stay with
     * the platform executor, so a batch never owns a surface.
     */
    if (!nk_executor_is_current(NK_EXECUTOR_RENDER))
        return fail(NKGPU_ERROR_WRONG_THREAD, "batch submission requires the render executor");
    if (bs->value.owner != renderer)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "batch belongs to another renderer");
    if (!bs->value.sealed)
        return fail(NKGPU_ERROR_WRONG_STATE, "batch is not sealed");
    if (bs->value.passes.empty())
        return fail(NKGPU_ERROR_WRONG_STATE, "batch has no passes");
    if (frame_target) {
        const nkgpu_result bound = nkgpu_bind_frame_target(frame_target);
        if (bound != NKGPU_OK)
            return bound;
    }
    /* Reject an impossible submission before any GPU state changes. */
    if (rs->value.state != RendererState::Ready || active_renderer)
        return fail(NKGPU_ERROR_WRONG_STATE, "a renderer frame is already active");
    for (const auto &pass : bs->value.passes)
        if (pass.kind == NKGPU_BATCH_PASS_TARGET && !render_target_pool.get_retained(pass.target))
            return fail(NKGPU_ERROR_INVALID_HANDLE, "batch render target is unavailable");

    for (const auto &retained : bs->value.retained)
        set_retained_active(retained.kind, retained.slot, true);

    nkgpu_result result = frame_target ? begin_frame_with_target(renderer, *frame_target)
                                       : nkgpu_frame_begin(renderer);
    size_t index = 0;
    while (result == NKGPU_OK && index < bs->value.passes.size()) {
        const BatchPass &pass = bs->value.passes[index];
        result =
            pass.kind == NKGPU_BATCH_PASS_WINDOW
                ? nkgpu_begin_window_pass(renderer, pass.width, pass.height, pass.clear)
                : nkgpu_begin_target_pass(renderer, nkgpu_render_target{pass.target}, pass.clear);
        if (result != NKGPU_OK)
            break;
        if (!pass.commands.empty())
            result = nkgpu_submit_commands(renderer, pass.commands.data(),
                                           static_cast<uint32_t>(pass.commands.size()));
        const nkgpu_result ended = nkgpu_end_pass(renderer);
        if (result == NKGPU_OK)
            result = ended;
        ++index;
    }
    if (active_renderer == renderer) {
        /* Presentation stays with the surface owner, so the frame is not presented. */
        const nkgpu_result committed = nkgpu_end_frame_deferred_present(renderer);
        if (result == NKGPU_OK)
            result = committed;
    }

    for (const auto &retained : bs->value.retained)
        set_retained_active(retained.kind, retained.slot, false);
    return result;
}

nkgpu_result nkgpu_bind_frame_target(const nk_surface_frame_target *frame_target) {
    if (!frame_target || frame_target->struct_size < sizeof(nk_surface_frame_target) ||
        frame_target->api == 0 || !frame_target->device.id || frame_target->width <= 0 ||
        frame_target->height <= 0)
        return fail(NKGPU_ERROR_INVALID_ARGUMENT, "invalid acquired frame target");
    if (!nk_executor_is_current(NK_EXECUTOR_RENDER))
        return fail(NKGPU_ERROR_WRONG_THREAD, "frame-target binding requires the render executor");
    /* Explicit APIs carry all state needed by Sokol in the immutable target.
       Their immediate context/command queue is intentionally render-owned, so
       binding is validation rather than a second surface lookup. */
    if (frame_target->api == NK_GRAPHICS_D3D11 || frame_target->api == NK_GRAPHICS_METAL) {
        if (!frame_target->native_device || !frame_target->native_context)
            return fail(NKGPU_ERROR_INVALID_ARGUMENT,
                        "explicit frame target is missing its device binding");
        return NKGPU_OK;
    }
    /* GTK/Web remain aliased during the GL migration. Physical GL/EGL backends
       bind their retained context and drawable without querying the surface. */
    if (nk::core::render_executor_physical()) {
        nk_result bound = NK_OK;
        if (frame_target->frame != NK_INVALID_HANDLE) {
            nk::core::FrameTicket ticket{};
            if (!nk::core::lookup_frame_ticket(frame_target->frame, &ticket))
                return fail(NKGPU_ERROR_INVALID_HANDLE, "render frame ticket is unavailable");
            if (!ticket.backend.bind)
                return fail(NKGPU_ERROR_UNKNOWN, "render frame ticket has no bind operation");
            bound = ticket.backend.bind(ticket);
        } else {
            bound = nk_graphics_bind_frame_target(frame_target);
        }
        if (bound != NK_OK)
            return fail(NKGPU_ERROR_UNKNOWN, "frame-target binding: %s", nk_last_error());
    }
    return NKGPU_OK;
}

nkgpu_result nkgpu_batch_destroy(nkgpu_batch batch) {
    auto *slot = batch_pool.get(batch);
    if (!slot)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale batch");
    release_batch_retention(slot->value, 0);
    release_batch_images(slot->value, 0);
    batch_pool.remove(*slot);
    return NKGPU_OK;
}

static nkgpu_result end_frame(nkgpu_renderer r, bool present_surface) {
    auto *rs = renderer_pool.get(r);
    if (!rs)
        return fail(NKGPU_ERROR_INVALID_HANDLE, "stale renderer");
    if (rs->value.state == RendererState::Lost)
        return fail(NKGPU_ERROR_DEVICE_LOST, "renderer device is lost");
    if (rs->value.state != RendererState::FrameActive || active_renderer != r)
        return fail(NKGPU_ERROR_WRONG_STATE, "no active frame");
    const nkgpu_result activated = activate_renderer(r);
    if (activated != NKGPU_OK)
        return activated;
    if (rs->value.in_pass)
        sg_end_pass();
    sg_commit();
    if (!present_surface && rs->value.frame_target.frame != NK_INVALID_HANDLE) {
        nk_result submitted = NK_OK;
        nk::core::FrameTicket ticket{};
        if (!nk::core::lookup_frame_ticket(rs->value.frame_target.frame, &ticket))
            return fail(NKGPU_ERROR_INVALID_HANDLE, "render frame ticket is unavailable");
        if (!ticket.backend.submit)
            return fail(NKGPU_ERROR_UNKNOWN, "render frame ticket has no submit operation");
        submitted = ticket.backend.submit(ticket);
        if (submitted != NK_OK)
            return fail(NKGPU_ERROR_UNKNOWN, "render submit: %s", nk_last_error());
        nk::core::mark_frame_render_submitted(rs->value.frame_target.frame);
    }
    rs->value.state = RendererState::Ready;
    rs->value.in_pass = false;
    rs->value.active_target = 0;
    rs->value.pass_width = 0;
    rs->value.pass_height = 0;
    rs->value.has_frame_target = false;
    rs->value.frame_target = {};
    active_renderer = 0;
    nk_result present_result = NK_OK;
    if (present_surface) {
#if defined(NKGPU_TESTING)
        present_result =
            fail_next_present ? NK_ERROR_UNKNOWN : nk_surface_present(rs->value.surface);
#else
        present_result = nk_surface_present(rs->value.surface);
#endif
    }
#if defined(NKGPU_TESTING)
    if (present_surface && fail_next_present) {
        fail_next_present = false;
        mark_renderer_lost(r, rs->value);
        return fail(NKGPU_ERROR_DEVICE_LOST, "injected present failure");
    }
#endif
    if (present_result != NK_OK)
        return fail(NKGPU_ERROR_UNKNOWN, "present: %s", nk_last_error());
    ++rs->value.frames;
#if defined(NKGPU_TESTING)
    if (rs->value.test_frames_before_loss != UINT64_MAX) {
        if (rs->value.test_frames_before_loss)
            --rs->value.test_frames_before_loss;
        if (!rs->value.test_frames_before_loss)
            mark_renderer_lost(r, rs->value);
    }
#endif
    return NKGPU_OK;
}

nkgpu_result nkgpu_end_frame(nkgpu_renderer r) {
    return end_frame(r, true);
}

nkgpu_result nkgpu_end_frame_deferred_present(nkgpu_renderer r) {
    return end_frame(r, false);
}
}
