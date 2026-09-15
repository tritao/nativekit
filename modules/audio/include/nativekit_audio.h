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

/** Opaque handle for one audio mixer bus. */
typedef uint32_t nk_audio_bus NK_HANDLE NK_HANDLE_DESTROY(nk_audio_bus_destroy);

/** Optional creation and routing settings for a voice. */
typedef struct nk_audio_voice_options {
    /** Set to sizeof(nk_audio_voice_options) before passing the structure. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Combination of NK_AUDIO_VOICE_* flags. */
    nk_audio_voice_flags flags;
    /** Optional mixer bus; NK_INVALID_HANDLE routes the voice to the master bus. */
    nk_audio_bus bus;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_voice_options;

/** Use the voice's current fader volume as the start of a fade. */
#define NK_AUDIO_VOLUME_CURRENT (-1.0f)

/* ------------------------------------------------------------------------- */
/* Mixer buses                                                                */
/* ------------------------------------------------------------------------- */

/** Creates an active mixer bus. Voices retain their bus until destroyed. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_create(nk_audio_bus *out_bus NK_OUT NK_OWNED);
/** Stops and destroys a mixer bus, invalidating its handle. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_destroy(nk_audio_bus bus);
/** Starts all voices routed through the bus. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_start(nk_audio_bus bus);
/** Stops all voices routed through the bus without destroying them. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_stop(nk_audio_bus bus);
/**
 * Schedules all voices routed through the bus to start at an absolute
 * process-wide audio time in PCM frames. Call nk_audio_bus_start() after
 * setting the schedule.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_schedule_start(
    nk_audio_bus bus, uint64_t absolute_time_pcm_frames);
/**
 * Schedules all voices routed through the bus to stop at an absolute
 * process-wide audio time in PCM frames.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_schedule_stop(
    nk_audio_bus bus, uint64_t absolute_time_pcm_frames);
/** Clears the bus's scheduled start, stop, and fade transitions. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_clear_schedule(nk_audio_bus bus);
/** Returns whether at least one voice routed through the bus is playing. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_is_playing(nk_audio_bus bus,
                                                       nk_bool *out_playing NK_OUT);
/** Sets the bus's linear gain; zero mutes the bus. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_set_volume(nk_audio_bus bus, float volume);
/** Returns the bus's configured linear gain. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_get_volume(nk_audio_bus bus,
                                                       float *out_volume NK_OUT);
/**
 * Fades the bus between linear gains over a duration in PCM frames. The
 * starting volume may be NK_AUDIO_VOLUME_CURRENT.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_fade(
    nk_audio_bus bus, float volume_begin, float volume_end, uint64_t duration_pcm_frames);
/** Fades the bus at an absolute process-wide audio time in PCM frames. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_fade_at(
    nk_audio_bus bus, float volume_begin, float volume_end, uint64_t duration_pcm_frames,
    uint64_t absolute_start_time_pcm_frames);
/** Enables or disables the bus mute state without changing its configured gain. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_set_muted(nk_audio_bus bus, nk_bool muted);
/** Returns whether the bus is muted. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_is_muted(nk_audio_bus bus,
                                                    nk_bool *out_muted NK_OUT);

/* ------------------------------------------------------------------------- */
/* Clip and voice lifetime and transport                                     */
/* ------------------------------------------------------------------------- */

/**
 * Creates a reusable clip from a UTF-8 native filesystem path. NativeKit
 * copies the path and validates the source before returning. The clip is
 * stopped and has no playback state; create a voice to play it.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_clip_create_from_file(
    const char *path NK_UTF8, nk_audio_clip *out_clip NK_OUT NK_OWNED);

/**
 * Creates a reusable clip from a readable URI resource. NativeKit opens the
 * resource through its platform provider and keeps each voice's decoder
 * backed by an independent resource stream.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_clip_create_from_resource(
    const nk_resource *resource, nk_audio_clip *out_clip NK_OUT NK_OWNED);

/**
 * Creates a reusable clip from encoded audio bytes. NativeKit copies the
 * bytes before returning, so the caller may release its buffer after the
 * call. The source currently supports WAV, FLAC, and MP3.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_clip_create_from_memory(
    const void *data NK_BORROWED_BUFFER(data_size), uint64_t data_size,
    nk_audio_clip *out_clip NK_OUT NK_OWNED);

/** Stops using and destroys a clip. Existing voices retain their source. */
NKAUDIO_API nk_result NK_CALL nk_audio_clip_destroy(nk_audio_clip clip);

/** Creates a stopped independent playback voice from a reusable clip. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_create(
    nk_audio_clip clip, const nk_audio_voice_options *options,
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
NKAUDIO_API nk_result NK_CALL nk_audio_voice_schedule_start(
    nk_audio_voice voice, uint64_t absolute_time_pcm_frames);
/**
 * Schedules a voice to stop at an absolute process-wide audio time in PCM
 * frames. A scheduled stop does not emit a natural-end completion event.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_schedule_stop(
    nk_audio_voice voice, uint64_t absolute_time_pcm_frames);
/** Clears a voice's scheduled start, stop, and fade transitions. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_clear_schedule(nk_audio_voice voice);

/**
 * Fades a voice over a duration in PCM frames. volume_begin may be
 * NK_AUDIO_VOLUME_CURRENT; volume_end must be finite and non-negative.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_fade(
    nk_audio_voice voice, float volume_begin, float volume_end, uint64_t duration_pcm_frames);
/** Fades a voice starting at an absolute process-wide audio time in PCM frames. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_fade_at(
    nk_audio_voice voice, float volume_begin, float volume_end, uint64_t duration_pcm_frames,
    uint64_t absolute_start_time_pcm_frames);
/** Returns whether a voice is currently playing. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_is_playing(nk_audio_voice voice,
                                                         nk_bool *out_playing NK_OUT);
/** Returns the asynchronous loading lifecycle state of a voice. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_load_state(
    nk_audio_voice voice, nk_audio_voice_load_state *out_state NK_OUT);
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
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_pan(nk_audio_voice voice,
                                                     float *out_pan NK_OUT);
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
/* Global mixer                                                               */
/* ------------------------------------------------------------------------- */

/** Returns the process-wide audio engine clock in PCM frames. */
NKAUDIO_API nk_result NK_CALL nk_audio_get_time_pcm_frames(
    uint64_t *out_time_pcm_frames NK_OUT);
/** Returns the process-wide audio engine sample rate in frames per second. */
NKAUDIO_API nk_result NK_CALL nk_audio_get_sample_rate(uint32_t *out_sample_rate NK_OUT);

/** Sets linear gain for the process-wide NativeKit audio mixer. */
NKAUDIO_API nk_result NK_CALL nk_audio_set_master_volume(float volume);
/** Returns linear gain for the process-wide NativeKit audio mixer. */
NKAUDIO_API nk_result NK_CALL nk_audio_get_master_volume(float *out_volume NK_OUT);

#ifdef __cplusplus
}
#endif

#endif
