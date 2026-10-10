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
    NK_AUDIO_DSP_CAPABILITY_MODULATION = 1u << 8,
    /** Specialized physical, percussion, spectral, noise and granular generators. */
    NK_AUDIO_DSP_CAPABILITY_SPECIALIZED_SOURCE = 1u << 9
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
    NK_AUDIO_DSP_MODULATION_SOURCE_ENVELOPE = 1,
    /** Audio-rate output from one oscillator source. */
    NK_AUDIO_DSP_MODULATION_SOURCE_OSCILLATOR = 2
};

/** Destinations that can receive a patch modulation route. */
typedef uint32_t nk_audio_dsp_modulation_destination;
enum NK_ENUM(nk_audio_dsp_modulation_destination) {
    /** Signed pitch offset in semitones. */
    NK_AUDIO_DSP_MODULATION_DESTINATION_PITCH_SEMITONES = 0,
    /** Cutoff offset in Hz. */
    NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_HZ = 1,
    /** Relative linear output-gain amount around the patch gain. */
    NK_AUDIO_DSP_MODULATION_DESTINATION_AMPLITUDE = 2,
    /** Relative linear level for the targeted oscillator sources. */
    NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_LEVEL = 3,
    /** Non-accumulating normalized phase offset for the targeted oscillator sources. */
    NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_PHASE = 4,
    /** Audio-rate frequency offset in Hz for the targeted oscillator source. */
    NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_FREQUENCY_HZ = 5
};

/** Normalization applied to a modulation source before amount scaling. */
typedef uint32_t nk_audio_dsp_modulation_polarity;
enum NK_ENUM(nk_audio_dsp_modulation_polarity) {
    /** Preserve a bipolar LFO/oscillator, or center a unipolar envelope around zero. */
    NK_AUDIO_DSP_MODULATION_BIPOLAR = 0,
    /** Map a bipolar LFO/oscillator to [0, 1], while preserving an envelope's [0, 1]. */
    NK_AUDIO_DSP_MODULATION_UNIPOLAR = 1
};

/** One-based oscillator target; zero targets all oscillator sources. */
enum { NK_AUDIO_DSP_MODULATION_TARGET_ALL = 0 };

/** Maximum number of fixed, ABI-safe modulation routes in one patch. */
enum { NK_AUDIO_DSP_MAX_MODULATION_ROUTES = 8 };

/** Maximum number of independently tuned pitched sources in one patch. */
enum { NK_AUDIO_DSP_MAX_OSCILLATORS = 4 };

/** Instrument parameter identifiers; oscillator-specific values require a source index. */
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
    NK_AUDIO_DSP_PARAMETER_FILTER_RESONANCE = 8,
    /** Per-source waveform; the event oscillator index selects the source. */
    NK_AUDIO_DSP_PARAMETER_OSCILLATOR_WAVEFORM = 9,
    /** Per-source linear level in the inclusive range [0, 1]. */
    NK_AUDIO_DSP_PARAMETER_OSCILLATOR_LEVEL = 10,
    /** Per-source tuning offset in cents. */
    NK_AUDIO_DSP_PARAMETER_OSCILLATOR_DETUNE_CENTS = 11,
    /** Per-source normalized phase offset in the inclusive range [0, 1]. */
    NK_AUDIO_DSP_PARAMETER_OSCILLATOR_PHASE = 12
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
    /** Initial and automatable normalized phase offset in the range [0, 1]. */
    float phase;
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
    /** One-based oscillator target for oscillator destinations; zero targets all. */
    uint32_t oscillator_index;
    /** One-based oscillator source index when source is OSCILLATOR; otherwise zero. */
    uint32_t source_oscillator_index;
    /** Reserved for compatible extensions; set to zero. */
    uint32_t reserved2;
} nk_audio_dsp_modulation_route_options;

/**
 * Immutable reusable DSP patch. Components are evaluated in source, envelope,
 * modulation, filter, and output-gain order. Modulation amounts use the
 * destination's units: semitones for pitch, Hz for filter cutoff and
 * oscillator frequency, linear relative gain for amplitude and oscillator
 * level, and normalized cycles for oscillator phase. LFO and envelope routes
 * may target one source using a one-based index or all sources with
 * NK_AUDIO_DSP_MODULATION_TARGET_ALL. Oscillator-source routes are audio-rate
 * operator routes and require one specific source and target; cycles are
 * rejected.
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
    NK_AUDIO_DSP_EVENT_PARAMETER = 2,
    /** Linearly changes one parameter over duration_frames. */
    NK_AUDIO_DSP_EVENT_PARAMETER_RAMP = 3
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
    /** Instrument for NOTE_ON, PARAMETER, and PARAMETER_RAMP; ignored for NOTE_OFF. */
    nk_audio_dsp_instrument instrument;
    /** Caller-owned voice identity for NOTE_ON/NOTE_OFF; ignored for parameter events. */
    uint32_t voice_id;
    /** MIDI note number for NOTE_ON, in the inclusive range [0, 127]. */
    uint32_t note;
    /** NOTE_ON velocity in the inclusive range [0, 1]. */
    float velocity;
    /** Parameter ID and start value for PARAMETER and PARAMETER_RAMP. */
    nk_audio_dsp_parameter parameter;
    float value;
    /** Zero-based oscillator source for oscillator-specific parameters. */
    uint32_t oscillator_index;
    /** End value for PARAMETER_RAMP; ignored for other event kinds. */
    float end_value;
    /** Number of frames from value to end_value for PARAMETER_RAMP. */
    uint32_t duration_frames;
    /** Reserved for compatible extensions; set all elements to zero. */
    uint64_t reserved2[1];
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
/** Restores instruments and voices to their creation state. Detach before calling;
 * use clear_schedule to reset voices during live playback. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_reset(nk_audio_dsp_engine engine);
/** Routes a DSP renderer into the process-wide playback device output.
 * Multiple renderers may attach independently; repeated attachment is idempotent. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_attach_device(nk_audio_dsp_engine engine);
/** Stops routing a DSP renderer into the process-wide playback device output. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_detach_device(nk_audio_dsp_engine engine);
/**
 * Queues sorted events against the playback device's PCM-frame clock. The
 * block-local frame offsets in events are added to start_frame. Events are
 * copied and may be released after this call.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_schedule(
    nk_audio_dsp_engine engine, uint64_t start_frame,
    const nk_audio_dsp_event *events NK_IN_ARRAY(event_count), uint32_t event_count);
/** Invalidates queued events and requests a voice reset on the audio consumer.
 * Attached queue slots become reusable when the consumer next drains them. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_clear_schedule(nk_audio_dsp_engine engine);

/** Specialized DaisySP generator. Existing patches keep the oscillator graph. */
typedef uint32_t nk_audio_dsp_source_kind;
enum NK_ENUM(nk_audio_dsp_source_kind) {
    NK_AUDIO_DSP_SOURCE_FM2 = 1,
    NK_AUDIO_DSP_SOURCE_STRING_VOICE = 2,
    NK_AUDIO_DSP_SOURCE_KARPLUS_STRING = 3,
    NK_AUDIO_DSP_SOURCE_MODAL_VOICE = 4,
    NK_AUDIO_DSP_SOURCE_RESONATOR = 5,
    NK_AUDIO_DSP_SOURCE_DRIP = 6,
    NK_AUDIO_DSP_SOURCE_ANALOG_BASS_DRUM = 7,
    NK_AUDIO_DSP_SOURCE_SYNTHETIC_BASS_DRUM = 8,
    NK_AUDIO_DSP_SOURCE_ANALOG_SNARE_DRUM = 9,
    NK_AUDIO_DSP_SOURCE_SYNTHETIC_SNARE_DRUM = 10,
    NK_AUDIO_DSP_SOURCE_HI_HAT = 11,
    NK_AUDIO_DSP_SOURCE_FORMANT = 12,
    NK_AUDIO_DSP_SOURCE_VOSIM = 13,
    NK_AUDIO_DSP_SOURCE_ZOSC = 14,
    NK_AUDIO_DSP_SOURCE_VARIABLE_SAW = 15,
    NK_AUDIO_DSP_SOURCE_VARIABLE_SHAPE = 16,
    NK_AUDIO_DSP_SOURCE_OSCILLATOR_BANK = 17,
    NK_AUDIO_DSP_SOURCE_HARMONIC = 18,
    NK_AUDIO_DSP_SOURCE_GRAINLET = 19,
    NK_AUDIO_DSP_SOURCE_PARTICLE = 20,
    NK_AUDIO_DSP_SOURCE_DUST = 21,
    NK_AUDIO_DSP_SOURCE_CLOCKED_NOISE = 22,
    NK_AUDIO_DSP_SOURCE_FRACTAL_NOISE = 23,
    NK_AUDIO_DSP_SOURCE_GRANULAR = 24
};
#define NK_AUDIO_DSP_SOURCE_KIND_COUNT 24
typedef uint32_t nk_audio_dsp_source_parameter;
enum NK_ENUM(nk_audio_dsp_source_parameter) {
    NK_AUDIO_DSP_SOURCE_PARAMETER_RATIO = 0,
    NK_AUDIO_DSP_SOURCE_PARAMETER_INDEX = 1,
    NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT = 2,
    NK_AUDIO_DSP_SOURCE_PARAMETER_STRUCTURE = 3,
    NK_AUDIO_DSP_SOURCE_PARAMETER_BRIGHTNESS = 4,
    NK_AUDIO_DSP_SOURCE_PARAMETER_DAMPING = 5,
    NK_AUDIO_DSP_SOURCE_PARAMETER_NONLINEARITY = 6,
    NK_AUDIO_DSP_SOURCE_PARAMETER_POSITION = 7,
    NK_AUDIO_DSP_SOURCE_PARAMETER_RESOLUTION = 8,
    NK_AUDIO_DSP_SOURCE_PARAMETER_DETTACK = 9,
    NK_AUDIO_DSP_SOURCE_PARAMETER_TONE = 10,
    NK_AUDIO_DSP_SOURCE_PARAMETER_DECAY = 11,
    NK_AUDIO_DSP_SOURCE_PARAMETER_ATTACK_FM = 12,
    NK_AUDIO_DSP_SOURCE_PARAMETER_SELF_FM = 13,
    NK_AUDIO_DSP_SOURCE_PARAMETER_DIRTINESS = 14,
    NK_AUDIO_DSP_SOURCE_PARAMETER_FM_AMOUNT = 15,
    NK_AUDIO_DSP_SOURCE_PARAMETER_FM_DECAY = 16,
    NK_AUDIO_DSP_SOURCE_PARAMETER_SNAPPY = 17,
    NK_AUDIO_DSP_SOURCE_PARAMETER_NOISINESS = 18,
    NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO = 19,
    NK_AUDIO_DSP_SOURCE_PARAMETER_PHASE_SHIFT = 20,
    NK_AUDIO_DSP_SOURCE_PARAMETER_SECOND_FORMANT_RATIO = 21,
    NK_AUDIO_DSP_SOURCE_PARAMETER_SHAPE = 22,
    NK_AUDIO_DSP_SOURCE_PARAMETER_MODE = 23,
    NK_AUDIO_DSP_SOURCE_PARAMETER_PULSE_WIDTH = 24,
    NK_AUDIO_DSP_SOURCE_PARAMETER_SYNC_RATIO = 25,
    NK_AUDIO_DSP_SOURCE_PARAMETER_FIRST_HARMONIC = 26,
    NK_AUDIO_DSP_SOURCE_PARAMETER_BLEED = 27,
    NK_AUDIO_DSP_SOURCE_PARAMETER_RESONANCE = 28,
    NK_AUDIO_DSP_SOURCE_PARAMETER_RANDOM_RATE = 29,
    NK_AUDIO_DSP_SOURCE_PARAMETER_DENSITY = 30,
    NK_AUDIO_DSP_SOURCE_PARAMETER_SPREAD = 31,
    NK_AUDIO_DSP_SOURCE_PARAMETER_COLOR = 32,
    NK_AUDIO_DSP_SOURCE_PARAMETER_SPEED = 33,
    NK_AUDIO_DSP_SOURCE_PARAMETER_ROOT_NOTE = 34,
    NK_AUDIO_DSP_SOURCE_PARAMETER_GRAIN_MS = 35,
    NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN = 36,
    NK_AUDIO_DSP_SOURCE_PARAMETER_SYNC_ENABLED = 37
};
#define NK_AUDIO_DSP_SOURCE_PARAMETER_COUNT 38
#define NK_AUDIO_DSP_SOURCE_AMPLITUDE_COUNT 16
/** Values use source-specific domains; obtain defaults before editing.
 * Spectral amplitudes are nonnegative and must sum to one (7 for bank, 16 for harmonic).
 * Other sources require zero amplitudes. Source configuration is immutable.
 */
typedef struct nk_audio_dsp_source_options {
    uint32_t struct_size;
    nk_audio_dsp_source_kind kind;
    float values[NK_AUDIO_DSP_SOURCE_PARAMETER_COUNT];
    float amplitudes[NK_AUDIO_DSP_SOURCE_AMPLITUDE_COUNT];
} nk_audio_dsp_source_options;
/** Returns domains for supported controls; unsupported controls return INVALID_ARGUMENT. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_source_parameter_info(
    nk_audio_dsp_source_kind kind, nk_audio_dsp_source_parameter parameter,
    float *out_minimum NK_OUT, float *out_maximum NK_OUT, float *out_default NK_OUT);
/** Initializes all fields, including struct_size. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_source_defaults(
    nk_audio_dsp_source_kind kind, nk_audio_dsp_source_options *out_options NK_OUT);
/** Copies mono PCM at the engine sample rate for Granular; all other sources require zero samples.
 * Samples are finite in [-1,1], between 2 and 1048576 frames. Note pitch is relative to RootNote.
 * Specialized sources replace oscillator output except KarplusString/Resonator, which process it.
 * Replacement sources require one neutral oscillator slot (SINE, no wavetable or phase).
 * Its level and detune control the generator. Pitch/level routes target oscillator 1;
 * oscillator operator and phase routes are rejected. Noise remains additive.
 * Resonator resolution is a multiple of four; harmonic index and root note are integers.
 */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_patch_create_source(
    const nk_audio_dsp_patch_options *options, const nk_audio_dsp_source_options *source,
    const float *samples NK_IN_ARRAY(sample_count), uint32_t sample_count,
    nk_audio_dsp_patch *out_patch NK_OUT NK_OWNED);

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
/** Sets one oscillator-specific parameter between render calls. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_instrument_set_oscillator_parameter(
    nk_audio_dsp_instrument instrument, uint32_t oscillator_index, nk_audio_dsp_parameter parameter,
    float value);
/** Reads one current instrument parameter. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_instrument_get_parameter(
    nk_audio_dsp_instrument instrument, nk_audio_dsp_parameter parameter, float *out_value NK_OUT);
/** Reads one oscillator-specific parameter. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_instrument_get_oscillator_parameter(
    nk_audio_dsp_instrument instrument, uint32_t oscillator_index, nk_audio_dsp_parameter parameter,
    float *out_value NK_OUT);

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

/** Byte-buffer entry point for managed callers; sample storage is interleaved float32. */
NKAUDIO_API nk_result NK_CALL nk_audio_dsp_engine_render_buffer(
    nk_audio_dsp_engine engine, void *samples NK_IN_ARRAY(sample_bytes), uint64_t sample_bytes,
    uint32_t frame_count, uint32_t channels,
    const nk_audio_dsp_event *events NK_IN_ARRAY(event_count), uint32_t event_count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NATIVEKIT_AUDIO_DSP_H */
