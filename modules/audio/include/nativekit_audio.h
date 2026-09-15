#ifndef NATIVEKIT_AUDIO_H
#define NATIVEKIT_AUDIO_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit.h"

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
/* Sound types and options                                                   */
/* ------------------------------------------------------------------------- */

/** Opaque handle for one decoded or streaming audio sound. */
typedef uint32_t nk_audio_sound NK_HANDLE NK_HANDLE_DESTROY(nk_audio_sound_destroy);

/** Flags controlling how a sound is loaded and played. */
typedef uint32_t nk_audio_sound_flags;
enum NK_FLAGS(nk_audio_sound_flags) {
    /** Repeats the sound after it reaches its end. */
    NK_AUDIO_SOUND_LOOPING = 1u << 0,
    /** Streams the source instead of decoding the complete source up front. */
    NK_AUDIO_SOUND_STREAM = 1u << 1,
    /** Loads the source asynchronously when the backend supports it. */
    NK_AUDIO_SOUND_ASYNC = 1u << 2
};

/** Optional creation flags for a sound. */
typedef struct nk_audio_sound_options {
    /** Set to sizeof(nk_audio_sound_options) before passing the structure. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Combination of NK_AUDIO_SOUND_* flags. */
    nk_audio_sound_flags flags;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_sound_options;

/* ------------------------------------------------------------------------- */
/* Sound lifetime and transport                                              */
/* ------------------------------------------------------------------------- */

/**
 * Creates a sound from a UTF-8 native filesystem path. The path is consumed
 * during this call. Sounds are created stopped; call nk_audio_sound_start().
 * The options pointer may be NULL.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_create_from_file(
    const char *path NK_UTF8, const nk_audio_sound_options *options,
    nk_audio_sound *out_sound NK_OUT NK_OWNED);

/**
 * Creates a sound from encoded audio bytes. NativeKit copies the bytes before
 * returning, so the caller may release its buffer after the call. The memory
 * source currently supports the built-in WAV, FLAC, and MP3 decoders.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_create_from_memory(
    const void *data NK_BORROWED_BUFFER(data_size), uint64_t data_size,
    const nk_audio_sound_options *options, nk_audio_sound *out_sound NK_OUT NK_OWNED);

/** Stops and destroys a sound, invalidating its handle. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_destroy(nk_audio_sound sound);
/** Starts a sound from its current position. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_start(nk_audio_sound sound);
/** Stops a sound without rewinding it. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_stop(nk_audio_sound sound);
/** Seeks a sound back to its beginning without changing its playing state. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_rewind(nk_audio_sound sound);
/** Returns whether a sound is currently playing. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_is_playing(nk_audio_sound sound,
                                                         nk_bool *out_playing NK_OUT);
/** Returns whether a sound has reached its end. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_at_end(nk_audio_sound sound,
                                                     nk_bool *out_at_end NK_OUT);

/* ------------------------------------------------------------------------- */
/* Sound controls                                                             */
/* ------------------------------------------------------------------------- */

/** Sets linear sound gain; zero mutes the sound. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_set_volume(nk_audio_sound sound, float volume);
/** Returns linear sound gain. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_get_volume(nk_audio_sound sound,
                                                        float *out_volume NK_OUT);
/** Sets stereo pan in the range [-1, 1]. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_set_pan(nk_audio_sound sound, float pan);
/** Returns stereo pan in the range [-1, 1]. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_get_pan(nk_audio_sound sound,
                                                     float *out_pan NK_OUT);
/** Sets playback pitch where 1 is the source pitch. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_set_pitch(nk_audio_sound sound, float pitch);
/** Returns playback pitch. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_get_pitch(nk_audio_sound sound,
                                                       float *out_pitch NK_OUT);
/** Enables or disables looping. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_set_looping(nk_audio_sound sound, nk_bool looping);
/** Returns whether looping is enabled. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_is_looping(nk_audio_sound sound,
                                                        nk_bool *out_looping NK_OUT);
/** Returns the current playback position in seconds. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_get_time_seconds(nk_audio_sound sound,
                                                               float *out_seconds NK_OUT);
/** Returns the decoded sound length in seconds. */
NKAUDIO_API nk_result NK_CALL nk_audio_sound_get_length_seconds(nk_audio_sound sound,
                                                                 float *out_seconds NK_OUT);

/* ------------------------------------------------------------------------- */
/* Global mixer                                                               */
/* ------------------------------------------------------------------------- */

/** Sets linear gain for the process-wide NativeKit audio mixer. */
NKAUDIO_API nk_result NK_CALL nk_audio_set_master_volume(float volume);
/** Returns linear gain for the process-wide NativeKit audio mixer. */
NKAUDIO_API nk_result NK_CALL nk_audio_get_master_volume(float *out_volume NK_OUT);

#ifdef __cplusplus
}
#endif

#endif
