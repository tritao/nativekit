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

/** Opaque handle for an immutable, reusable DSP patch definition. */
typedef uint32_t nk_audio_dsp_patch NK_HANDLE NK_HANDLE_DESTROY(nk_audio_dsp_patch_destroy);

/** Opaque handle for an immutable, renderer-independent wavetable asset. */
typedef uint32_t nk_audio_dsp_wavetable NK_HANDLE NK_HANDLE_DESTROY(nk_audio_dsp_wavetable_destroy);

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
    NK_AUDIO_DSP_CAPABILITY_PHASE_MODULATION = 1u << 7,
    /** Generic patch modulation sources and destinations. */
    NK_AUDIO_DSP_CAPABILITY_MODULATION = 1u << 8
};

/** Built-in pitched oscillator shapes. */
typedef uint32_t nk_audio_dsp_waveform;
enum NK_ENUM(nk_audio_dsp_waveform) {
    NK_AUDIO_DSP_WAVEFORM_SINE = 0,
    NK_AUDIO_DSP_WAVEFORM_TRIANGLE = 1,
    NK_AUDIO_DSP_WAVEFORM_SAW = 2,
    NK_AUDIO_DSP_WAVEFORM_SQUARE = 3
};

/** Filter components supported by the DSP patch model. */
typedef uint32_t nk_audio_dsp_filter_type;
enum NK_ENUM(nk_audio_dsp_filter_type) {
    NK_AUDIO_DSP_FILTER_NONE = 0,
    NK_AUDIO_DSP_FILTER_SVF_LOW_PASS = 1
};

/** LFO phase behavior when a voice receives NOTE_ON. */
typedef uint32_t nk_audio_dsp_lfo_mode;
enum NK_ENUM(nk_audio_dsp_lfo_mode) {
    /** Reset the LFO phase to its patch phase on every NOTE_ON. */
    NK_AUDIO_DSP_LFO_RETRIGGER = 0,
    /** Keep the LFO phase when a live voice receives another NOTE_ON. */
    NK_AUDIO_DSP_LFO_FREE_RUNNING = 1
};

/** Sources that can feed a patch modulation route. */
typedef uint32_t nk_audio_dsp_modulation_source;
enum NK_ENUM(nk_audio_dsp_modulation_source) {
    NK_AUDIO_DSP_MODULATION_SOURCE_LFO = 0,
    NK_AUDIO_DSP_MODULATION_SOURCE_ENVELOPE = 1
};

/** Destinations that can receive a patch modulation route. */
typedef uint32_t nk_audio_dsp_modulation_destination;
enum NK_ENUM(nk_audio_dsp_modulation_destination) {
    /** Signed pitch offset in semitones. */
    NK_AUDIO_DSP_MODULATION_DESTINATION_PITCH_SEMITONES = 0,
    /** Cutoff offset in Hz. */
    NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_HZ = 1,
    /** Relative linear output-gain amount around the patch gain. */
    NK_AUDIO_DSP_MODULATION_DESTINATION_AMPLITUDE = 2
};

/** Normalization applied to a modulation source before amount scaling. */
typedef uint32_t nk_audio_dsp_modulation_polarity;
enum NK_ENUM(nk_audio_dsp_modulation_polarity) {
    /** Preserve a bipolar LFO, or center a unipolar envelope around zero. */
    NK_AUDIO_DSP_MODULATION_BIPOLAR = 0,
    /** Map a bipolar LFO to [0, 1], while preserving an envelope's [0, 1]. */
    NK_AUDIO_DSP_MODULATION_UNIPOLAR = 1
};

/** Maximum number of fixed, ABI-safe modulation routes in one patch. */
enum { NK_AUDIO_DSP_MAX_MODULATION_ROUTES = 8 };

/** Maximum number of independently tuned pitched sources in one patch. */
enum { NK_AUDIO_DSP_MAX_OSCILLATORS = 4 };

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

/* ------------------------------------------------------------------------- */
/* Patch components                                                          */
/* ------------------------------------------------------------------------- */

/** Oscillator source component in a reusable patch. */
typedef struct nk_audio_dsp_oscillator_options {
    /** Set to sizeof(nk_audio_dsp_oscillator_options) before use. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Built-in oscillator shape. */
    nk_audio_dsp_waveform waveform;
    /** Linear source level in the inclusive range [0, 1]. */
    float level;
    /** Optional wavetable source; invalid selects the built-in waveform. */
    nk_audio_dsp_wavetable wavetable;
    /** Relative tuning in cents; zero preserves the note frequency. */
    float detune_cents;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[1];
} nk_audio_dsp_oscillator_options;

/** White-noise source component in a reusable patch. */
typedef struct nk_audio_dsp_noise_options {
    /** Set to sizeof(nk_audio_dsp_noise_options) before use. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Linear source level in the inclusive range [0, 1]. */
    float level;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_dsp_noise_options;

/** Amplitude envelope component in a reusable patch. */
typedef struct nk_audio_dsp_envelope_options {
    /** Set to sizeof(nk_audio_dsp_envelope_options) before use. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Attack duration in seconds. */
    float attack_seconds;
    /** Decay duration in seconds. */
    float decay_seconds;
    /** Sustained amplitude in the inclusive range [0, 1]. */
    float sustain_level;
    /** Release duration in seconds. */
    float release_seconds;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_dsp_envelope_options;

/** Filter component in a reusable patch. */
typedef struct nk_audio_dsp_filter_options {
    /** Set to sizeof(nk_audio_dsp_filter_options) before use. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Filter algorithm; NONE or SVF_LOW_PASS. */
    nk_audio_dsp_filter_type type;
    /** Cutoff in Hz; zero bypasses the filter. */
    float cutoff_hz;
    /** Resonance in the inclusive range [0, 1]. */
    float resonance;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_dsp_filter_options;

/** Low-frequency oscillator component in a reusable patch. */
typedef struct nk_audio_dsp_lfo_options {
    /** Set to sizeof(nk_audio_dsp_lfo_options) before use. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Built-in LFO shape. */
    nk_audio_dsp_waveform waveform;
    /** Phase behavior for voices receiving NOTE_ON. */
    nk_audio_dsp_lfo_mode mode;
    /** LFO frequency in Hz; zero disables LFO motion. */
    float rate_hz;
    /** Initial/retrigger phase normalized to [0, 1]. */
    float phase;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[1];
} nk_audio_dsp_lfo_options;

/** One generic source-to-destination modulation route. */
typedef struct nk_audio_dsp_modulation_route_options {
    /** Set to sizeof(nk_audio_dsp_modulation_route_options) before use. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Route source. */
    nk_audio_dsp_modulation_source source;
    /** Route destination. */
    nk_audio_dsp_modulation_destination destination;
    /** Source normalization mode. */
    nk_audio_dsp_modulation_polarity polarity;
    /** Signed destination-unit amount. */
    float amount;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[1];
} nk_audio_dsp_modulation_route_options;

/**
 * Immutable reusable DSP patch. Components are evaluated in source, envelope,
 * modulation, filter, and output-gain order. Modulation amounts use the
 * destination's units: semitones for pitch, Hz for filter cutoff, and linear
 * relative gain for amplitude.
 */
typedef struct nk_audio_dsp_patch_options {
    /** Set to sizeof(nk_audio_dsp_patch_options) before use. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Pitched oscillator source components. */
    nk_audio_dsp_oscillator_options oscillators[NK_AUDIO_DSP_MAX_OSCILLATORS];
    /** Number of active pitched oscillator sources. Zero permits noise-only patches. */
    uint32_t oscillator_count;
    /** White-noise source component. */
    nk_audio_dsp_noise_options noise;
    /** Amplitude envelope component. */
    nk_audio_dsp_envelope_options envelope;
    /** Optional filter component. */
    nk_audio_dsp_filter_options filter;
    /** Linear output gain; zero is silent. */
    float gain;
    /** Optional low-frequency oscillator used by modulation routes. */
    nk_audio_dsp_lfo_options lfo;
    /** Fixed modulation route storage; only route_count entries are active. */
    nk_audio_dsp_modulation_route_options routes[NK_AUDIO_DSP_MAX_MODULATION_ROUTES];
    /** Number of active modulation routes. */
    uint32_t route_count;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[2];
} nk_audio_dsp_patch_options;

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

/** Legacy flat parameters for one pitched instrument. Prefer patches. */
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
    float *samples NK_BORROWED_BUFFER(sample_count);
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

/** Creates an immutable, renderer-independent patch definition. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_patch_create(
    const nk_audio_dsp_patch_options *options, nk_audio_dsp_patch *out_patch NK_OUT NK_OWNED);
/** Destroys a patch definition; instruments created from it retain their copy. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_patch_destroy(nk_audio_dsp_patch patch);
/** Destroys a wavetable handle; patches retain tables they reference. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_wavetable_destroy(nk_audio_dsp_wavetable wavetable);
/**
 * Creates an immutable, periodic wavetable from one source cycle. The source
 * length must be a power of two in the inclusive range [32, 4096]. NativeKit
 * builds harmonic-limited bands for alias-resistant playback.
 */
NKAUDIO_API nk_result NK_CALL
nk_audio_dsp_wavetable_create(const float *samples NK_IN_ARRAY(sample_count), uint32_t sample_count,
                              nk_audio_dsp_wavetable *out_wavetable NK_OUT NK_OWNED);
/** Creates an instrument from an immutable reusable patch definition. */
NKAUDIO_API nk_result NK_CALL
nk_audio_dsp_instrument_create_from_patch(nk_audio_dsp_engine engine, nk_audio_dsp_patch patch,
                                          nk_audio_dsp_instrument *out_instrument NK_OUT NK_OWNED);
/** Creates an instrument from the legacy flat options. Prefer patch_create(). */
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
