#ifndef NATIVEKIT_AUDIO_DSP_H
#define NATIVEKIT_AUDIO_DSP_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit_audio.h"

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* DSP handles and capabilities                                              */
/* ------------------------------------------------------------------------- */

/** Opaque handle for one standalone DSP renderer. */
typedef uint32_t nk_audio_dsp_engine NK_HANDLE NK_HANDLE_DESTROY(nk_audio_dsp_engine_destroy);

/** Opaque handle for one instrument owned by a DSP renderer. */
typedef uint32_t
    nk_audio_dsp_instrument NK_HANDLE NK_HANDLE_DESTROY(nk_audio_dsp_instrument_destroy);

/** Features implemented by the selected DSP backend. */
typedef uint32_t nk_audio_dsp_capabilities;
enum NK_FLAGS(nk_audio_dsp_capabilities) {
    /** Oscillator-based pitched sound generation. */
    NK_AUDIO_DSP_CAPABILITY_OSCILLATOR = 1u << 0,
    /** Noise source generation. */
    NK_AUDIO_DSP_CAPABILITY_NOISE = 1u << 1,
    /** Wavetable playback. */
    NK_AUDIO_DSP_CAPABILITY_WAVETABLE = 1u << 2,
    /** Attack/decay/sustain/release amplitude shaping. */
    NK_AUDIO_DSP_CAPABILITY_ENVELOPE = 1u << 3,
    /** Low-frequency modulation source. */
    NK_AUDIO_DSP_CAPABILITY_LFO = 1u << 4,
    /** Audio-rate filtering. */
    NK_AUDIO_DSP_CAPABILITY_FILTER = 1u << 5,
    /** Frequency or phase modulation. */
    NK_AUDIO_DSP_CAPABILITY_FM = 1u << 6,
    /** Phase modulation. */
    NK_AUDIO_DSP_CAPABILITY_PHASE_MODULATION = 1u << 7
};

/** Built-in pitched oscillator shapes. */
typedef uint32_t nk_audio_dsp_waveform;
enum NK_ENUM(nk_audio_dsp_waveform) {
    NK_AUDIO_DSP_WAVEFORM_SINE = 0,
    NK_AUDIO_DSP_WAVEFORM_TRIANGLE = 1,
    NK_AUDIO_DSP_WAVEFORM_SAW = 2,
    NK_AUDIO_DSP_WAVEFORM_SQUARE = 3
};

/** Instrument parameters accepted by nk_audio_dsp_instrument_set_parameter. */
typedef uint32_t nk_audio_dsp_parameter;
enum NK_ENUM(nk_audio_dsp_parameter) {
    NK_AUDIO_DSP_PARAMETER_WAVEFORM = 0,
    NK_AUDIO_DSP_PARAMETER_GAIN = 1,
    NK_AUDIO_DSP_PARAMETER_ATTACK_SECONDS = 2,
    NK_AUDIO_DSP_PARAMETER_DECAY_SECONDS = 3,
    NK_AUDIO_DSP_PARAMETER_SUSTAIN_LEVEL = 4,
    NK_AUDIO_DSP_PARAMETER_RELEASE_SECONDS = 5,
    /** Additive white-noise source level in the inclusive range [0, 1]. */
    NK_AUDIO_DSP_PARAMETER_NOISE_LEVEL = 6,
    /** State-variable low-pass cutoff in Hz; zero bypasses the filter. */
    NK_AUDIO_DSP_PARAMETER_FILTER_CUTOFF_HZ = 7,
    /** State-variable low-pass resonance in the inclusive range [0, 1]. */
    NK_AUDIO_DSP_PARAMETER_FILTER_RESONANCE = 8
};

/** Events applied at exact sample offsets while rendering a block. */
typedef uint32_t nk_audio_dsp_event_kind;
enum NK_ENUM(nk_audio_dsp_event_kind) {
    NK_AUDIO_DSP_EVENT_NOTE_ON = 0,
    NK_AUDIO_DSP_EVENT_NOTE_OFF = 1,
    NK_AUDIO_DSP_EVENT_PARAMETER = 2
};

/* ------------------------------------------------------------------------- */
/* Configuration and render descriptors                                     */
/* ------------------------------------------------------------------------- */

/** Configuration for one standalone DSP renderer. Zero fields select defaults. */
typedef struct nk_audio_dsp_engine_options {
    /** Set to sizeof(nk_audio_dsp_engine_options) before passing the structure. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Output sample rate in frames per second; zero selects 48000. */
    uint32_t sample_rate;
    /** Interleaved output channel count; zero selects two channels. */
    uint32_t channels;
    /** Maximum render block size; zero selects 256 frames. */
    uint32_t block_size;
    /** Maximum simultaneously active voice IDs; zero selects 64 voices. */
    uint32_t max_voices;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_dsp_engine_options;

/** Initial parameters for one pitched instrument. */
typedef struct nk_audio_dsp_instrument_options {
    /** Set to sizeof(nk_audio_dsp_instrument_options) before passing the structure. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Initial oscillator shape. */
    nk_audio_dsp_waveform waveform;
    /** Linear output gain; zero is silent. */
    float gain;
    /** Amplitude attack duration in seconds. */
    float attack_seconds;
    /** Amplitude decay duration in seconds. */
    float decay_seconds;
    /** Sustained amplitude in the inclusive range [0, 1]. */
    float sustain_level;
    /** Amplitude release duration in seconds. */
    float release_seconds;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_dsp_instrument_options;

/** Interleaved float output supplied to nk_audio_dsp_engine_render. */
typedef struct nk_audio_dsp_render_target {
    /** Set to sizeof(nk_audio_dsp_render_target) before passing the structure. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Interleaved destination samples, with frame_count * channels elements. */
    float *samples;
    /** Number of PCM frames to render. Must not exceed the engine block size. */
    uint32_t frame_count;
    /** Number of interleaved channels; must match the engine configuration. */
    uint32_t channels;
    /** Number of float elements available at samples. */
    uint64_t sample_count;
} nk_audio_dsp_render_target;

/** One sample-accurate event in a render block. Events must be frame sorted. */
typedef struct nk_audio_dsp_event {
    /** Set to sizeof(nk_audio_dsp_event) before passing the structure. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Event operation. */
    nk_audio_dsp_event_kind kind;
    /** Frame offset within the render target; frame_count is allowed. */
    uint32_t frame_offset;
    /** Instrument for NOTE_ON and PARAMETER; ignored for NOTE_OFF. */
    nk_audio_dsp_instrument instrument;
    /** Caller-owned voice identity for NOTE_ON/NOTE_OFF; ignored for PARAMETER. */
    uint32_t voice_id;
    /** MIDI note number for NOTE_ON, in the inclusive range [0, 127]. */
    uint32_t note;
    /** NOTE_ON velocity in the inclusive range [0, 1]. */
    float velocity;
    /** Parameter ID and value for PARAMETER. */
    nk_audio_dsp_parameter parameter;
    float value;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_dsp_event;

/* ------------------------------------------------------------------------- */
/* Renderer and instrument lifetime                                          */
/* ------------------------------------------------------------------------- */

/** Creates a standalone DSP renderer; no playback device is required. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_create(
    const nk_audio_dsp_engine_options *options, nk_audio_dsp_engine *out_engine NK_OUT NK_OWNED);
/** Stops all voices and destroys a DSP renderer. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_destroy(nk_audio_dsp_engine engine);
/** Returns the renderer's normalized configuration. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_get_options(
    nk_audio_dsp_engine engine, nk_audio_dsp_engine_options *out_options NK_OUT);
/** Returns the capabilities of the renderer's private backend. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_get_capabilities(
    nk_audio_dsp_engine engine, nk_audio_dsp_capabilities *out_capabilities NK_OUT);
/** Restores instruments and voices to their creation state. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_reset(nk_audio_dsp_engine engine);

/** Creates an instrument associated with a renderer. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_instrument_create(
    nk_audio_dsp_engine engine, const nk_audio_dsp_instrument_options *options,
    nk_audio_dsp_instrument *out_instrument NK_OUT NK_OWNED);
/** Destroys an instrument and releases any voices using it. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_instrument_destroy(nk_audio_dsp_instrument instrument);
/** Sets one instrument parameter between render calls. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_instrument_set_parameter(
    nk_audio_dsp_instrument instrument, nk_audio_dsp_parameter parameter, float value);
/** Reads one current instrument parameter. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_instrument_get_parameter(
    nk_audio_dsp_instrument instrument, nk_audio_dsp_parameter parameter, float *out_value NK_OUT);

/* ------------------------------------------------------------------------- */
/* Rendering                                                                 */
/* ------------------------------------------------------------------------- */

/**
 * Renders one interleaved float block. The destination is cleared before
 * voices are mixed. Events are consumed only for this call and are applied at
 * their exact frame offsets, including an event at frame_count for the next
 * block's state.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_render(
    nk_audio_dsp_engine engine, nk_audio_dsp_render_target *target NK_INOUT,
    const nk_audio_dsp_event *events NK_IN_ARRAY(event_count), uint32_t event_count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NATIVEKIT_AUDIO_DSP_H */
