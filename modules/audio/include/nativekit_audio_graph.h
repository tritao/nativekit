#ifndef NATIVEKIT_AUDIO_GRAPH_H
#define NATIVEKIT_AUDIO_GRAPH_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit_audio.h"
#include "nativekit_audio_dsp.h"

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Mixer graph types                                                         */
/* ------------------------------------------------------------------------- */

/** Opaque handle for one audio mixer bus. */
typedef uint32_t nk_audio_bus NK_HANDLE NK_HANDLE_DESTROY(nk_audio_bus_destroy);

/** Policy used when a bus must make room for a higher-priority voice. */
typedef uint32_t nk_audio_voice_steal_policy;
enum NK_ENUM(nk_audio_voice_steal_policy) {
    /** Never steal an existing voice. A new voice is rejected or virtualized. */
    NK_AUDIO_VOICE_STEAL_NONE = 0,
    /** Steal the oldest eligible voice. */
    NK_AUDIO_VOICE_STEAL_OLDEST = 1,
    /** Steal the eligible voice with the lowest configured gain. */
    NK_AUDIO_VOICE_STEAL_QUIETEST = 2,
    /** Steal the eligible voice with the lowest priority, then the oldest. */
    NK_AUDIO_VOICE_STEAL_LOWEST_PRIORITY = 3
};

/** Opaque handle for one reusable set of mixer bus targets. */
typedef uint32_t nk_audio_mix_snapshot NK_HANDLE NK_HANDLE_DESTROY(nk_audio_mix_snapshot_destroy);

/** Optional creation settings for one audio mixer bus. */
typedef struct nk_audio_bus_options {
    /** Set to sizeof(nk_audio_bus_options) before passing the structure. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Optional parent bus; NK_INVALID_HANDLE routes the bus to the master endpoint. */
    nk_audio_bus parent;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_bus_options;

/** Voice-concurrency policy applied to a bus and all of its child buses. */
typedef struct nk_audio_bus_concurrency_options {
    /** Set to sizeof(nk_audio_bus_concurrency_options) before passing the structure. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Maximum number of playing voices in this bus subtree; zero means unlimited. */
    uint32_t max_voices;
    /** Policy used to make room when max_voices is reached. */
    nk_audio_voice_steal_policy steal_policy;
    /** Keep an admitted voice silent and advance it until a slot becomes available. */
    nk_bool virtualize;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_bus_concurrency_options;

/** Opaque handle for one effect inserted into an audio mixer bus. */
typedef uint32_t nk_audio_bus_effect NK_HANDLE NK_HANDLE_DESTROY(nk_audio_bus_effect_destroy);

/** An independently controlled post-fader route to another bus. */
typedef uint32_t nk_audio_bus_send NK_HANDLE NK_HANDLE_DESTROY(nk_audio_bus_send_destroy);
/** Maximum parallel sends per source bus; the primary parent route is separate. */
#define NK_AUDIO_MAX_BUS_SENDS 8

/** Creates a post-fader send with gain in [0, 1]. Parent/send routing cycles are rejected. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_send_create(nk_audio_bus source, nk_audio_bus destination,
    float volume, nk_audio_bus_send *out_send NK_OUT NK_OWNED);
/** Disconnects the send without resetting the destination's effect tails. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_send_destroy(nk_audio_bus_send send);
NKAUDIO_API nk_result NK_CALL nk_audio_bus_send_set_volume(nk_audio_bus_send send, float volume);
NKAUDIO_API nk_result NK_CALL nk_audio_bus_send_get_volume(nk_audio_bus_send send,
    float *out_volume NK_OUT);
/** Sends are invalidated when either endpoint's bus handle is destroyed. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_send_get_destination(nk_audio_bus_send send,
    nk_audio_bus *out_destination NK_OUT);

/** Built-in processing effect types available on mixer buses. */
typedef uint32_t nk_audio_effect_type;
enum NK_ENUM(nk_audio_effect_type) {
    /** A Butterworth low-pass filter. */
    NK_AUDIO_EFFECT_LOW_PASS = 0,
    /** A Butterworth high-pass filter. */
    NK_AUDIO_EFFECT_HIGH_PASS = 1,
    /** A feedback delay with configurable wet/dry mix. */
    NK_AUDIO_EFFECT_DELAY = 2,
    /** A stereo algorithmic reverb. */
    NK_AUDIO_EFFECT_REVERB = 3,
    /** A compressor, expander, and noise gate. */
    NK_AUDIO_EFFECT_DYNAMICS = 4,
    /** A tempo-aware interpolated stereo/ping-pong delay. */
    NK_AUDIO_EFFECT_STEREO_DELAY = 5
};

/** Parameters for reverb, dynamics, and stereo delay processors; unsupported combinations are rejected. */
typedef uint32_t nk_audio_effect_parameter;
enum NK_ENUM(nk_audio_effect_parameter) {
    NK_AUDIO_EFFECT_PARAMETER_WET = 0,
    NK_AUDIO_EFFECT_PARAMETER_DRY = 1,
    /** Reverb room size in milliseconds, [10, 200]. */
    NK_AUDIO_EFFECT_PARAMETER_ROOM_MS = 2,
    /** Reverb decay to -60 dB in seconds, [0.03, 90]. */
    NK_AUDIO_EFFECT_PARAMETER_DECAY_SECONDS = 3,
    NK_AUDIO_EFFECT_PARAMETER_LOW_CUT_HZ = 4,
    NK_AUDIO_EFFECT_PARAMETER_HIGH_CUT_HZ = 5,
    /** Reverb damping multipliers, [1, 10]. */
    NK_AUDIO_EFFECT_PARAMETER_LOW_DAMPING = 6,
    NK_AUDIO_EFFECT_PARAMETER_HIGH_DAMPING = 7,
    /** Reverb early-reflection gain, [0, 2.5]. */
    NK_AUDIO_EFFECT_PARAMETER_EARLY_REFLECTIONS = 8,
    /** Compressor threshold in dB, [-60, 0]. */
    NK_AUDIO_EFFECT_PARAMETER_THRESHOLD_DB = 9,
    /** Compressor ratio, [1, 100]. */
    NK_AUDIO_EFFECT_PARAMETER_RATIO = 10,
    NK_AUDIO_EFFECT_PARAMETER_ATTACK_MS = 11,
    NK_AUDIO_EFFECT_PARAMETER_RELEASE_MS = 12,
    NK_AUDIO_EFFECT_PARAMETER_MAKEUP_DB = 13,
    NK_AUDIO_EFFECT_PARAMETER_GATE_THRESHOLD_DB = 14,
    NK_AUDIO_EFFECT_PARAMETER_EXPANDER_THRESHOLD_DB = 15,
    NK_AUDIO_EFFECT_PARAMETER_EXPANDER_RATIO = 16,
    /** Dynamics wet/dry mix, [0, 1]. */
    NK_AUDIO_EFFECT_PARAMETER_MIX = 17,
    /** Manual delay time in seconds, [one PCM frame, 4]; disables tempo sync. */
    NK_AUDIO_EFFECT_PARAMETER_DELAY_SECONDS = 18,
    /** Delay feedback gain, [0, 0.95]. */
    NK_AUDIO_EFFECT_PARAMETER_FEEDBACK = 19,
    /** Stereo feedback crossfeed, [0, 1]; 1 is ping-pong. */
    NK_AUDIO_EFFECT_PARAMETER_PING_PONG = 20,
    /** Delay tempo, [20, 300] beats per minute. */
    NK_AUDIO_EFFECT_PARAMETER_BPM = 21,
    /** Delay length in quarter-note beats, [0, 8]; 0 selects manual time. */
    NK_AUDIO_EFFECT_PARAMETER_DELAY_BEATS = 22
};

/**
 * Routes the attached DSP renderer through a bus, or the master endpoint when
 * bus is NK_INVALID_HANDLE. Does not reset voices or queued events. The device
 * must be initialized. Destroying the routed bus restores master routing.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_set_bus(nk_audio_dsp_engine engine,
                                                        nk_audio_bus bus);

/** Creates a stereo reverb (room 80 ms, decay 3 s, wet 0.5, dry 1). */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_create_reverb(
    nk_audio_bus bus, nk_audio_bus_effect *out_effect NK_OUT NK_OWNED);
/** Creates dynamics with compression/expansion disabled and gate at -80 dB. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_create_dynamics(
    nk_audio_bus bus, nk_audio_bus_effect *out_effect NK_OUT NK_OWNED);
/** Creates a stereo delay: 250 ms, feedback 0.35, wet 0.3, dry 1; max delay 4 s. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_create_stereo_delay(
    nk_audio_bus bus, nk_audio_bus_effect *out_effect NK_OUT NK_OWNED);
/** Sets tempo and beat length together. Effective time must not exceed 4 seconds. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_set_delay_tempo(
    nk_audio_bus_effect effect, float bpm, float beats);
/** Sets a finite, range-checked parameter; changes are smoothed by the processor. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_set_parameter(
    nk_audio_bus_effect effect, nk_audio_effect_parameter parameter, float value);
/** Returns the target parameter value, rather than its intermediate smoothed value. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_get_parameter(
    nk_audio_bus_effect effect, nk_audio_effect_parameter parameter, float *out_value NK_OUT);
/** Requests state/tail reset at the next processing block, preserving target parameters. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_reset(nk_audio_bus_effect effect);
/** Returns algorithmic latency in PCM frames for reverb/dynamics processors. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_get_latency(
    nk_audio_bus_effect effect, uint32_t *out_frames NK_OUT);
/** Returns an estimated -60 dB tail in PCM frames for reverb/dynamics processors. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_get_tail(
    nk_audio_bus_effect effect, uint32_t *out_frames NK_OUT);

/* ------------------------------------------------------------------------- */
/* Mixer buses                                                               */
/* ------------------------------------------------------------------------- */

/** Creates an active mixer bus, optionally routed through a parent bus. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_create(const nk_audio_bus_options *options,
                                                  nk_audio_bus *out_bus NK_OUT NK_OWNED);
/** Stops and destroys a mixer bus and its effects, invalidating their handles. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_destroy(nk_audio_bus bus);
/**
 * Reparents a mixer bus. NK_INVALID_HANDLE routes it to the master endpoint;
 * cycles and buses belonging to another audio engine are rejected.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_set_parent(nk_audio_bus bus, nk_audio_bus parent);
/** Returns the live parent bus handle, or NK_INVALID_HANDLE for the master endpoint. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_get_parent(nk_audio_bus bus,
                                                      nk_audio_bus *out_parent NK_OUT);
/** Starts all voices routed through the bus. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_start(nk_audio_bus bus);
/** Stops all voices routed through the bus without destroying them. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_stop(nk_audio_bus bus);
/**
 * Schedules all voices routed through the bus to start at an absolute
 * process-wide audio time in PCM frames. Call nk_audio_bus_start() after
 * setting the schedule.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_schedule_start(nk_audio_bus bus,
                                                          uint64_t absolute_time_pcm_frames);
/**
 * Schedules all voices routed through the bus to stop at an absolute
 * process-wide audio time in PCM frames.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_schedule_stop(nk_audio_bus bus,
                                                         uint64_t absolute_time_pcm_frames);
/** Clears the bus's scheduled start, stop, and fade transitions. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_clear_schedule(nk_audio_bus bus);
/** Returns whether at least one voice routed through the bus is playing. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_is_playing(nk_audio_bus bus,
                                                      nk_bool *out_playing NK_OUT);
/** Sets the bus's linear gain; zero mutes the bus. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_set_volume(nk_audio_bus bus, float volume);
/** Returns the bus's configured linear gain. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_get_volume(nk_audio_bus bus, float *out_volume NK_OUT);
/**
 * Fades the bus between absolute linear gains over a duration in PCM frames. The
 * starting volume may be NK_AUDIO_VOLUME_CURRENT. The configured gain becomes
 * the target gain; set_volume cancels any active fade.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_fade(nk_audio_bus bus, float volume_begin,
                                                float volume_end, uint64_t duration_pcm_frames);
/** Fades the bus at an absolute process-wide audio time in PCM frames. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_fade_at(nk_audio_bus bus, float volume_begin,
                                                   float volume_end, uint64_t duration_pcm_frames,
                                                   uint64_t absolute_start_time_pcm_frames);
/** Enables or disables the bus mute state without changing its configured gain. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_set_muted(nk_audio_bus bus, nk_bool muted);
/** Returns whether the bus is muted. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_is_muted(nk_audio_bus bus, nk_bool *out_muted NK_OUT);
/** Configures the voice limit and admission policy for a bus subtree. */
NKAUDIO_API nk_result NK_CALL
nk_audio_bus_set_concurrency(nk_audio_bus bus, const nk_audio_bus_concurrency_options *options);
/** Returns the voice-concurrency policy configured for a bus. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_get_concurrency(
    nk_audio_bus bus, nk_audio_bus_concurrency_options *out_options NK_OUT);

/* ------------------------------------------------------------------------- */
/* Mixer snapshots                                                           */
/* ------------------------------------------------------------------------- */

/** Creates an empty mixer snapshot. Add bus targets before applying it. */
NKAUDIO_API nk_result NK_CALL
nk_audio_mix_snapshot_create(nk_audio_mix_snapshot *out_snapshot NK_OUT NK_OWNED);
/** Destroys a mixer snapshot and releases its captured bus targets. */
NKAUDIO_API nk_result NK_CALL nk_audio_mix_snapshot_destroy(nk_audio_mix_snapshot snapshot);
/** Captures one bus's current configured volume and mute state. */
NKAUDIO_API nk_result NK_CALL nk_audio_mix_snapshot_capture_bus(nk_audio_mix_snapshot snapshot,
                                                                nk_audio_bus bus);
/** Sets or replaces one bus target in the snapshot. */
NKAUDIO_API nk_result NK_CALL nk_audio_mix_snapshot_set_bus(nk_audio_mix_snapshot snapshot,
                                                            nk_audio_bus bus, float volume,
                                                            nk_bool muted);
/** Removes one bus target from the snapshot. */
NKAUDIO_API nk_result NK_CALL nk_audio_mix_snapshot_remove_bus(nk_audio_mix_snapshot snapshot,
                                                               nk_audio_bus bus);
/** Removes every bus target from the snapshot. */
NKAUDIO_API nk_result NK_CALL nk_audio_mix_snapshot_clear(nk_audio_mix_snapshot snapshot);
/** Returns the number of bus targets stored in the snapshot. */
NKAUDIO_API nk_result NK_CALL nk_audio_mix_snapshot_get_bus_count(nk_audio_mix_snapshot snapshot,
                                                                  uint32_t *out_count NK_OUT);
/** Applies all targets immediately or over a duration in PCM frames. */
NKAUDIO_API nk_result NK_CALL nk_audio_mix_snapshot_apply(nk_audio_mix_snapshot snapshot,
                                                          uint64_t duration_pcm_frames);
/** Applies all targets at an absolute process-wide audio time. */
NKAUDIO_API nk_result NK_CALL
nk_audio_mix_snapshot_apply_at(nk_audio_mix_snapshot snapshot, uint64_t duration_pcm_frames,
                               uint64_t absolute_start_time_pcm_frames);

/* ------------------------------------------------------------------------- */
/* Bus effects                                                               */
/* ------------------------------------------------------------------------- */

/**
 * Appends a low-pass filter to the bus effect chain. Cutoff is in Hz and
 * order must be in the range [1, 8].
 */
NKAUDIO_API nk_result NK_CALL
nk_audio_bus_effect_create_low_pass(nk_audio_bus bus, float cutoff_frequency_hz, uint32_t order,
                                    nk_audio_bus_effect *out_effect NK_OUT NK_OWNED);
/** Appends a high-pass filter to the bus effect chain. */
NKAUDIO_API nk_result NK_CALL
nk_audio_bus_effect_create_high_pass(nk_audio_bus bus, float cutoff_frequency_hz, uint32_t order,
                                     nk_audio_bus_effect *out_effect NK_OUT NK_OWNED);
/**
 * Appends a delay to the bus effect chain. Delay frames must be positive and
 * decay is in the range [0, 1].
 */
NKAUDIO_API nk_result NK_CALL
nk_audio_bus_effect_create_delay(nk_audio_bus bus, uint32_t delay_pcm_frames, float decay,
                                 nk_audio_bus_effect *out_effect NK_OUT NK_OWNED);
/** Removes an effect from its bus and invalidates its handle. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_destroy(nk_audio_bus_effect effect);
/** Returns the effect type. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_get_type(nk_audio_bus_effect effect,
                                                           nk_audio_effect_type *out_type NK_OUT);
/** Enables or bypasses an effect without removing it from the chain. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_set_enabled(nk_audio_bus_effect effect,
                                                              nk_bool enabled);
/** Returns whether an effect is active in its bus chain. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_is_enabled(nk_audio_bus_effect effect,
                                                             nk_bool *out_enabled NK_OUT);
/** Moves an effect to a zero-based position in its bus chain. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_set_position(nk_audio_bus_effect effect,
                                                               uint32_t position);
/** Returns an effect's zero-based position in its bus chain. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_get_position(nk_audio_bus_effect effect,
                                                               uint32_t *out_position NK_OUT);
/** Updates a low-pass filter's cutoff and order. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_set_low_pass(nk_audio_bus_effect effect,
                                                               float cutoff_frequency_hz,
                                                               uint32_t order);
/** Returns a low-pass filter's cutoff and order. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_get_low_pass(
    nk_audio_bus_effect effect, float *out_cutoff_frequency_hz NK_OUT, uint32_t *out_order NK_OUT);
/** Updates a high-pass filter's cutoff and order. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_set_high_pass(nk_audio_bus_effect effect,
                                                                float cutoff_frequency_hz,
                                                                uint32_t order);
/** Returns a high-pass filter's cutoff and order. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_get_high_pass(
    nk_audio_bus_effect effect, float *out_cutoff_frequency_hz NK_OUT, uint32_t *out_order NK_OUT);
/** Sets a delay's wet gain. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_set_delay_wet(nk_audio_bus_effect effect,
                                                                float wet);
/** Returns a delay's wet gain. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_get_delay_wet(nk_audio_bus_effect effect,
                                                                float *out_wet NK_OUT);
/** Sets a delay's dry gain. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_set_delay_dry(nk_audio_bus_effect effect,
                                                                float dry);
/** Returns a delay's dry gain. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_get_delay_dry(nk_audio_bus_effect effect,
                                                                float *out_dry NK_OUT);
/** Sets a delay's feedback decay. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_set_delay_decay(nk_audio_bus_effect effect,
                                                                  float decay);
/** Returns a delay's feedback decay. */
NKAUDIO_API nk_result NK_CALL nk_audio_bus_effect_get_delay_decay(nk_audio_bus_effect effect,
                                                                  float *out_decay NK_OUT);

/* ------------------------------------------------------------------------- */
/* Voice graph policy                                                        */
/* ------------------------------------------------------------------------- */

/** Routes a stopped voice to a bus, or to the master endpoint when invalid. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_bus(nk_audio_voice voice, nk_audio_bus bus);
/** Returns the voice's live bus, or NK_INVALID_HANDLE for the master endpoint. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_bus(nk_audio_voice voice,
                                                     nk_audio_bus *out_bus NK_OUT);
/** Sets the voice's concurrency priority; larger values are more important. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_set_priority(nk_audio_voice voice, uint32_t priority);
/** Returns the voice's concurrency priority. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_get_priority(nk_audio_voice voice,
                                                          uint32_t *out_priority NK_OUT);
/** Returns whether the voice is currently running without an audible bus slot. */
NKAUDIO_API nk_result NK_CALL nk_audio_voice_is_virtualized(nk_audio_voice voice,
                                                            nk_bool *out_virtualized NK_OUT);

/* ------------------------------------------------------------------------- */
/* Global mix                                                                 */
/* ------------------------------------------------------------------------- */

/** Sets linear gain for the process-wide NativeKit audio mixer. */
NKAUDIO_API nk_result NK_CALL nk_audio_set_master_volume(float volume);
/** Returns linear gain for the process-wide NativeKit audio mixer. */
NKAUDIO_API nk_result NK_CALL nk_audio_get_master_volume(float *out_volume NK_OUT);

#ifdef __cplusplus
}
#endif

#endif
