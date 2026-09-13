#include "nativekit_sokol.h"
#include "nativekit_graphics.h"
#include "nativekit_sokol_api.h"
#include "core/graphics_image_registry.h"

#include "sokol_gfx.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
    RenderTargetKind
};
using Handle = uint32_t;

template <class T, Kind K, size_t N> struct Pool {
    struct Slot {
        uint16_t generation = 1;
        bool active = false;
        T value{};
    };
    std::array<Slot, N> slots{};
    Handle add(const T &value) {
        for (uint32_t i = 0; i < N; ++i)
            if (!slots[i].active) {
                slots[i].active = true;
                slots[i].value = value;
                return (uint32_t(K) << 28) | (uint32_t(slots[i].generation) << 16) | (i + 1);
            }
        return 0;
    }
    Slot *get(Handle h) {
        uint32_t encoded = h & 0xFFFF, generation = (h >> 16) & 0xFFF;
        if ((h >> 28) != K || !encoded || encoded > N || !generation)
            return nullptr;
        Slot &s = slots[encoded - 1];
        return s.active && s.generation == generation ? &s : nullptr;
    }
    void remove(Slot &s) {
        s.active = false;
        s.generation = (s.generation % 0xFFF) + 1;
        s.value = T{};
    }
};

struct Renderer {
    nk_handle surface = 0;
    const nk_sokol_api *api = nullptr;
    nk_graphics_api graphics_api = 0;
    nk_graphics_device device{};
    sg_bindings bindings{};
    bool in_frame = false;
    bool in_pass = false;
    Handle active_target = 0;
};
struct Buffer {
    Handle owner = 0;
    sg_buffer object{};
};
struct Shader {
    Handle owner = 0;
    sg_shader object{};
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
    sg_shader_desc desc{};
    std::string vertex_source;
    std::string fragment_source;
    std::array<std::array<std::string, SG_MAX_UNIFORMBLOCK_MEMBERS>, SG_MAX_UNIFORMBLOCK_BINDSLOTS>
        uniform_names;
    std::array<std::string, SG_MAX_TEXTURE_SAMPLER_PAIRS> texture_names;
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

static Pool<Renderer, RendererKind, 8> renderer_pool;
static Pool<Buffer, BufferKind, 256> buffer_pool;
static Pool<Shader, ShaderKind, 256> shader_pool;
static Pool<Pipeline, PipelineKind, 256> pipeline_pool;
static Pool<BufferBuilder, BufferBuilderKind, 16> buffer_builder_pool;
static Pool<PipelineBuilder, PipelineBuilderKind, 16> pipeline_builder_pool;
static Pool<ShaderBuilder, ShaderBuilderKind, 16> shader_builder_pool;
static Pool<UniformBuilder, UniformBuilderKind, 16> uniform_builder_pool;
static Pool<Image, ImageKind, 256> image_pool;
static Pool<ImageBuilder, ImageBuilderKind, 16> image_builder_pool;
static Pool<Sampler, SamplerKind, 256> sampler_pool;
static Pool<RenderTarget, RenderTargetKind, 128> render_target_pool;
static Handle active_renderer = 0;
static Handle selected_renderer = 0;
static const nk_sokol_api *selected_api = nullptr;
static char error_message[256];
static nks_result fail(nks_result code, const char *format, ...);

static const sg_api *runtime_gfx() {
    return selected_api ? selected_api->gfx : nullptr;
}

static nks_result activate_renderer(Handle handle) {
    auto *slot = renderer_pool.get(handle);
    if (!slot)
        return fail(NKS_ERROR_INVALID_HANDLE, "stale renderer");
    if (active_renderer && active_renderer != handle)
        return fail(NKS_ERROR_WRONG_STATE, "another renderer has an active frame");
    if (!slot->value.in_frame && nk_surface_make_current(slot->value.surface) != NK_OK)
        return fail(NKS_ERROR_UNKNOWN, "current: %s", nk_last_error());
    selected_renderer = handle;
    selected_api = slot->value.api;
    return NKS_OK;
}

#define sg_apply_bindings(...) (runtime_gfx()->apply_bindings(__VA_ARGS__))
#define sg_apply_pipeline(...) (runtime_gfx()->apply_pipeline(__VA_ARGS__))
#define sg_apply_uniforms(...) (runtime_gfx()->apply_uniforms(__VA_ARGS__))
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

static const nk_sokol_api *api_for_graphics_api(nk_graphics_api api) {
#if defined(NK_SOKOL_RUNTIME_MATRIX)
    switch (api) {
    case NK_GRAPHICS_OPENGL:
        return nk_sokol_glcore_get_api();
    case NK_GRAPHICS_OPENGL_ES:
        return nk_sokol_gles3_get_api();
    default:
        return nullptr;
    }
#else
    const nk_sokol_api *runtime = nk_sokol_get_api();
    if (!runtime || !runtime->gfx)
        return nullptr;
    #if defined(NK_SOKOL_BACKEND_GLES3)
        #if defined(__EMSCRIPTEN__)
    return (api == NK_GRAPHICS_OPENGL || api == NK_GRAPHICS_OPENGL_ES) ? runtime : nullptr;
        #else
    return api == NK_GRAPHICS_OPENGL_ES ? runtime : nullptr;
        #endif
    #else
    return api == NK_GRAPHICS_OPENGL ? runtime : nullptr;
    #endif
#endif
}

static nks_backend convert_backend(const nk_sokol_api *api) {
    if (!api || !api->gfx)
        return 0;
    switch (api->gfx->query_backend()) {
    case SG_BACKEND_GLCORE:
        return NKS_BACKEND_GLCORE;
    case SG_BACKEND_GLES3:
        return NKS_BACKEND_GLES3;
    default:
        return 0;
    }
}

static int release_graphics_image(const void *runtime, nk_graphics_device device,
                                  uint64_t backend_image) {
    auto *api = static_cast<const nk_sokol_api *>(runtime);
    if (!api || !api->external_image_release || !device.id || !backend_image ||
        nk_surface_make_current(device.id) != NK_OK)
        return 0;
    api->external_image_release(static_cast<uint32_t>(backend_image));
    return 1;
}

static nks_result fail(nks_result code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(error_message, sizeof(error_message), format, args);
    va_end(args);
    return code;
}
static sg_vertex_format convert_format(nks_vertex_format f) {
    switch (f) {
    case 1:
        return SG_VERTEXFORMAT_FLOAT;
    case 2:
        return SG_VERTEXFORMAT_FLOAT2;
    case 3:
        return SG_VERTEXFORMAT_FLOAT3;
    case 4:
        return SG_VERTEXFORMAT_FLOAT4;
    default:
        return SG_VERTEXFORMAT_INVALID;
    }
}
static sg_uniform_type convert_uniform(nks_uniform_type t) {
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
const char *nks_last_error(void) {
    return error_message;
}

nks_backend nks_query_backend(nks_renderer renderer) {
    auto *slot = renderer_pool.get(renderer);
    return slot ? convert_backend(slot->value.api) : 0;
}

nk_graphics_api nks_query_graphics_api(nks_renderer renderer) {
    auto *slot = renderer_pool.get(renderer);
    return slot ? slot->value.graphics_api : static_cast<nk_graphics_api>(0);
}

nks_result nks_surface_create(nk_handle window, int32_t width, int32_t height,
                              nk_handle *out) {
    #if defined(NK_SOKOL_BACKEND_GLES3)
    return nks_surface_create_for_api(window, NK_GRAPHICS_OPENGL_ES, width, height, out);
    #else
    return nks_surface_create_for_api(window, NK_GRAPHICS_OPENGL, width, height, out);
    #endif
}

nks_result nks_surface_create_for_api(nk_handle window, nk_graphics_api api,
                                      int32_t width, int32_t height,
                                      nk_handle *out) {
    if (!window || width <= 0 || height <= 0 || !out)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid surface arguments");
    if (api != NK_GRAPHICS_OPENGL && api != NK_GRAPHICS_OPENGL_ES)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "unsupported graphics API request");
    if (!api_for_graphics_api(api))
        return fail(NKS_ERROR_UNKNOWN, "requested Sokol runtime backend is unavailable");
    nk_surface_options o{};
    o.struct_size = sizeof(o);
    o.flags = api == NK_GRAPHICS_OPENGL ? NK_SURFACE_FORWARD_COMPATIBLE : 0;
    o.api = api;
    o.major_version = 3;
    o.minor_version = api == NK_GRAPHICS_OPENGL ? 3 : 0;
    o.width = width;
    o.height = height;
    return nk_surface_create(window, &o, out) == NK_OK
               ? NKS_OK
               : fail(NKS_ERROR_UNKNOWN, "surface: %s", nk_last_error());
}
nks_result nks_surface_resize(nk_handle s, int32_t w, int32_t h) {
    return nk_surface_set_bounds(s, 0, 0, w, h) == NK_OK
               ? NKS_OK
               : fail(NKS_ERROR_UNKNOWN, "resize: %s", nk_last_error());
}
nks_result nks_surface_destroy(nk_handle s) {
    return nk_surface_destroy(s) == NK_OK
               ? NKS_OK
               : fail(NKS_ERROR_UNKNOWN, "destroy surface: %s", nk_last_error());
}
nks_result nks_renderer_create(nk_handle surface, nks_renderer *out) {
    if (!surface || !out)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid renderer arguments");
    if (active_renderer)
        return fail(NKS_ERROR_WRONG_STATE, "cannot create a renderer during an active frame");
    if (nk_surface_make_current(surface) != NK_OK)
        return fail(NKS_ERROR_UNKNOWN, "current: %s", nk_last_error());
    nk_surface_frame_target target{};
    target.struct_size = sizeof(target);
    if (nk_surface_get_frame_target(surface, &target) != NK_OK || !target.device.id)
        return fail(NKS_ERROR_UNKNOWN, "surface has no graphics-device identity");
    const nk_sokol_api *api = api_for_graphics_api(target.api);
    if (!api || !api->runtime_acquire || !api->runtime_release || !api->gfx)
        return fail(NKS_ERROR_UNKNOWN, "surface graphics backend is unavailable");
    sg_desc desc{};
    desc.environment.defaults = {.color_format = SG_PIXELFORMAT_RGBA8,
                                 .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
                                 .sample_count = 1};
    if (nk_graphics_device_retain(target.device) != NK_OK)
        return fail(NKS_ERROR_UNKNOWN, "graphics device retention failed");
    if (!api->runtime_acquire(&desc, target.device)) {
        nk_graphics_device_release(target.device);
        return fail(NKS_ERROR_UNKNOWN, "Sokol graphics runtime acquisition failed");
    }
    Renderer renderer_state{};
    renderer_state.surface = surface;
    renderer_state.api = api;
    renderer_state.graphics_api = target.api;
    renderer_state.device = target.device;
    Handle h = renderer_pool.add(renderer_state);
    if (!h) {
        api->runtime_release();
        nk_graphics_device_release(target.device);
        return fail(NKS_ERROR_UNKNOWN, "renderer pool full");
    }
    selected_renderer = h;
    selected_api = api;
    *out = h;
    return NKS_OK;
}
static void destroy_render_target(Pool<RenderTarget, RenderTargetKind, 128>::Slot &slot) {
    auto &target = slot.value;
    if (target.depth_attachment.id)
        sg_destroy_view(target.depth_attachment);
    if (target.color_attachment.id)
        sg_destroy_view(target.color_attachment);
    if (target.depth.id)
        sg_destroy_image(target.depth);
    if (target.image.id)
        nk_graphics_image_release(target.image);
    render_target_pool.remove(slot);
}
static void destroy_owned(Handle owner) {
    for (auto &s : render_target_pool.slots)
        if (s.active && s.value.owner == owner)
            destroy_render_target(s);
    for (auto &s : sampler_pool.slots)
        if (s.active && s.value.owner == owner) {
            sg_destroy_sampler(s.value.object);
            sampler_pool.remove(s);
        }
    for (auto &s : image_pool.slots)
        if (s.active && s.value.owner == owner) {
            sg_destroy_view(s.value.view);
            sg_destroy_image(s.value.object);
            image_pool.remove(s);
        }
    for (auto &s : pipeline_pool.slots)
        if (s.active && s.value.owner == owner) {
            sg_destroy_pipeline(s.value.object);
            pipeline_pool.remove(s);
        }
    for (auto &s : shader_pool.slots)
        if (s.active && s.value.owner == owner) {
            sg_destroy_shader(s.value.object);
            shader_pool.remove(s);
        }
    for (auto &s : buffer_pool.slots)
        if (s.active && s.value.owner == owner) {
            sg_destroy_buffer(s.value.object);
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
nks_result nks_renderer_destroy(nks_renderer h) {
    auto *s = renderer_pool.get(h);
    if (!s)
        return fail(NKS_ERROR_INVALID_HANDLE, "stale renderer");
    if (s->value.in_frame)
        return fail(NKS_ERROR_WRONG_STATE, "renderer has active frame");
    const nks_result activated = activate_renderer(h);
    if (activated != NKS_OK)
        return activated;
    const nk_sokol_api *api = s->value.api;
    const nk_graphics_device device = s->value.device;
    destroy_owned(h);
    renderer_pool.remove(*s);
    api->runtime_release();
    nk_graphics_device_release(device);
    if (selected_renderer == h) {
        selected_renderer = 0;
        selected_api = nullptr;
    }
    return NKS_OK;
}
nks_result nks_render_target_create(nks_renderer renderer, uint32_t width, uint32_t height,
                                    uint32_t depth_stencil, nks_render_target *out) {
    auto *owner = renderer_pool.get(renderer);
    if (!owner || !width || !height || !out || depth_stencil > 1 ||
        width > static_cast<uint32_t>(INT32_MAX) || height > static_cast<uint32_t>(INT32_MAX) ||
        owner->value.in_frame)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid render-target arguments");
    const nks_result activated = activate_renderer(renderer);
    if (activated != NKS_OK)
        return activated;

    RenderTarget target{};
    target.owner = renderer;
    target.width = static_cast<int32_t>(width);
    target.height = static_cast<int32_t>(height);
    sg_image_desc color_desc{};
    color_desc.width = static_cast<int>(width);
    color_desc.height = static_cast<int>(height);
    color_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    color_desc.usage.color_attachment = true;
    target.color = sg_make_image(&color_desc);
    if (sg_query_image_state(target.color) != SG_RESOURCESTATE_VALID)
        return fail(NKS_ERROR_UNKNOWN, "render-target color image creation failed");

    sg_view_desc sampled_view_desc{};
    sampled_view_desc.texture.image = target.color;
    const sg_view sampled_view = sg_make_view(&sampled_view_desc);
    if (sg_query_view_state(sampled_view) != SG_RESOURCESTATE_VALID) {
        sg_destroy_image(target.color);
        return fail(NKS_ERROR_UNKNOWN, "render-target sampled view creation failed");
    }

    sg_view_desc color_attachment_desc{};
    color_attachment_desc.color_attachment.image = target.color;
    target.color_attachment = sg_make_view(&color_attachment_desc);
    if (sg_query_view_state(target.color_attachment) != SG_RESOURCESTATE_VALID) {
        sg_destroy_view(sampled_view);
        sg_destroy_image(target.color);
        return fail(NKS_ERROR_UNKNOWN, "render-target color attachment creation failed");
    }

    if (depth_stencil) {
        sg_image_desc depth_desc{};
        depth_desc.width = static_cast<int>(width);
        depth_desc.height = static_cast<int>(height);
        depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        depth_desc.usage.depth_stencil_attachment = true;
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
            return fail(NKS_ERROR_UNKNOWN, "render-target depth attachment creation failed");
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
        return fail(NKS_ERROR_UNKNOWN, "Sokol sampled image registry is full");
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
        return fail(NKS_ERROR_UNKNOWN, "NativeKit graphics image registration failed");
    }

    const Handle handle = render_target_pool.add(target);
    if (!handle) {
        if (target.depth_attachment.id)
            sg_destroy_view(target.depth_attachment);
        if (target.depth.id)
            sg_destroy_image(target.depth);
        sg_destroy_view(target.color_attachment);
        nk_graphics_image_release(target.image);
        return fail(NKS_ERROR_UNKNOWN, "render-target pool full");
    }
    *out = handle;
    return NKS_OK;
}
nks_result nks_render_target_get_image(nks_renderer renderer, nks_render_target handle,
                                       nk_graphics_image *out) {
    auto *owner = renderer_pool.get(renderer);
    auto *target = render_target_pool.get(handle);
    if (!owner || !target || target->value.owner != renderer || !out)
        return fail(NKS_ERROR_INVALID_HANDLE, "invalid render-target image request");
    *out = target->value.image;
    return NKS_OK;
}
nks_result nks_render_target_destroy(nks_renderer renderer, nks_render_target handle) {
    auto *owner = renderer_pool.get(renderer);
    auto *target = render_target_pool.get(handle);
    if (!owner || !target || target->value.owner != renderer)
        return fail(NKS_ERROR_INVALID_HANDLE, "invalid render-target handle/state");
    if (owner->value.in_frame || owner->value.active_target == handle)
        return fail(NKS_ERROR_WRONG_STATE, "cannot destroy a render target during an active pass");
    const nks_result activated = activate_renderer(renderer);
    if (activated != NKS_OK)
        return activated;
    destroy_render_target(*target);
    return NKS_OK;
}
nks_result nks_begin_render_target(nks_renderer renderer, nks_render_target handle,
                                   uint32_t clear) {
    auto *owner = renderer_pool.get(renderer);
    auto *target = render_target_pool.get(handle);
    if (!owner || !target || target->value.owner != renderer || clear > 1 || owner->value.in_frame ||
        active_renderer)
        return fail(NKS_ERROR_WRONG_STATE, "invalid render-target frame state");
    const nks_result activated = activate_renderer(renderer);
    if (activated != NKS_OK)
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
    owner->value.in_frame = true;
    owner->value.in_pass = true;
    owner->value.active_target = handle;
    owner->value.bindings = {};
    active_renderer = renderer;
    return NKS_OK;
}
nks_result nks_end_render_target(nks_renderer renderer) {
    auto *owner = renderer_pool.get(renderer);
    if (!owner || !owner->value.in_frame || !owner->value.in_pass ||
        !owner->value.active_target || active_renderer != renderer)
        return fail(NKS_ERROR_WRONG_STATE, "no active render-target pass");
    const nks_result activated = activate_renderer(renderer);
    if (activated != NKS_OK)
        return activated;
    sg_end_pass();
    sg_commit();
    owner->value.in_frame = false;
    owner->value.in_pass = false;
    owner->value.active_target = 0;
    active_renderer = 0;
    return NKS_OK;
}
static nks_result save_buffer(Handle owner, sg_buffer object, nks_buffer *out) {
    Handle h = buffer_pool.add(Buffer{owner, object});
    if (!h) {
        sg_destroy_buffer(object);
        return fail(NKS_ERROR_UNKNOWN, "buffer pool full");
    }
    *out = h;
    return NKS_OK;
}
nks_result nks_buffer_create(nks_renderer r, const uint8_t *data, uint32_t size, nks_buffer *out) {
    if (!renderer_pool.get(r) || !data || !size || !out)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid buffer arguments");
    const nks_result activated = activate_renderer(r);
    if (activated != NKS_OK)
        return activated;
    sg_buffer_desc desc{};
    desc.data = {data, size};
    sg_buffer b = sg_make_buffer(&desc);
    return sg_query_buffer_state(b) == SG_RESOURCESTATE_VALID
               ? save_buffer(r, b, out)
               : fail(NKS_ERROR_UNKNOWN, "buffer creation failed");
}
nks_result nks_buffer_begin(nks_renderer r, uint32_t size, nks_buffer_builder *out) {
    return nks_buffer_begin_kind(r, size, NKS_BUFFER_VERTEX, out);
}
nks_result nks_buffer_begin_kind(nks_renderer r, uint32_t size, nks_buffer_usage usage,
                                 nks_buffer_builder *out) {
    if (!renderer_pool.get(r) || !size || !out)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid buffer builder");
    if (usage != NKS_BUFFER_VERTEX && usage != NKS_BUFFER_INDEX)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid buffer usage");
    uint8_t *data = (uint8_t *)calloc(1, size);
    if (!data)
        return fail(NKS_ERROR_UNKNOWN, "allocation failed");
    Handle h = buffer_builder_pool.add(BufferBuilder{r, data, size, usage == NKS_BUFFER_INDEX});
    if (!h) {
        free(data);
        return fail(NKS_ERROR_UNKNOWN, "builder pool full");
    }
    *out = h;
    return NKS_OK;
}
nks_result nks_buffer_write_u16(nks_buffer_builder h, uint32_t offset, uint32_t value) {
    auto *s = buffer_builder_pool.get(h);
    if (!s || value > UINT16_MAX || offset > s->value.size || s->value.size - offset < 2)
        return fail(NKS_ERROR_INVALID_HANDLE, "stale/out-of-range buffer builder");
    uint16_t v = (uint16_t)value;
    memcpy(s->value.data + offset, &v, 2);
    return NKS_OK;
}
nks_result nks_buffer_write_f32(nks_buffer_builder h, uint32_t offset, float value) {
    auto *s = buffer_builder_pool.get(h);
    if (!s || offset > s->value.size || s->value.size - offset < sizeof(value))
        return fail(NKS_ERROR_INVALID_HANDLE, "stale/out-of-range buffer builder");
    memcpy(s->value.data + offset, &value, sizeof(value));
    return NKS_OK;
}
nks_result nks_buffer_end(nks_buffer_builder h, nks_buffer *out) {
    auto *s = buffer_builder_pool.get(h);
    if (!s || !out)
        return fail(NKS_ERROR_INVALID_HANDLE, "stale buffer builder");
    auto owner = s->value.owner;
    const nks_result activated = activate_renderer(owner);
    if (activated != NKS_OK)
        return activated;
    sg_buffer_desc desc{};
    desc.data = {s->value.data, s->value.size};
    desc.usage.index_buffer = s->value.index;
    desc.usage.vertex_buffer = !s->value.index;
    sg_buffer b = sg_make_buffer(&desc);
    free(s->value.data);
    buffer_builder_pool.remove(*s);
    return sg_query_buffer_state(b) == SG_RESOURCESTATE_VALID
               ? save_buffer(owner, b, out)
               : fail(NKS_ERROR_UNKNOWN, "buffer creation failed");
}
nks_result nks_buffer_destroy(nks_renderer r, nks_buffer h) {
    auto *s = buffer_pool.get(h);
    if (!renderer_pool.get(r) || !s || s->value.owner != r)
        return fail(NKS_ERROR_INVALID_HANDLE, "stale/foreign buffer");
    const nks_result activated = activate_renderer(r);
    if (activated != NKS_OK)
        return activated;
    sg_destroy_buffer(s->value.object);
    buffer_pool.remove(*s);
    return NKS_OK;
}
nks_result nks_shader_create(nks_renderer r, const char *vs, const char *fs, nks_shader *out) {
    if (!renderer_pool.get(r) || !vs || !fs || !out)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid shader arguments");
    const nks_result activated = activate_renderer(r);
    if (activated != NKS_OK)
        return activated;
    sg_shader_desc desc{};
    desc.vertex_func.source = vs;
    desc.fragment_func.source = fs;
    sg_shader object = sg_make_shader(&desc);
    if (sg_query_shader_state(object) != SG_RESOURCESTATE_VALID)
        return fail(NKS_ERROR_UNKNOWN, "shader creation failed");
    Handle h = shader_pool.add(Shader{r, object});
    if (!h) {
        sg_destroy_shader(object);
        return fail(NKS_ERROR_UNKNOWN, "shader pool full");
    }
    *out = h;
    return NKS_OK;
}
nks_result nks_shader_destroy(nks_renderer r, nks_shader h) {
    auto *s = shader_pool.get(h);
    if (!renderer_pool.get(r) || !s || s->value.owner != r)
        return fail(NKS_ERROR_INVALID_HANDLE, "stale/foreign shader");
    const nks_result activated = activate_renderer(r);
    if (activated != NKS_OK)
        return activated;
    sg_destroy_shader(s->value.object);
    shader_pool.remove(*s);
    return NKS_OK;
}
nks_result nks_shader_begin(nks_renderer r, const char *vs, const char *fs,
                            nks_shader_builder *out) {
    if (!renderer_pool.get(r) || !vs || !fs || !out)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid shader builder");
    Handle h = shader_builder_pool.add(ShaderBuilder{});
    if (!h)
        return fail(NKS_ERROR_UNKNOWN, "shader builder pool full");
    auto *s = shader_builder_pool.get(h);
    s->value.owner = r;
    s->value.vertex_source = vs;
    s->value.fragment_source = fs;
    s->value.desc.vertex_func.source = s->value.vertex_source.c_str();
    s->value.desc.fragment_func.source = s->value.fragment_source.c_str();
    *out = h;
    return NKS_OK;
}
nks_result nks_shader_uniform_block(nks_shader_builder h, uint32_t slot, nks_shader_stage stage,
                                    uint32_t size) {
    auto *s = shader_builder_pool.get(h);
    if (!s || slot >= SG_MAX_UNIFORMBLOCK_BINDSLOTS || !size ||
        (stage != NKS_SHADERSTAGE_VERTEX && stage != NKS_SHADERSTAGE_FRAGMENT))
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid uniform block");
    auto &b = s->value.desc.uniform_blocks[slot];
    b.stage = stage == NKS_SHADERSTAGE_VERTEX ? SG_SHADERSTAGE_VERTEX : SG_SHADERSTAGE_FRAGMENT;
    b.size = size;
    return NKS_OK;
}
nks_result nks_shader_uniform(nks_shader_builder h, uint32_t block, uint32_t member,
                              const char *name, nks_uniform_type type, uint32_t count) {
    auto *s = shader_builder_pool.get(h);
    auto converted = convert_uniform(type);
    if (!s || block >= SG_MAX_UNIFORMBLOCK_BINDSLOTS || member >= SG_MAX_UNIFORMBLOCK_MEMBERS ||
        !name || converted == SG_UNIFORMTYPE_INVALID)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid shader uniform");
    s->value.uniform_names[block][member] = name;
    auto &u = s->value.desc.uniform_blocks[block].glsl_uniforms[member];
    u.glsl_name = s->value.uniform_names[block][member].c_str();
    u.type = converted;
    u.array_count = count ? count : 1;
    return NKS_OK;
}
nks_result nks_shader_texture(nks_shader_builder h, uint32_t view_slot, uint32_t sampler_slot,
                              nks_shader_stage stage, const char *name) {
    auto *s = shader_builder_pool.get(h);
    if (!s || view_slot >= SG_MAX_VIEW_BINDSLOTS || sampler_slot >= SG_MAX_SAMPLER_BINDSLOTS ||
        view_slot >= SG_MAX_TEXTURE_SAMPLER_PAIRS || !name ||
        (stage != NKS_SHADERSTAGE_VERTEX && stage != NKS_SHADERSTAGE_FRAGMENT))
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid shader texture binding");
    const sg_shader_stage converted =
        stage == NKS_SHADERSTAGE_VERTEX ? SG_SHADERSTAGE_VERTEX : SG_SHADERSTAGE_FRAGMENT;
    s->value.desc.views[view_slot].texture.stage = converted;
    s->value.desc.views[view_slot].texture.image_type = SG_IMAGETYPE_2D;
    s->value.desc.views[view_slot].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    s->value.desc.samplers[sampler_slot].stage = converted;
    s->value.desc.samplers[sampler_slot].sampler_type = SG_SAMPLERTYPE_FILTERING;
    s->value.texture_names[view_slot] = name;
    auto &pair = s->value.desc.texture_sampler_pairs[view_slot];
    pair.stage = converted;
    pair.view_slot = (uint8_t)view_slot;
    pair.sampler_slot = (uint8_t)sampler_slot;
    pair.glsl_name = s->value.texture_names[view_slot].c_str();
    return NKS_OK;
}
nks_result nks_shader_end(nks_shader_builder h, nks_shader *out) {
    auto *s = shader_builder_pool.get(h);
    if (!s || !out)
        return fail(NKS_ERROR_INVALID_HANDLE, "stale shader builder");
    Handle owner = s->value.owner;
    const nks_result activated = activate_renderer(owner);
    if (activated != NKS_OK)
        return activated;
    sg_shader object = sg_make_shader(&s->value.desc);
    shader_builder_pool.remove(*s);
    if (sg_query_shader_state(object) != SG_RESOURCESTATE_VALID)
        return fail(NKS_ERROR_UNKNOWN, "shader creation failed");
    Handle result = shader_pool.add(Shader{owner, object});
    if (!result) {
        sg_destroy_shader(object);
        return fail(NKS_ERROR_UNKNOWN, "shader pool full");
    }
    *out = result;
    return NKS_OK;
}
nks_result nks_pipeline_begin(nks_renderer r, nks_shader shader, uint32_t stride,
                              nks_pipeline_builder *out) {
    auto *sh = shader_pool.get(shader);
    if (!renderer_pool.get(r) || !sh || sh->value.owner != r || !stride || !out)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid pipeline builder");
    PipelineBuilder b{};
    b.owner = r;
    b.desc.shader = sh->value.object;
    b.desc.layout.buffers[0].stride = (int)stride;
    b.desc.depth.pixel_format = SG_PIXELFORMAT_NONE;
    Handle h = pipeline_builder_pool.add(b);
    if (!h)
        return fail(NKS_ERROR_UNKNOWN, "builder pool full");
    *out = h;
    return NKS_OK;
}
nks_result nks_pipeline_attribute(nks_pipeline_builder h, uint32_t location, uint32_t buffer_index,
                                  uint32_t offset, nks_vertex_format format) {
    auto *s = pipeline_builder_pool.get(h);
    auto f = convert_format(format);
    if (!s || location >= SG_MAX_VERTEX_ATTRIBUTES ||
        buffer_index >= SG_MAX_VERTEXBUFFER_BINDSLOTS || f == SG_VERTEXFORMAT_INVALID)
        return fail(NKS_ERROR_INVALID_HANDLE, "invalid pipeline attribute");
    auto &a = s->value.desc.layout.attrs[location];
    a.buffer_index = (int)buffer_index;
    a.offset = (int)offset;
    a.format = f;
    return NKS_OK;
}
nks_result nks_pipeline_index_type(nks_pipeline_builder h, nks_index_type type) {
    auto *s = pipeline_builder_pool.get(h);
    if (!s || type > NKS_INDEXTYPE_UINT32)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid index type");
    s->value.desc.index_type = type == NKS_INDEXTYPE_UINT16   ? SG_INDEXTYPE_UINT16
                               : type == NKS_INDEXTYPE_UINT32 ? SG_INDEXTYPE_UINT32
                                                              : SG_INDEXTYPE_NONE;
    return NKS_OK;
}
nks_result nks_pipeline_depth_stencil(nks_pipeline_builder h, uint32_t enabled) {
    auto *s = pipeline_builder_pool.get(h);
    if (!s || enabled > 1)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid pipeline depth/stencil state");
    s->value.desc.depth.pixel_format = enabled ? SG_PIXELFORMAT_DEPTH_STENCIL : SG_PIXELFORMAT_NONE;
    s->value.desc.depth.write_enabled = enabled != 0;
    s->value.desc.depth.compare = enabled ? SG_COMPAREFUNC_LESS_EQUAL : SG_COMPAREFUNC_ALWAYS;
    return NKS_OK;
}
nks_result nks_pipeline_end(nks_pipeline_builder h, nks_pipeline *out) {
    auto *s = pipeline_builder_pool.get(h);
    if (!s || !out)
        return fail(NKS_ERROR_INVALID_HANDLE, "stale pipeline builder");
    auto owner = s->value.owner;
    const nks_result activated = activate_renderer(owner);
    if (activated != NKS_OK)
        return activated;
    sg_pipeline object = sg_make_pipeline(&s->value.desc);
    pipeline_builder_pool.remove(*s);
    if (sg_query_pipeline_state(object) != SG_RESOURCESTATE_VALID)
        return fail(NKS_ERROR_UNKNOWN, "pipeline creation failed");
    Handle result = pipeline_pool.add(Pipeline{owner, object});
    if (!result) {
        sg_destroy_pipeline(object);
        return fail(NKS_ERROR_UNKNOWN, "pipeline pool full");
    }
    *out = result;
    return NKS_OK;
}
nks_result nks_pipeline_destroy(nks_renderer r, nks_pipeline h) {
    auto *s = pipeline_pool.get(h);
    if (!renderer_pool.get(r) || !s || s->value.owner != r)
        return fail(NKS_ERROR_INVALID_HANDLE, "stale/foreign pipeline");
    const nks_result activated = activate_renderer(r);
    if (activated != NKS_OK)
        return activated;
    sg_destroy_pipeline(s->value.object);
    pipeline_pool.remove(*s);
    return NKS_OK;
}
nks_result nks_begin_frame(nks_renderer h) {
    auto *s = renderer_pool.get(h);
    if (!s || active_renderer)
        return fail(NKS_ERROR_WRONG_STATE, "invalid renderer/frame active");
    const nks_result activated = activate_renderer(h);
    if (activated != NKS_OK)
        return activated;
    sg_reset_state_cache();
    nk_surface_frame_target target{};
    target.struct_size = sizeof(target);
    if (nk_surface_get_frame_target(s->value.surface, &target) != NK_OK || target.width <= 0 ||
        target.height <= 0 ||
        target.api != s->value.graphics_api || target.device.id != s->value.device.id)
        return fail(NKS_ERROR_UNKNOWN, "framebuffer target does not match renderer device");
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {.035f, .055f, .11f, 1};
    pass.swapchain = {.width = target.width,
                      .height = target.height,
                      .sample_count = 1,
                      .color_format = SG_PIXELFORMAT_RGBA8,
                      .depth_format = SG_PIXELFORMAT_NONE,
                      .gl = {.framebuffer = static_cast<uint32_t>(target.native_target)}};
    sg_begin_pass(&pass);
    s->value.in_frame = true;
    s->value.in_pass = true;
    s->value.active_target = 0;
    active_renderer = h;
    s->value.bindings = {};
    return NKS_OK;
}
nks_result nks_apply_pipeline(nks_renderer r, nks_pipeline h) {
    auto *rs = renderer_pool.get(r);
    auto *p = pipeline_pool.get(h);
    if (!rs || !rs->value.in_frame || !rs->value.in_pass || active_renderer != r || !p ||
        p->value.owner != r)
        return fail(NKS_ERROR_INVALID_HANDLE, "invalid pipeline/frame");
    sg_apply_pipeline(p->value.object);
    return NKS_OK;
}
nks_result nks_apply_vertex_buffer(nks_renderer r, uint32_t slot, nks_buffer h, uint32_t offset) {
    auto *rs = renderer_pool.get(r);
    auto *b = buffer_pool.get(h);
    if (!rs || !rs->value.in_frame || !rs->value.in_pass || active_renderer != r || !b ||
        b->value.owner != r ||
        slot >= SG_MAX_VERTEXBUFFER_BINDSLOTS)
        return fail(NKS_ERROR_INVALID_HANDLE, "invalid buffer/frame");
    rs->value.bindings.vertex_buffers[slot] = b->value.object;
    rs->value.bindings.vertex_buffer_offsets[slot] = (int)offset;
    return NKS_OK;
}
nks_result nks_apply_index_buffer(nks_renderer r, nks_buffer h, uint32_t offset) {
    auto *rs = renderer_pool.get(r);
    auto *b = buffer_pool.get(h);
    if (!rs || !rs->value.in_frame || !rs->value.in_pass || active_renderer != r || !b ||
        b->value.owner != r)
        return fail(NKS_ERROR_INVALID_HANDLE, "invalid index buffer/frame");
    rs->value.bindings.index_buffer = b->value.object;
    rs->value.bindings.index_buffer_offset = (int)offset;
    return NKS_OK;
}
nks_result nks_uniforms_begin(nks_renderer r, uint32_t size, nks_uniform_builder *out) {
    if (!renderer_pool.get(r) || !size || !out)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid uniform builder");
    uint8_t *data = (uint8_t *)calloc(1, size);
    if (!data)
        return fail(NKS_ERROR_UNKNOWN, "allocation failed");
    Handle h = uniform_builder_pool.add(UniformBuilder{r, data, size});
    if (!h) {
        free(data);
        return fail(NKS_ERROR_UNKNOWN, "uniform builder pool full");
    }
    *out = h;
    return NKS_OK;
}
nks_result nks_uniforms_write_f32(nks_uniform_builder h, uint32_t offset, float value) {
    auto *s = uniform_builder_pool.get(h);
    if (!s || offset > s->value.size || s->value.size - offset < sizeof(value))
        return fail(NKS_ERROR_INVALID_HANDLE, "stale/out-of-range uniform builder");
    memcpy(s->value.data + offset, &value, sizeof(value));
    return NKS_OK;
}
nks_result nks_apply_uniforms(nks_renderer r, uint32_t slot, nks_uniform_builder h) {
    auto *rs = renderer_pool.get(r);
    auto *u = uniform_builder_pool.get(h);
    if (!rs || !rs->value.in_frame || !rs->value.in_pass || active_renderer != r || !u ||
        u->value.owner != r)
        return fail(NKS_ERROR_INVALID_HANDLE, "invalid uniforms/frame");
    sg_range range{u->value.data, u->value.size};
    sg_apply_uniforms((int)slot, &range);
    free(u->value.data);
    uniform_builder_pool.remove(*u);
    return NKS_OK;
}
nks_result nks_image_begin(nks_renderer r, uint32_t width, uint32_t height,
                           nks_image_builder *out) {
    if (!renderer_pool.get(r) || !width || !height || !out || width > UINT32_MAX / height / 4)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid image builder");
    uint8_t *data = (uint8_t *)calloc((size_t)width * height, 4);
    if (!data)
        return fail(NKS_ERROR_UNKNOWN, "image allocation failed");
    Handle h = image_builder_pool.add(ImageBuilder{r, data, width, height});
    if (!h) {
        free(data);
        return fail(NKS_ERROR_UNKNOWN, "image builder pool full");
    }
    *out = h;
    return NKS_OK;
}
nks_result nks_image_write_rgba8(nks_image_builder h, uint32_t x, uint32_t y, uint32_t red,
                                 uint32_t green, uint32_t blue, uint32_t alpha) {
    auto *s = image_builder_pool.get(h);
    if (!s || x >= s->value.width || y >= s->value.height || red > 255 || green > 255 ||
        blue > 255 || alpha > 255)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid image pixel write");
    uint8_t *pixel = s->value.data + ((size_t)y * s->value.width + x) * 4;
    pixel[0] = (uint8_t)red;
    pixel[1] = (uint8_t)green;
    pixel[2] = (uint8_t)blue;
    pixel[3] = (uint8_t)alpha;
    return NKS_OK;
}
nks_result nks_image_end(nks_image_builder h, nks_image *out) {
    auto *s = image_builder_pool.get(h);
    if (!s || !out)
        return fail(NKS_ERROR_INVALID_HANDLE, "stale image builder");
    const Handle owner = s->value.owner;
    const nks_result activated = activate_renderer(owner);
    if (activated != NKS_OK)
        return activated;
    sg_image_desc desc{};
    desc.width = (int)s->value.width;
    desc.height = (int)s->value.height;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0] = {s->value.data, (size_t)s->value.width * s->value.height * 4};
    const sg_image image = sg_make_image(&desc);
    free(s->value.data);
    image_builder_pool.remove(*s);
    if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID)
        return fail(NKS_ERROR_UNKNOWN, "image creation failed");
    sg_view_desc view_desc{};
    view_desc.texture.image = image;
    const sg_view view = sg_make_view(&view_desc);
    if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID) {
        sg_destroy_image(image);
        return fail(NKS_ERROR_UNKNOWN, "texture view creation failed");
    }
    Handle result = image_pool.add(Image{owner, image, view});
    if (!result) {
        sg_destroy_view(view);
        sg_destroy_image(image);
        return fail(NKS_ERROR_UNKNOWN, "image pool full");
    }
    *out = result;
    return NKS_OK;
}
nks_result nks_image_destroy(nks_renderer r, nks_image h) {
    auto *s = image_pool.get(h);
    if (!renderer_pool.get(r) || !s || s->value.owner != r)
        return fail(NKS_ERROR_INVALID_HANDLE, "stale/foreign image");
    const nks_result activated = activate_renderer(r);
    if (activated != NKS_OK)
        return activated;
    sg_destroy_view(s->value.view);
    sg_destroy_image(s->value.object);
    image_pool.remove(*s);
    return NKS_OK;
}
nks_result nks_sampler_create(nks_renderer r, nks_filter min_filter, nks_filter mag_filter,
                              nks_wrap wrap_u, nks_wrap wrap_v, nks_sampler *out) {
    if (!renderer_pool.get(r) || !out || min_filter < 1 || min_filter > 2 || mag_filter < 1 ||
        mag_filter > 2 || wrap_u < 1 || wrap_u > 2 || wrap_v < 1 || wrap_v > 2)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid sampler arguments");
    const nks_result activated = activate_renderer(r);
    if (activated != NKS_OK)
        return activated;
    sg_sampler_desc desc{};
    desc.min_filter = min_filter == NKS_FILTER_LINEAR ? SG_FILTER_LINEAR : SG_FILTER_NEAREST;
    desc.mag_filter = mag_filter == NKS_FILTER_LINEAR ? SG_FILTER_LINEAR : SG_FILTER_NEAREST;
    desc.wrap_u = wrap_u == NKS_WRAP_CLAMP_TO_EDGE ? SG_WRAP_CLAMP_TO_EDGE : SG_WRAP_REPEAT;
    desc.wrap_v = wrap_v == NKS_WRAP_CLAMP_TO_EDGE ? SG_WRAP_CLAMP_TO_EDGE : SG_WRAP_REPEAT;
    const sg_sampler sampler = sg_make_sampler(&desc);
    if (sg_query_sampler_state(sampler) != SG_RESOURCESTATE_VALID)
        return fail(NKS_ERROR_UNKNOWN, "sampler creation failed");
    Handle result = sampler_pool.add(Sampler{r, sampler});
    if (!result) {
        sg_destroy_sampler(sampler);
        return fail(NKS_ERROR_UNKNOWN, "sampler pool full");
    }
    *out = result;
    return NKS_OK;
}
nks_result nks_sampler_destroy(nks_renderer r, nks_sampler h) {
    auto *s = sampler_pool.get(h);
    if (!renderer_pool.get(r) || !s || s->value.owner != r)
        return fail(NKS_ERROR_INVALID_HANDLE, "stale/foreign sampler");
    const nks_result activated = activate_renderer(r);
    if (activated != NKS_OK)
        return activated;
    sg_destroy_sampler(s->value.object);
    sampler_pool.remove(*s);
    return NKS_OK;
}
nks_result nks_apply_image(nks_renderer r, uint32_t slot, nks_image h) {
    auto *rs = renderer_pool.get(r);
    auto *image = image_pool.get(h);
    if (!rs || !rs->value.in_frame || !rs->value.in_pass || active_renderer != r || !image ||
        image->value.owner != r ||
        slot >= SG_MAX_VIEW_BINDSLOTS)
        return fail(NKS_ERROR_INVALID_HANDLE, "invalid image/frame");
    rs->value.bindings.views[slot] = image->value.view;
    return NKS_OK;
}
nks_result nks_apply_sampler(nks_renderer r, uint32_t slot, nks_sampler h) {
    auto *rs = renderer_pool.get(r);
    auto *sampler = sampler_pool.get(h);
    if (!rs || !rs->value.in_frame || !rs->value.in_pass || active_renderer != r || !sampler ||
        sampler->value.owner != r || slot >= SG_MAX_SAMPLER_BINDSLOTS)
        return fail(NKS_ERROR_INVALID_HANDLE, "invalid sampler/frame");
    rs->value.bindings.samplers[slot] = sampler->value.object;
    return NKS_OK;
}
nks_result nks_draw(nks_renderer r, uint32_t base, uint32_t count, uint32_t instances) {
    auto *rs = renderer_pool.get(r);
    if (!rs || !rs->value.in_frame || !rs->value.in_pass || active_renderer != r || !count ||
        !instances)
        return fail(NKS_ERROR_WRONG_STATE, "invalid draw/frame");
    sg_apply_bindings(&rs->value.bindings);
    sg_draw((int)base, (int)count, (int)instances);
    return NKS_OK;
}
static uint32_t read_u32(const uint8_t *data) {
    return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) |
           (uint32_t(data[3]) << 24);
}
static nks_result submit_command(nks_renderer r, uint32_t opcode, const uint8_t *payload,
                                 uint32_t size) {
    switch (opcode) {
    case NKS_COMMAND_APPLY_PIPELINE:
        return size == 4 ? nks_apply_pipeline(r, nks_pipeline{read_u32(payload)})
                         : NKS_ERROR_INVALID_ARGUMENT;
    case NKS_COMMAND_APPLY_VERTEX_BUFFER:
        return size == 12 ? nks_apply_vertex_buffer(r, read_u32(payload),
                                                    nks_buffer{read_u32(payload + 4)},
                                                    read_u32(payload + 8))
                          : NKS_ERROR_INVALID_ARGUMENT;
    case NKS_COMMAND_APPLY_INDEX_BUFFER:
        return size == 8
                   ? nks_apply_index_buffer(r, nks_buffer{read_u32(payload)}, read_u32(payload + 4))
                   : NKS_ERROR_INVALID_ARGUMENT;
    case NKS_COMMAND_APPLY_IMAGE:
        return size == 8 ? nks_apply_image(r, read_u32(payload), nks_image{read_u32(payload + 4)})
                         : NKS_ERROR_INVALID_ARGUMENT;
    case NKS_COMMAND_APPLY_SAMPLER:
        return size == 8
                   ? nks_apply_sampler(r, read_u32(payload), nks_sampler{read_u32(payload + 4)})
                   : NKS_ERROR_INVALID_ARGUMENT;
    case NKS_COMMAND_APPLY_UNIFORMS: {
        if (size < 8 || size - 8 != read_u32(payload + 4))
            return NKS_ERROR_INVALID_ARGUMENT;
        auto *renderer = renderer_pool.get(r);
        if (!renderer || !renderer->value.in_frame || !renderer->value.in_pass ||
            active_renderer != r)
            return NKS_ERROR_WRONG_STATE;
        sg_range range{payload + 8, size - 8};
        sg_apply_uniforms(read_u32(payload), &range);
        return NKS_OK;
    }
    case NKS_COMMAND_DRAW:
        return size == 12
                   ? nks_draw(r, read_u32(payload), read_u32(payload + 4), read_u32(payload + 8))
                   : NKS_ERROR_INVALID_ARGUMENT;
    default:
        return NKS_ERROR_INVALID_ARGUMENT;
    }
}
nks_result nks_submit_commands(nks_renderer r, const uint8_t *commands, uint32_t size) {
    auto *renderer = renderer_pool.get(r);
    if (!renderer || !renderer->value.in_frame || !renderer->value.in_pass || active_renderer != r)
        return fail(NKS_ERROR_WRONG_STATE, "no active frame for command submission");
    if (!commands || !size)
        return fail(NKS_ERROR_INVALID_ARGUMENT, "empty command stream");
    uint32_t offset = 0;
    while (offset < size) {
        if (size - offset < 8)
            return fail(NKS_ERROR_INVALID_ARGUMENT, "truncated command header at %u", offset);
        const uint32_t opcode = read_u32(commands + offset);
        const uint32_t record_size = read_u32(commands + offset + 4);
        if (record_size < 8 || record_size > size - offset)
            return fail(NKS_ERROR_INVALID_ARGUMENT, "invalid command size at %u", offset);
        const nks_result result = submit_command(r, opcode, commands + offset + 8, record_size - 8);
        if (result != NKS_OK)
            return fail(result, "invalid command %u at %u", opcode, offset);
        offset += record_size;
    }
    return NKS_OK;
}
nks_result nks_end_frame(nks_renderer r) {
    auto *rs = renderer_pool.get(r);
    if (!rs || !rs->value.in_frame || !rs->value.in_pass || active_renderer != r ||
        rs->value.active_target)
        return fail(NKS_ERROR_WRONG_STATE, "no active frame");
    sg_end_pass();
    sg_commit();
    rs->value.in_frame = false;
    rs->value.in_pass = false;
    active_renderer = 0;
    return nk_surface_present(rs->value.surface) == NK_OK
               ? NKS_OK
               : fail(NKS_ERROR_UNKNOWN, "present: %s", nk_last_error());
}
}
