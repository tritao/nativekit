#ifndef NATIVEKIT_SENSOR_H
#define NATIVEKIT_SENSOR_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A generation-checked handle for one enumerated physical sensor. */
typedef uint32_t nk_sensor NK_HANDLE;

typedef uint32_t nk_sensor_type;
enum NK_ENUM(nk_sensor_type) {
    NK_SENSOR_ACCELEROMETER = 1,
    NK_SENSOR_GYROSCOPE = 2,
    NK_SENSOR_MAGNETOMETER = 3,
    NK_SENSOR_GRAVITY = 4,
    NK_SENSOR_LINEAR_ACCELERATION = 5,
    NK_SENSOR_ROTATION_VECTOR = 6,
    NK_SENSOR_DEVICE_MOTION = 7
};

/** The coordinate space used by all NativeKit sensor values. */
typedef uint32_t nk_sensor_coordinate_space;
enum NK_ENUM(nk_sensor_coordinate_space) {
    /** Device-local right-handed axes: +X right, +Y toward the top edge,
     * and +Z out of the display toward the user. */
    NK_SENSOR_COORDINATE_DEVICE = 1
};

typedef uint32_t nk_sensor_accuracy;
enum NK_ENUM(nk_sensor_accuracy) {
    NK_SENSOR_ACCURACY_UNAVAILABLE = 0,
    NK_SENSOR_ACCURACY_UNRELIABLE = 1,
    NK_SENSOR_ACCURACY_LOW = 2,
    NK_SENSOR_ACCURACY_MEDIUM = 3,
    NK_SENSOR_ACCURACY_HIGH = 4
};

typedef uint32_t nk_sensor_permission_status;
enum NK_ENUM(nk_sensor_permission_status) {
    NK_SENSOR_PERMISSION_GRANTED = 1,
    NK_SENSOR_PERMISSION_DENIED = 2,
    NK_SENSOR_PERMISSION_UNAVAILABLE = 3,
    NK_SENSOR_PERMISSION_NOT_REQUIRED = 4
};

/** Static information for one sensor returned by nk_sensor_get_info(). */
typedef struct nk_sensor_info {
    uint32_t struct_size NK_STRUCT_SIZE;
    nk_sensor_type type;
    uint32_t value_count;
    uint64_t minimum_interval_ns;
    uint64_t maximum_batch_latency_ns;
    float maximum_range;
    nk_sensor_coordinate_space coordinate_space;
    uint32_t reserved;
    uint64_t reserved2[2];
} nk_sensor_info;

/** Requested sampling configuration. Zero timing fields select backend defaults. */
typedef struct nk_sensor_options {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint64_t sample_interval_ns;
    uint64_t maximum_batch_latency_ns;
    nk_sensor_coordinate_space coordinate_space;
    uint32_t reserved;
    uint64_t reserved2[2];
} nk_sensor_options;

/** Fixed, pointer-free payload of NK_EVENT_SENSOR_UPDATE. Values are SI units:
 * m/s², radians/sec, microtesla, or a normalized quaternion (x,y,z,w). */
typedef struct nk_sensor_sample {
    uint32_t struct_size NK_STRUCT_SIZE;
    nk_sensor_type type;
    nk_sensor_accuracy accuracy;
    nk_sensor_coordinate_space coordinate_space;
    uint64_t timestamp_ns;
    uint64_t sequence;
    float values[4];
    uint32_t reserved[2];
} nk_sensor_sample;

/** Fixed, pointer-free payload of NK_EVENT_SENSOR_PERMISSION_COMPLETE. */
typedef struct nk_sensor_permission_event {
    uint32_t struct_size NK_STRUCT_SIZE;
    nk_sensor_permission_status status;
    uint32_t reserved[3];
} nk_sensor_permission_event;

/** Writes the currently available generation-checked sensor handles. */
NK_API nk_result NK_CALL nk_sensor_list(nk_sensor *sensors, uint32_t *inout_count);
/** Returns static information for an enumerated sensor. */
NK_API nk_result NK_CALL nk_sensor_get_info(nk_sensor sensor, nk_sensor_info *out_info NK_OUT);
/** Starts a sensor with the requested sampling configuration. */
NK_API nk_result NK_CALL nk_sensor_start(nk_sensor sensor, const nk_sensor_options *options);
/** Stops a sensor; its last sample remains queryable until the next start. */
NK_API nk_result NK_CALL nk_sensor_stop(nk_sensor sensor);
/** Returns the latest sample after a sensor has been started. */
NK_API nk_result NK_CALL nk_sensor_get_latest(nk_sensor sensor,
                                              nk_sensor_sample *out_sample NK_OUT);
/** Asynchronously requests motion permission where the platform requires it. */
NK_API nk_result NK_CALL nk_sensor_request_permission(nk_request_id *out_request NK_OUT);

#ifdef __cplusplus
}
#endif

#endif
