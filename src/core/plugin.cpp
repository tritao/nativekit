#include "core/plugin.hpp"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/executor.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"
#include "nativekit_plugin.h"

#include <algorithm>
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

constexpr uint32_t plugin_descriptor_v1_size = sizeof(nk_plugin_descriptor);
constexpr uint32_t plugin_service_v1_size = sizeof(nk_plugin_service);
constexpr uint32_t plugin_reply_v1_size = sizeof(nk_plugin_reply);
constexpr uint32_t plugin_event_data_v1_size = sizeof(nk_plugin_event_data);
constexpr std::size_t plugin_name_max = 256;

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
    void *state = nullptr;
    bool destroying = false;
    bool destroyed = false;
};

/** Routed call waiting for the application executor. */
struct PluginCallTask {
    std::uint64_t generation = 0;
    nk_service_id service = 0;
    nk_method_id method = 0;
    nk_request_id request = NK_INVALID_REQUEST_ID;
    std::shared_ptr<const std::vector<std::byte>> payload;
};

std::mutex plugin_mutex;
std::unordered_map<nk_service_id, ServiceEntry> service_table;
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
    if (!plugin || plugin->destroying || plugin->destroyed ||
        plugin->generation != runtime_generation())
        return {};
    return plugin;
}

nk_result push_plugin_event(nk_event_kind kind, nk_plugin_instance instance, nk_service_id service,
                            nk_method_id method, nk_result result, nk_request_id request,
                            nk_handle handle, const void *payload, uint64_t payload_size) noexcept {
    try {
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
        event.data.resize(sizeof(header) + static_cast<std::size_t>(payload_size));
        std::memcpy(event.data.data(), &header, sizeof(header));
        if (payload_size != 0)
            std::memcpy(event.data.data() + sizeof(header), payload,
                        static_cast<std::size_t>(payload_size));
        return push_event(std::move(event));
    } catch (...) {
        set_error("out of memory while queueing a plugin event");
        return NK_ERROR_OUT_OF_MEMORY;
    }
}

nk_plugin_host make_host() {
    nk_plugin_host host{};
    host.struct_size = sizeof(host);
    host.abi_version = NK_PLUGIN_ABI_VERSION;
    host.register_service = &nk_plugin_service_register;
    host.unregister_service = &nk_plugin_service_unregister;
    host.emit_event = &nk_plugin_emit;
    host.dispatch_to_app = &nk_dispatch_to_app;
    host.current_executor = &nk_executor_current;
    host.runtime_generation = &nk_runtime_generation;
    return host;
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
    ServiceEntry service;
    bool found = false;
    {
        std::lock_guard lock(plugin_mutex);
        const auto entry = service_table.find(task.service);
        if (entry != service_table.end()) {
            service = entry->second;
            found = true;
        }
    }
    if (!found) {
        push_plugin_event(NK_EVENT_PLUGIN_COMPLETE, NK_INVALID_HANDLE, task.service, task.method,
                          NK_ERROR_NOT_FOUND, task.request, NK_INVALID_HANDLE, nullptr, 0);
        return;
    }
    auto plugin = live_instance(service.instance);
    if (!plugin || service.generation != runtime_generation() ||
        plugin->generation != service.generation) {
        push_plugin_event(NK_EVENT_PLUGIN_COMPLETE, service.instance, task.service, task.method,
                          NK_ERROR_INVALID_HANDLE, task.request, NK_INVALID_HANDLE, nullptr, 0);
        return;
    }

    const void *request_payload =
        task.payload && !task.payload->empty() ? task.payload->data() : nullptr;
    const std::uint64_t request_size =
        task.payload ? static_cast<std::uint64_t>(task.payload->size()) : 0;
    nk_plugin_reply reply{};
    reply.struct_size = plugin_reply_v1_size;
    nk_result result = NK_ERROR_UNKNOWN;
    if (service.descriptor.invoke) {
        try {
            result = service.descriptor.invoke(service.instance, task.method, request_payload,
                                               request_size, task.request, &reply);
        } catch (...) {
            result = NK_ERROR_UNKNOWN;
        }
    }

    nk_handle reply_handle = NK_INVALID_HANDLE;
    const void *reply_payload = nullptr;
    std::uint64_t reply_size = 0;
    if (result == NK_OK) {
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
    push_plugin_event(NK_EVENT_PLUGIN_COMPLETE, service.instance, task.service, task.method, result,
                      task.request, reply_handle, reply_payload, reply_size);
}

void NK_CALL plugin_call_trampoline(void *user_data) {
    std::unique_ptr<PluginCallTask> task(static_cast<PluginCallTask *>(user_data));
    if (!task || task->generation != runtime_generation())
        return;
    dispatch_plugin_call(*task);
}

} // namespace

void plugins_shutdown() noexcept {
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
        if (!plugin || plugin->destroyed)
            continue;
        plugin->destroying = true;
        if (plugin->destroy) {
            try {
                plugin->destroy(instance);
            } catch (...) {
                /* Plugin teardown must not escape the runtime shutdown path. */
            }
        }
        plugin->destroying = false;
        plugin->destroyed = true;
        plugin->state = nullptr;
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
            nk_plugin_instance created = instance;
            nk_result result = NK_ERROR_UNKNOWN;
            try {
                result = descriptor->create(&host, &created);
            } catch (...) {
                result = NK_ERROR_UNKNOWN;
            }
            if (result != NK_OK || created != instance) {
                nk::core::forget_services(instance);
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
            nk::core::forget_instance(instance, plugin->id);
            plugin->destroying = true;
            if (plugin->destroy) {
                try {
                    plugin->destroy(instance);
                } catch (...) {
                    nk::core::set_error("plugin destroy raised an exception");
                }
            }
            plugin->destroying = false;
            plugin->destroyed = true;
            plugin->state = nullptr;
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
                if (nk::core::service_table.count(entry.descriptor.service_id) != 0) {
                    nk::core::set_error("this service id is already registered");
                    return NK_ERROR_INVALID_ARGUMENT;
                }
                auto inserted =
                    nk::core::service_table.emplace(entry.descriptor.service_id, std::move(entry))
                        .first;
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
            const auto entry = nk::core::service_table.find(service_id);
            if (entry == nk::core::service_table.end() || entry->second.instance != instance) {
                nk::core::set_error("this instance does not own that service");
                return NK_ERROR_NOT_FOUND;
            }
            nk::core::service_table.erase(entry);
            return NK_OK;
        });
}

nk_result NK_CALL nk_plugin_call(nk_service_id service, nk_method_id method, const void *payload,
                                 uint64_t payload_size, nk_request_id *out_request_id) {
    return nk::core::result_boundary(
        "unexpected exception while routing a plugin call", [&]() -> nk_result {
            nk::core::clear_error();
            if (out_request_id)
                *out_request_id = NK_INVALID_REQUEST_ID;
            if (service == 0 || method == 0 || !out_request_id) {
                nk::core::set_error("nk_plugin_call needs a service, a method, and an "
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

            auto task = std::unique_ptr<nk::core::PluginCallTask>(new (std::nothrow)
                                                                      nk::core::PluginCallTask());
            if (!task)
                return NK_ERROR_OUT_OF_MEMORY;
            task->generation = nk::core::runtime_generation();
            task->service = service;
            task->method = method;
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
            const nk_result queued =
                nk::core::dispatch_to_app(&nk::core::plugin_call_trampoline, task.get());
            if (queued != NK_OK)
                return queued;
            task.release();
            *out_request_id = request;
            return NK_OK;
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
                const auto entry = nk::core::service_table.find(service_id);
                if (entry == nk::core::service_table.end() || entry->second.instance != instance) {
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
            plugin->state = state;
            return NK_OK;
        });
}

void *NK_CALL nk_plugin_get_state(nk_plugin_instance instance) {
    if (nk::core::require_executor(NK_EXECUTOR_APP) != NK_OK)
        return nullptr;
    /* Teardown still needs to read its own state while destroy() runs. */
    auto plugin = nk::core::instance_resource(instance);
    return plugin && !plugin->destroyed ? plugin->state : nullptr;
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
            if (!event || !out_view || out_view->struct_size < sizeof(nk_plugin_event_view)) {
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
            std::memcpy(out_view, &view,
                        std::min<std::size_t>(out_view->struct_size, sizeof(view)));
            return NK_OK;
        });
}

} // extern "C"
