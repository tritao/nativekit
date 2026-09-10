#include "nativekit_accessibility.h"

#include "core/error.hpp"

#include <cstring>

#if !defined(NK_BACKEND_ANDROID)
namespace {
nk_result unsupported_accessibility() {
    nk::core::set_error("custom-surface accessibility is unavailable on this backend");
    return NK_ERROR_UNSUPPORTED;
}
} // namespace
#endif

extern "C" {

#if !defined(NK_BACKEND_ANDROID)
nk_result NK_CALL nk_surface_accessibility_set_node(nk_handle,
                                                     const nk_accessibility_node *) {
    return unsupported_accessibility();
}
nk_result NK_CALL nk_surface_accessibility_remove_node(nk_handle, nk_accessibility_node_id) {
    return unsupported_accessibility();
}
nk_result NK_CALL nk_surface_accessibility_clear(nk_handle) {
    return unsupported_accessibility();
}
nk_result NK_CALL nk_surface_accessibility_set_focus(nk_handle, nk_accessibility_node_id) {
    return unsupported_accessibility();
}
#endif

nk_result NK_CALL nk_accessibility_action_event_value(const nk_event *event,
                                                       const char **out_value,
                                                       uint32_t *out_length) {
    if (!event || event->kind != NK_EVENT_ACCESSIBILITY_ACTION || !event->data ||
        event->data_size < sizeof(nk_accessibility_action_event) || !out_value || !out_length) {
        nk::core::set_error("invalid accessibility action event payload");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto *payload = static_cast<const nk_accessibility_action_event *>(event->data);
    if (!payload->value_length) {
        *out_value = nullptr;
        *out_length = 0;
        return NK_OK;
    }
    const uint64_t end = static_cast<uint64_t>(payload->value_offset) + payload->value_length;
    if (payload->value_offset < sizeof(*payload) || end >= event->data_size ||
        static_cast<const char *>(event->data)[end] != '\0') {
        nk::core::set_error("accessibility action text is outside the event payload");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    *out_value = static_cast<const char *>(event->data) + payload->value_offset;
    *out_length = payload->value_length;
    return NK_OK;
}

} // extern "C"
