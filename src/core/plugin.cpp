#include "core/plugin.hpp"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/executor.hpp"
#include "core/handle_registry.hpp"
#include "core/request.hpp"
#include "core/runtime.hpp"
#include "nativekit_plugin.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nk::core {
namespace {

constexpr uint32_t plugin_descriptor_v1_size = NK_PLUGIN_DESCRIPTOR_V1_SIZE;
constexpr uint32_t plugin_host_v1_size = NK_PLUGIN_HOST_V1_SIZE;
constexpr uint32_t plugin_service_v1_size = NK_PLUGIN_SERVICE_V1_SIZE;
constexpr uint32_t plugin_reply_v1_size = NK_PLUGIN_REPLY_V1_SIZE;
constexpr uint32_t plugin_event_data_v1_size = NK_PLUGIN_EVENT_DATA_V1_SIZE;
constexpr uint32_t plugin_event_view_v1_size = NK_PLUGIN_EVENT_VIEW_V1_SIZE;
constexpr std::size_t plugin_name_max = 256;

struct ServiceKey {
    nk_plugin_instance instance = NK_INVALID_HANDLE;
    nk_service_id service = 0;

    bool operator==(const ServiceKey &other) const {
        return instance == other.instance && service == other.service;
    }
};

struct ServiceKeyHash {
    std::size_t operator()(const ServiceKey &key) const noexcept {
        return std::hash<std::uint32_t>{}(key.instance) ^
               (std::hash<std::uint32_t>{}(key.service) << 1);
    }
};

/** One registered service, copied out of the plugin's own descriptor. */
struct ServiceEntry {
    nk_plugin_instance instance = NK_INVALID_HANDLE;
    std::uint64_t generation = 0;
    std::string name;
    nk_plugin_service descriptor{};
};

class PluginInstanceResource final : public Resource {
  public:
    std::string id;
    std::uint64_t generation = 0;
    nk_plugin_destroy_fn destroy = nullptr;
    std::atomic<void *> state{nullptr};
    std::atomic<bool> destroying{false};
    std::atomic<bool> destroyed{false};

    bool acquire_invocation() noexcept {
        std::lock_guard lock(lifecycle_mutex);
        if (destroying.load(std::memory_order_acquire) || destroyed.load(std::memory_order_acquire))
            return false;
        ++active_invocations;
        return true;
    }

    void release_invocation() noexcept {
        std::lock_guard lock(lifecycle_mutex);
        if (active_invocations != 0 && --active_invocations == 0)
            lifecycle_condition.notify_all();
    }

    void begin_destroy() noexcept {
        std::unique_lock lock(lifecycle_mutex);
        destroying.store(true, std::memory_order_release);
        lifecycle_condition.wait(lock, [this] { return active_invocations == 0; });
    }

    void finish_destroy() noexcept {
        destroyed.store(true, std::memory_order_release);
        state.store(nullptr, std::memory_order_release);
    }

  private:
    std::mutex lifecycle_mutex;
    std::condition_variable lifecycle_condition;
    std::size_t active_invocations = 0;
};

/** Routed call waiting for the application executor. */
struct PluginCallTask {
    std::uint64_t generation = 0;
    nk_request_id request = NK_INVALID_REQUEST_ID;
    std::shared_ptr<const std::vector<std::byte>> payload;
};

constexpr std::size_t plugin_call_queue_overhead = sizeof(PluginCallTask);

constexpr std::uint32_t plugin_request_kind = 1;

std::mutex plugin_mutex;
std::unordered_map<ServiceKey, ServiceEntry, ServiceKeyHash> service_table;
std::unordered_map<std::string, nk_plugin_instance> instances_by_id;
std::vector<nk_plugin_instance> instance_order;

bool bound_service_executor(nk_executor executor) noexcept {
    return executor == NK_EXECUTOR_PLATFORM || executor == NK_EXECUTOR_APP ||
           executor == NK_EXECUTOR_RENDER;
}

std::shared_ptr<PluginInstanceResource> instance_resource(nk_plugin_instance instance) {
    if (instance == NK_INVALID_HANDLE)
        return {};
    auto resource = handles().get(static_cast<nk_handle>(instance), ResourceType::plugin);
    if (!resource)
        return {};
    return std::static_pointer_cast<PluginInstanceResource>(resource);
}

std::shared_ptr<PluginInstanceResource> live_instance(nk_plugin_instance instance) {
    auto plugin = instance_resource(instance);
    if (!plugin || plugin->destroying.load(std::memory_order_acquire) ||
        plugin->destroyed.load(std::memory_order_acquire) ||
        plugin->generation != runtime_generation())
        return {};
    return plugin;
}

nk_result push_plugin_event(nk_event_kind kind, nk_plugin_instance instance, nk_service_id service,
                            nk_method_id method, nk_result result, nk_request_id request,
                            nk_handle handle, const void *payload, uint64_t payload_size) noexcept {
    nk_plugin_event_data header{};
    header.struct_size = plugin_event_data_v1_size;
    if (payload_size != 0)
        header.flags |= NK_PLUGIN_EVENT_HAS_PAYLOAD;
    if (handle != NK_INVALID_HANDLE)
        header.flags |= NK_PLUGIN_EVENT_HAS_HANDLE;
    header.service_id = service;
    header.method_id = method;
    header.handle = handle;
    header.payload_size = payload_size;

    QueuedEvent event;
    event.kind = kind;
    event.source = static_cast<nk_handle>(instance);
    event.flags = header.flags;
    event.request_id = request;
    event.result = result;
    event.data_count = 1;
    event.data.resize(plugin_event_data_v1_size + static_cast<std::size_t>(payload_size));
    std::memcpy(event.data.data(), &header, plugin_event_data_v1_size);
    if (payload_size != 0)
        std::memcpy(event.data.data() + plugin_event_data_v1_size, payload,
                    static_cast<std::size_t>(payload_size));
    return push_event(std::move(event));
}

nk_plugin_host make_host() {
    nk_plugin_host host{};
    host.struct_size = plugin_host_v1_size;
    host.abi_version = NK_PLUGIN_ABI_VERSION;
    host.register_service = &nk_plugin_service_register;
    host.unregister_service = &nk_plugin_service_unregister;
    host.emit_event = &nk_plugin_emit;
    host.dispatch_to_app = &nk_dispatch_to_app;
    host.current_executor = &nk_executor_current;
    host.runtime_generation = &nk_runtime_generation;
    host.complete_request = &nk_plugin_complete;
    return host;
}

bool service_for(nk_plugin_instance instance, nk_service_id service, ServiceEntry &out) {
    std::lock_guard lock(plugin_mutex);
    const auto found = service_table.find(ServiceKey{instance, service});
    if (found == service_table.end())
        return false;
    out = found->second;
    return true;
}

void plugin_task_cleanup(void *user_data) noexcept {
    delete static_cast<PluginCallTask *>(user_data);
}

nk_result complete_request(nk_plugin_instance instance, nk_request_id request, nk_result result,
                           nk_handle handle, const void *payload, uint64_t payload_size) noexcept {
    if (request == NK_INVALID_REQUEST_ID || result == NK_PLUGIN_PENDING) {
        set_error("a plugin completion needs a terminal request result");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (payload_size > NK_PLUGIN_PAYLOAD_MAX) {
        set_error("a plugin completion exceeded the control-plane payload limit");
        return NK_ERROR_PAYLOAD_TOO_LARGE;
    }
    if (payload_size != 0 && !payload) {
        set_error("a plugin completion payload is null but not empty");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    PendingRequest pending;
    if (!requests().take(request, static_cast<nk_handle>(instance), plugin_request_kind,
                         runtime_generation(), pending)) {
        set_error("the plugin request is stale, completed, or owned by another instance");
        return NK_ERROR_INVALID_REQUEST;
    }
    return push_plugin_event(
        NK_EVENT_PLUGIN_COMPLETE, static_cast<nk_plugin_instance>(pending.source),
        static_cast<nk_service_id>(pending.operation), static_cast<nk_method_id>(pending.auxiliary),
        result, request, handle, payload, payload_size);
}

void cancel_plugin_requests(nk_plugin_instance instance, nk_result result) noexcept {
    const auto canceled =
        instance == NK_INVALID_HANDLE
            ? requests().cancel_all(plugin_request_kind)
            : requests().cancel_source(static_cast<nk_handle>(instance), plugin_request_kind);
    for (const auto &pending : canceled)
        push_plugin_event(NK_EVENT_PLUGIN_COMPLETE, static_cast<nk_plugin_instance>(pending.source),
                          static_cast<nk_service_id>(pending.operation),
                          static_cast<nk_method_id>(pending.auxiliary), result, pending.id,
                          NK_INVALID_HANDLE, nullptr, 0);
}

void forget_instance(nk_plugin_instance instance, const std::string &id) noexcept {
    std::lock_guard lock(plugin_mutex);
    auto entry = instances_by_id.find(id);
    if (entry != instances_by_id.end() && entry->second == instance)
        instances_by_id.erase(entry);
    instance_order.erase(std::remove(instance_order.begin(), instance_order.end(), instance),
                         instance_order.end());
}

void forget_services(nk_plugin_instance instance) noexcept {
    std::lock_guard lock(plugin_mutex);
    for (auto entry = service_table.begin(); entry != service_table.end();) {
        if (entry->second.instance == instance)
            entry = service_table.erase(entry);
        else
            ++entry;
    }
}

void dispatch_plugin_call(const PluginCallTask &task) noexcept {
    PendingRequest pending;
    if (!requests().get(task.request, pending) || pending.kind != plugin_request_kind ||
        pending.generation != task.generation)
        return;
    const auto instance = static_cast<nk_plugin_instance>(pending.source);
    const auto service_id = static_cast<nk_service_id>(pending.operation);
    const auto method_id = static_cast<nk_method_id>(pending.auxiliary);
    ServiceEntry service;
    if (!service_for(instance, service_id, service)) {
        complete_request(instance, task.request, NK_ERROR_NOT_FOUND, NK_INVALID_HANDLE, nullptr, 0);
        return;
    }
    auto plugin = live_instance(service.instance);
    if (!plugin || service.generation != runtime_generation() ||
        plugin->generation != service.generation) {
        complete_request(instance, task.request, NK_ERROR_INVALID_HANDLE, NK_INVALID_HANDLE,
                         nullptr, 0);
        return;
    }
    if (!plugin->acquire_invocation()) {
        complete_request(instance, task.request, NK_ERROR_INVALID_REQUEST, NK_INVALID_HANDLE,
                         nullptr, 0);
        return;
    }
    struct InvocationGuard {
        std::shared_ptr<PluginInstanceResource> plugin;
        ~InvocationGuard() { plugin->release_invocation(); }
    } invocation_guard{plugin};

    const void *request_payload =
        task.payload && !task.payload->empty() ? task.payload->data() : nullptr;
    const std::uint64_t request_size =
        task.payload ? static_cast<std::uint64_t>(task.payload->size()) : 0;
    nk_plugin_reply reply{};
    reply.struct_size = plugin_reply_v1_size;
    nk_result result = NK_ERROR_UNKNOWN;
    if (service.descriptor.invoke) {
        result = service.descriptor.invoke(service.instance, method_id, request_payload,
                                           request_size, task.request, &reply);
    }

    nk_handle reply_handle = NK_INVALID_HANDLE;
    const void *reply_payload = nullptr;
    std::uint64_t reply_size = 0;
    if (result == NK_OK) {
        const bool valid_reply =
            reply.struct_size >= plugin_reply_v1_size && reply.flags == 0 && reply.reserved == 0;
        if (!valid_reply) {
            result = NK_ERROR_INVALID_ARGUMENT;
        } else {
            reply_handle = reply.handle;
            reply_payload = reply.payload;
            reply_size = reply.payload_size;
            if (reply_size > NK_PLUGIN_PAYLOAD_MAX) {
                set_error("a plugin reply exceeded the control-plane payload limit");
                reply_handle = NK_INVALID_HANDLE;
                reply_payload = nullptr;
                reply_size = 0;
                result = NK_ERROR_PAYLOAD_TOO_LARGE;
            } else if (reply_size != 0 && !reply_payload) {
                reply_handle = NK_INVALID_HANDLE;
                reply_size = 0;
                result = NK_ERROR_INVALID_ARGUMENT;
            }
        }
    }
    if (result == NK_PLUGIN_PENDING)
        return;
    /* A callback is allowed to complete synchronously; do not publish twice. */
    complete_request(instance, task.request, result, reply_handle, reply_payload, reply_size);
}

void NK_CALL plugin_call_trampoline(void *user_data) {
    auto *task = static_cast<PluginCallTask *>(user_data);
    if (!task || task->generation != runtime_generation())
        return;
    dispatch_plugin_call(*task);
}

} // namespace

void plugins_shutdown() noexcept {
    cancel_plugin_requests(NK_INVALID_HANDLE, NK_ERROR_INVALID_REQUEST);
    std::vector<nk_plugin_instance> instances;
    {
        std::lock_guard lock(plugin_mutex);
        instances.assign(instance_order.rbegin(), instance_order.rend());
        instance_order.clear();
        instances_by_id.clear();
        service_table.clear();
    }
    for (const auto instance : instances) {
        auto plugin = instance_resource(instance);
        if (!plugin || plugin->destroyed.load(std::memory_order_acquire))
            continue;
        plugin->begin_destroy();
        if (plugin->destroy)
            plugin->destroy(instance);
        plugin->finish_destroy();
        handles().erase(static_cast<nk_handle>(instance), ResourceType::plugin);
    }
}

} // namespace nk::core

extern "C" {

nk_result NK_CALL nk_plugin_register(const nk_plugin_descriptor *descriptor,
                                     nk_plugin_instance *out_instance) {
    return nk::core::result_boundary(
        "unexpected exception while registering a plugin", [&]() -> nk_result {
            nk::core::clear_error();
            if (!out_instance) {
                nk::core::set_error("nk_plugin_register requires an output instance");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            *out_instance = NK_INVALID_HANDLE;
            if (!descriptor || descriptor->struct_size < nk::core::plugin_descriptor_v1_size) {
                nk::core::set_error("nk_plugin_descriptor is missing or too small");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (descriptor->abi_version != NK_PLUGIN_ABI_VERSION) {
                nk::core::set_error("unsupported plugin ABI version");
                return NK_ERROR_UNSUPPORTED;
            }
            if (!descriptor->id || descriptor->id[0] == '\0') {
                nk::core::set_error("nk_plugin_descriptor needs a non-empty id");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (std::strlen(descriptor->id) > nk::core::plugin_name_max) {
                nk::core::set_error("plugin id exceeds the supported length");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (!descriptor->create || !descriptor->destroy) {
                nk::core::set_error("nk_plugin_descriptor needs create and destroy");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (const auto result = nk::core::require_executor(NK_EXECUTOR_APP); result != NK_OK)
                return result;

            auto plugin = std::make_shared<nk::core::PluginInstanceResource>();
            plugin->id = descriptor->id;
            plugin->generation = nk::core::runtime_generation();
            plugin->destroy = descriptor->destroy;
            {
                std::lock_guard lock(nk::core::plugin_mutex);
                if (nk::core::instances_by_id.count(plugin->id) != 0) {
                    nk::core::set_error("a plugin with this id is already registered");
                    return NK_ERROR_INVALID_ARGUMENT;
                }
            }
            const nk_handle handle =
                nk::core::handles().insert(nk::core::ResourceType::plugin, plugin);
            if (handle == NK_INVALID_HANDLE) {
                nk::core::set_error("NativeKit could not allocate a plugin instance");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            const auto instance = static_cast<nk_plugin_instance>(handle);
            {
                std::lock_guard lock(nk::core::plugin_mutex);
                nk::core::instances_by_id.emplace(plugin->id, instance);
                nk::core::instance_order.push_back(instance);
            }

            const nk_plugin_host host = nk::core::make_host();
            nk_result result = NK_ERROR_UNKNOWN;
            result = descriptor->create(&host, instance);
            if (result != NK_OK) {
                nk::core::forget_services(instance);
                nk::core::cancel_plugin_requests(instance, NK_ERROR_INVALID_REQUEST);
                nk::core::forget_instance(instance, plugin->id);
                nk::core::handles().erase(handle, nk::core::ResourceType::plugin);
                if (result == NK_OK)
                    result = NK_ERROR_INVALID_ARGUMENT;
                return result;
            }
            *out_instance = instance;
            return NK_OK;
        });
}

nk_result NK_CALL nk_plugin_unregister(nk_plugin_instance instance) {
    return nk::core::result_boundary(
        "unexpected exception while unregistering a plugin", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto result = nk::core::require_executor(NK_EXECUTOR_APP); result != NK_OK)
                return result;
            auto plugin = nk::core::live_instance(instance);
            if (!plugin) {
                nk::core::set_error("nk_plugin_instance is not a live plugin of this "
                                    "runtime");
                return NK_ERROR_INVALID_HANDLE;
            }
            nk::core::forget_services(instance);
            nk::core::cancel_plugin_requests(instance, NK_ERROR_INVALID_REQUEST);
            nk::core::forget_instance(instance, plugin->id);
            plugin->begin_destroy();
            if (plugin->destroy)
                plugin->destroy(instance);
            plugin->finish_destroy();
            nk::core::handles().erase(static_cast<nk_handle>(instance),
                                      nk::core::ResourceType::plugin);
            return NK_OK;
        });
}

nk_result NK_CALL nk_plugin_service_register(nk_plugin_instance instance,
                                             const nk_plugin_service *service) {
    return nk::core::result_boundary(
        "unexpected exception while registering a plugin service", [&]() -> nk_result {
            nk::core::clear_error();
            if (!service || service->struct_size < nk::core::plugin_service_v1_size) {
                nk::core::set_error("nk_plugin_service is missing or too small");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (service->service_id == 0) {
                nk::core::set_error("nk_plugin_service needs a non-zero service_id");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (!service->invoke || service->reserved != 0) {
                nk::core::set_error("nk_plugin_service needs an invoke function and "
                                    "zeroed reserved fields");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (!nk::core::bound_service_executor(service->executor)) {
                nk::core::set_error(service->executor == NK_EXECUTOR_WORKER
                                        ? "worker-executor plugin services are not "
                                          "supported yet"
                                        : "unknown plugin service executor");
                return service->executor == NK_EXECUTOR_WORKER ? NK_ERROR_UNSUPPORTED
                                                               : NK_ERROR_INVALID_ARGUMENT;
            }
            if (service->name && std::strlen(service->name) > nk::core::plugin_name_max) {
                nk::core::set_error("plugin service name exceeds the supported "
                                    "length");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (const auto result = nk::core::require_executor(NK_EXECUTOR_APP); result != NK_OK)
                return result;
            auto plugin = nk::core::live_instance(instance);
            if (!plugin) {
                nk::core::set_error("nk_plugin_instance is not a live plugin of this "
                                    "runtime");
                return NK_ERROR_INVALID_HANDLE;
            }

            nk::core::ServiceEntry entry;
            entry.instance = instance;
            entry.generation = plugin->generation;
            entry.name = service->name ? service->name : "";
            entry.descriptor = *service;
            entry.descriptor.name = nullptr;
            {
                std::lock_guard lock(nk::core::plugin_mutex);
                const nk::core::ServiceKey key{instance, entry.descriptor.service_id};
                if (nk::core::service_table.count(key) != 0) {
                    nk::core::set_error("this instance already registered that service id");
                    return NK_ERROR_INVALID_ARGUMENT;
                }
                auto inserted = nk::core::service_table.emplace(key, std::move(entry)).first;
                inserted->second.descriptor.name =
                    inserted->second.name.empty() ? nullptr : inserted->second.name.c_str();
            }
            return NK_OK;
        });
}

nk_result NK_CALL nk_plugin_service_unregister(nk_plugin_instance instance,
                                               nk_service_id service_id) {
    return nk::core::result_boundary(
        "unexpected exception while unregistering a plugin service", [&]() -> nk_result {
            nk::core::clear_error();
            if (service_id == 0) {
                nk::core::set_error("nk_plugin_service_unregister needs a service "
                                    "identifier");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (const auto result = nk::core::require_executor(NK_EXECUTOR_APP); result != NK_OK)
                return result;
            if (!nk::core::live_instance(instance)) {
                nk::core::set_error("nk_plugin_instance is not a live plugin of this "
                                    "runtime");
                return NK_ERROR_INVALID_HANDLE;
            }
            std::lock_guard lock(nk::core::plugin_mutex);
            const auto entry =
                nk::core::service_table.find(nk::core::ServiceKey{instance, service_id});
            if (entry == nk::core::service_table.end()) {
                nk::core::set_error("this instance does not own that service");
                return NK_ERROR_NOT_FOUND;
            }
            nk::core::service_table.erase(entry);
            return NK_OK;
        });
}

nk_result NK_CALL nk_plugin_call(nk_plugin_instance instance, nk_service_id service,
                                 nk_method_id method, const void *payload, uint64_t payload_size,
                                 nk_request_id *out_request_id) {
    return nk::core::result_boundary(
        "unexpected exception while routing a plugin call", [&]() -> nk_result {
            nk::core::clear_error();
            if (out_request_id)
                *out_request_id = NK_INVALID_REQUEST_ID;
            if (instance == NK_INVALID_HANDLE || service == 0 || method == 0 || !out_request_id) {
                nk::core::set_error("nk_plugin_call needs an instance, a service, a method, and an "
                                    "output request identifier");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (payload_size > NK_PLUGIN_PAYLOAD_MAX) {
                nk::core::set_error("plugin calls carry at most NK_PLUGIN_PAYLOAD_MAX "
                                    "bytes; return a handle for bulk data");
                return NK_ERROR_PAYLOAD_TOO_LARGE;
            }
            if (payload_size != 0 && !payload) {
                nk::core::set_error("plugin call payload is null but not empty");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (nk::core::runtime_generation() == 0) {
                nk::core::set_error("NativeKit is not initialized");
                return NK_ERROR_NOT_INITIALIZED;
            }

            auto plugin = nk::core::live_instance(instance);
            if (!plugin) {
                nk::core::set_error("nk_plugin_instance is not a live plugin of this runtime");
                return NK_ERROR_INVALID_HANDLE;
            }
            nk::core::ServiceEntry service_entry;
            const bool service_found = nk::core::service_for(instance, service, service_entry);
            const nk_executor target = service_found ? service_entry.descriptor.executor
                                                     : static_cast<nk_executor>(NK_EXECUTOR_APP);

            auto task = std::unique_ptr<nk::core::PluginCallTask>(new (std::nothrow)
                                                                      nk::core::PluginCallTask());
            if (!task)
                return NK_ERROR_OUT_OF_MEMORY;
            task->generation = nk::core::runtime_generation();
            task->request = nk::core::next_request_id();
            if (payload_size != 0) {
                auto bytes = std::make_shared<std::vector<std::byte>>(
                    static_cast<std::size_t>(payload_size));
                std::memcpy(bytes->data(), payload, static_cast<std::size_t>(payload_size));
                task->payload = std::move(bytes);
            } else {
                task->payload = std::make_shared<const std::vector<std::byte>>();
            }
            const nk_request_id request = task->request;
            if (!nk::core::requests().begin(nk::core::PendingRequest{
                    request, task->generation, static_cast<nk_handle>(instance),
                    nk::core::plugin_request_kind, service, method}))
                return (nk::core::set_error("NativeKit could not track the plugin request"),
                        NK_ERROR_OUT_OF_MEMORY);
            const nk_result queued = nk::core::dispatch_to_executor(
                target, &nk::core::plugin_call_trampoline, task.get(),
                &nk::core::plugin_task_cleanup,
                nk::core::plugin_call_queue_overhead + static_cast<std::size_t>(payload_size));
            if (queued != NK_OK) {
                nk::core::PendingRequest ignored;
                nk::core::requests().take_any(request, ignored);
                return queued;
            }
            task.release();
            *out_request_id = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_plugin_complete(nk_plugin_instance instance, nk_request_id request_id,
                                     nk_result result, nk_handle handle, const void *payload,
                                     uint64_t payload_size) {
    return nk::core::result_boundary(
        "unexpected exception while completing a plugin request", [&]() -> nk_result {
            nk::core::clear_error();
            if (instance == NK_INVALID_HANDLE || request_id == NK_INVALID_REQUEST_ID)
                return (nk::core::set_error("plugin completion needs an instance and request"),
                        NK_ERROR_INVALID_ARGUMENT);
            if (!nk::core::live_instance(instance)) {
                nk::core::set_error("plugin instance is no longer live");
                return NK_ERROR_INVALID_REQUEST;
            }
            return nk::core::complete_request(instance, request_id, result, handle, payload,
                                              payload_size);
        });
}

nk_result NK_CALL nk_plugin_emit(nk_plugin_instance instance, nk_service_id service_id,
                                 nk_method_id method_id, const void *payload,
                                 uint64_t payload_size) {
    return nk::core::result_boundary(
        "unexpected exception while emitting a plugin event", [&]() -> nk_result {
            nk::core::clear_error();
            if (service_id == 0 || method_id == 0) {
                nk::core::set_error("nk_plugin_emit needs a service and a notification "
                                    "identifier");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (payload_size > NK_PLUGIN_PAYLOAD_MAX) {
                nk::core::set_error("plugin events carry at most "
                                    "NK_PLUGIN_PAYLOAD_MAX bytes; transfer a handle "
                                    "for bulk data");
                return NK_ERROR_PAYLOAD_TOO_LARGE;
            }
            if (payload_size != 0 && !payload) {
                nk::core::set_error("plugin event payload is null but not empty");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (nk::core::runtime_generation() == 0) {
                nk::core::set_error("NativeKit is not initialized");
                return NK_ERROR_NOT_INITIALIZED;
            }
            if (!nk::core::live_instance(instance)) {
                nk::core::set_error("nk_plugin_instance is not a live plugin of this "
                                    "runtime");
                return NK_ERROR_INVALID_HANDLE;
            }
            {
                std::lock_guard lock(nk::core::plugin_mutex);
                const auto entry =
                    nk::core::service_table.find(nk::core::ServiceKey{instance, service_id});
                if (entry == nk::core::service_table.end()) {
                    nk::core::set_error("this instance does not own that service");
                    return NK_ERROR_NOT_FOUND;
                }
            }
            return nk::core::push_plugin_event(NK_EVENT_PLUGIN_EVENT, instance, service_id,
                                               method_id, NK_OK, NK_INVALID_REQUEST_ID,
                                               NK_INVALID_HANDLE, payload, payload_size);
        });
}

nk_result NK_CALL nk_plugin_set_state(nk_plugin_instance instance, void *state) {
    return nk::core::result_boundary(
        "unexpected exception while storing plugin state", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto result = nk::core::require_executor(NK_EXECUTOR_APP); result != NK_OK)
                return result;
            auto plugin = nk::core::live_instance(instance);
            if (!plugin) {
                nk::core::set_error("nk_plugin_instance is not a live plugin of this "
                                    "runtime");
                return NK_ERROR_INVALID_HANDLE;
            }
            plugin->state.store(state, std::memory_order_release);
            return NK_OK;
        });
}

void *NK_CALL nk_plugin_get_state(nk_plugin_instance instance) {
    /* The state slot is atomic so service callbacks may read it on any declared
       invocation executor, while teardown can still read it during destroy(). */
    auto plugin = nk::core::instance_resource(instance);
    return plugin && !plugin->destroyed.load(std::memory_order_acquire)
               ? plugin->state.load(std::memory_order_acquire)
               : nullptr;
}

nk_result NK_CALL nk_plugin_instance_generation(nk_plugin_instance instance,
                                                uint64_t *out_generation) {
    return nk::core::result_boundary(
        "unexpected exception while reading a plugin generation", [&]() -> nk_result {
            nk::core::clear_error();
            if (!out_generation) {
                nk::core::set_error("nk_plugin_instance_generation needs an output "
                                    "generation");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            *out_generation = 0;
            auto plugin = nk::core::live_instance(instance);
            if (!plugin) {
                nk::core::set_error("nk_plugin_instance is not a live plugin of this "
                                    "runtime");
                return NK_ERROR_INVALID_HANDLE;
            }
            *out_generation = plugin->generation;
            return NK_OK;
        });
}

nk_result NK_CALL nk_plugin_event_query(const nk_event *event, nk_plugin_event_view *out_view) {
    return nk::core::result_boundary(
        "unexpected exception while decoding a plugin event", [&]() -> nk_result {
            nk::core::clear_error();
            if (!event || !out_view ||
                out_view->struct_size < nk::core::plugin_event_view_v1_size) {
                nk::core::set_error("nk_plugin_event_query needs an event and a sized "
                                    "view");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (event->kind != NK_EVENT_PLUGIN_COMPLETE && event->kind != NK_EVENT_PLUGIN_EVENT) {
                nk::core::set_error("the event is not a plugin event");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (!event->data || event->data_size < nk::core::plugin_event_data_v1_size) {
                nk::core::set_error("the plugin event payload is missing or too "
                                    "small");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            const auto *header = static_cast<const nk_plugin_event_data *>(event->data);
            if (header->struct_size < nk::core::plugin_event_data_v1_size ||
                header->struct_size > event->data_size ||
                header->payload_size > event->data_size - header->struct_size) {
                nk::core::set_error("the plugin event payload is malformed");
                return NK_ERROR_INVALID_ARGUMENT;
            }

            nk_plugin_event_view view{};
            view.struct_size = out_view->struct_size;
            view.flags = header->flags;
            view.instance = static_cast<nk_plugin_instance>(event->source);
            view.service_id = header->service_id;
            view.method_id = header->method_id;
            view.handle = header->handle;
            view.request_id = event->request_id;
            view.result = event->result;
            view.payload_size = header->payload_size;
            view.payload = header->payload_size != 0
                               ? static_cast<const std::byte *>(event->data) + header->struct_size
                               : nullptr;
            std::memcpy(
                out_view, &view,
                std::min<std::size_t>(out_view->struct_size, nk::core::plugin_event_view_v1_size));
            return NK_OK;
        });
}

} // extern "C"
