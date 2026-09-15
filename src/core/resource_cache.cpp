#include "nativekit_resource.h"

#include "core/boundary.hpp"
#include "core/resource_cache.hpp"
#include "core/error.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct ResourceCacheEntry;
struct ResourceAssetResource;

struct ResourceCacheResource final : nk::core::Resource {
    std::unordered_map<std::string, std::shared_ptr<ResourceCacheEntry>> entries;
    std::atomic<nk_resource_cache> handle{NK_INVALID_HANDLE};

    ~ResourceCacheResource() override {
        handle.store(NK_INVALID_HANDLE, std::memory_order_release);
    }
};

struct ResourceCacheEntry final {
    std::string uri;
    std::string mime_type;
    std::string display_name;
    nk_resource_flags flags = 0;
    std::atomic<nk_resource_asset_load_state> load_state{NK_RESOURCE_ASSET_LOADING};
    std::atomic<nk_result> load_result{NK_OK};
    std::atomic<nk_request_id> load_request{NK_INVALID_REQUEST_ID};
    std::atomic<bool> cancelled{false};
    std::mutex mutex;
    std::shared_ptr<const std::vector<std::byte>> data;
    std::vector<std::weak_ptr<ResourceAssetResource>> assets;
};

struct ResourceAssetResource final : nk::core::Resource {
    std::shared_ptr<ResourceCacheEntry> entry;
    std::atomic<nk_resource_asset> handle{NK_INVALID_HANDLE};
    std::atomic<bool> load_event_emitted{false};

    ~ResourceAssetResource() override {
        handle.store(NK_INVALID_HANDLE, std::memory_order_release);
    }
};

struct ResourceCacheLoadContext {
    std::shared_ptr<ResourceCacheEntry> entry;
};

void resource_cache_load_callback(nk_request_id request, nk_result result, const void *data,
                                  uint64_t data_size, void *user_data) noexcept;
void resource_cache_load_cleanup(void *user_data) noexcept;

nk_result invalid_argument(const char *message) {
    nk::core::set_error(message);
    return NK_ERROR_INVALID_ARGUMENT;
}

std::shared_ptr<ResourceCacheResource> get_cache(nk_resource_cache handle) {
    auto resource = nk::core::handles().get(handle, nk::core::ResourceType::resource_cache);
    if (!resource) {
        nk::core::set_error("invalid resource cache handle");
        return {};
    }
    auto cache = std::dynamic_pointer_cast<ResourceCacheResource>(std::move(resource));
    if (!cache)
        nk::core::set_error("invalid resource cache resource");
    return cache;
}

std::shared_ptr<ResourceAssetResource> get_asset(nk_resource_asset handle) {
    auto resource = nk::core::handles().get(handle, nk::core::ResourceType::resource_asset);
    if (!resource) {
        nk::core::set_error("invalid resource asset handle");
        return {};
    }
    auto asset = std::dynamic_pointer_cast<ResourceAssetResource>(std::move(resource));
    if (!asset)
        nk::core::set_error("invalid resource asset resource");
    return asset;
}

void discard_asset(nk_resource_asset handle) noexcept {
    auto asset = get_asset(handle);
    if (!asset)
        return;
    asset->handle.store(NK_INVALID_HANDLE, std::memory_order_release);
    (void)nk::core::handles().erase(handle, nk::core::ResourceType::resource_asset);
}

bool valid_resource(const nk_resource *resource) {
    return resource && resource->struct_size >= sizeof(nk_resource) &&
           (resource->flags & NK_RESOURCE_READABLE) != 0 && resource->uri && *resource->uri;
}

std::shared_ptr<ResourceCacheEntry> make_entry(const nk_resource *resource) {
    auto entry = std::make_shared<ResourceCacheEntry>();
    entry->uri = resource->uri;
    entry->flags = resource->flags;
    if (resource->mime_type)
        entry->mime_type = resource->mime_type;
    if (resource->display_name)
        entry->display_name = resource->display_name;
    return entry;
}

void publish_asset_load_event(ResourceAssetResource &asset) noexcept {
    const auto entry = asset.entry;
    if (!entry)
        return;
    const auto state = entry->load_state.load(std::memory_order_acquire);
    if (state == NK_RESOURCE_ASSET_LOADING || entry->load_request.load(std::memory_order_acquire) ==
                                                  NK_INVALID_REQUEST_ID ||
        entry->cancelled.load(std::memory_order_acquire))
        return;
    const auto handle = asset.handle.load(std::memory_order_acquire);
    if (handle == NK_INVALID_HANDLE)
        return;

    bool expected = false;
    if (!asset.load_event_emitted.compare_exchange_strong(expected, true,
                                                          std::memory_order_acq_rel))
        return;

    nk::core::QueuedEvent event;
    event.kind = state == NK_RESOURCE_ASSET_READY ? NK_EVENT_RESOURCE_CACHE_READY
                                                  : NK_EVENT_RESOURCE_CACHE_LOAD_FAILED;
    event.source = handle;
    event.request_id = entry->load_request.load(std::memory_order_acquire);
    event.result = entry->load_result.load(std::memory_order_acquire);
    if (nk::core::push_event(std::move(event)) != NK_OK)
        asset.load_event_emitted.store(false, std::memory_order_release);
}

void publish_entry_load_events(ResourceCacheEntry &entry) noexcept {
    std::vector<std::shared_ptr<ResourceAssetResource>> assets;
    {
        std::lock_guard lock(entry.mutex);
        entry.assets.erase(
            std::remove_if(entry.assets.begin(), entry.assets.end(), [&](const auto &weak_asset) {
                auto asset = weak_asset.lock();
                if (!asset)
                    return true;
                assets.push_back(std::move(asset));
                return false;
            }),
            entry.assets.end());
    }
    for (const auto &asset : assets)
        publish_asset_load_event(*asset);
}

nk_result acquire_asset(const std::shared_ptr<ResourceCacheEntry> &entry,
                        nk_resource_asset *out_asset) {
    std::shared_ptr<ResourceAssetResource> asset;
    nk_resource_asset handle = NK_INVALID_HANDLE;
    try {
        asset = std::make_shared<ResourceAssetResource>();
        asset->entry = entry;
        handle = nk::core::handles().insert(nk::core::ResourceType::resource_asset, asset);
        if (handle == NK_INVALID_HANDLE) {
            nk::core::set_error("could not allocate resource asset handle");
            return NK_ERROR_OUT_OF_MEMORY;
        }
        asset->handle.store(handle, std::memory_order_release);
        std::lock_guard lock(entry->mutex);
        entry->assets.emplace_back(asset);
        *out_asset = handle;
        publish_asset_load_event(*asset);
        return NK_OK;
    } catch (const std::bad_alloc &) {
        if (handle != NK_INVALID_HANDLE) {
            asset->handle.store(NK_INVALID_HANDLE, std::memory_order_release);
            (void)nk::core::handles().erase(handle, nk::core::ResourceType::resource_asset);
        }
        nk::core::set_error("could not allocate resource asset handle");
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        if (handle != NK_INVALID_HANDLE) {
            asset->handle.store(NK_INVALID_HANDLE, std::memory_order_release);
            (void)nk::core::handles().erase(handle, nk::core::ResourceType::resource_asset);
        }
        nk::core::set_error("could not retain resource asset view");
        return NK_ERROR_UNKNOWN;
    }
}

struct ResourceStreamGuard {
    nk_resource_stream stream = NK_INVALID_HANDLE;

    ~ResourceStreamGuard() {
        if (stream != NK_INVALID_HANDLE)
            nk_resource_close(stream);
    }
};

nk_result read_resource_bytes(const nk_resource *resource, std::vector<std::byte> &output) {
    nk_resource_stream stream = NK_INVALID_HANDLE;
    auto result = nk_resource_open(resource, NK_RESOURCE_OPEN_READ, &stream);
    if (result != NK_OK)
        return result;
    ResourceStreamGuard guard{stream};

    nk_resource_stream_info info{};
    info.struct_size = sizeof(info);
    result = nk_resource_stream_info_get(stream, &info);
    if (result != NK_OK)
        return result;

    const bool size_known = (info.flags & NK_RESOURCE_STREAM_SIZE_KNOWN) != 0;
    if (size_known && info.size > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()))
        return NK_ERROR_OUT_OF_MEMORY;

    constexpr std::size_t chunk_size = 64u * 1024u;
    std::vector<std::byte> chunk(chunk_size);
    output.clear();
    if (size_known)
        output.resize(static_cast<std::size_t>(info.size));

    uint64_t total = 0;
    while (true) {
        const auto remaining = size_known ? info.size - total : static_cast<uint64_t>(chunk.size());
        const auto requested = std::min<uint64_t>(remaining, chunk.size());
        if (requested == 0)
            break;
        uint64_t read = 0;
        result = nk_resource_read(stream, chunk.data(), requested, &read);
        if (result != NK_OK)
            return result;
        if (read == 0)
            break;
        if (read > requested || total > std::numeric_limits<uint64_t>::max() - read)
            return NK_ERROR_UNKNOWN;
        if (size_known) {
            std::memcpy(output.data() + static_cast<std::size_t>(total), chunk.data(),
                        static_cast<std::size_t>(read));
        } else {
            if (total + read > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()))
                return NK_ERROR_OUT_OF_MEMORY;
            output.insert(output.end(), chunk.begin(), chunk.begin() + static_cast<std::size_t>(read));
        }
        total += read;
        if (size_known && total == info.size)
            break;
    }
    if (size_known)
        output.resize(static_cast<std::size_t>(total));
    return NK_OK;
}

nk_result cancel_entry_load(const std::shared_ptr<ResourceCacheEntry> &entry) {
    if (entry->load_state.load(std::memory_order_acquire) != NK_RESOURCE_ASSET_LOADING)
        return NK_OK;
    const auto request = entry->load_request.load(std::memory_order_acquire);
    if (request == NK_INVALID_REQUEST_ID)
        return invalid_argument("resource cache entry has no pending request");
    entry->cancelled.store(true, std::memory_order_release);
    const auto result = nk::core::cancel_resource_load(request);
    if (result != NK_OK) {
        entry->cancelled.store(false, std::memory_order_release);
        return result;
    }
    entry->load_result.store(NK_ERROR_INVALID_REQUEST, std::memory_order_release);
    entry->load_state.store(NK_RESOURCE_ASSET_LOAD_FAILED, std::memory_order_release);
    return NK_OK;
}

nk_result clear_cache(ResourceCacheResource &cache) {
    nk_result first_error = NK_OK;
    for (auto it = cache.entries.begin(); it != cache.entries.end();) {
        const auto result = cancel_entry_load(it->second);
        if (result != NK_OK && first_error == NK_OK) {
            first_error = result;
            ++it;
            continue;
        }
        it = cache.entries.erase(it);
    }
    return first_error;
}

void resource_cache_load_callback(nk_request_id request, nk_result result, const void *data,
                                  uint64_t data_size, void *user_data) noexcept {
    auto *context = static_cast<ResourceCacheLoadContext *>(user_data);
    if (!context || !context->entry)
        return;
    auto &entry = *context->entry;
    if (entry.cancelled.load(std::memory_order_acquire))
        return;

    auto load_result = result;
    try {
        if (load_result == NK_OK) {
            if (data_size > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()))
                load_result = NK_ERROR_OUT_OF_MEMORY;
            else if (data_size != 0 && !data)
                load_result = NK_ERROR_UNKNOWN;
            else {
                auto bytes = std::make_shared<std::vector<std::byte>>(
                    static_cast<std::size_t>(data_size));
                if (data_size != 0)
                    std::memcpy(bytes->data(), data, static_cast<std::size_t>(data_size));
                std::lock_guard lock(entry.mutex);
                entry.data = std::move(bytes);
            }
        }
    } catch (const std::bad_alloc &) {
        load_result = NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        load_result = NK_ERROR_UNKNOWN;
    }
    entry.load_request.store(request, std::memory_order_release);
    entry.load_result.store(load_result, std::memory_order_release);
    entry.load_state.store(load_result == NK_OK ? NK_RESOURCE_ASSET_READY
                                                : NK_RESOURCE_ASSET_LOAD_FAILED,
                           std::memory_order_release);
    publish_entry_load_events(entry);
}

void resource_cache_load_cleanup(void *user_data) noexcept {
    delete static_cast<ResourceCacheLoadContext *>(user_data);
}

} // namespace

namespace nk::core {

nk_result resource_asset_get_bytes(nk_resource_asset asset,
                                   ResourceAssetBytes &out_bytes) noexcept {
    out_bytes.reset();
    auto value = get_asset(asset);
    if (!value)
        return NK_ERROR_INVALID_HANDLE;

    const auto state = value->entry->load_state.load(std::memory_order_acquire);
    if (state != NK_RESOURCE_ASSET_READY)
        return state == NK_RESOURCE_ASSET_LOADING
                   ? NK_ERROR_INVALID_REQUEST
                   : value->entry->load_result.load(std::memory_order_acquire);

    std::lock_guard lock(value->entry->mutex);
    if (!value->entry->data)
        return NK_ERROR_UNKNOWN;
    out_bytes = value->entry->data;
    return NK_OK;
}

} // namespace nk::core

extern "C" {

nk_result NK_CALL nk_resource_cache_create(nk_resource_cache *out_cache) {
    return nk::core::result_boundary("unexpected error while creating a resource cache",
                                     [&]() -> nk_result {
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        if (!out_cache)
            return invalid_argument("resource cache output is missing");
        *out_cache = NK_INVALID_HANDLE;
        auto cache = std::make_shared<ResourceCacheResource>();
        const auto handle =
            nk::core::handles().insert(nk::core::ResourceType::resource_cache, cache);
        if (handle == NK_INVALID_HANDLE) {
            nk::core::set_error("could not allocate resource cache handle");
            return NK_ERROR_OUT_OF_MEMORY;
        }
        cache->handle.store(handle, std::memory_order_release);
        *out_cache = handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_cache_destroy(nk_resource_cache cache) {
    return nk::core::result_boundary("unexpected error while destroying a resource cache",
                                     [&]() -> nk_result {
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        auto value = get_cache(cache);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        if (const auto result = clear_cache(*value); result != NK_OK)
            return result;
        value->handle.store(NK_INVALID_HANDLE, std::memory_order_release);
        if (!nk::core::handles().erase(cache, nk::core::ResourceType::resource_cache))
            return NK_ERROR_INVALID_HANDLE;
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_cache_load(nk_resource_cache cache, const nk_resource *resource,
                                         nk_resource_asset *out_asset) {
    return nk::core::result_boundary("unexpected error while loading a resource into the cache",
                                     [&]() -> nk_result {
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        if (!valid_resource(resource) || !out_asset)
            return invalid_argument("resource cache load arguments are invalid");
        *out_asset = NK_INVALID_HANDLE;
        auto value = get_cache(cache);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;

        const auto found = value->entries.find(resource->uri);
        if (found != value->entries.end())
            return acquire_asset(found->second, out_asset);

        auto entry = make_entry(resource);
        std::vector<std::byte> data;
        const auto read_result = read_resource_bytes(resource, data);
        if (read_result != NK_OK)
            return read_result;
        auto bytes = std::make_shared<std::vector<std::byte>>(std::move(data));
        {
            std::lock_guard lock(entry->mutex);
            entry->data = std::move(bytes);
        }
        entry->load_result.store(NK_OK, std::memory_order_release);
        entry->load_state.store(NK_RESOURCE_ASSET_READY, std::memory_order_release);
        value->entries.emplace(entry->uri, entry);
        const auto result = acquire_asset(entry, out_asset);
        if (result != NK_OK)
            value->entries.erase(entry->uri);
        return result;
    });
}

nk_result NK_CALL nk_resource_cache_load_async(nk_resource_cache cache,
                                               const nk_resource *resource,
                                               nk_resource_asset *out_asset,
                                               nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while starting an asynchronous resource cache load",
        [&]() -> nk_result {
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            if (!valid_resource(resource) || !out_asset || !out_request)
                return invalid_argument("asynchronous resource cache load arguments are invalid");
            *out_asset = NK_INVALID_HANDLE;
            *out_request = NK_INVALID_REQUEST_ID;
            auto value = get_cache(cache);
            if (!value)
                return NK_ERROR_INVALID_HANDLE;

            const auto found = value->entries.find(resource->uri);
            if (found != value->entries.end()) {
                const auto result = acquire_asset(found->second, out_asset);
                if (result == NK_OK &&
                    found->second->load_state.load(std::memory_order_acquire) ==
                        NK_RESOURCE_ASSET_LOADING)
                    *out_request = found->second->load_request.load(std::memory_order_acquire);
                return result;
            }

            auto entry = make_entry(resource);
            value->entries.emplace(entry->uri, entry);
            const auto acquire_result = acquire_asset(entry, out_asset);
            if (acquire_result != NK_OK) {
                value->entries.erase(entry->uri);
                return acquire_result;
            }

            std::unique_ptr<ResourceCacheLoadContext> context;
            try {
                context = std::make_unique<ResourceCacheLoadContext>();
            } catch (const std::bad_alloc &) {
                value->entries.erase(entry->uri);
                discard_asset(*out_asset);
                *out_asset = NK_INVALID_HANDLE;
                nk::core::set_error("could not allocate resource cache load context");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            context->entry = entry;
            nk_request_id request = NK_INVALID_REQUEST_ID;
            const auto load_result = nk::core::start_resource_load(
                resource, &request, resource_cache_load_callback, context.release(),
                resource_cache_load_cleanup);
            if (load_result != NK_OK) {
                value->entries.erase(entry->uri);
                discard_asset(*out_asset);
                *out_asset = NK_INVALID_HANDLE;
                return load_result;
            }
            entry->load_request.store(request, std::memory_order_release);
            *out_request = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_resource_cache_find(nk_resource_cache cache, const char *uri,
                                         nk_resource_asset *out_asset) {
    return nk::core::result_boundary("unexpected error while finding a cached resource",
                                     [&]() -> nk_result {
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        if (!uri || !*uri || !out_asset)
            return invalid_argument("resource cache find arguments are invalid");
        *out_asset = NK_INVALID_HANDLE;
        auto value = get_cache(cache);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        const auto found = value->entries.find(uri);
        if (found == value->entries.end()) {
            nk::core::set_error("resource URI is not present in the cache");
            return NK_ERROR_INVALID_REQUEST;
        }
        return acquire_asset(found->second, out_asset);
    });
}

nk_result NK_CALL nk_resource_cache_remove(nk_resource_cache cache, const char *uri) {
    return nk::core::result_boundary("unexpected error while removing a cached resource",
                                     [&]() -> nk_result {
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        if (!uri || !*uri)
            return invalid_argument("resource cache remove URI is invalid");
        auto value = get_cache(cache);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        const auto found = value->entries.find(uri);
        if (found == value->entries.end()) {
            nk::core::set_error("resource URI is not present in the cache");
            return NK_ERROR_INVALID_REQUEST;
        }
        if (const auto result = cancel_entry_load(found->second); result != NK_OK)
            return result;
        value->entries.erase(found);
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_cache_clear(nk_resource_cache cache) {
    return nk::core::result_boundary("unexpected error while clearing a resource cache",
                                     [&]() -> nk_result {
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        auto value = get_cache(cache);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        return clear_cache(*value);
    });
}

nk_result NK_CALL nk_resource_cache_get_count(nk_resource_cache cache, uint32_t *out_count) {
    return nk::core::result_boundary("unexpected error while querying a resource cache",
                                     [&]() -> nk_result {
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        if (!out_count)
            return invalid_argument("resource cache count output is missing");
        auto value = get_cache(cache);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        if (value->entries.size() > std::numeric_limits<uint32_t>::max())
            return NK_ERROR_OUT_OF_MEMORY;
        *out_count = static_cast<uint32_t>(value->entries.size());
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_asset_destroy(nk_resource_asset asset) {
    return nk::core::result_boundary("unexpected error while destroying a resource asset",
                                     [&]() -> nk_result {
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        auto value = get_asset(asset);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        value->handle.store(NK_INVALID_HANDLE, std::memory_order_release);
        if (!nk::core::handles().erase(asset, nk::core::ResourceType::resource_asset))
            return NK_ERROR_INVALID_HANDLE;
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_asset_get_load_state(nk_resource_asset asset,
                                                    nk_resource_asset_load_state *out_state) {
    return nk::core::result_boundary("unexpected error while querying a resource asset state",
                                     [&]() -> nk_result {
        if (!out_state)
            return invalid_argument("resource asset load state output is missing");
        auto value = get_asset(asset);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        *out_state = value->entry->load_state.load(std::memory_order_acquire);
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_asset_get_result(nk_resource_asset asset, nk_result *out_result) {
    return nk::core::result_boundary("unexpected error while querying a resource asset result",
                                     [&]() -> nk_result {
        if (!out_result)
            return invalid_argument("resource asset result output is missing");
        auto value = get_asset(asset);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        *out_result = value->entry->load_result.load(std::memory_order_acquire);
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_asset_get_size(nk_resource_asset asset, uint64_t *out_size) {
    return nk::core::result_boundary("unexpected error while querying a resource asset size",
                                     [&]() -> nk_result {
        if (!out_size)
            return invalid_argument("resource asset size output is missing");
        auto value = get_asset(asset);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        const auto state = value->entry->load_state.load(std::memory_order_acquire);
        if (state != NK_RESOURCE_ASSET_READY)
            return state == NK_RESOURCE_ASSET_LOADING
                       ? NK_ERROR_INVALID_REQUEST
                       : value->entry->load_result.load(std::memory_order_acquire);
        std::lock_guard lock(value->entry->mutex);
        if (!value->entry->data)
            return NK_ERROR_UNKNOWN;
        *out_size = static_cast<uint64_t>(value->entry->data->size());
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_asset_copy_data(nk_resource_asset asset, void *buffer,
                                               uint64_t *inout_size) {
    return nk::core::result_boundary("unexpected error while copying a resource asset",
                                     [&]() -> nk_result {
        if (!inout_size)
            return invalid_argument("resource asset data size is missing");
        auto value = get_asset(asset);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        const auto state = value->entry->load_state.load(std::memory_order_acquire);
        if (state != NK_RESOURCE_ASSET_READY)
            return state == NK_RESOURCE_ASSET_LOADING
                       ? NK_ERROR_INVALID_REQUEST
                       : value->entry->load_result.load(std::memory_order_acquire);
        std::lock_guard lock(value->entry->mutex);
        if (!value->entry->data)
            return NK_ERROR_UNKNOWN;
        const auto required = static_cast<uint64_t>(value->entry->data->size());
        if (!buffer || *inout_size < required) {
            *inout_size = required;
            return NK_ERROR_BUFFER_TOO_SMALL;
        }
        if (required != 0)
            std::memcpy(buffer, value->entry->data->data(), value->entry->data->size());
        *inout_size = required;
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_asset_get_uri(nk_resource_asset asset, char *buffer,
                                             uint32_t *inout_size) {
    return nk::core::result_boundary("unexpected error while querying a resource asset URI",
                                     [&]() -> nk_result {
        if (!inout_size)
            return invalid_argument("resource asset URI size is missing");
        auto value = get_asset(asset);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        if (value->entry->uri.size() == std::numeric_limits<std::size_t>::max() ||
            value->entry->uri.size() + 1 > std::numeric_limits<uint32_t>::max())
            return NK_ERROR_OUT_OF_MEMORY;
        const auto required = value->entry->uri.size() + 1;
        if (!buffer || *inout_size < required) {
            *inout_size = static_cast<uint32_t>(required);
            return NK_ERROR_BUFFER_TOO_SMALL;
        }
        std::memcpy(buffer, value->entry->uri.c_str(), required);
        *inout_size = static_cast<uint32_t>(required);
        return NK_OK;
    });
}

} // extern "C"
