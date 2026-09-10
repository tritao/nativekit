#include "nativekit_ui.h"

#include "display_list/display_list.h"
#include "prepare/skribidi_adapter.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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
    std::vector<nkui_path_element> path;
    nkui_color color{};
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    nkui_image_format image_format = NKUI_IMAGE_FORMAT_INVALID;
    std::vector<uint8_t> pixels;
};

std::mutex lists_mutex;
std::vector<DisplayListSlot> lists;
std::mutex resources_mutex;
std::vector<ResourceSlot> resources;

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
    bool valid = layout_slot->text->valid();
    for (const auto &font : font_entries)
        valid = valid && layout_slot->text->add_font(font.path.c_str(), font.family);
    valid = valid && layout_slot->text->layout_utf8(text, width, font_size);
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
