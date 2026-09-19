#import <CoreMotion/CoreMotion.h>
#import <UIKit/UIKit.h>

#include "nativekit_haptics.h"

#include "core/haptics_internal.hpp"
#include "core/sensor_internal.hpp"

#include <cmath>
#include <cstdint>
#include <dispatch/dispatch.h>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr float gravity = 9.80665f;
CMMotionManager *motion_manager = nil;
std::unordered_map<nk_sensor, nk_sensor_type> active_sensors;
std::unordered_set<nk_request_id> pending_permissions;

void ensure_manager() {
    if (!motion_manager)
        motion_manager = [CMMotionManager new];
}

double interval_seconds(const nk_sensor_options &options) {
    return options.sample_interval_ns == 0
               ? 1.0 / 60.0
               : std::max(0.001, static_cast<double>(options.sample_interval_ns) / 1.0e9);
}

void complete_permissions(nk_sensor_permission_status status) {
    const auto pending = std::move(pending_permissions);
    pending_permissions.clear();
    for (const auto request : pending)
        nk::core::sensor_permission_complete(request, NK_OK, status);
}

bool active_type(nk_sensor_type type) {
    for (const auto &[handle, active] : active_sensors) {
        (void)handle;
        if (active == type)
            return true;
    }
    return false;
}

} // namespace

namespace nk::core::sensor_backend {

nk_result list(std::vector<SensorBackendDescriptor> &out) noexcept {
    ensure_manager();
    out.clear();
    if (motion_manager.accelerometerAvailable)
        out.push_back({NK_SENSOR_ACCELEROMETER, 3, 1000000ULL, 100000000ULL, 100.0f});
    if (motion_manager.gyroAvailable)
        out.push_back({NK_SENSOR_GYROSCOPE, 3, 1000000ULL, 100000000ULL, 100.0f});
    if (motion_manager.magnetometerAvailable)
        out.push_back({NK_SENSOR_MAGNETOMETER, 3, 1000000ULL, 100000000ULL, 2000.0f});
    if (motion_manager.deviceMotionAvailable) {
        out.push_back({NK_SENSOR_GRAVITY, 3, 1000000ULL, 100000000ULL, 100.0f});
        out.push_back({NK_SENSOR_LINEAR_ACCELERATION, 3, 1000000ULL, 100000000ULL, 100.0f});
        out.push_back({NK_SENSOR_ROTATION_VECTOR, 4, 1000000ULL, 100000000ULL, 1.0f});
        out.push_back({NK_SENSOR_DEVICE_MOTION, 4, 1000000ULL, 100000000ULL, 1.0f});
    }
    return NK_OK;
}

nk_result start(nk_sensor sensor, nk_sensor_type type, const nk_sensor_options &options) noexcept {
    @autoreleasepool {
        ensure_manager();
        const auto interval = interval_seconds(options);
        motion_manager.deviceMotionUpdateInterval = interval;
        motion_manager.accelerometerUpdateInterval = interval;
        motion_manager.gyroUpdateInterval = interval;
        motion_manager.magnetometerUpdateInterval = interval;
        switch (type) {
        case NK_SENSOR_ACCELEROMETER:
            if (!motion_manager.accelerometerAvailable)
                return NK_ERROR_UNSUPPORTED;
            [motion_manager
                startAccelerometerUpdatesToQueue:[NSOperationQueue mainQueue]
                                     withHandler:^(CMAccelerometerData *data, NSError *) {
                                       if (!data)
                                           return;
                                       const float values[4] = {
                                           static_cast<float>(data.acceleration.x * gravity),
                                           static_cast<float>(data.acceleration.y * gravity),
                                           static_cast<float>(data.acceleration.z * gravity), 0};
                                       nk::core::sensor_publish(sensor, type, values,
                                                                NK_SENSOR_ACCURACY_HIGH);
                                     }];
            break;
        case NK_SENSOR_GYROSCOPE:
            if (!motion_manager.gyroAvailable)
                return NK_ERROR_UNSUPPORTED;
            [motion_manager
                startGyroUpdatesToQueue:[NSOperationQueue mainQueue]
                            withHandler:^(CMGyroData *data, NSError *) {
                              if (!data)
                                  return;
                              const float values[4] = {static_cast<float>(data.rotationRate.x),
                                                       static_cast<float>(data.rotationRate.y),
                                                       static_cast<float>(data.rotationRate.z), 0};
                              nk::core::sensor_publish(sensor, type, values,
                                                       NK_SENSOR_ACCURACY_HIGH);
                            }];
            break;
        case NK_SENSOR_MAGNETOMETER:
            if (!motion_manager.magnetometerAvailable)
                return NK_ERROR_UNSUPPORTED;
            [motion_manager startMagnetometerUpdatesToQueue:[NSOperationQueue mainQueue]
                                                withHandler:^(CMMagnetometerData *data, NSError *) {
                                                  if (!data)
                                                      return;
                                                  const float values[4] = {
                                                      static_cast<float>(data.magneticField.x),
                                                      static_cast<float>(data.magneticField.y),
                                                      static_cast<float>(data.magneticField.z), 0};
                                                  nk::core::sensor_publish(sensor, type, values,
                                                                           NK_SENSOR_ACCURACY_HIGH);
                                                }];
            break;
        case NK_SENSOR_GRAVITY:
        case NK_SENSOR_LINEAR_ACCELERATION:
        case NK_SENSOR_ROTATION_VECTOR:
        case NK_SENSOR_DEVICE_MOTION:
            if (!motion_manager.deviceMotionAvailable)
                return NK_ERROR_UNSUPPORTED;
            [motion_manager
                startDeviceMotionUpdatesToQueue:[NSOperationQueue mainQueue]
                                    withHandler:^(CMDeviceMotion *data, NSError *) {
                                      if (!data)
                                          return;
                                      for (const auto &[handle, active] : active_sensors) {
                                          float values[4] = {};
                                          if (active == NK_SENSOR_GRAVITY) {
                                              values[0] = data.gravity.x * gravity;
                                              values[1] = data.gravity.y * gravity;
                                              values[2] = data.gravity.z * gravity;
                                          } else if (active == NK_SENSOR_LINEAR_ACCELERATION) {
                                              values[0] = data.userAcceleration.x * gravity;
                                              values[1] = data.userAcceleration.y * gravity;
                                              values[2] = data.userAcceleration.z * gravity;
                                          } else {
                                              values[0] = data.attitude.quaternion.x;
                                              values[1] = data.attitude.quaternion.y;
                                              values[2] = data.attitude.quaternion.z;
                                              values[3] = data.attitude.quaternion.w;
                                          }
                                          nk::core::sensor_publish(handle, active, values,
                                                                   NK_SENSOR_ACCURACY_HIGH);
                                      }
                                    }];
            break;
        default:
            return NK_ERROR_UNSUPPORTED;
        }
        active_sensors[sensor] = type;
        return NK_OK;
    }
}

nk_result stop(nk_sensor sensor) noexcept {
    active_sensors.erase(sensor);
    if (!active_type(NK_SENSOR_ACCELEROMETER))
        [motion_manager stopAccelerometerUpdates];
    if (!active_type(NK_SENSOR_GYROSCOPE))
        [motion_manager stopGyroUpdates];
    if (!active_type(NK_SENSOR_MAGNETOMETER))
        [motion_manager stopMagnetometerUpdates];
    if (!active_type(NK_SENSOR_GRAVITY) && !active_type(NK_SENSOR_LINEAR_ACCELERATION) &&
        !active_type(NK_SENSOR_ROTATION_VECTOR) && !active_type(NK_SENSOR_DEVICE_MOTION))
        [motion_manager stopDeviceMotionUpdates];
    return NK_OK;
}

nk_result request_permission(nk_request_id request) noexcept {
    @autoreleasepool {
        ensure_manager();
        if (!motion_manager.deviceMotionAvailable) {
            sensor_permission_complete(request, NK_OK, NK_SENSOR_PERMISSION_UNAVAILABLE);
            return NK_OK;
        }
        if (!active_sensors.empty()) {
            sensor_permission_complete(request, NK_OK, NK_SENSOR_PERMISSION_GRANTED);
            return NK_OK;
        }
        pending_permissions.insert(request);
        [motion_manager startDeviceMotionUpdatesToQueue:[NSOperationQueue mainQueue]
                                            withHandler:^(CMDeviceMotion *, NSError *) {
                                              complete_permissions(NK_SENSOR_PERMISSION_GRANTED);
                                              if (!active_type(NK_SENSOR_GRAVITY) &&
                                                  !active_type(NK_SENSOR_LINEAR_ACCELERATION) &&
                                                  !active_type(NK_SENSOR_ROTATION_VECTOR) &&
                                                  !active_type(NK_SENSOR_DEVICE_MOTION))
                                                  [motion_manager stopDeviceMotionUpdates];
                                            }];
        return NK_OK;
    }
}

void shutdown() noexcept {
    @autoreleasepool {
        active_sensors.clear();
        pending_permissions.clear();
        [motion_manager stopAccelerometerUpdates];
        [motion_manager stopGyroUpdates];
        [motion_manager stopMagnetometerUpdates];
        [motion_manager stopDeviceMotionUpdates];
        motion_manager = nil;
    }
}

} // namespace nk::core::sensor_backend

namespace nk::core::haptics_backend {

nk_result vibrate(const nk_haptic_vibration &options) noexcept {
    if (@available(iOS 10.0, *)) {
        UIImpactFeedbackGenerator *generator =
            [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium];
        [generator prepare];
        if (@available(iOS 13.0, *))
            [generator impactOccurredWithIntensity:options.intensity];
        else
            [generator impactOccurred];
        return NK_OK;
    }
    return NK_ERROR_UNSUPPORTED;
}

nk_result stop_vibration() noexcept {
    return NK_OK;
}
} // namespace nk::core::haptics_backend
