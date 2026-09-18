#include "nativekit_sensor.h"

#include "core/error.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"
#include "core/sensor_internal.hpp"
#include "nativekit_time.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct SensorResource final : nk::core::Resource {
    explicit SensorResource(const nk::core::SensorBackendDescriptor &descriptor)
        : info{sizeof(nk_sensor_info),
               descriptor.type,
               descriptor.value_count,
               descriptor.minimum_interval_ns,
               descriptor.maximum_batch_latency_ns,
               descriptor.maximum_range,
               NK_SENSOR_COORDINATE_DEVICE,
               0,
               {0, 0}} {
        latest.struct_size = sizeof(latest);
        latest.type = descriptor.type;
        latest.accuracy = NK_SENSOR_ACCURACY_UNAVAILABLE;
        latest.coordinate_space = NK_SENSOR_COORDINATE_DEVICE;
    }

    nk_sensor handle = NK_INVALID_HANDLE;
    nk_sensor_info info{};
    nk_sensor_sample latest{};
    nk_sensor_options options{};
    std::uint64_t sequence = 0;
    bool started = false;
    mutable std::mutex mutex;
};

std::mutex sensors_mutex;
std::unordered_map<nk_sensor_type, nk_sensor> sensors;

std::shared_ptr<SensorResource> lookup(nk_sensor sensor) {
    return std::dynamic_pointer_cast<SensorResource>(
        nk::core::handles().get(sensor, nk::core::ResourceType::sensor));
}

nk_result require_sensor_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

bool valid_type(nk_sensor_type type) {
    return type >= NK_SENSOR_ACCELEROMETER && type <= NK_SENSOR_DEVICE_MOTION;
}

template <typename T>
nk_result copy_handles(const std::vector<T> &source, T *output, std::uint32_t *inout_count) {
    if (!inout_count) {
        nk::core::set_error("sensor count output is required");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (!output || *inout_count < source.size()) {
        *inout_count = static_cast<std::uint32_t>(source.size());
        return source.empty() ? NK_OK : NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::copy(source.begin(), source.end(), output);
    *inout_count = static_cast<std::uint32_t>(source.size());
    return NK_OK;
}

} // namespace

#if !defined(NK_BACKEND_ANDROID) && !defined(NK_BACKEND_IOS) && !defined(NK_BACKEND_WEB)
namespace nk::core::sensor_backend {
nk_result list(std::vector<SensorBackendDescriptor> &) noexcept {
    return NK_ERROR_UNSUPPORTED;
}
nk_result start(nk_sensor, nk_sensor_type, const nk_sensor_options &) noexcept {
    return NK_ERROR_UNSUPPORTED;
}
nk_result stop(nk_sensor) noexcept {
    return NK_ERROR_UNSUPPORTED;
}
nk_result request_permission(nk_request_id) noexcept {
    return NK_ERROR_UNSUPPORTED;
}
void shutdown() noexcept {}
} // namespace nk::core::sensor_backend
#endif

namespace nk::core {

void sensor_publish(nk_sensor sensor, nk_sensor_type type, const float values[4],
                    nk_sensor_accuracy accuracy) noexcept {
    try {
        const auto resource = lookup(sensor);
        if (!resource || !values)
            return;
        nk_sensor_sample sample{};
        {
            std::lock_guard lock(resource->mutex);
            if (!resource->started || resource->info.type != type)
                return;
            sample = resource->latest;
            sample.struct_size = sizeof(sample);
            sample.timestamp_ns = nk_time_now_ns();
            sample.sequence = ++resource->sequence;
            sample.accuracy = accuracy;
            std::copy(values, values + 4, sample.values);
            resource->latest = sample;
        }
        QueuedEvent event;
        event.kind = NK_EVENT_SENSOR_UPDATE;
        event.source = sensor;
        event.data.resize(sizeof(sample));
        std::memcpy(event.data.data(), &sample, sizeof(sample));
        (void)push_event(std::move(event));
    } catch (...) {
    }
}

void sensor_permission_complete(nk_request_id request, nk_result result,
                                nk_sensor_permission_status status) noexcept {
    try {
        nk_sensor_permission_event payload{sizeof(payload), status, {0, 0, 0}};
        QueuedEvent event;
        event.kind = NK_EVENT_SENSOR_PERMISSION_COMPLETE;
        event.request_id = request;
        event.result = result;
        event.data.resize(sizeof(payload));
        std::memcpy(event.data.data(), &payload, sizeof(payload));
        (void)push_event(std::move(event));
    } catch (...) {
    }
}

} // namespace nk::core

extern "C" {

nk_result NK_CALL nk_sensor_list(nk_sensor *output, std::uint32_t *inout_count) {
    if (const auto result = require_sensor_ui(); result != NK_OK)
        return result;
    std::vector<nk::core::SensorBackendDescriptor> descriptors;
    const auto listed = nk::core::sensor_backend::list(descriptors);
    if (listed != NK_OK) {
        nk::core::set_error("raw sensors are unavailable on this backend");
        return listed;
    }
    std::vector<nk_sensor> handles;
    handles.reserve(descriptors.size());
    std::lock_guard guard(sensors_mutex);
    for (const auto &descriptor : descriptors) {
        if (!valid_type(descriptor.type) || descriptor.value_count == 0 ||
            descriptor.value_count > 4)
            continue;
        nk_sensor handle = NK_INVALID_HANDLE;
        const auto found = sensors.find(descriptor.type);
        if (found != sensors.end() && lookup(found->second)) {
            handle = found->second;
        } else {
            auto resource = std::make_shared<SensorResource>(descriptor);
            handle = nk::core::handles().insert(nk::core::ResourceType::sensor, resource);
            if (!handle) {
                nk::core::set_error("sensor handle registry is full");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            resource->handle = handle;
            sensors[descriptor.type] = handle;
        }
        handles.push_back(handle);
    }
    return copy_handles(handles, output, inout_count);
}

nk_result NK_CALL nk_sensor_get_info(nk_sensor sensor, nk_sensor_info *out_info) {
    if (const auto result = require_sensor_ui(); result != NK_OK)
        return result;
    if (!out_info || out_info->struct_size < sizeof(*out_info)) {
        nk::core::set_error("sensor info output is missing or too small");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto resource = lookup(sensor);
    if (!resource) {
        nk::core::set_error("invalid or stale sensor handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    const auto size = out_info->struct_size;
    *out_info = resource->info;
    out_info->struct_size = size;
    return NK_OK;
}

nk_result NK_CALL nk_sensor_start(nk_sensor sensor, const nk_sensor_options *options) {
    if (const auto result = require_sensor_ui(); result != NK_OK)
        return result;
    const auto resource = lookup(sensor);
    if (!resource) {
        nk::core::set_error("invalid or stale sensor handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    nk_sensor_options selected{};
    selected.struct_size = sizeof(selected);
    selected.coordinate_space = NK_SENSOR_COORDINATE_DEVICE;
    if (options) {
        if (options->struct_size < sizeof(*options)) {
            nk::core::set_error("sensor options are missing or too small");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        selected = *options;
        if (selected.coordinate_space != NK_SENSOR_COORDINATE_DEVICE) {
            nk::core::set_error("unsupported sensor coordinate space");
            return NK_ERROR_UNSUPPORTED;
        }
    }
    {
        std::lock_guard lock(resource->mutex);
        resource->options = selected;
        resource->started = true;
        resource->latest.struct_size = sizeof(resource->latest);
        resource->latest.type = resource->info.type;
        resource->latest.coordinate_space = selected.coordinate_space;
        resource->latest.accuracy = NK_SENSOR_ACCURACY_UNAVAILABLE;
    }
    const auto result = nk::core::sensor_backend::start(sensor, resource->info.type, selected);
    if (result != NK_OK) {
        std::lock_guard lock(resource->mutex);
        resource->started = false;
        nk::core::set_error("sensor could not be started");
        return result;
    }
    return NK_OK;
}

nk_result NK_CALL nk_sensor_stop(nk_sensor sensor) {
    if (const auto result = require_sensor_ui(); result != NK_OK)
        return result;
    const auto resource = lookup(sensor);
    if (!resource) {
        nk::core::set_error("invalid or stale sensor handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    const auto result = nk::core::sensor_backend::stop(sensor);
    if (result != NK_OK && result != NK_ERROR_UNSUPPORTED) {
        nk::core::set_error("sensor could not be stopped");
        return result;
    }
    std::lock_guard lock(resource->mutex);
    resource->started = false;
    return NK_OK;
}

nk_result NK_CALL nk_sensor_get_latest(nk_sensor sensor, nk_sensor_sample *out_sample) {
    if (const auto result = require_sensor_ui(); result != NK_OK)
        return result;
    if (!out_sample || out_sample->struct_size < sizeof(*out_sample)) {
        nk::core::set_error("sensor sample output is missing or too small");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto resource = lookup(sensor);
    if (!resource) {
        nk::core::set_error("invalid or stale sensor handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    std::lock_guard lock(resource->mutex);
    if (!resource->started) {
        nk::core::set_error("sensor has not been started");
        return NK_ERROR_INVALID_REQUEST;
    }
    const auto size = out_sample->struct_size;
    *out_sample = resource->latest;
    out_sample->struct_size = size;
    return NK_OK;
}

nk_result NK_CALL nk_sensor_request_permission(nk_request_id *out_request) {
    if (const auto result = require_sensor_ui(); result != NK_OK)
        return result;
    if (!out_request) {
        nk::core::set_error("sensor permission request output must not be null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    *out_request = NK_INVALID_REQUEST_ID;
    const auto request = nk::core::next_request_id();
    const auto result = nk::core::sensor_backend::request_permission(request);
    if (result != NK_OK) {
        nk::core::set_error("sensor permission is unavailable");
        return result;
    }
    *out_request = request;
    return NK_OK;
}

} // extern "C"
