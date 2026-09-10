#include "nativekit_ui.h"

#include "display_list/display_list.h"

#include <memory>
#include <mutex>
#include <vector>

static_assert(sizeof(nkui_command_header) == sizeof(nkui::CommandHeader));
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

std::mutex lists_mutex;
std::vector<DisplayListSlot> lists;

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
