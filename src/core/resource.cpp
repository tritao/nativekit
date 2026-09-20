#include "nativekit_resource.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace {
struct ResourceLoadEntry {
    nk::core::ResourceDataHandler handler = nullptr;
    void *user_data = nullptr;
    nk::core::ResourceDataHandlerCleanup cleanup = nullptr;
    bool canceled = false;
};

std::mutex resource_loads_mutex;
std::unordered_map<nk_request_id, ResourceLoadEntry> resource_loads;

bool string_view(const unsigned char *bytes, std::size_t size, uint32_t offset, uint32_t minimum,
                 const char **out, uint32_t *out_length) {
    if (offset == 0) {
        *out = nullptr;
        *out_length = 0;
        return true;
    }
    if (offset < minimum || offset >= size)
        return false;
    const auto *value = reinterpret_cast<const char *>(bytes + offset);
    const auto *end = static_cast<const char *>(std::memchr(value, '\0', size - offset));
    if (!end || static_cast<std::size_t>(end - value) > std::numeric_limits<uint32_t>::max())
        return false;
    *out = value;
    *out_length = static_cast<uint32_t>(end - value);
    return true;
}
} // namespace

namespace nk::core {

nk_result register_resource_load(nk_request_id request, ResourceDataHandler handler, void *user_data,
                                  ResourceDataHandlerCleanup cleanup) noexcept {
    if (request == NK_INVALID_REQUEST_ID || (handler == nullptr) != (cleanup == nullptr))
        return NK_ERROR_INVALID_ARGUMENT;
#if NK_ENABLE_NO_EXCEPTIONS
    {
#else
    try {
#endif
        std::lock_guard lock(resource_loads_mutex);
        if (!resource_loads.emplace(request, ResourceLoadEntry{handler, user_data, cleanup, false})
                 .second)
            return NK_ERROR_ALREADY_INITIALIZED;
        return NK_OK;
#if !NK_ENABLE_NO_EXCEPTIONS
    } catch (...) {
        return NK_ERROR_OUT_OF_MEMORY;
    }
#else
    }
#endif
}

void unregister_resource_load(nk_request_id request) noexcept {
    ResourceLoadEntry entry;
    {
        std::lock_guard lock(resource_loads_mutex);
        const auto found = resource_loads.find(request);
        if (found == resource_loads.end())
            return;
        entry = found->second;
        resource_loads.erase(found);
    }
    if (entry.cleanup)
        entry.cleanup(entry.user_data);
}

bool is_resource_load_pending(nk_request_id request) noexcept {
    if (request == NK_INVALID_REQUEST_ID)
        return false;
    std::lock_guard lock(resource_loads_mutex);
    return resource_loads.find(request) != resource_loads.end();
}

bool dispatch_resource_data_event(const nk_event &event) noexcept {
    if (event.kind != NK_EVENT_RESOURCE_DATA_COMPLETE ||
        event.request_id == NK_INVALID_REQUEST_ID)
        return false;
    ResourceLoadEntry entry;
    {
        std::lock_guard lock(resource_loads_mutex);
        const auto found = resource_loads.find(event.request_id);
        if (found == resource_loads.end())
            return false;
        entry = found->second;
        resource_loads.erase(found);
    }
    if (entry.canceled) {
        if (entry.cleanup)
            entry.cleanup(entry.user_data);
        return true;
    }
    if (!entry.handler)
        return false;
    entry.handler(event.request_id, event.result, event.data, event.data_size, entry.user_data);
    if (entry.cleanup)
        entry.cleanup(entry.user_data);
    return true;
}

void clear_resource_loads() noexcept {
    std::unordered_map<nk_request_id, ResourceLoadEntry> pending;
    {
        std::lock_guard lock(resource_loads_mutex);
        pending.swap(resource_loads);
    }
    for (const auto &[request, entry] : pending) {
        (void)nk::backend::cancel_resource_load(request);
        if (entry.cleanup)
            entry.cleanup(entry.user_data);
    }
}

nk_result cancel_resource_load(nk_request_id request) noexcept {
    if (const auto result = require_ui_thread(); result != NK_OK)
        return result;
    if (request == NK_INVALID_REQUEST_ID) {
        set_error("invalid resource load request");
        return NK_ERROR_INVALID_REQUEST;
    }
    {
        std::lock_guard lock(resource_loads_mutex);
        const auto found = resource_loads.find(request);
        if (found == resource_loads.end() || found->second.canceled) {
            set_error("invalid or completed resource load request");
            return NK_ERROR_INVALID_REQUEST;
        }
        found->second.canceled = true;
    }
    const auto result = nk::backend::cancel_resource_load(request);
    if (result != NK_OK) {
        std::lock_guard lock(resource_loads_mutex);
        const auto found = resource_loads.find(request);
        if (found != resource_loads.end() && found->second.canceled)
            found->second.canceled = false;
    }
    return result;
}

nk_result start_resource_load(const struct nk_resource *resource, nk_request_id *out_request,
                              ResourceDataHandler handler, void *user_data,
                              ResourceDataHandlerCleanup cleanup) noexcept {
    if (const auto result = require_ui_thread(); result != NK_OK) {
        if (cleanup)
            cleanup(user_data);
        return result;
    }
    if (!resource || resource->struct_size < sizeof(nk_resource) || !resource->uri ||
        !*resource->uri || !out_request || (handler == nullptr) != (cleanup == nullptr)) {
        set_error("resource load arguments are invalid");
        if (cleanup)
            cleanup(user_data);
        return NK_ERROR_INVALID_ARGUMENT;
    }
    *out_request = NK_INVALID_REQUEST_ID;
    const auto request = next_request_id();
    const auto registration = register_resource_load(request, handler, user_data, cleanup);
    if (registration != NK_OK) {
        set_error("could not register resource load handler");
        if (cleanup)
            cleanup(user_data);
        return registration;
    }
    const auto result = nk::backend::load_resource_async(resource, request);
    if (result != NK_OK) {
        unregister_resource_load(request);
        return result;
    }
    *out_request = request;
    return NK_OK;
}

} // namespace nk::core

#if defined(NK_BACKEND_ANDROID)
namespace nk::backend {

nk_result load_resource_async(const struct nk_resource *, nk_request_id) noexcept {
    nk::core::set_error("asynchronous URI resource loading is unavailable on this platform");
    return NK_ERROR_UNSUPPORTED;
}

nk_result cancel_resource_load(nk_request_id) noexcept {
    nk::core::set_error("asynchronous URI resource cancellation is unavailable on this platform");
    return NK_ERROR_UNSUPPORTED;
}

} // namespace nk::backend
#endif

extern "C" nk_result NK_CALL nk_resource_load_async(const nk_resource *resource,
                                                    nk_request_id *out_request) {
    return nk::core::start_resource_load(resource, out_request);
}

extern "C" nk_result NK_CALL nk_resource_load_cancel(nk_request_id request) {
    nk::core::clear_error();
    return nk::core::cancel_resource_load(request);
}

extern "C" nk_result NK_CALL nk_resource_event_item(const nk_event *event, uint32_t index,
                                                    nk_resource_view *out_resource) {
    nk::core::clear_error();
    if (!event ||
        (event->kind != NK_EVENT_DIALOG_RESOURCES_COMPLETE &&
         event->kind != NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE &&
         event->kind != NK_EVENT_RESOURCE_OPENED && event->kind != NK_EVENT_SHARE_RECEIVED &&
         event->kind != NK_EVENT_RESOURCE_DROP) ||
        !out_resource || out_resource->struct_size < sizeof(nk_resource_view) || !event->data ||
        event->data_size < sizeof(nk_resource_list) ||
        event->data_size > std::numeric_limits<std::size_t>::max()) {
        nk::core::set_error("invalid resource event arguments");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto size = static_cast<std::size_t>(event->data_size);
    std::size_t list_offset = 0;
    if (event->kind == NK_EVENT_SHARE_RECEIVED || event->kind == NK_EVENT_RESOURCE_DROP) {
        const auto header_size = event->kind == NK_EVENT_SHARE_RECEIVED ? sizeof(nk_received_share)
                                                                        : sizeof(nk_resource_drop);
        if (size < header_size) {
            nk::core::set_error("malformed resource-bearing payload");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        uint32_t encoded_offset = 0;
        std::memcpy(&encoded_offset, event->data, sizeof(encoded_offset));
        list_offset = encoded_offset;
    }
    if (list_offset > size || sizeof(nk_resource_list) > size - list_offset) {
        nk::core::set_error("malformed resource-list offset");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    nk_resource_list list{};
    const auto *bytes = static_cast<const unsigned char *>(event->data);
    std::memcpy(&list, bytes + list_offset, sizeof(list));
    const auto table_size = static_cast<std::size_t>(list.item_count) * sizeof(nk_resource_item);
    if (index >= list.item_count || list.items_offset < list_offset + sizeof(nk_resource_list) ||
        list.items_offset > size || table_size > size - list.items_offset ||
        list.strings_offset < list.items_offset + table_size || list.strings_offset > size) {
        nk::core::set_error("malformed resource event payload");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    nk_resource_item item{};
    std::memcpy(&item, bytes + list.items_offset + index * sizeof(item), sizeof(item));
    nk_resource_view result{};
    result.struct_size = out_resource->struct_size;
    result.flags = item.flags;
    if (!string_view(bytes, size, item.uri_offset, list.strings_offset, &result.uri,
                     &result.uri_length) ||
        !result.uri || result.uri_length == 0 ||
        !string_view(bytes, size, item.mime_type_offset, list.strings_offset, &result.mime_type,
                     &result.mime_type_length) ||
        !string_view(bytes, size, item.display_name_offset, list.strings_offset,
                     &result.display_name, &result.display_name_length)) {
        nk::core::set_error("malformed resource event string");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    *out_resource = result;
    return NK_OK;
}

namespace {
nk_result share_string(const nk_event *event, bool subject, const char **out,
                       uint32_t *out_length) {
    nk::core::clear_error();
    if (!event || event->kind != NK_EVENT_SHARE_RECEIVED || !event->data || !out || !out_length ||
        event->data_size < sizeof(nk_received_share) ||
        event->data_size > std::numeric_limits<std::size_t>::max()) {
        nk::core::set_error("invalid received-share event arguments");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    nk_received_share share{};
    std::memcpy(&share, event->data, sizeof(share));
    const auto offset = subject ? share.subject_offset : share.text_offset;
    const auto *bytes = static_cast<const unsigned char *>(event->data);
    if (!string_view(bytes, static_cast<std::size_t>(event->data_size), offset,
                     sizeof(nk_received_share), out, out_length)) {
        nk::core::set_error("malformed received-share string");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    return NK_OK;
}
} // namespace

extern "C" nk_result NK_CALL nk_share_event_text(const nk_event *event, const char **out_text,
                                                 uint32_t *out_length) {
    return share_string(event, false, out_text, out_length);
}

extern "C" nk_result NK_CALL nk_share_event_subject(const nk_event *event, const char **out_subject,
                                                    uint32_t *out_length) {
    return share_string(event, true, out_subject, out_length);
}

extern "C" nk_result NK_CALL nk_resource_drop_event_text(const nk_event *event,
                                                         const char **out_text,
                                                         uint32_t *out_length) {
    nk::core::clear_error();
    if (!event || event->kind != NK_EVENT_RESOURCE_DROP || !event->data || !out_text ||
        !out_length || event->data_size < sizeof(nk_resource_drop) ||
        event->data_size > std::numeric_limits<std::size_t>::max()) {
        nk::core::set_error("invalid resource-drop event arguments");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    nk_resource_drop drop{};
    std::memcpy(&drop, event->data, sizeof(drop));
    if (!string_view(static_cast<const unsigned char *>(event->data),
                     static_cast<std::size_t>(event->data_size), drop.text_offset,
                     sizeof(nk_resource_drop), out_text, out_length)) {
        nk::core::set_error("malformed resource-drop text");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    return NK_OK;
}
