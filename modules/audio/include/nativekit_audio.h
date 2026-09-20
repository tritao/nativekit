#ifndef NATIVEKIT_AUDIO_H
#define NATIVEKIT_AUDIO_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit.h"
#include "nativekit_resource.h"

/* ------------------------------------------------------------------------- */
/* Export visibility                                                         */
/* ------------------------------------------------------------------------- */

#if defined(_WIN32)
#if defined(NK_STATIC)
#define NKAUDIO_API
#elif defined(NKAUDIO_BUILDING_LIBRARY)
#define NKAUDIO_API __declspec(dllexport)
#else
#define NKAUDIO_API __declspec(dllimport)
#endif
#else
#define NKAUDIO_API __attribute__((visibility("default")))
#endif

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Audio types and voice options                                             */
/* ------------------------------------------------------------------------- */

/** Opaque handle for one reusable audio clip source. */
typedef uint32_t nk_audio_clip NK_HANDLE NK_HANDLE_DESTROY(nk_audio_clip_destroy);

/** Opaque handle for one independent playback voice. */
typedef uint32_t nk_audio_voice NK_HANDLE NK_HANDLE_DESTROY(nk_audio_voice_destroy);

/** Flags controlling how a voice is loaded and played. */
typedef uint32_t nk_audio_voice_flags;
enum NK_FLAGS(nk_audio_voice_flags) {
    /** Repeats the voice after it reaches its end. */
    NK_AUDIO_VOICE_LOOPING = 1u << 0,
    /** Streams the source instead of decoding the complete source up front. */
    NK_AUDIO_VOICE_STREAM = 1u << 1,
    /** Loads the source asynchronously when the backend supports it. */
    NK_AUDIO_VOICE_ASYNC = 1u << 2
};

/** Loading lifecycle for an audio voice. Synchronous voices start ready. */
typedef uint32_t nk_audio_voice_load_state;
enum NK_ENUM(nk_audio_voice_load_state) {
    /** The asynchronous source is still loading. */
    NK_AUDIO_VOICE_LOADING = 0,
    /** The source has reached its playable readiness point. */
    NK_AUDIO_VOICE_READY = 1,
    /** The source failed to load; the failure event carries the result code. */
    NK_AUDIO_VOICE_LOAD_FAILED = 2
};

/** Three-dimensional audio coordinate in the right-handed OpenGL convention. */
typedef struct nk_audio_vec3 {
    /** Positive X points right. */
    float x;
    /** Positive Y points up. */
    float y;
    /** Negative Z points forward. */
    float z;
} nk_audio_vec3;

/** Distance attenuation model applied by a spatialized voice. */
typedef uint32_t nk_audio_attenuation_model;
enum NK_ENUM(nk_audio_attenuation_model) {
    /** No distance attenuation. */
    NK_AUDIO_ATTENUATION_NONE = 0,
    /** Inverse-distance attenuation, clamped to the configured distances. */
    NK_AUDIO_ATTENUATION_INVERSE = 1,
    /** Linear attenuation between the configured distances. */
    NK_AUDIO_ATTENUATION_LINEAR = 2,
    /** Exponential attenuation between the configured distances. */
    NK_AUDIO_ATTENUATION_EXPONENTIAL = 3
};

/** Coordinate space used by a voice's spatial position. */
typedef uint32_t nk_audio_positioning;
enum NK_ENUM(nk_audio_positioning) {
    /** The voice position is expressed in world space. */
    NK_AUDIO_POSITIONING_ABSOLUTE = 0,
    /** The voice position is expressed relative to the listener. */
    NK_AUDIO_POSITIONING_RELATIVE = 1
};

/** Selects the backend's default playback device. */
#define NK_AUDIO_DEVICE_DEFAULT UINT32_MAX

/** Lifecycle state of the process-wide audio playback device. */
typedef uint32_t nk_audio_device_state;
enum NK_ENUM(nk_audio_device_state) {
    /** The engine has not initialized an audio device yet. */
    NK_AUDIO_DEVICE_UNINITIALIZED = 0,
    /** The audio device is initialized but not processing audio. */
    NK_AUDIO_DEVICE_STOPPED = 1,
    /** The audio device is processing audio. */
    NK_AUDIO_DEVICE_STARTED = 2,
    /** The audio device is transitioning to the started state. */
    NK_AUDIO_DEVICE_STARTING = 3,
    /** The audio device is transitioning to the stopped state. */
    NK_AUDIO_DEVICE_STOPPING = 4,
    /** The backend reported an interruption which has not ended yet. */
    NK_AUDIO_DEVICE_INTERRUPTED = 5
};

/** Configuration applied when NativeKit lazily creates its audio device. */
typedef struct nk_audio_device_options {
    /** Set to sizeof(nk_audio_device_options) before passing the structure. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Playback device index returned by nk_audio_device_get_count/name; use
     * NK_AUDIO_DEVICE_DEFAULT to select the backend default. */
    uint32_t playback_device_index;
    /** Requested output sample rate, or zero for the backend default. */
    uint32_t sample_rate;
    /** Requested output channel count, or zero for the backend default. */
    uint32_t channels;
    /** Requested device period in PCM frames, or zero for the backend default. */
    uint32_t period_size_in_frames;
    /** Requested device period in milliseconds, or zero for the backend default. */
    uint32_t period_size_in_milliseconds;
    /** Do not start the device automatically during engine initialization. */
    nk_bool no_auto_start;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_device_options;

/** Optional creation settings for a voice. */
typedef struct nk_audio_voice_options {
    /** Set to sizeof(nk_audio_voice_options) before passing the structure. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Combination of NK_AUDIO_VOICE_* flags. */
    nk_audio_voice_flags flags;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_voice_options;

/** Use the voice's current fader volume as the start of a fade. */
#define NK_AUDIO_VOLUME_CURRENT (-1.0f)

/* ------------------------------------------------------------------------- */
/* Audio device lifecycle                                                    */
/* ------------------------------------------------------------------------- */

/**
 * Configures the process-wide playback device before the first audio engine
 * use. Configuration becomes immutable for the runtime generation once the
 * engine has been initialized; call nk_shutdown() before configuring a new
 * device. Device indices are snapshots and may change when devices are added
 * or removed.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_device_configure(const nk_audio_device_options *options);
/** Returns the number of currently enumerated playback devices. */
NKAUDIO_API nk_result NK_CALL nk_audio_device_get_count(uint32_t *out_count NK_OUT);
/**
 * Copies one currently enumerated playback device name into the caller's
 * UTF-8 buffer. The required size, including the NUL terminator, is always
 * returned through inout_size.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_device_get_name(uint32_t index,
                                                       char *buffer NK_OUT_BUFFER(inout_size),
                                                       uint32_t *inout_size NK_INOUT);
/** Returns whether one currently enumerated playback device is the backend default. */
NKAUDIO_API nk_result NK_CALL nk_audio_device_is_default(uint32_t index,
                                                         nk_bool *out_default NK_OUT);
/** Starts the process-wide audio playback device. */
NKAUDIO_API nk_result NK_CALL nk_audio_device_start(void);
/** Stops the process-wide audio playback device without destroying the engine graph. */
NKAUDIO_API nk_result NK_CALL nk_audio_device_stop(void);
/** Stops and starts the process-wide audio playback device to recover from an interruption. */
NKAUDIO_API nk_result NK_CALL nk_audio_device_restart(void);
/** Returns the current process-wide audio playback device state. */
NKAUDIO_API nk_result NK_CALL nk_audio_device_get_state(nk_audio_device_state *out_state NK_OUT);
/* ------------------------------------------------------------------------- */
/* Clip and voice lifetime and transport                                     */
/* ------------------------------------------------------------------------- */

/**
 * Creates a reusable clip from a UTF-8 native filesystem path. NativeKit
 * copies the path and validates the source before returning. The clip is
 * stopped and has no playback state; create a voice to play it.
 */
NKAUDIO_API nk_result NK_CALL
nk_audio_clip_create_from_file(const char *path NK_UTF8, nk_audio_clip *out_clip NK_OUT NK_OWNED);

/**
 * Creates a reusable clip from encoded audio bytes. NativeKit copies the
 * bytes before returning, so the caller may release its buffer after the
 * call. The source currently supports WAV, FLAC, and MP3.
 */
NKAUDIO_API nk_result NK_CALL
nk_audio_clip_create_from_memory(const void *data NK_BORROWED_BUFFER(data_size), uint64_t data_size,
                                 nk_audio_clip *out_clip NK_OUT NK_OWNED);

/**
 * Creates a reusable clip from a ready asset in the core resource cache.
 * NativeKit retains the immutable encoded bytes; the asset may be destroyed
 * after this call returns.
 */
NKAUDIO_API nk_result NK_CALL
nk_audio_clip_create_from_asset(nk_resource_asset asset, nk_audio_clip *out_clip NK_OUT NK_OWNED);

/**
 * Creates a reusable streaming clip from a readable URI resource descriptor.
 * Each voice opens and owns an independent provider stream; the complete
 * encoded resource is never retained by the clip. With NK_AUDIO_VOICE_ASYNC,
 * the voice becomes ready when its first decoded page is available.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_clip_create_from_stream(
    const nk_resource *resource, nk_audio_clip *out_clip NK_OUT NK_OWNED);

/** Stops using and destroys a clip. Existing voices retain their source. */
NKAUDIO_API nk_result NK_CALL nk_audio_clip_destroy(nk_audio_clip clip);

/** Creates a stopped independent playback voice from a reusable clip. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_create(nk_audio_clip clip,
                                                    const nk_audio_voice_options *options,
                                                    nk_audio_voice *out_voice NK_OUT NK_OWNED);

/** Stops and destroys a playback voice, invalidating its handle. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_destroy(nk_audio_voice voice);

/** Starts a voice from its current position. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_start(nk_audio_voice voice);
/** Stops a voice without rewinding it. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_stop(nk_audio_voice voice);
/** Seeks a voice back to its beginning without changing its playing state. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_rewind(nk_audio_voice voice);

/**
 * Schedules a voice to start at an absolute process-wide audio time in PCM
 * frames. Call nk_audio_voice_start() after setting the schedule.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_schedule_start(nk_audio_voice voice,
                                                            uint64_t absolute_time_pcm_frames);
/**
 * Schedules a voice to stop at an absolute process-wide audio time in PCM
 * frames. A scheduled stop does not emit a natural-end completion event.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_schedule_stop(nk_audio_voice voice,
                                                           uint64_t absolute_time_pcm_frames);
/** Clears a voice's scheduled start, stop, and fade transitions. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_clear_schedule(nk_audio_voice voice);

/**
 * Fades a voice over a duration in PCM frames. volume_begin may be
 * NK_AUDIO_VOLUME_CURRENT; volume_end must be finite and non-negative.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_fade(nk_audio_voice voice, float volume_begin,
                                                  float volume_end, uint64_t duration_pcm_frames);
/** Fades a voice starting at an absolute process-wide audio time in PCM frames. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_fade_at(nk_audio_voice voice, float volume_begin,
                                                     float volume_end, uint64_t duration_pcm_frames,
                                                     uint64_t absolute_start_time_pcm_frames);
/** Returns whether a voice is currently playing. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_is_playing(nk_audio_voice voice,
                                                        nk_bool *out_playing NK_OUT);
/** Returns the asynchronous loading lifecycle state of a voice. */
NKAUDIO_API nk_result NK_CALL
nk_audio_voice_get_load_state(nk_audio_voice voice, nk_audio_voice_load_state *out_state NK_OUT);
/** Returns whether a voice has reached its end. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_at_end(nk_audio_voice voice,
                                                    nk_bool *out_at_end NK_OUT);
/** Sets linear voice gain; zero mutes the voice. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_volume(nk_audio_voice voice, float volume);
/** Returns linear voice gain. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_volume(nk_audio_voice voice,
                                                        float *out_volume NK_OUT);
/** Sets stereo voice pan in the range [-1, 1]. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_pan(nk_audio_voice voice, float pan);
/** Returns stereo voice pan in the range [-1, 1]. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_pan(nk_audio_voice voice, float *out_pan NK_OUT);
/** Sets voice playback pitch where 1 is the source pitch. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_pitch(nk_audio_voice voice, float pitch);
/** Returns voice playback pitch. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_pitch(nk_audio_voice voice,
                                                       float *out_pitch NK_OUT);
/** Enables or disables voice looping. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_looping(nk_audio_voice voice, nk_bool looping);
/** Returns whether voice looping is enabled. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_is_looping(nk_audio_voice voice,
                                                        nk_bool *out_looping NK_OUT);
/** Returns the current voice playback position in seconds. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_time_seconds(nk_audio_voice voice,
                                                              float *out_seconds NK_OUT);
/** Returns the decoded voice length in seconds. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_length_seconds(nk_audio_voice voice,
                                                                float *out_seconds NK_OUT);

/* ------------------------------------------------------------------------- */
/* Spatial audio                                                             */
/* ------------------------------------------------------------------------- */

/** Sets the single process-wide listener's world position. */
NKAUDIO_API nk_result NK_CALL nk_audio_listener_set_position(nk_audio_vec3 position);
/** Returns the single process-wide listener's world position. */
NKAUDIO_API nk_result NK_CALL nk_audio_listener_get_position(nk_audio_vec3 *out_position NK_OUT);
/** Sets the listener's forward direction; it must be finite and non-zero. */
NKAUDIO_API nk_result NK_CALL nk_audio_listener_set_direction(nk_audio_vec3 direction);
/** Returns the listener's forward direction. */
NKAUDIO_API nk_result NK_CALL nk_audio_listener_get_direction(nk_audio_vec3 *out_direction NK_OUT);
/** Sets the listener's velocity in world units per second. */
NKAUDIO_API nk_result NK_CALL nk_audio_listener_set_velocity(nk_audio_vec3 velocity);
/** Returns the listener's velocity in world units per second. */
NKAUDIO_API nk_result NK_CALL nk_audio_listener_get_velocity(nk_audio_vec3 *out_velocity NK_OUT);
/** Sets the listener's world-up direction; it must be finite and non-zero. */
NKAUDIO_API nk_result NK_CALL nk_audio_listener_set_world_up(nk_audio_vec3 world_up);
/** Returns the listener's world-up direction. */
NKAUDIO_API nk_result NK_CALL nk_audio_listener_get_world_up(nk_audio_vec3 *out_world_up NK_OUT);
/** Sets the listener's directional attenuation cone in radians and linear gain. */
NKAUDIO_API nk_result NK_CALL nk_audio_listener_set_cone(float inner_angle_radians,
                                                         float outer_angle_radians,
                                                         float outer_gain);
/** Returns the listener's directional attenuation cone. */
NKAUDIO_API nk_result NK_CALL nk_audio_listener_get_cone(float *out_inner_angle_radians NK_OUT,
                                                         float *out_outer_angle_radians NK_OUT,
                                                         float *out_outer_gain NK_OUT);
/** Sets the speed of sound used for Doppler calculations in world units per second. */
NKAUDIO_API nk_result NK_CALL nk_audio_listener_set_speed_of_sound(float speed);
/** Returns the speed of sound used for Doppler calculations. */
NKAUDIO_API nk_result NK_CALL nk_audio_listener_get_speed_of_sound(float *out_speed NK_OUT);

/** Enables or disables 3D spatialization for a voice. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_spatialization_enabled(nk_audio_voice voice,
                                                                        nk_bool enabled);
/** Returns whether 3D spatialization is enabled for a voice. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_is_spatialization_enabled(nk_audio_voice voice,
                                                                       nk_bool *out_enabled NK_OUT);
/** Sets a voice's position in world or listener-relative coordinates. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_position(nk_audio_voice voice,
                                                          nk_audio_vec3 position);
/** Returns a voice's position. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_position(nk_audio_voice voice,
                                                          nk_audio_vec3 *out_position NK_OUT);
/** Sets a voice's forward direction; it must be finite and non-zero. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_direction(nk_audio_voice voice,
                                                           nk_audio_vec3 direction);
/** Returns a voice's forward direction. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_direction(nk_audio_voice voice,
                                                           nk_audio_vec3 *out_direction NK_OUT);
/** Sets a voice's velocity in world units per second. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_velocity(nk_audio_voice voice,
                                                          nk_audio_vec3 velocity);
/** Returns a voice's velocity. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_velocity(nk_audio_voice voice,
                                                          nk_audio_vec3 *out_velocity NK_OUT);
/** Sets the voice's distance attenuation model. */
NKAUDIO_API nk_result NK_CALL
nk_audio_voice_set_attenuation_model(nk_audio_voice voice, nk_audio_attenuation_model model);
/** Returns the voice's distance attenuation model. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_attenuation_model(
    nk_audio_voice voice, nk_audio_attenuation_model *out_model NK_OUT);
/** Sets whether the voice position is absolute or listener-relative. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_positioning(nk_audio_voice voice,
                                                             nk_audio_positioning positioning);
/** Returns the voice's position interpretation. */
NKAUDIO_API nk_result NK_CALL
nk_audio_voice_get_positioning(nk_audio_voice voice, nk_audio_positioning *out_positioning NK_OUT);
/** Sets the distance attenuation rolloff; zero disables falloff progression. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_rolloff(nk_audio_voice voice, float rolloff);
/** Returns the distance attenuation rolloff. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_rolloff(nk_audio_voice voice,
                                                         float *out_rolloff NK_OUT);
/** Sets the minimum and maximum gain applied by spatialization. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_gain_limits(nk_audio_voice voice, float min_gain,
                                                             float max_gain);
/** Returns the minimum and maximum gain applied by spatialization. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_gain_limits(nk_audio_voice voice,
                                                             float *out_min_gain NK_OUT,
                                                             float *out_max_gain NK_OUT);
/** Sets the minimum and maximum distances used by attenuation. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_distance_limits(nk_audio_voice voice,
                                                                 float min_distance,
                                                                 float max_distance);
/** Returns the minimum and maximum distances used by attenuation. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_distance_limits(nk_audio_voice voice,
                                                                 float *out_min_distance NK_OUT,
                                                                 float *out_max_distance NK_OUT);
/** Sets the voice's Doppler multiplier; zero disables Doppler pitch shifting. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_doppler_factor(nk_audio_voice voice, float factor);
/** Returns the voice's Doppler multiplier. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_doppler_factor(nk_audio_voice voice,
                                                                float *out_factor NK_OUT);
/** Sets the voice's directional attenuation cone in radians and linear gain. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_cone(nk_audio_voice voice,
                                                      float inner_angle_radians,
                                                      float outer_angle_radians, float outer_gain);
/** Returns the voice's directional attenuation cone. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_cone(nk_audio_voice voice,
                                                      float *out_inner_angle_radians NK_OUT,
                                                      float *out_outer_angle_radians NK_OUT,
                                                      float *out_outer_gain NK_OUT);
/** Sets how strongly source direction affects per-channel spatial gain. */
NKAUDIO_API nk_result NK_CALL
nk_audio_voice_set_directional_attenuation_factor(nk_audio_voice voice, float factor);
/** Returns how strongly source direction affects per-channel spatial gain. */
NKAUDIO_API nk_result NK_CALL
nk_audio_voice_get_directional_attenuation_factor(nk_audio_voice voice, float *out_factor NK_OUT);

/* ------------------------------------------------------------------------- */
/* Engine timing                                                             */
/* ------------------------------------------------------------------------- */

/** Returns the process-wide audio engine clock in PCM frames. */
NKAUDIO_API nk_result NK_CALL nk_audio_get_time_pcm_frames(uint64_t *out_time_pcm_frames NK_OUT);
/** Returns the process-wide audio engine sample rate in frames per second. */
NKAUDIO_API nk_result NK_CALL nk_audio_get_sample_rate(uint32_t *out_sample_rate NK_OUT);

#ifdef __cplusplus
}
#endif

#endif
