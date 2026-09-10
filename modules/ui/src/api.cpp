#include "nativekit_ui.h"

#include "nativekit_graphics.h"

#include "display_list/display_list.h"
#include "compositor/compositor.h"
#include "prepare/nanovg_recorder.h"
#include "prepare/skribidi_adapter.h"
#include "render/frame_resources.h"
#include "render/render_plan_executor.h"
#include "render/sokol_backend.h"

#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <GL/gl.h>

static_assert(sizeof(nkui_command_header) == sizeof(nkui::CommandHeader));
static_assert(sizeof(nkui_text_metrics) == 5 * sizeof(uint32_t));
static_assert(sizeof(nkui_text_position) == 2 * sizeof(uint32_t));
static_assert(sizeof(nkui_text_caret) == 7 * sizeof(uint32_t));
static_assert(sizeof(nkui_path_element) == 7 * sizeof(uint32_t));
static_assert(sizeof(nkui_color) == 4 * sizeof(uint32_t));
static_assert(sizeof(nkui_transform_command) == sizeof(nkui::SetTransformCommand));
static_assert(sizeof(nkui_resource_command) == sizeof(nkui::DrawResourceCommand));
static_assert(sizeof(nkui_scalar_command) == sizeof(nkui::SetGlobalAlphaCommand));
static_assert(sizeof(nkui_composite_command) == sizeof(nkui::SetCompositeModeCommand));
static_assert(sizeof(nkui_rect_command) == sizeof(nkui::ClipRectCommand));
static_assert(sizeof(nkui_draw_rect_command) == sizeof(nkui::DrawRectResourceCommand));
static_assert(sizeof(nkui_layer_command) == sizeof(nkui::BeginLayerCommand));

namespace {

struct DisplayListSlot {
    std::unique_ptr<nkui::DisplayList> list;
    uint16_t generation = 1;
};

struct FontEntry {
    std::string path;
    nkui::FontFamily family = nkui::FontFamily::Default;
};

struct ResourceSlot {
    nkui::ResourceKind kind{};
    uint16_t generation = 1;
    std::vector<FontEntry> fonts;
    std::unique_ptr<nkui::SkribidiAdapter> text;
    nkui::PreparedGlyphs text_glyphs;
    std::unordered_map<int32_t, nkui::PreparedGlyphs> scaled_text_glyphs;
    std::vector<nkui_path_element> path;
    nkui_color color{};
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    nkui_image_format image_format = NKUI_IMAGE_FORMAT_INVALID;
    std::vector<uint8_t> pixels;
};

struct RendererSlot {
    std::unique_ptr<nkui::SokolBackend> backend;
    std::unique_ptr<nkui::NanoVGRecorder> recorder;
    nkui::Compositor compositor;
    std::unordered_map<uint32_t, int> images;
    uint16_t generation = 1;
};

std::mutex lists_mutex;
std::vector<DisplayListSlot> lists;
std::mutex resources_mutex;
std::vector<ResourceSlot> resources;
std::mutex renderers_mutex;
std::vector<RendererSlot> renderers;

uint32_t make_handle(uint16_t generation, uint16_t slot) {
    return (static_cast<uint32_t>(generation) << 16) | slot;
}

DisplayListSlot *resolve(nkui_display_list handle) {
    const uint16_t slot = static_cast<uint16_t>(handle.id);
    const uint16_t generation = static_cast<uint16_t>(handle.id >> 16);
    if (!slot || slot > lists.size())
        return nullptr;
    auto &entry = lists[slot - 1];
    return entry.list && entry.generation == generation ? &entry : nullptr;
}

ResourceSlot *resolve(nkui_resource handle, nkui::ResourceKind expected) {
    const nkui::ResourceId id{handle.id};
    if (!nkui::is_resource_id(id, expected))
        return nullptr;
    const uint16_t slot = static_cast<uint16_t>(handle.id);
    const uint16_t generation = static_cast<uint16_t>((handle.id >> 16) & 0x0FFF);
    if (!slot || slot > resources.size())
        return nullptr;
    auto &entry = resources[slot - 1];
    return entry.kind == expected && entry.generation == generation ? &entry : nullptr;
}

RendererSlot *resolve(nkui_renderer handle) {
    const uint16_t slot = static_cast<uint16_t>(handle.id);
    const uint16_t generation = static_cast<uint16_t>(handle.id >> 16);
    if (!slot || slot > renderers.size())
        return nullptr;
    auto &entry = renderers[slot - 1];
    return entry.backend && entry.generation == generation ? &entry : nullptr;
}

void append_path(NVGcontext *context, const std::vector<nkui_path_element> &elements) {
    nvgBeginPath(context);
    for (const auto &element : elements) {
        const float *v = element.values;
        switch (element.verb) {
        case NKUI_PATH_MOVE_TO:
            nvgMoveTo(context, v[0], v[1]);
            break;
        case NKUI_PATH_LINE_TO:
            nvgLineTo(context, v[0], v[1]);
            break;
        case NKUI_PATH_BEZIER_TO:
            nvgBezierTo(context, v[0], v[1], v[2], v[3], v[4], v[5]);
            break;
        case NKUI_PATH_QUADRATIC_TO:
            nvgQuadTo(context, v[0], v[1], v[2], v[3]);
            break;
        case NKUI_PATH_ARC_TO:
            nvgArcTo(context, v[0], v[1], v[2], v[3], v[4]);
            break;
        case NKUI_PATH_CLOSE:
            nvgClosePath(context);
            break;
        default:
            break;
        }
    }
}

void set_nanovg_transform(NVGcontext *context, const std::array<float, 6> &matrix) {
    nvgResetTransform(context);
    nvgTransform(context, matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]);
}

bool uniform_scale(const std::array<float, 6> &matrix, float &scale) {
    constexpr float epsilon = 0.0001f;
    if (std::abs(matrix[1]) > epsilon || std::abs(matrix[2]) > epsilon || matrix[0] <= 0.0f ||
        matrix[3] <= 0.0f || std::abs(matrix[0] - matrix[3]) > epsilon)
        return false;
    scale = matrix[0];
    return true;
}

NVGcolor paint_color(ResourceSlot *paint) {
    if (!paint)
        return nvgRGBAf(0.0f, 0.0f, 0.0f, 1.0f);
    return nvgRGBAf(paint->color.red, paint->color.green, paint->color.blue, paint->color.alpha);
}

nkui_result allocate_resource(nkui::ResourceKind kind, nkui_resource *out,
                              ResourceSlot **out_slot) {
    if (!out)
        return NKUI_ERROR_INVALID_ARGUMENT;
    out->id = 0;
    for (uint32_t index = 0; index < resources.size(); ++index) {
        auto &entry = resources[index];
        if (entry.kind == nkui::ResourceKind{}) {
            entry.kind = kind;
            out->id =
                nkui::make_resource_id(kind, entry.generation, static_cast<uint16_t>(index + 1))
                    .value;
            *out_slot = &entry;
            return NKUI_OK;
        }
    }
    if (resources.size() >= UINT16_MAX)
        return NKUI_ERROR_OUT_OF_MEMORY;
    try {
        resources.emplace_back();
        resources.back().kind = kind;
    } catch (...) {
        return NKUI_ERROR_OUT_OF_MEMORY;
    }
    out->id = nkui::make_resource_id(kind, 1, static_cast<uint16_t>(resources.size())).value;
    *out_slot = &resources.back();
    return NKUI_OK;
}

void release_resource_slot(ResourceSlot &slot) {
    slot.text.reset();
    slot.text_glyphs = {};
    slot.scaled_text_glyphs.clear();
    slot.fonts.clear();
    slot.path.clear();
    slot.pixels.clear();
    slot.color = {};
    slot.image_width = 0;
    slot.image_height = 0;
    slot.image_format = NKUI_IMAGE_FORMAT_INVALID;
    slot.kind = {};
    slot.generation = static_cast<uint16_t>((slot.generation % 0x0FFF) + 1);
}

} // namespace

extern "C" uint32_t nkui_api_version(void) {
    return NKUI_API_VERSION;
}

extern "C" nkui_result nkui_display_list_create(nkui_display_list *out_list) {
    if (!out_list)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(lists_mutex);
    try {
        for (uint32_t index = 0; index < lists.size(); ++index) {
            auto &slot = lists[index];
            if (!slot.list) {
                slot.list = std::make_unique<nkui::DisplayList>();
                out_list->id = make_handle(slot.generation, static_cast<uint16_t>(index + 1));
                return NKUI_OK;
            }
        }
        if (lists.size() >= UINT16_MAX)
            return NKUI_ERROR_OUT_OF_MEMORY;
        lists.push_back({std::make_unique<nkui::DisplayList>(), 1});
        out_list->id = make_handle(1, static_cast<uint16_t>(lists.size()));
        return NKUI_OK;
    } catch (...) {
        return NKUI_ERROR_OUT_OF_MEMORY;
    }
}

extern "C" nkui_result nkui_display_list_destroy(nkui_display_list list) {
    std::lock_guard<std::mutex> lock(lists_mutex);
    auto *slot = resolve(list);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    slot->list.reset();
    if (++slot->generation == 0)
        slot->generation = 1;
    return NKUI_OK;
}

extern "C" nkui_result nkui_display_list_reset(nkui_display_list list) {
    std::lock_guard<std::mutex> lock(lists_mutex);
    auto *slot = resolve(list);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    slot->list->reset();
    return NKUI_OK;
}

extern "C" nkui_result nkui_display_list_submit(nkui_display_list list, const uint8_t *commands,
                                                uint32_t command_bytes) {
    if (!nkui::validate_display_list(commands, command_bytes))
        return NKUI_ERROR_INVALID_TRANSACTION;
    std::lock_guard<std::mutex> lock(lists_mutex);
    auto *slot = resolve(list);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    return slot->list->assign_validated(commands, command_bytes) ? NKUI_OK
                                                                 : NKUI_ERROR_OUT_OF_MEMORY;
}

extern "C" nkui_result nkui_display_list_get_info(nkui_display_list list,
                                                  nkui_transaction_info *out_info) {
    if (!out_info)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(lists_mutex);
    auto *slot = resolve(list);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    *out_info = {sizeof(*out_info), NKUI_API_VERSION, static_cast<uint32_t>(slot->list->size()),
                 slot->list->command_count()};
    return NKUI_OK;
}

extern "C" nkui_result nkui_font_collection_create(nkui_resource *out_fonts) {
    std::lock_guard<std::mutex> lock(resources_mutex);
    ResourceSlot *slot = nullptr;
    return allocate_resource(nkui::ResourceKind::FontCollection, out_fonts, &slot);
}

extern "C" nkui_result nkui_font_collection_add(nkui_resource fonts, const char *path,
                                                nkui_font_family family) {
    if (!path || !*path || (family != NKUI_FONT_FAMILY_DEFAULT && family != NKUI_FONT_FAMILY_EMOJI))
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(fonts, nkui::ResourceKind::FontCollection);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    try {
        slot->fonts.push_back({path, family == NKUI_FONT_FAMILY_EMOJI ? nkui::FontFamily::Emoji
                                                                      : nkui::FontFamily::Default});
    } catch (...) {
        return NKUI_ERROR_OUT_OF_MEMORY;
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_create(nkui_resource fonts, const char *text, float width,
                                               float font_size, nkui_resource *out_layout) {
    if (!text || !out_layout || !std::isfinite(width) || !std::isfinite(font_size) ||
        width <= 0.0f || font_size <= 0.0f)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *font_slot = resolve(fonts, nkui::ResourceKind::FontCollection);
    if (!font_slot || font_slot->fonts.empty())
        return NKUI_ERROR_INVALID_HANDLE;
    std::vector<FontEntry> font_entries;
    try {
        font_entries = font_slot->fonts;
    } catch (...) {
        return NKUI_ERROR_OUT_OF_MEMORY;
    }
    ResourceSlot *layout_slot = nullptr;
    const nkui_result allocated =
        allocate_resource(nkui::ResourceKind::TextLayout, out_layout, &layout_slot);
    if (allocated != NKUI_OK)
        return allocated;
    try {
        layout_slot->text = std::make_unique<nkui::SkribidiAdapter>();
    } catch (...) {
        release_resource_slot(*layout_slot);
        out_layout->id = 0;
        return NKUI_ERROR_OUT_OF_MEMORY;
    }
    bool valid = layout_slot->text->valid() &&
                 layout_slot->text->set_atlas_namespace(static_cast<uint16_t>(out_layout->id));
    for (const auto &font : font_entries)
        valid = valid && layout_slot->text->add_font(font.path.c_str(), font.family);
    valid = valid && layout_slot->text->layout_utf8(text, width, font_size);
    valid = valid && layout_slot->text->prepare_glyphs(0.0f, 0.0f, 1.0f, nkui::GlyphMode::Alpha,
                                                       layout_slot->text_glyphs);
    if (!valid) {
        release_resource_slot(*layout_slot);
        out_layout->id = 0;
        return NKUI_ERROR_INVALID_ARGUMENT;
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_measure(nkui_resource layout,
                                                nkui_text_metrics *out_metrics) {
    if (!out_metrics)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto bounds = slot->text->bounds();
    *out_metrics = {sizeof(*out_metrics), bounds.x, bounds.y, bounds.width, bounds.height};
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_hit_test(nkui_resource layout, float x, float y,
                                                 nkui_text_position *out_position) {
    if (!out_position || !std::isfinite(x) || !std::isfinite(y))
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto position = slot->text->hit_test(x, y);
    *out_position = {position.offset, position.affinity};
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_caret(nkui_resource layout, nkui_text_position position,
                                              nkui_text_caret *out_caret) {
    if (!out_caret || position.offset < 0 || position.affinity > 4)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto caret =
        slot->text->caret({position.offset, static_cast<uint8_t>(position.affinity)});
    *out_caret = {sizeof(*out_caret), caret.x,     caret.y,        caret.ascender,
                  caret.descender,    caret.slope, caret.direction};
    return NKUI_OK;
}

extern "C" nkui_result nkui_resource_destroy(nkui_resource resource) {
    std::lock_guard<std::mutex> lock(resources_mutex);
    const uint16_t slot_index = static_cast<uint16_t>(resource.id);
    const uint16_t generation = static_cast<uint16_t>((resource.id >> 16) & 0x0FFF);
    if (!slot_index || slot_index > resources.size())
        return NKUI_ERROR_INVALID_HANDLE;
    auto &slot = resources[slot_index - 1];
    if (slot.kind == nkui::ResourceKind{} || slot.generation != generation)
        return NKUI_ERROR_INVALID_HANDLE;
    release_resource_slot(slot);
    return NKUI_OK;
}

extern "C" nkui_result nkui_path_create(const nkui_path_element *elements, uint32_t count,
                                        nkui_resource *out_path) {
    if (!elements || !count || !out_path)
        return NKUI_ERROR_INVALID_ARGUMENT;
    bool has_geometry = false;
    for (uint32_t index = 0; index < count; ++index) {
        const auto &element = elements[index];
        uint32_t value_count = 0;
        switch (element.verb) {
        case NKUI_PATH_MOVE_TO:
        case NKUI_PATH_LINE_TO:
            value_count = 2;
            has_geometry = true;
            break;
        case NKUI_PATH_BEZIER_TO:
            value_count = 6;
            has_geometry = true;
            break;
        case NKUI_PATH_QUADRATIC_TO:
            value_count = 4;
            has_geometry = true;
            break;
        case NKUI_PATH_ARC_TO:
            value_count = 5;
            has_geometry = true;
            break;
        case NKUI_PATH_CLOSE:
            break;
        default:
            return NKUI_ERROR_INVALID_ARGUMENT;
        }
        for (uint32_t value = 0; value < value_count; ++value)
            if (!std::isfinite(element.values[value]))
                return NKUI_ERROR_INVALID_ARGUMENT;
    }
    if (!has_geometry)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    ResourceSlot *slot = nullptr;
    const auto result = allocate_resource(nkui::ResourceKind::Path, out_path, &slot);
    if (result != NKUI_OK)
        return result;
    try {
        slot->path.assign(elements, elements + count);
    } catch (...) {
        release_resource_slot(*slot);
        out_path->id = 0;
        return NKUI_ERROR_OUT_OF_MEMORY;
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_paint_create_solid(nkui_color color, nkui_resource *out_paint) {
    if (!out_paint || !std::isfinite(color.red) || !std::isfinite(color.green) ||
        !std::isfinite(color.blue) || !std::isfinite(color.alpha))
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    ResourceSlot *slot = nullptr;
    const auto result = allocate_resource(nkui::ResourceKind::Paint, out_paint, &slot);
    if (result == NKUI_OK)
        slot->color = color;
    return result;
}

extern "C" nkui_result nkui_image_create(uint32_t width, uint32_t height, nkui_image_format format,
                                         const uint8_t *pixels, uint32_t pixel_bytes,
                                         nkui_resource *out_image) {
    const uint32_t bytes_per_pixel = format == NKUI_IMAGE_R8      ? 1
                                     : format == NKUI_IMAGE_RGBA8 ? 4
                                                                  : 0;
    const uint64_t required = static_cast<uint64_t>(width) * height * bytes_per_pixel;
    if (!out_image || !pixels || !width || !height || !bytes_per_pixel || required != pixel_bytes ||
        required > UINT32_MAX)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    ResourceSlot *slot = nullptr;
    const auto result = allocate_resource(nkui::ResourceKind::Image, out_image, &slot);
    if (result != NKUI_OK)
        return result;
    try {
        slot->pixels.assign(pixels, pixels + pixel_bytes);
    } catch (...) {
        release_resource_slot(*slot);
        out_image->id = 0;
        return NKUI_ERROR_OUT_OF_MEMORY;
    }
    slot->image_width = width;
    slot->image_height = height;
    slot->image_format = format;
    return NKUI_OK;
}

extern "C" nkui_result nkui_renderer_create(nkui_renderer *out_renderer) {
    if (!out_renderer)
        return NKUI_ERROR_INVALID_ARGUMENT;
    out_renderer->id = 0;
    std::lock_guard<std::mutex> lock(renderers_mutex);
    try {
        for (uint32_t index = 0; index < renderers.size(); ++index) {
            auto &slot = renderers[index];
            if (!slot.backend) {
                slot.backend = std::make_unique<nkui::SokolBackend>();
                slot.recorder = std::make_unique<nkui::NanoVGRecorder>();
                if (!slot.recorder->valid()) {
                    slot.backend.reset();
                    slot.recorder.reset();
                    return NKUI_ERROR_RENDERING;
                }
                out_renderer->id = make_handle(slot.generation, static_cast<uint16_t>(index + 1));
                return NKUI_OK;
            }
        }
        if (renderers.size() >= UINT16_MAX)
            return NKUI_ERROR_OUT_OF_MEMORY;
        renderers.emplace_back();
        auto &slot = renderers.back();
        slot.backend = std::make_unique<nkui::SokolBackend>();
        slot.recorder = std::make_unique<nkui::NanoVGRecorder>();
        if (!slot.recorder->valid()) {
            renderers.pop_back();
            return NKUI_ERROR_RENDERING;
        }
        out_renderer->id = make_handle(1, static_cast<uint16_t>(renderers.size()));
        return NKUI_OK;
    } catch (...) {
        return NKUI_ERROR_OUT_OF_MEMORY;
    }
}

extern "C" nkui_result nkui_renderer_destroy(nkui_renderer renderer) {
    std::lock_guard<std::mutex> lock(renderers_mutex);
    auto *slot = resolve(renderer);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    slot->backend.reset();
    slot->recorder.reset();
    slot->images.clear();
    slot->generation = static_cast<uint16_t>(slot->generation + 1);
    if (!slot->generation)
        slot->generation = 1;
    return NKUI_OK;
}

extern "C" nkui_result nkui_renderer_render(nkui_renderer renderer, nkui_display_list list,
                                            nk_handle surface) {
    int32_t width = 0;
    int32_t height = 0;
    if (!surface || nk_surface_make_current(surface) != NK_OK ||
        nk_surface_get_framebuffer_size(surface, &width, &height) != NK_OK || width <= 0 ||
        height <= 0)
        return NKUI_ERROR_INVALID_ARGUMENT;
    GLint framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    std::scoped_lock lock(renderers_mutex, lists_mutex, resources_mutex);
    auto *renderer_slot = resolve(renderer);
    auto *list_slot = resolve(list);
    if (!renderer_slot || !list_slot)
        return NKUI_ERROR_INVALID_HANDLE;
    const nkui::ResourceId main_target =
        nkui::make_resource_id(nkui::ResourceKind::RenderTarget, 1, 1);
    nkui::RenderPlan plan;
    if (!renderer_slot->compositor.compile(*list_slot->list, main_target, plan))
        return NKUI_ERROR_INVALID_TRANSACTION;

    auto &recorder = *renderer_slot->recorder;
    recorder.reset();
    NVGcontext *vg = recorder.context();
    nvgBeginFrame(vg, static_cast<float>(width), static_cast<float>(height), 1.0f);
    nkui::FrameResources frame_resources;
    std::vector<nkui::SkribidiAdapter *> text_adapters;
    uint16_t prepared_slot = 1;
    bool valid = true;

    for (auto &pass : plan.passes) {
        for (auto &command : pass.commands) {
            if (command.kind == nkui::RenderCommandKind::Path) {
                auto *path =
                    resolve(nkui_resource{command.resource.value}, nkui::ResourceKind::Path);
                auto *paint = command.paint.value ? resolve(nkui_resource{command.paint.value},
                                                            nkui::ResourceKind::Paint)
                                                  : nullptr;
                if (!path || (command.paint.value && !paint) || !prepared_slot) {
                    valid = false;
                    break;
                }
                set_nanovg_transform(vg, command.transform);
                append_path(vg, path->path);
                nvgFillColor(vg, paint_color(paint));
                nvgFill(vg);
                const nkui::ResourceId prepared_id =
                    nkui::make_resource_id(nkui::ResourceKind::Path, 0x0FFE, prepared_slot++);
                command.resource = prepared_id;
                command.transform = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
                valid = frame_resources.bind_path(prepared_id, recorder,
                                                  recorder.operations().size() - 1);
            } else if (command.kind == nkui::RenderCommandKind::Image) {
                auto *image =
                    resolve(nkui_resource{command.resource.value}, nkui::ResourceKind::Image);
                if (!image || !prepared_slot) {
                    valid = false;
                    break;
                }
                int nvg_image = 0;
                const auto cached = renderer_slot->images.find(command.resource.value);
                if (cached != renderer_slot->images.end()) {
                    nvg_image = cached->second;
                } else {
                    const uint8_t *pixels = image->pixels.data();
                    std::vector<uint8_t> expanded;
                    if (image->image_format == NKUI_IMAGE_R8) {
                        expanded.resize(image->pixels.size() * 4);
                        for (size_t index = 0; index < image->pixels.size(); ++index) {
                            expanded[index * 4 + 0] = 255;
                            expanded[index * 4 + 1] = 255;
                            expanded[index * 4 + 2] = 255;
                            expanded[index * 4 + 3] = image->pixels[index];
                        }
                        pixels = expanded.data();
                    }
                    nvg_image =
                        nvgCreateImageRGBA(vg, static_cast<int>(image->image_width),
                                           static_cast<int>(image->image_height), 0, pixels);
                    if (nvg_image)
                        renderer_slot->images.emplace(command.resource.value, nvg_image);
                }
                if (!nvg_image) {
                    valid = false;
                    break;
                }
                set_nanovg_transform(vg, command.transform);
                nvgBeginPath(vg);
                nvgRect(vg, command.x, command.y, command.width, command.height);
                nvgFillPaint(vg, nvgImagePattern(vg, command.x, command.y, command.width,
                                                 command.height, 0.0f, nvg_image, 1.0f));
                nvgFill(vg);
                const nkui::ResourceId prepared_id =
                    nkui::make_resource_id(nkui::ResourceKind::Path, 0x0FFE, prepared_slot++);
                command.kind = nkui::RenderCommandKind::Path;
                command.resource = prepared_id;
                command.transform = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
                valid = frame_resources.bind_path(prepared_id, recorder,
                                                  recorder.operations().size() - 1);
            } else if (command.kind == nkui::RenderCommandKind::GlyphBatch) {
                auto *layout =
                    resolve(nkui_resource{command.resource.value}, nkui::ResourceKind::TextLayout);
                if (!layout || !layout->text) {
                    valid = false;
                    break;
                }
                float requested_scale = 1.0f;
                if (!uniform_scale(command.transform, requested_scale))
                    requested_scale = 1.0f;
                const int32_t scale_bucket =
                    std::max(1, static_cast<int32_t>(std::round(requested_scale * 8.0f)));
                const float raster_scale = scale_bucket / 8.0f;
                nkui::PreparedGlyphs *glyphs = nullptr;
                if (scale_bucket == 8) {
                    glyphs = &layout->text_glyphs;
                } else {
                    auto found = layout->scaled_text_glyphs.find(scale_bucket);
                    if (found == layout->scaled_text_glyphs.end()) {
                        nkui::PreparedGlyphs prepared;
                        valid = layout->text->prepare_glyphs(0.0f, 0.0f, raster_scale,
                                                            nkui::GlyphMode::Alpha, prepared);
                        if (!valid)
                            break;
                        found = layout->scaled_text_glyphs.emplace(scale_bucket,
                                                                  std::move(prepared))
                                    .first;
                    }
                    glyphs = &found->second;
                }
                const nkui::ResourceId prepared_id = nkui::make_resource_id(
                    nkui::ResourceKind::TextLayout, 0x0FFE, prepared_slot++);
                valid = frame_resources.bind_text(prepared_id, *glyphs);
                command.resource = prepared_id;
                if (requested_scale != 1.0f) {
                    const float correction = requested_scale / raster_scale;
                    command.x *= raster_scale;
                    command.y *= raster_scale;
                    command.transform = {correction, 0.0f, 0.0f, correction,
                                         command.transform[4], command.transform[5]};
                }
                text_adapters.push_back(layout->text.get());
            } else if (command.kind == nkui::RenderCommandKind::CompositeTarget &&
                       command.resource.value < (UINT32_C(4) << 28)) {
                valid = false;
                break;
            }
        }
        if (!valid)
            break;
    }
    nvgEndFrame(vg);
    if (!valid)
        return NKUI_ERROR_INVALID_HANDLE;
    const bool new_backend = !renderer_slot->backend->valid();
    if (new_backend && !renderer_slot->backend->initialize())
        return NKUI_ERROR_RENDERING;
    for (auto *adapter : text_adapters)
        if (!renderer_slot->backend->upload_atlases(*adapter, new_backend))
            return NKUI_ERROR_RENDERING;
    const bool executed =
        nkui::execute_render_plan(*renderer_slot->backend, plan, frame_resources,
                                  {main_target, width, height, static_cast<uint32_t>(framebuffer)});
    return executed ? NKUI_OK : NKUI_ERROR_RENDERING;
}
