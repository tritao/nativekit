#pragma once

#include "nativekit_sensor.h"

#include <cstdint>
#include <vector>

namespace nk::core {

struct SensorBackendDescriptor {
    nk_sensor_type type = 0;
    std::uint32_t value_count = 0;
    std::uint64_t minimum_interval_ns = 0;
    std::uint64_t maximum_batch_latency_ns = 0;
    float maximum_range = 0.0f;
};

namespace sensor_backend {
nk_result list(std::vector<SensorBackendDescriptor> &out) noexcept;
nk_result start(nk_sensor sensor, nk_sensor_type type, const nk_sensor_options &options) noexcept;
nk_result stop(nk_sensor sensor) noexcept;
nk_result request_permission(nk_request_id request) noexcept;
void shutdown() noexcept;
} // namespace sensor_backend

void sensor_publish(nk_sensor sensor, nk_sensor_type type, const float values[4],
                    nk_sensor_accuracy accuracy) noexcept;
void sensor_permission_complete(nk_request_id request, nk_result result,
                                nk_sensor_permission_status status) noexcept;

} // namespace nk::core
