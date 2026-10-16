#include "nativekit_audio_dsp.h"

#include "audio_dsp_backend.hpp"
#include "audio_soundfont.hpp"
#include "audio_dsp_internal.hpp"
#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t default_sample_rate = 48000;
constexpr uint32_t default_channels = 2;
constexpr uint32_t default_block_size = 256;
constexpr uint32_t default_max_voices = 64;
constexpr uint32_t max_channels = 8;
constexpr uint32_t max_block_size = 65536;
constexpr uint32_t max_voice_count = 4096;
constexpr uint32_t min_wavetable_samples = 32;
constexpr uint32_t max_wavetable_samples = 4096;
constexpr nk_audio_dsp_capabilities builtin_capabilities =
    NK_AUDIO_DSP_CAPABILITY_OSCILLATOR | NK_AUDIO_DSP_CAPABILITY_NOISE |
    NK_AUDIO_DSP_CAPABILITY_WAVETABLE | NK_AUDIO_DSP_CAPABILITY_ENVELOPE |
    NK_AUDIO_DSP_CAPABILITY_LFO | NK_AUDIO_DSP_CAPABILITY_FILTER | NK_AUDIO_DSP_CAPABILITY_FM |
    NK_AUDIO_DSP_CAPABILITY_PHASE_MODULATION | NK_AUDIO_DSP_CAPABILITY_MODULATION |
    NK_AUDIO_DSP_CAPABILITY_SPECIALIZED_SOURCE | NK_AUDIO_DSP_CAPABILITY_STEREO_UNISON |
    NK_AUDIO_DSP_CAPABILITY_FILTER_ENVELOPE | NK_AUDIO_DSP_CAPABILITY_MULTI_LFO |
    NK_AUDIO_DSP_CAPABILITY_PAN_MODULATION;

using DspParameters = nk::audio_dsp::PatchParameters;

struct DspEngineResource;
struct DspPatchResource;
struct DspWavetableResource;
struct DspInstrumentResource;
constexpr uint32_t scheduled_event_capacity = 8192;
constexpr uint32_t scheduled_event_mask = scheduled_event_capacity - 1;
constexpr uint32_t max_attached_parameter_ramps = 128;

struct ScheduledDspEvent {
    uint64_t frame = 0;
    uint64_t generation = 0;
    nk_audio_dsp_event event{};
    uint64_t note_token = 0, sequence = 0;
    uint64_t hold_frames = 0;
    int32_t priority = 0;
    std::shared_ptr<DspInstrumentResource> instrument;
};

struct DspPatchResource final : nk::core::Resource {
    DspParameters parameters;
};

struct DspWavetableResource final : nk::core::Resource {
    std::shared_ptr<const nk::audio_dsp::Wavetable> table;
};

struct SoundFontResource final : nk::core::Resource {
    std::shared_ptr<nk::audio_dsp::SoundFontBank> bank;
    struct Preset {
        uint32_t bank, program;
        std::string name;
    };
    std::vector<Preset> presets;
};

struct DspInstrumentResource final : nk::core::Resource {
    std::weak_ptr<DspEngineResource> engine;
    std::shared_ptr<DspPatchResource> patch;
    std::shared_ptr<nk::audio_dsp::InstrumentSource> provider;
    DspParameters defaults;
    DspParameters current;
    std::atomic<uint32_t> choke_group{0};
    uint64_t parameters_version = 1;
    std::array<std::atomic<float>, 9> published{};
    std::array<std::array<std::atomic<float>, 4>, NK_AUDIO_DSP_MAX_OSCILLATORS>
        published_oscillators{};
    bool retired = false;
};

struct MonoNote {
    uint32_t voice_id = 0, note = 0, score_note = 0;
    uint64_t token = 0, release_frame = 0, score_cycle = 0;
    float velocity = 0;
    int32_t priority = 0;
};
inline constexpr uint32_t max_mono_notes = 16;

struct DspVoice {
    nk::audio_dsp::Voice backend;
    std::shared_ptr<nk::audio_dsp::InstrumentSource> provider;
    int provider_slot = -1;
    std::shared_ptr<DspInstrumentResource> instrument;
    uint32_t voice_id = 0;
    uint32_t note = 0;
    float velocity = 0.0f;
    uint64_t parameters_version = 0;
    bool active = false;
    bool released = false;
    uint64_t note_token = 0;
    uint64_t started = 0;
    uint64_t release_frame = 0;
    uint64_t score_cycle = 0;
    uint32_t score_note = 0;
    uint32_t choke_group = 0;
    int32_t priority = 0;
    uint32_t fade_remaining = 0, fade_in_remaining = 0;
    float energy = 0;
    std::array<MonoNote, max_mono_notes> held{};
    uint32_t held_count = 0;

    void clear() noexcept {
        if (provider)
            provider->release(provider_slot);
        provider_slot = -1;
        provider.reset();
        backend.release_resources();
        instrument.reset();
        voice_id = 0;
        note = 0;
        velocity = 0.0f;
        parameters_version = 0;
        active = false;
        released = false;
        note_token = started = release_frame = score_cycle = 0;
        score_note = fade_remaining = fade_in_remaining = 0;
        priority = 0;
        energy = 0;
        held_count = 0;
    }
};

struct ActiveParameterRamp {
    std::shared_ptr<DspInstrumentResource> instrument;
    nk_audio_dsp_parameter parameter;
    uint32_t oscillator_index;
    float start_value;
    float end_value;
    uint64_t start_frame;
    uint64_t end_frame;
};

struct TransportEvent {
    nk_audio_dsp_transport_event value{};
    double end_frame = 0;
    std::shared_ptr<DspInstrumentResource> instrument;
};
struct TransportTempo {
    double frame, bpm;
};
struct TransportSection {
    double length = 0;
    std::vector<TransportEvent> events;
    std::vector<double> bars;
    std::vector<TransportTempo> tempos;
};
struct TransportProgram {
    uint64_t id = 0, last_command = 0;
    bool retired = false;
    std::vector<TransportSection> sections;
};
struct RuntimePublication {
    std::atomic<bool> ready{false};
    std::atomic<uint32_t> remaining{0};
    uint64_t frame = 0;
};
struct TransportCommand {
    enum Kind { play, relocate, set_paused, request, stop, queued_note } kind = stop;
    TransportProgram *program = nullptr;
    uint64_t sequence = 0, frame = 0;
    uint32_t section = 0;
    double position = 0;
    bool paused = false;
    ScheduledDspEvent note;
    std::shared_ptr<RuntimePublication> publication;
};
constexpr uint32_t transport_command_capacity = 64;
struct NativeTransport {
    TransportProgram *program = nullptr;
    uint64_t start = 0, cycle = 1, loops = 0;
    double origin = 0;
    uint32_t section = 0, event = 0;
    uint32_t pending = UINT32_MAX;
    double boundary = 0;
    double bpm = 120;
    uint64_t next_frame = 0;
    bool has_frame = false;
    bool paused = false;
    double paused_position = 0;
    uint64_t paused_frame = 0;
};

struct DspEngineResource final : nk::core::Resource, nk::audio_dsp::DeviceOutput {
    nk_audio_dsp_engine_options options{};
    std::mutex mutex;
    std::vector<DspVoice> voices;
    // Strong control-side pins prevent final destruction on the audio callback.
    std::vector<std::shared_ptr<DspInstrumentResource>> instruments;
    std::vector<DspVoice> tails;
    bool stealing = false;
    uint32_t fade_frames = 240;
    uint64_t voice_serial = 0, offline_frame = 0;
    uint64_t runtime_next_frame = 0;
    bool runtime_has_frame = false;
    std::atomic<uint64_t> next_note_token{1};
    std::atomic<uint64_t> stolen_voices{0}, dropped_notes{0}, clock_discontinuities{0};
    std::vector<std::unique_ptr<TransportProgram>> programs;
    uint64_t next_program = 1, next_command = 1;
    std::array<TransportCommand, transport_command_capacity> transport_commands{};
    std::atomic<uint32_t> transport_write{0}, transport_read{0};
    std::atomic<uint64_t> consumed_command{0}, active_program{0};
    NativeTransport transport;
    const std::shared_ptr<nk::audio_dsp::TransportClock> clock =
        std::make_shared<nk::audio_dsp::TransportClock>();
    std::vector<std::shared_ptr<RuntimePublication>> publications;
    std::array<ScheduledDspEvent, 512> note_heap{};
    uint32_t note_heap_count = 0;
    std::atomic<uint64_t> status_sequence{0}, status_frame{0}, status_loops{0};
    std::atomic<uint32_t> status_section{0}, status_playing{0};
    std::atomic<uint32_t> status_voices{0}, status_tails{0};
    std::atomic<double> status_bpm{120}, status_section_frame{0};
    std::array<ScheduledDspEvent, scheduled_event_capacity> scheduled_events{};
    std::atomic<uint32_t> scheduled_write{0};
    std::atomic<uint32_t> scheduled_read{0};
    std::atomic<uint64_t> scheduled_generation{1};
    std::atomic<bool> attached{false};
    std::atomic<uint32_t> output_readers{0};
    uint64_t output_frame = 0;
    uint32_t output_sample_rate = 0;
    uint32_t output_channels = 0, device_quantum = 0;
    std::vector<float> output_scratch;
    std::array<ActiveParameterRamp, max_attached_parameter_ramps> output_ramps{};
    uint32_t output_ramp_count = 0;
    uint64_t output_generation = 0;
    void process(float *, uint64_t, uint32_t, uint32_t, uint64_t) noexcept override;
    void detach() noexcept override;
    bool alive = true;
    ~DspEngineResource() override {
        for (auto &command : transport_commands)
            if (command.publication) {
                auto *publication = command.publication.get();
                command.publication.reset();
                publication->remaining.fetch_sub(1, std::memory_order_release);
            }
    }
};

nk_result invalid_argument(const char *message) {
    nk::core::set_error(message);
    return NK_ERROR_INVALID_ARGUMENT;
}

nk_result invalid_request(const char *message) {
    nk::core::set_error(message);
    return NK_ERROR_INVALID_REQUEST;
}

nk_result enter_dsp() {
    nk::core::clear_error();
    if (nk::core::runtime_generation() == 0) {
        nk::core::set_error("NativeKit is not initialized");
        return NK_ERROR_NOT_INITIALIZED;
    }
    return NK_OK;
}

std::shared_ptr<DspEngineResource> get_engine(nk_audio_dsp_engine handle) {
    auto resource = nk::core::handles().get(handle, nk::core::ResourceType::audio_dsp_engine);
    if (!resource) {
        nk::core::set_error("invalid audio DSP engine handle");
        return {};
    }
    return std::dynamic_pointer_cast<DspEngineResource>(std::move(resource));
}

std::shared_ptr<DspInstrumentResource> get_instrument(nk_audio_dsp_instrument handle) {
    auto resource = nk::core::handles().get(handle, nk::core::ResourceType::audio_dsp_instrument);
    if (!resource) {
        nk::core::set_error("invalid audio DSP instrument handle");
        return {};
    }
    return std::dynamic_pointer_cast<DspInstrumentResource>(std::move(resource));
}

std::shared_ptr<DspPatchResource> get_patch(nk_audio_dsp_patch handle) {
    auto resource = nk::core::handles().get(handle, nk::core::ResourceType::audio_dsp_patch);
    if (!resource) {
        nk::core::set_error("invalid audio DSP patch handle");
        return {};
    }
    return std::dynamic_pointer_cast<DspPatchResource>(std::move(resource));
}

std::shared_ptr<DspWavetableResource> get_wavetable(nk_audio_dsp_wavetable handle) {
    auto resource = nk::core::handles().get(handle, nk::core::ResourceType::audio_dsp_wavetable);
    if (!resource) {
        nk::core::set_error("invalid audio DSP wavetable handle");
        return {};
    }
    return std::dynamic_pointer_cast<DspWavetableResource>(std::move(resource));
}

bool valid_waveform(nk_audio_dsp_waveform waveform) {
    return waveform <= NK_AUDIO_DSP_WAVEFORM_SQUARE;
}

bool valid_filter_type(nk_audio_dsp_filter_type type) {
    return type == NK_AUDIO_DSP_FILTER_NONE || type == NK_AUDIO_DSP_FILTER_SVF_LOW_PASS;
}

bool valid_lfo_mode(nk_audio_dsp_lfo_mode mode) {
    return mode == NK_AUDIO_DSP_LFO_RETRIGGER || mode == NK_AUDIO_DSP_LFO_FREE_RUNNING;
}

bool valid_modulation_source(nk_audio_dsp_modulation_source source) {
    return source == NK_AUDIO_DSP_MODULATION_SOURCE_LFO ||
           source == NK_AUDIO_DSP_MODULATION_SOURCE_ENVELOPE ||
           source == NK_AUDIO_DSP_MODULATION_SOURCE_OSCILLATOR ||
           source == NK_AUDIO_DSP_MODULATION_SOURCE_FILTER_ENVELOPE ||
           source == NK_AUDIO_DSP_MODULATION_SOURCE_LFO2 ||
           source == NK_AUDIO_DSP_MODULATION_SOURCE_LFO3;
}

bool valid_modulation_destination(nk_audio_dsp_modulation_destination destination) {
    return destination == NK_AUDIO_DSP_MODULATION_DESTINATION_PITCH_SEMITONES ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_HZ ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_AMPLITUDE ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_LEVEL ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_PHASE ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_FREQUENCY_HZ ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_PAN ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_SEMITONES;
}

bool valid_modulation_polarity(nk_audio_dsp_modulation_polarity polarity) {
    return polarity == NK_AUDIO_DSP_MODULATION_BIPOLAR ||
           polarity == NK_AUDIO_DSP_MODULATION_UNIPOLAR;
}

bool valid_nonnegative_finite(float value) {
    return std::isfinite(value) && value >= 0.0f;
}

bool valid_oscillator_target(uint32_t oscillator_index) {
    return oscillator_index <= NK_AUDIO_DSP_MAX_OSCILLATORS;
}

bool modulation_destination_targets_oscillator(nk_audio_dsp_modulation_destination destination) {
    return destination == NK_AUDIO_DSP_MODULATION_DESTINATION_PITCH_SEMITONES ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_LEVEL ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_PHASE ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_FREQUENCY_HZ;
}

bool valid_operator_destination(nk_audio_dsp_modulation_destination destination) {
    return destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_PHASE ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_FREQUENCY_HZ;
}

bool valid_operator_routing(const DspParameters &parameters) {
    std::array<std::array<bool, NK_AUDIO_DSP_MAX_OSCILLATORS>, NK_AUDIO_DSP_MAX_OSCILLATORS>
        edges{};
    std::array<uint32_t, NK_AUDIO_DSP_MAX_OSCILLATORS> incoming{};
    for (uint32_t index = 0; index < parameters.route_count; ++index) {
        const auto &route = parameters.routes[index];
        if (route.source != NK_AUDIO_DSP_MODULATION_SOURCE_OSCILLATOR)
            continue;
        const auto source = route.source_oscillator_index - 1;
        const auto target = route.oscillator_index - 1;
        if (source >= parameters.oscillator_count || target >= parameters.oscillator_count ||
            source == target || !valid_operator_destination(route.destination))
            return false;
        if (!edges[source][target]) {
            edges[source][target] = true;
            ++incoming[target];
        }
    }

    std::array<bool, NK_AUDIO_DSP_MAX_OSCILLATORS> removed{};
    for (uint32_t count = 0; count < parameters.oscillator_count; ++count) {
        uint32_t next = NK_AUDIO_DSP_MAX_OSCILLATORS;
        for (uint32_t candidate = 0; candidate < parameters.oscillator_count; ++candidate) {
            if (!removed[candidate] && incoming[candidate] == 0) {
                next = candidate;
                break;
            }
        }
        if (next == NK_AUDIO_DSP_MAX_OSCILLATORS)
            return false;
        removed[next] = true;
        for (uint32_t target = 0; target < parameters.oscillator_count; ++target) {
            if (edges[next][target])
                --incoming[target];
        }
    }
    return true;
}

bool valid_wavetable_sample_count(uint32_t sample_count) {
    return sample_count >= min_wavetable_samples && sample_count <= max_wavetable_samples &&
           (sample_count & (sample_count - 1)) == 0;
}

bool valid_patch_parameters(const DspParameters &parameters) {
    if (parameters.oscillator_count > NK_AUDIO_DSP_MAX_OSCILLATORS)
        return false;
    for (uint32_t index = 0; index < parameters.oscillator_count; ++index) {
        const auto &oscillator = parameters.oscillators[index];
        if (!valid_waveform(oscillator.waveform) || !valid_nonnegative_finite(oscillator.level) ||
            oscillator.level > 1.0f || !std::isfinite(oscillator.detune_cents) ||
            !std::isfinite(oscillator.phase) || oscillator.phase < 0.0f || oscillator.phase > 1.0f)
            return false;
    }
    if (!valid_nonnegative_finite(parameters.noise.level) || parameters.noise.level > 1.0f ||
        !valid_nonnegative_finite(parameters.gain) ||
        !valid_nonnegative_finite(parameters.envelope.attack_seconds) ||
        !valid_nonnegative_finite(parameters.envelope.decay_seconds) ||
        !std::isfinite(parameters.envelope.sustain_level) ||
        parameters.envelope.sustain_level < 0.0f || parameters.envelope.sustain_level > 1.0f ||
        !valid_nonnegative_finite(parameters.envelope.release_seconds) ||
        !valid_filter_type(parameters.filter.type) ||
        !valid_nonnegative_finite(parameters.filter.cutoff_hz) ||
        !std::isfinite(parameters.filter.resonance) || parameters.filter.resonance < 0.0f ||
        parameters.filter.resonance > 1.0f ||
        (parameters.filter.type == NK_AUDIO_DSP_FILTER_NONE &&
         parameters.filter.cutoff_hz != 0.0f) ||
        !valid_waveform(parameters.lfo.waveform) || !valid_lfo_mode(parameters.lfo.mode) ||
        !valid_nonnegative_finite(parameters.lfo.rate_hz) || !std::isfinite(parameters.lfo.phase) ||
        parameters.lfo.phase < 0.0f || parameters.lfo.phase > 1.0f ||
        parameters.route_count > NK_AUDIO_DSP_MAX_MODULATION_ROUTES)
        return false;
    for (uint32_t index = 0; index < parameters.route_count; ++index) {
        const auto &route = parameters.routes[index];
        if (!valid_modulation_source(route.source) ||
            !valid_modulation_destination(route.destination) ||
            !valid_modulation_polarity(route.polarity) || !std::isfinite(route.amount) ||
            !valid_oscillator_target(route.oscillator_index) ||
            (route.oscillator_index != NK_AUDIO_DSP_MODULATION_TARGET_ALL &&
             route.oscillator_index > parameters.oscillator_count) ||
            (!modulation_destination_targets_oscillator(route.destination) &&
             route.oscillator_index != NK_AUDIO_DSP_MODULATION_TARGET_ALL) ||
            (route.source == NK_AUDIO_DSP_MODULATION_SOURCE_OSCILLATOR &&
             (route.source_oscillator_index == 0 ||
              route.source_oscillator_index > parameters.oscillator_count ||
              route.oscillator_index == NK_AUDIO_DSP_MODULATION_TARGET_ALL)) ||
            (route.source != NK_AUDIO_DSP_MODULATION_SOURCE_OSCILLATOR &&
             route.source_oscillator_index != 0))
            return false;
    }
    return valid_operator_routing(parameters);
}

bool valid_filter_cutoff(float cutoff_hz, uint32_t sample_rate) {
    return cutoff_hz == 0.0f || cutoff_hz <= static_cast<float>(sample_rate) / 3.0f;
}

nk_result validate_engine_parameters(const DspParameters &parameters,
                                     const DspEngineResource &engine) {
    if (!valid_patch_parameters(parameters) ||
        !valid_filter_cutoff(parameters.filter.cutoff_hz, engine.options.sample_rate))
        return invalid_argument("audio DSP patch parameters are invalid");
    return NK_OK;
}

nk_result normalize_engine_options(const nk_audio_dsp_engine_options *input,
                                   nk_audio_dsp_engine_options &output) {
    output = {};
    output.struct_size = sizeof(output);
    output.sample_rate = default_sample_rate;
    output.channels = default_channels;
    output.block_size = default_block_size;
    output.max_voices = default_max_voices;
    if (!input)
        return NK_OK;
    if (input->struct_size < sizeof(nk_audio_dsp_engine_options))
        return invalid_argument("audio DSP engine options are missing or too small");
    if (input->sample_rate != 0)
        output.sample_rate = input->sample_rate;
    if (input->channels != 0)
        output.channels = input->channels;
    if (input->block_size != 0)
        output.block_size = input->block_size;
    if (input->max_voices != 0)
        output.max_voices = input->max_voices;
    if (output.sample_rate == 0 || output.sample_rate > 384000)
        return invalid_argument("audio DSP sample rate is out of range");
    if (output.channels == 0 || output.channels > max_channels)
        return invalid_argument("audio DSP channel count is out of range");
    if (output.block_size == 0 || output.block_size > max_block_size)
        return invalid_argument("audio DSP block size is out of range");
    if (output.max_voices == 0 || output.max_voices > max_voice_count)
        return invalid_argument("audio DSP voice count is out of range");
    return NK_OK;
}

nk_result normalize_voice_options(const nk_audio_dsp_voice_options *input, DspParameters &output) {
    if (input) {
        // The prefix is the complete v1 struct, including its tail padding.
        constexpr auto v1_size = offsetof(nk_audio_dsp_voice_options, additional_lfos);
        constexpr auto v2_size = offsetof(nk_audio_dsp_voice_options, filter_drive);
        constexpr auto v3_size = offsetof(nk_audio_dsp_voice_options, monophonic);
        if (input->struct_size != v1_size && input->struct_size != v2_size &&
            input->struct_size != v3_size && input->struct_size < sizeof(*input))
            return invalid_argument("audio DSP voice options are missing or truncated");
        const auto &env = input->filter_envelope;
        if (env.struct_size < sizeof(env) ||
            input->unison_count > nk::audio_dsp::max_unison_copies ||
            !valid_nonnegative_finite(input->unison_detune_cents) ||
            input->unison_detune_cents > 100 ||
            !valid_nonnegative_finite(input->unison_stereo_spread) ||
            input->unison_stereo_spread > 1 ||
            !std::isfinite(input->filter_envelope_amount_semitones) ||
            std::abs(input->filter_envelope_amount_semitones) > 96 ||
            !valid_nonnegative_finite(env.attack_seconds) ||
            !valid_nonnegative_finite(env.decay_seconds) ||
            !valid_nonnegative_finite(env.sustain_level) || env.sustain_level > 1 ||
            !valid_nonnegative_finite(env.release_seconds))
            return invalid_argument("invalid DSP unison or filter envelope configuration");
        output.unison = {input->unison_count == 0 ? 1 : input->unison_count,
                         input->unison_detune_cents, input->unison_stereo_spread};
        output.filter_envelope = {env.attack_seconds, env.decay_seconds, env.sustain_level,
                                  env.release_seconds};
        output.filter_envelope_amount_semitones = input->filter_envelope_amount_semitones;
        if (input->struct_size >= v3_size) {
            if (!valid_nonnegative_finite(input->filter_drive) || input->filter_drive > 1)
                return invalid_argument("invalid DSP filter drive");
            output.filter_drive = input->filter_drive;
        }
        if (input->struct_size >= sizeof(*input)) {
            if (input->monophonic > 1 || input->legato > 1 ||
                !valid_nonnegative_finite(input->glide_seconds) || input->glide_seconds > 2 ||
                !valid_nonnegative_finite(input->accent_gain) || input->accent_gain > 1 ||
                !valid_nonnegative_finite(input->accent_filter_semitones) ||
                input->accent_filter_semitones > 36 ||
                !valid_nonnegative_finite(input->accent_velocity_threshold) ||
                input->accent_velocity_threshold >= 1 ||
                (!input->monophonic && (input->legato || input->glide_seconds > 0)) ||
                (input->monophonic && output.source.options.kind != 0))
                return invalid_argument("invalid DSP articulation configuration");
            output.monophonic = input->monophonic != 0;
            output.legato = input->legato != 0;
            output.glide_seconds = input->glide_seconds;
            output.accent_gain = input->accent_gain;
            output.accent_filter_semitones = input->accent_filter_semitones;
            output.accent_velocity_threshold = input->accent_velocity_threshold;
        }
        if (input->struct_size >= v2_size) {
            if (input->additional_lfo_count >= NK_AUDIO_DSP_MAX_LFOS)
                return invalid_argument("too many additional DSP LFOs");
            output.additional_lfo_count = input->additional_lfo_count;
            for (uint32_t i = 0; i < output.additional_lfo_count; ++i) {
                const auto &lfo = input->additional_lfos[i];
                if (lfo.struct_size < sizeof(lfo) || !valid_waveform(lfo.waveform) ||
                    !valid_lfo_mode(lfo.mode) || !valid_nonnegative_finite(lfo.rate_hz) ||
                    !std::isfinite(lfo.phase) || lfo.phase < 0 || lfo.phase > 1)
                    return invalid_argument("invalid additional DSP LFO");
                output.additional_lfos[i] = {lfo.waveform, lfo.mode, lfo.rate_hz, lfo.phase};
            }
        }
    }
    for (uint32_t i = 0; i < output.route_count; ++i) {
        const auto source = output.routes[i].source;
        if ((source == NK_AUDIO_DSP_MODULATION_SOURCE_LFO2 && output.additional_lfo_count < 1) ||
            (source == NK_AUDIO_DSP_MODULATION_SOURCE_LFO3 && output.additional_lfo_count < 2))
            return invalid_argument("DSP modulation route references an unconfigured LFO");
    }
    return NK_OK;
}

nk_result normalize_patch_options(const nk_audio_dsp_patch_options *input, DspParameters &output) {
    output = {};
    if (!input)
        return NK_OK;
    if (input->struct_size < sizeof(nk_audio_dsp_patch_options))
        return invalid_argument("audio DSP patch options are missing or too small");
    if (input->noise.struct_size < sizeof(nk_audio_dsp_noise_options) ||
        input->envelope.struct_size < sizeof(nk_audio_dsp_envelope_options) ||
        input->filter.struct_size < sizeof(nk_audio_dsp_filter_options) ||
        input->lfo.struct_size < sizeof(nk_audio_dsp_lfo_options))
        return invalid_argument("audio DSP patch component options are missing or too small");
    if (input->oscillator_count > NK_AUDIO_DSP_MAX_OSCILLATORS)
        return invalid_argument("audio DSP patch has too many oscillators");
    if (input->route_count > NK_AUDIO_DSP_MAX_MODULATION_ROUTES)
        return invalid_argument("audio DSP patch has too many modulation routes");
    output.oscillator_count = input->oscillator_count;
    for (uint32_t index = 0; index < output.oscillator_count; ++index) {
        const auto &input_oscillator = input->oscillators[index];
        if (input_oscillator.struct_size < sizeof(nk_audio_dsp_oscillator_options))
            return invalid_argument("audio DSP oscillator options are missing or too small");
        auto &oscillator = output.oscillators[index];
        oscillator.waveform = input_oscillator.waveform;
        oscillator.level = input_oscillator.level;
        oscillator.detune_cents = input_oscillator.detune_cents;
        oscillator.phase = input_oscillator.phase;
        if (input_oscillator.wavetable != NK_INVALID_HANDLE) {
            auto wavetable = get_wavetable(input_oscillator.wavetable);
            if (!wavetable)
                return NK_ERROR_INVALID_HANDLE;
            oscillator.wavetable = wavetable->table;
        }
    }
    output.noise.level = input->noise.level;
    output.envelope.attack_seconds = input->envelope.attack_seconds;
    output.envelope.decay_seconds = input->envelope.decay_seconds;
    output.envelope.sustain_level = input->envelope.sustain_level;
    output.envelope.release_seconds = input->envelope.release_seconds;
    output.filter.type = input->filter.type;
    output.filter.cutoff_hz = input->filter.cutoff_hz;
    output.filter.resonance = input->filter.resonance;
    output.gain = input->gain;
    output.lfo.waveform = input->lfo.waveform;
    output.lfo.mode = input->lfo.mode;
    output.lfo.rate_hz = input->lfo.rate_hz;
    output.lfo.phase = input->lfo.phase;
    output.route_count = input->route_count;
    for (uint32_t index = 0; index < output.route_count; ++index) {
        const auto &route = input->routes[index];
        if (route.struct_size < sizeof(nk_audio_dsp_modulation_route_options))
            return invalid_argument("audio DSP modulation route is missing or too small");
        output.routes[index].source = route.source;
        output.routes[index].destination = route.destination;
        output.routes[index].polarity = route.polarity;
        output.routes[index].amount = route.amount;
        output.routes[index].oscillator_index = route.oscillator_index;
        output.routes[index].source_oscillator_index = route.source_oscillator_index;
    }
    if (!valid_patch_parameters(output))
        return invalid_argument("audio DSP patch parameters are invalid");
    return NK_OK;
}

nk_result normalize_instrument_options(const nk_audio_dsp_instrument_options *input,
                                       uint32_t sample_rate, DspParameters &output) {
    output = {};
    if (!input)
        return NK_OK;
    if (input->struct_size < sizeof(nk_audio_dsp_instrument_options))
        return invalid_argument("audio DSP instrument options are missing or too small");
    output.oscillator_count = 1;
    output.oscillators[0].waveform = input->waveform;
    output.oscillators[0].level = 1.0f;
    output.gain = input->gain;
    output.envelope.attack_seconds = input->attack_seconds;
    output.envelope.decay_seconds = input->decay_seconds;
    output.envelope.sustain_level = input->sustain_level;
    output.envelope.release_seconds = input->release_seconds;
    if (!valid_patch_parameters(output) ||
        !valid_filter_cutoff(output.filter.cutoff_hz, sample_rate))
        return invalid_argument("audio DSP instrument parameters are invalid");
    return NK_OK;
}

bool valid_parameter(nk_audio_dsp_parameter parameter) {
    return parameter <= NK_AUDIO_DSP_PARAMETER_OSCILLATOR_PHASE;
}

bool valid_oscillator_parameter(nk_audio_dsp_parameter parameter) {
    return parameter == NK_AUDIO_DSP_PARAMETER_OSCILLATOR_WAVEFORM ||
           parameter == NK_AUDIO_DSP_PARAMETER_OSCILLATOR_LEVEL ||
           parameter == NK_AUDIO_DSP_PARAMETER_OSCILLATOR_DETUNE_CENTS ||
           parameter == NK_AUDIO_DSP_PARAMETER_OSCILLATOR_PHASE;
}

bool replacement_source(const DspParameters &parameters) {
    const auto kind = parameters.source.options.kind;
    return kind != 0 && kind != NK_AUDIO_DSP_SOURCE_KARPLUS_STRING &&
           kind != NK_AUDIO_DSP_SOURCE_RESONATOR;
}

nk_result set_oscillator_parameter(DspParameters &parameters, uint32_t oscillator_index,
                                   nk_audio_dsp_parameter parameter, float value) {
    if (!valid_oscillator_parameter(parameter) || oscillator_index >= parameters.oscillator_count ||
        !std::isfinite(value))
        return invalid_argument("audio DSP oscillator parameter target is invalid");
    if (replacement_source(parameters) &&
        (parameter == NK_AUDIO_DSP_PARAMETER_OSCILLATOR_WAVEFORM ||
         parameter == NK_AUDIO_DSP_PARAMETER_OSCILLATOR_PHASE))
        return invalid_argument("specialized source has no waveform or phase control");
    auto &oscillator = parameters.oscillators[oscillator_index];
    switch (parameter) {
    case NK_AUDIO_DSP_PARAMETER_OSCILLATOR_WAVEFORM:
        if (value < 0.0f || value > static_cast<float>(NK_AUDIO_DSP_WAVEFORM_SQUARE) ||
            std::floor(value) != value)
            return invalid_argument("audio DSP oscillator waveform parameter is invalid");
        oscillator.waveform = static_cast<nk_audio_dsp_waveform>(value);
        break;
    case NK_AUDIO_DSP_PARAMETER_OSCILLATOR_LEVEL:
        if (value < 0.0f || value > 1.0f)
            return invalid_argument("audio DSP oscillator level parameter is invalid");
        oscillator.level = value;
        break;
    case NK_AUDIO_DSP_PARAMETER_OSCILLATOR_DETUNE_CENTS:
        if (!std::isfinite(value))
            return invalid_argument("audio DSP oscillator detune parameter is invalid");
        oscillator.detune_cents = value;
        break;
    case NK_AUDIO_DSP_PARAMETER_OSCILLATOR_PHASE:
        if (value < 0.0f || value > 1.0f)
            return invalid_argument("audio DSP oscillator phase parameter is invalid");
        oscillator.phase = value;
        break;
    default:
        return invalid_argument("audio DSP oscillator parameter is invalid");
    }
    return NK_OK;
}

nk_result set_parameter(DspParameters &parameters, nk_audio_dsp_parameter parameter, float value) {
    if (!valid_parameter(parameter) || !std::isfinite(value))
        return invalid_argument("audio DSP parameter is invalid");
    switch (parameter) {
    case NK_AUDIO_DSP_PARAMETER_WAVEFORM:
        if (replacement_source(parameters))
            return invalid_argument("specialized source has no waveform control");
        if (value < 0.0f || value > static_cast<float>(NK_AUDIO_DSP_WAVEFORM_SQUARE) ||
            std::floor(value) != value)
            return invalid_argument("audio DSP waveform parameter is invalid");
        if (parameters.oscillator_count == 0)
            return invalid_argument("audio DSP patch has no oscillator to retune");
        parameters.oscillators[0].waveform = static_cast<nk_audio_dsp_waveform>(value);
        break;
    case NK_AUDIO_DSP_PARAMETER_GAIN:
        if (value < 0.0f)
            return invalid_argument("audio DSP gain parameter is invalid");
        parameters.gain = value;
        break;
    case NK_AUDIO_DSP_PARAMETER_ATTACK_SECONDS:
        if (value < 0.0f)
            return invalid_argument("audio DSP attack parameter is invalid");
        parameters.envelope.attack_seconds = value;
        break;
    case NK_AUDIO_DSP_PARAMETER_DECAY_SECONDS:
        if (value < 0.0f)
            return invalid_argument("audio DSP decay parameter is invalid");
        parameters.envelope.decay_seconds = value;
        break;
    case NK_AUDIO_DSP_PARAMETER_SUSTAIN_LEVEL:
        if (value < 0.0f || value > 1.0f)
            return invalid_argument("audio DSP sustain parameter is invalid");
        parameters.envelope.sustain_level = value;
        break;
    case NK_AUDIO_DSP_PARAMETER_RELEASE_SECONDS:
        if (value < 0.0f)
            return invalid_argument("audio DSP release parameter is invalid");
        parameters.envelope.release_seconds = value;
        break;
    case NK_AUDIO_DSP_PARAMETER_NOISE_LEVEL:
        if (value < 0.0f || value > 1.0f)
            return invalid_argument("audio DSP noise level parameter is invalid");
        parameters.noise.level = value;
        break;
    case NK_AUDIO_DSP_PARAMETER_FILTER_CUTOFF_HZ:
        if (value < 0.0f)
            return invalid_argument("audio DSP filter cutoff parameter is invalid");
        parameters.filter.cutoff_hz = value;
        parameters.filter.type =
            value == 0.0f ? NK_AUDIO_DSP_FILTER_NONE : NK_AUDIO_DSP_FILTER_SVF_LOW_PASS;
        break;
    case NK_AUDIO_DSP_PARAMETER_FILTER_RESONANCE:
        if (value < 0.0f || value > 1.0f)
            return invalid_argument("audio DSP filter resonance parameter is invalid");
        parameters.filter.resonance = value;
        break;
    default:
        return invalid_argument("audio DSP parameter is invalid");
    }
    return NK_OK;
}

float get_parameter(const DspParameters &parameters, nk_audio_dsp_parameter parameter) {
    switch (parameter) {
    case NK_AUDIO_DSP_PARAMETER_WAVEFORM:
        return parameters.oscillator_count == 0
                   ? static_cast<float>(NK_AUDIO_DSP_WAVEFORM_SINE)
                   : static_cast<float>(parameters.oscillators[0].waveform);
    case NK_AUDIO_DSP_PARAMETER_GAIN:
        return parameters.gain;
    case NK_AUDIO_DSP_PARAMETER_ATTACK_SECONDS:
        return parameters.envelope.attack_seconds;
    case NK_AUDIO_DSP_PARAMETER_DECAY_SECONDS:
        return parameters.envelope.decay_seconds;
    case NK_AUDIO_DSP_PARAMETER_SUSTAIN_LEVEL:
        return parameters.envelope.sustain_level;
    case NK_AUDIO_DSP_PARAMETER_RELEASE_SECONDS:
        return parameters.envelope.release_seconds;
    case NK_AUDIO_DSP_PARAMETER_NOISE_LEVEL:
        return parameters.noise.level;
    case NK_AUDIO_DSP_PARAMETER_FILTER_CUTOFF_HZ:
        return parameters.filter.cutoff_hz;
    case NK_AUDIO_DSP_PARAMETER_FILTER_RESONANCE:
        return parameters.filter.resonance;
    default:
        return 0.0f;
    }
}

float get_oscillator_parameter(const DspParameters &parameters, uint32_t oscillator_index,
                               nk_audio_dsp_parameter parameter) {
    if (!valid_oscillator_parameter(parameter) || oscillator_index >= parameters.oscillator_count)
        return 0.0f;
    const auto &oscillator = parameters.oscillators[oscillator_index];
    switch (parameter) {
    case NK_AUDIO_DSP_PARAMETER_OSCILLATOR_WAVEFORM:
        return static_cast<float>(oscillator.waveform);
    case NK_AUDIO_DSP_PARAMETER_OSCILLATOR_LEVEL:
        return oscillator.level;
    case NK_AUDIO_DSP_PARAMETER_OSCILLATOR_DETUNE_CENTS:
        return oscillator.detune_cents;
    case NK_AUDIO_DSP_PARAMETER_OSCILLATOR_PHASE:
        return oscillator.phase;
    default:
        return 0.0f;
    }
}

void publish_parameters(DspInstrumentResource &instrument) noexcept {
    for (uint32_t p = 0; p < 9; ++p)
        instrument.published[p].store(get_parameter(instrument.current, p),
                                      std::memory_order_relaxed);
    for (uint32_t o = 0; o < NK_AUDIO_DSP_MAX_OSCILLATORS; ++o)
        for (uint32_t p = 0; p < 4; ++p)
            instrument.published_oscillators[o][p].store(
                get_oscillator_parameter(instrument.current, o, p + 9), std::memory_order_relaxed);
}

DspParameters control_parameters(const DspInstrumentResource &instrument) {
    auto parameters = instrument.defaults;
    for (uint32_t p = 0; p < 9; ++p)
        (void)set_parameter(parameters, p, instrument.published[p].load(std::memory_order_relaxed));
    for (uint32_t o = 0; o < parameters.oscillator_count; ++o)
        for (uint32_t p = 0; p < 4; ++p)
            (void)set_oscillator_parameter(
                parameters, o, p + 9,
                instrument.published_oscillators[o][p].load(std::memory_order_relaxed));
    return parameters;
}

bool instrument_belongs_to(const std::shared_ptr<DspInstrumentResource> &instrument,
                           const DspEngineResource &engine) {
    return instrument && instrument->engine.lock().get() == &engine;
}

nk_result create_instrument_resource(const std::shared_ptr<DspEngineResource> &engine,
                                     const std::shared_ptr<DspPatchResource> &patch,
                                     const DspParameters &parameters,
                                     nk_audio_dsp_instrument *out_instrument) {
    auto instrument = std::make_shared<DspInstrumentResource>();
    instrument->engine = engine;
    instrument->patch = patch;
    instrument->defaults = parameters;
    instrument->current = parameters;
    publish_parameters(*instrument);
    std::lock_guard lock(engine->mutex);
    if (!engine->alive)
        return invalid_request("audio DSP engine is no longer alive");
    const auto handle =
        nk::core::handles().insert(nk::core::ResourceType::audio_dsp_instrument, instrument);
    if (handle == NK_INVALID_HANDLE) {
        nk::core::set_error("could not allocate an audio DSP instrument handle");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    engine->instruments.emplace_back(instrument);
    *out_instrument = handle;
    return NK_OK;
}

void sync_voice_parameters(DspVoice &voice) noexcept {
    if (!voice.instrument || voice.parameters_version == voice.instrument->parameters_version)
        return;
    if (!voice.instrument->provider)
        voice.backend.set_parameters(voice.instrument->current);
    voice.parameters_version = voice.instrument->parameters_version;
}

nk::audio_dsp::StereoSample voice_sample(DspVoice &voice) noexcept {
    if (!voice.active || !voice.instrument)
        return {};
    sync_voice_parameters(voice);
    auto result = voice.provider ? voice.provider->process(voice.provider_slot)
                                 : voice.backend.process_stereo();
    voice.active =
        voice.provider ? voice.provider->active(voice.provider_slot) : voice.backend.active();
    if (voice.provider) {
        result.left *= voice.instrument->current.gain;
        result.right *= voice.instrument->current.gain;
    }
    voice.energy +=
        0.01f * ((std::abs(result.left) + std::abs(result.right)) * 0.5f - voice.energy);
    if (!voice.active)
        voice.clear();
    return result;
}

DspVoice *find_voice(DspEngineResource &engine, uint32_t voice_id) {
    for (auto &voice : engine.voices) {
        if (!voice.active)
            continue;
        if (voice.note_token == 0 && voice.voice_id == voice_id)
            return &voice;
        for (uint32_t i = 0; i < voice.held_count; ++i)
            if (voice.held[i].token == 0 && voice.held[i].voice_id == voice_id)
                return &voice;
    }
    return nullptr;
}

DspVoice *find_free_voice(DspEngineResource &engine) {
    for (auto &voice : engine.voices) {
        if (!voice.active)
            return &voice;
    }
    return nullptr;
}

DspVoice *find_token(DspEngineResource &engine, uint64_t token) {
    for (auto &voice : engine.voices) {
        if (!voice.active)
            continue;
        if (voice.note_token == token)
            return &voice;
        for (uint32_t i = 0; i < voice.held_count; ++i)
            if (voice.held[i].token == token)
                return &voice;
    }
    return nullptr;
}

DspVoice *allocate_voice(DspEngineResource &engine, int32_t priority) {
    if (auto *voice = find_free_voice(engine)) {
        voice->clear();
        return voice;
    }
    if (!engine.stealing)
        return nullptr;
    DspVoice *candidate = nullptr;
    for (auto &voice : engine.voices) {
        if (!candidate ||
            (voice.released != candidate->released   ? voice.released
             : voice.priority != candidate->priority ? voice.priority < candidate->priority
             : voice.released && voice.energy != candidate->energy
                 ? voice.energy < candidate->energy
                 : voice.started < candidate->started))
            candidate = &voice;
    }
    if (!candidate || (!candidate->released && candidate->priority > priority))
        return nullptr;
    for (auto &tail : engine.tails) {
        if (tail.active)
            continue;
        tail.clear();
        std::swap(tail, *candidate); // Both backends were allocated during preparation.
        if (tail.provider)
            tail.provider->note_off(tail.provider_slot);
        else
            tail.backend.note_off();
        tail.released = true;
        tail.fade_remaining = engine.fade_frames;
        candidate->fade_in_remaining = engine.fade_frames;
        engine.stolen_voices.fetch_add(1, std::memory_order_relaxed);
        return candidate;
    }
    return nullptr; // Reserved tail budget is exhausted; never allocate a tail here.
}

void release_voice(DspVoice *voice);

nk_result start_voice(DspEngineResource &engine,
                      const std::shared_ptr<DspInstrumentResource> &instrument,
                      const nk_audio_dsp_event &event, uint64_t token = 0, int32_t priority = 0,
                      uint64_t release_frame = 0, uint64_t score_cycle = 0,
                      uint32_t score_note = 0) {
    auto *voice = token == 0 ? find_voice(engine, event.voice_id) : find_token(engine, token);
    if (!voice && instrument->current.monophonic)
        for (auto &candidate : engine.voices)
            if (candidate.active && candidate.instrument == instrument) {
                voice = &candidate;
                break;
            }
    if (!voice)
        voice = allocate_voice(engine, priority);
    if (!voice) {
        engine.dropped_notes.fetch_add(1, std::memory_order_relaxed);
        return NK_ERROR_QUEUE_FULL;
    }
    const bool instrument_changed = voice->instrument.get() != instrument.get();
    const bool connected =
        instrument->current.monophonic && !instrument_changed && voice->active && !voice->released;
    if (instrument_changed || !connected)
        voice->held_count = 0;
    if (instrument->current.monophonic) {
        // Repeated identities replace their previous entry instead of consuming capacity.
        for (uint32_t i = 0; i < voice->held_count; ++i) {
            const auto &held = voice->held[i];
            if (token ? held.token == token : held.token == 0 && held.voice_id == event.voice_id) {
                for (uint32_t j = i + 1; j < voice->held_count; ++j)
                    voice->held[j - 1] = voice->held[j];
                --voice->held_count;
                break;
            }
        }
        if (voice->held_count == max_mono_notes) {
            engine.dropped_notes.fetch_add(1, std::memory_order_relaxed);
            return NK_ERROR_QUEUE_FULL;
        }
        voice->held[voice->held_count++] =
            MonoNote{event.voice_id, event.note,  score_note,     token,
                     release_frame,  score_cycle, event.velocity, priority};
    }
    // Keep the previous owner pinned until its backend references are replaced.
    const auto previous_instrument = voice->instrument;
    voice->instrument = instrument;
    voice->voice_id = event.voice_id;
    voice->note_token = token;
    voice->note = event.note;
    voice->velocity = event.velocity;
    voice->priority = priority;
    voice->started = ++engine.voice_serial;
    voice->release_frame = release_frame;
    voice->score_cycle = score_cycle;
    voice->score_note = score_note;
    voice->released = false;
    voice->active = true;
    if (instrument_changed) {
        voice->parameters_version = 0;
        if (instrument->provider)
            voice->backend.release_resources();
    }
    sync_voice_parameters(*voice);
    if (voice->provider)
        voice->provider->release(voice->provider_slot);
    voice->provider = instrument->provider;
    voice->provider_slot = -1;
    if (voice->provider) {
        voice->provider_slot = voice->provider->acquire(event.note, event.velocity);
        if (voice->provider_slot < 0) {
            voice->clear();
            engine.dropped_notes.fetch_add(1, std::memory_order_relaxed);
            return NK_ERROR_QUEUE_FULL;
        }
    } else
        voice->backend.note_on(event.note, event.velocity, connected);
    voice->choke_group = instrument->choke_group.load(std::memory_order_relaxed);
    if (voice->choke_group)
        for (auto &other : engine.voices)
            if (&other != voice && other.active && !other.released &&
                other.choke_group == voice->choke_group)
                release_voice(&other);
    return NK_OK;
}

void release_voice(DspVoice *voice) {
    if (!voice)
        return;
    if (voice->provider)
        voice->provider->note_off(voice->provider_slot);
    else
        voice->backend.note_off();
    voice->released = true;
    voice->release_frame = 0;
    voice->held_count = 0;
}

void remove_mono_note(DspVoice &voice, uint32_t index) {
    const bool current = index + 1 == voice.held_count;
    for (uint32_t i = index + 1; i < voice.held_count; ++i)
        voice.held[i - 1] = voice.held[i];
    if (--voice.held_count == 0) {
        release_voice(&voice);
        return;
    }
    if (!current)
        return;
    const auto &note = voice.held[voice.held_count - 1];
    voice.voice_id = note.voice_id;
    voice.note_token = note.token;
    voice.score_cycle = note.score_cycle;
    voice.score_note = note.score_note;
    voice.note = note.note;
    voice.velocity = note.velocity;
    voice.priority = note.priority;
    voice.release_frame = note.release_frame;
    sync_voice_parameters(voice);
    voice.backend.note_on(note.note, note.velocity, true);
}

void release_note(DspVoice *voice, uint64_t token = 0, uint32_t id = 0, uint64_t cycle = 0,
                  uint32_t score_note = 0) {
    if (!voice)
        return;
    if (!voice->held_count) {
        release_voice(voice);
        return;
    }
    for (uint32_t i = 0; i < voice->held_count; ++i) {
        const auto &note = voice->held[i];
        const bool match = cycle   ? note.score_cycle == cycle && note.score_note == score_note
                           : token ? note.token == token
                                   : note.token == 0 && note.voice_id == id;
        if (match) {
            remove_mono_note(*voice, i);
            return;
        }
    }
}

void expire_mono_notes(DspEngineResource &engine, uint64_t frame) {
    for (auto &voice : engine.voices)
        // Remove expired lower notes first, so return priority never retriggers a stale note.
        for (uint32_t i = 0; i < voice.held_count;)
            if (voice.held[i].release_frame && frame >= voice.held[i].release_frame)
                remove_mono_note(voice, i);
            else
                ++i;
}

nk::audio_dsp::StereoSample mix_voices(DspEngineResource &engine, uint64_t frame) {
    nk::audio_dsp::StereoSample mixed;
    for (auto &voice : engine.voices) {
        if (engine.transport.paused && voice.score_cycle)
            continue;
        if (voice.release_frame && frame >= voice.release_frame)
            release_voice(&voice);
        auto sample = voice_sample(voice);
        if (voice.fade_in_remaining) {
            const auto t = 1.0f - static_cast<float>(voice.fade_in_remaining) / engine.fade_frames;
            const auto gain = t * t * (3.0f - 2.0f * t);
            sample.left *= gain;
            sample.right *= gain;
            --voice.fade_in_remaining;
        }
        mixed.left += sample.left;
        mixed.right += sample.right;
    }
    for (auto &voice : engine.tails) {
        if (!voice.active || (engine.transport.paused && voice.score_cycle))
            continue;
        const auto remaining = voice.fade_remaining;
        const auto sample = voice_sample(voice);
        const auto t = static_cast<float>(remaining) / engine.fade_frames;
        const auto gain = t * t * (3.0f - 2.0f * t);
        mixed.left += sample.left * gain;
        mixed.right += sample.right * gain;
        if (voice.active && --voice.fade_remaining == 0)
            voice.clear();
    }
    return mixed;
}

nk_result apply_parameter_value(DspEngineResource &engine,
                                const std::shared_ptr<DspInstrumentResource> &instrument,
                                nk_audio_dsp_parameter parameter, uint32_t oscillator_index,
                                float value) {
    if (!instrument)
        return NK_ERROR_INVALID_HANDLE;
    if (!instrument_belongs_to(instrument, engine))
        return invalid_request("audio DSP instrument belongs to another engine");
    if (instrument->provider && parameter != NK_AUDIO_DSP_PARAMETER_GAIN)
        return invalid_argument("Sampled instruments expose only output gain as a DSP parameter");
    auto parameters = instrument->current;
    auto result = valid_oscillator_parameter(parameter)
                      ? set_oscillator_parameter(parameters, oscillator_index, parameter, value)
                      : set_parameter(parameters, parameter, value);
    if (result == NK_OK)
        result = validate_engine_parameters(parameters, engine);
    if (result == NK_OK)
        instrument->current = parameters;
    if (result == NK_OK) {
        ++instrument->parameters_version;
        publish_parameters(*instrument);
    }
    return result;
}

nk_result apply_event(DspEngineResource &engine, const nk_audio_dsp_event &event) {
    switch (event.kind) {
    case NK_AUDIO_DSP_EVENT_NOTE_ON: {
        auto instrument = get_instrument(event.instrument);
        if (!instrument)
            return NK_ERROR_INVALID_HANDLE;
        if (!instrument_belongs_to(instrument, engine))
            return invalid_request("audio DSP instrument belongs to another engine");
        return start_voice(engine, instrument, event);
    }
    case NK_AUDIO_DSP_EVENT_NOTE_OFF: {
        auto *voice = find_voice(engine, event.voice_id);
        if (!voice)
            return NK_OK;
        release_note(voice, 0, event.voice_id);
        return NK_OK;
    }
    case NK_AUDIO_DSP_EVENT_PARAMETER: {
        auto instrument = get_instrument(event.instrument);
        return apply_parameter_value(engine, instrument, event.parameter, event.oscillator_index,
                                     event.value);
    }
    default:
        return invalid_argument("audio DSP event kind is invalid");
    }
}

nk_result begin_parameter_ramp(DspEngineResource &engine, const nk_audio_dsp_event &event,
                               std::vector<ActiveParameterRamp> &ramps) {
    auto instrument = get_instrument(event.instrument);
    const auto result = apply_parameter_value(engine, instrument, event.parameter,
                                              event.oscillator_index, event.value);
    if (result != NK_OK)
        return result;
    ramps.push_back({std::move(instrument), event.parameter, event.oscillator_index, event.value,
                     event.end_value, event.frame_offset,
                     event.frame_offset + event.duration_frames});
    return NK_OK;
}

nk_result apply_parameter_ramp(DspEngineResource &engine, const ActiveParameterRamp &ramp,
                               uint32_t frame) {
    if (frame >= ramp.end_frame)
        return apply_parameter_value(engine, ramp.instrument, ramp.parameter, ramp.oscillator_index,
                                     ramp.end_value);
    const auto progress = static_cast<float>(frame - ramp.start_frame) /
                          static_cast<float>(ramp.end_frame - ramp.start_frame);
    const auto value = ramp.start_value + (ramp.end_value - ramp.start_value) * progress;
    return apply_parameter_value(engine, ramp.instrument, ramp.parameter, ramp.oscillator_index,
                                 value);
}

nk_result validate_event(const DspEngineResource &engine, const nk_audio_dsp_event &event,
                         uint32_t frame_count, uint32_t previous_frame) {
    if (event.struct_size < sizeof(nk_audio_dsp_event))
        return invalid_argument("audio DSP event is missing or too small");
    if (event.frame_offset > frame_count || event.frame_offset < previous_frame)
        return invalid_argument("audio DSP events must be frame sorted within the block");
    if (event.kind != NK_AUDIO_DSP_EVENT_PARAMETER &&
        event.kind != NK_AUDIO_DSP_EVENT_PARAMETER_RAMP && event.voice_id == 0)
        return invalid_argument("audio DSP voice ID must be non-zero");
    switch (event.kind) {
    case NK_AUDIO_DSP_EVENT_NOTE_ON:
        if (event.note > 127 || !std::isfinite(event.velocity) || event.velocity < 0.0f ||
            event.velocity > 1.0f)
            return invalid_argument("audio DSP note-on event is invalid");
        break;
    case NK_AUDIO_DSP_EVENT_NOTE_OFF:
        break;
    case NK_AUDIO_DSP_EVENT_PARAMETER:
        if (!valid_parameter(event.parameter) || !std::isfinite(event.value))
            return invalid_argument("audio DSP parameter event is invalid");
        if (!valid_oscillator_parameter(event.parameter) && event.oscillator_index != 0)
            return invalid_argument("audio DSP parameter event source index is invalid");
        break;
    case NK_AUDIO_DSP_EVENT_PARAMETER_RAMP:
        if (!valid_parameter(event.parameter) || !std::isfinite(event.value) ||
            !std::isfinite(event.end_value) || event.duration_frames == 0 ||
            event.frame_offset >= frame_count ||
            event.duration_frames > frame_count - event.frame_offset)
            return invalid_argument("audio DSP parameter ramp event is invalid");
        if (event.parameter == NK_AUDIO_DSP_PARAMETER_WAVEFORM ||
            event.parameter == NK_AUDIO_DSP_PARAMETER_OSCILLATOR_WAVEFORM)
            return invalid_argument("audio DSP parameter ramp cannot target a waveform");
        if (!valid_oscillator_parameter(event.parameter) && event.oscillator_index != 0)
            return invalid_argument("audio DSP parameter ramp source index is invalid");
        break;
    default:
        return invalid_argument("audio DSP event kind is invalid");
    }
    if (event.kind != NK_AUDIO_DSP_EVENT_NOTE_OFF) {
        auto instrument = get_instrument(event.instrument);
        if (!instrument)
            return NK_ERROR_INVALID_HANDLE;
        if (!instrument_belongs_to(instrument, engine))
            return invalid_request("audio DSP instrument belongs to another engine");
        if (event.kind == NK_AUDIO_DSP_EVENT_PARAMETER ||
            event.kind == NK_AUDIO_DSP_EVENT_PARAMETER_RAMP) {
            if (instrument->provider && event.parameter != NK_AUDIO_DSP_PARAMETER_GAIN)
                return invalid_argument("unsupported sampled-instrument DSP parameter");
            auto parameters = control_parameters(*instrument);
            const auto result = valid_oscillator_parameter(event.parameter)
                                    ? set_oscillator_parameter(parameters, event.oscillator_index,
                                                               event.parameter, event.value)
                                    : set_parameter(parameters, event.parameter, event.value);
            if (result != NK_OK)
                return result;
            if (const auto result = validate_engine_parameters(parameters, engine); result != NK_OK)
                return result;
            if (event.kind == NK_AUDIO_DSP_EVENT_PARAMETER_RAMP) {
                parameters = control_parameters(*instrument);
                const auto end_result =
                    valid_oscillator_parameter(event.parameter)
                        ? set_oscillator_parameter(parameters, event.oscillator_index,
                                                   event.parameter, event.end_value)
                        : set_parameter(parameters, event.parameter, event.end_value);
                if (end_result != NK_OK)
                    return end_result;
                if (const auto end_validation = validate_engine_parameters(parameters, engine);
                    end_validation != NK_OK)
                    return end_validation;
            }
        }
    }
    return NK_OK;
}

void reset_voices(DspEngineResource &engine) {
    for (auto &voice : engine.voices)
        voice.clear();
    for (auto &voice : engine.tails)
        voice.clear();
    for (uint32_t i = 0; i < engine.note_heap_count; ++i)
        engine.note_heap[i].instrument.reset();
    engine.note_heap_count = 0;
}

nk_result apply_scheduled_event(DspEngineResource &engine, const ScheduledDspEvent &scheduled) {
    const auto &event = scheduled.event;
    switch (event.kind) {
    case NK_AUDIO_DSP_EVENT_NOTE_ON: {
        auto instrument = scheduled.instrument;
        if (!instrument || !instrument_belongs_to(instrument, engine))
            return NK_ERROR_INVALID_HANDLE;
        const auto release_frame =
            scheduled.hold_frames ? scheduled.frame + scheduled.hold_frames : 0;
        return start_voice(engine, instrument, event, scheduled.note_token, scheduled.priority,
                           release_frame);
    }
    case NK_AUDIO_DSP_EVENT_NOTE_OFF: {
        auto *voice = scheduled.note_token ? find_token(engine, scheduled.note_token)
                                           : find_voice(engine, event.voice_id);
        release_note(voice, scheduled.note_token, event.voice_id);
        return NK_OK;
    }
    case NK_AUDIO_DSP_EVENT_PARAMETER:
        return apply_parameter_value(engine, scheduled.instrument, event.parameter,
                                     event.oscillator_index, event.value);
    default:
        return invalid_argument("audio DSP scheduled event kind is invalid");
    }
}

nk_result begin_attached_parameter_ramp(DspEngineResource &engine,
                                        const ScheduledDspEvent &scheduled, uint64_t frame) {
    const auto &event = scheduled.event;
    if (engine.output_ramp_count >= max_attached_parameter_ramps)
        return NK_ERROR_QUEUE_FULL;
    auto instrument = scheduled.instrument;
    const auto result = apply_parameter_value(engine, instrument, event.parameter,
                                              event.oscillator_index, event.value);
    if (result != NK_OK)
        return result;
    engine.output_ramps[engine.output_ramp_count++] = ActiveParameterRamp{
        std::move(instrument), event.parameter, event.oscillator_index,       event.value,
        event.end_value,       frame,           frame + event.duration_frames};
    return NK_OK;
}

uint64_t next_note_token(DspEngineResource &engine) {
    auto next = engine.next_note_token.load(std::memory_order_relaxed);
    while (next != UINT64_MAX) {
        if (engine.next_note_token.compare_exchange_weak(next, next + 1, std::memory_order_relaxed))
            return next;
    }
    return 0;
}

void release_transport_voices(DspEngineResource &engine) {
    for (auto &voice : engine.voices) {
        if (!voice.active)
            continue;
        if (!voice.held_count) {
            if (voice.score_cycle)
                release_voice(&voice);
            continue;
        }
        // Manual held notes can coexist with a score; cancel every old score identity.
        for (uint32_t i = 0; i < voice.held_count;)
            if (voice.held[i].score_cycle)
                remove_mono_note(voice, i);
            else
                ++i;
    }
}

bool enqueue_command(DspEngineResource &engine, TransportCommand command) {
    const auto write = engine.transport_write.load(std::memory_order_relaxed);
    const auto read = engine.transport_read.load(std::memory_order_acquire);
    if (write - read == transport_command_capacity)
        return false;
    command.sequence = engine.next_command++;
    if (command.program)
        command.program->last_command = command.sequence;
    engine.transport_commands[write % transport_command_capacity] = std::move(command);
    engine.transport_write.store(write + 1, std::memory_order_release);
    return true;
}

void collect_resources(DspEngineResource &engine) {
    for (auto i = engine.publications.begin(); i != engine.publications.end();) {
        if ((*i)->remaining.load(std::memory_order_acquire) == 0)
            i = engine.publications.erase(i);
        else
            ++i;
    }
    const auto active = engine.active_program.load(std::memory_order_acquire);
    const auto consumed = engine.consumed_command.load(std::memory_order_acquire);
    for (auto i = engine.programs.begin(); i != engine.programs.end();) {
        const auto &program = *i;
        if (program->retired && program->id != active && program->last_command <= consumed)
            i = engine.programs.erase(i);
        else
            ++i;
    }
    for (auto i = engine.instruments.begin(); i != engine.instruments.end();) {
        if ((*i)->retired && i->use_count() == 1)
            i = engine.instruments.erase(i);
        else
            ++i;
    }
}

/** Atomically publish one musical action to independently routed track engines. */
nk_result publish_group(const nk_audio_dsp_transport_track *tracks, uint32_t count,
                        uint32_t section, uint64_t start, TransportCommand::Kind operation,
                        double position = -1, bool paused = false) {
    const bool play =
        operation == TransportCommand::play || operation == TransportCommand::relocate;
    const bool pause_only = operation == TransportCommand::set_paused;
    if (!tracks || !count || count > 128)
        return invalid_argument("invalid DSP transport group");
    std::vector<std::shared_ptr<DspEngineResource>> engines;
    for (uint32_t i = 0; i < count; ++i) {
        auto engine = get_engine(tracks[i].engine);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        for (const auto &previous : engines)
            if (previous.get() == engine.get())
                return invalid_argument("duplicate DSP group engine");
        engines.push_back(std::move(engine));
    }
    auto ordered = engines;
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
        return std::less<DspEngineResource *>{}(a.get(), b.get());
    });
    std::vector<std::unique_lock<std::mutex>> locks;
    for (auto &engine : ordered)
        locks.emplace_back(engine->mutex);
    std::vector<TransportProgram *> programs;
    uint32_t rate = engines[0]->options.sample_rate;
    const auto attached = engines[0]->attached.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < count; ++i) {
        auto &engine = *engines[i];
        if (!engine.alive || engine.options.sample_rate != rate ||
            engine.attached.load(std::memory_order_acquire) != attached)
            return invalid_request("DSP group engines require the same live sample clock");
        const auto write = engine.transport_write.load(std::memory_order_relaxed);
        const auto read = engine.transport_read.load(std::memory_order_acquire);
        if (write - read == transport_command_capacity)
            return NK_ERROR_QUEUE_FULL;
        TransportProgram *program = nullptr;
        if (play) {
            for (const auto &p : engine.programs)
                if (p->id == tracks[i].program_id && !p->retired)
                    program = p.get();
        } else {
            bool queued = false;
            for (uint32_t cursor = write; cursor != read;) {
                const auto &command =
                    engine.transport_commands[(--cursor) % transport_command_capacity];
                if (command.kind == TransportCommand::stop) {
                    queued = true;
                    break;
                }
                if (command.kind == TransportCommand::play ||
                    command.kind == TransportCommand::relocate) {
                    program = command.program;
                    queued = true;
                    break;
                }
            }
            if (!queued) {
                const auto id = engine.active_program.load(std::memory_order_acquire);
                for (const auto &p : engine.programs)
                    if (p->id == id)
                        program = p.get();
            }
        }
        if (!program || (!play && program->id != tracks[i].program_id) ||
            section >= program->sections.size())
            return invalid_argument("unknown DSP group program or section");
        if (position >= 0 && position >= program->sections[section].length)
            return invalid_argument("seek position outside section");
        programs.push_back(program);
    }
    // Independently routed tracks must share musical boundaries. Otherwise one
    // atomic section request could still commit at different sample frames.
    for (uint32_t i = 1; i < count; ++i) {
        if (programs[i]->sections.size() != programs[0]->sections.size())
            return invalid_argument("DSP group section count differs");
        for (uint32_t j = 0; j < programs[0]->sections.size(); ++j) {
            const auto &a = programs[0]->sections[j];
            const auto &b = programs[i]->sections[j];
            if (a.length != b.length || a.bars != b.bars)
                return invalid_argument("DSP group musical boundaries differ");
        }
    }
    auto publication = std::make_shared<RuntimePublication>();
    publication->remaining.store(count, std::memory_order_relaxed);
    // All callback-visible commands are gated until every engine has its command.
    for (uint32_t i = 0; i < count; ++i) {
        TransportCommand command;
        command.kind = operation;
        command.position = position;
        command.paused = paused;
        command.program = programs[i];
        command.section = section;
        command.publication = publication;
        engines[i]->publications.push_back(publication);
        (void)enqueue_command(*engines[i], std::move(command));
    }
    uint64_t cutoff = start;
    for (const auto &engine : engines) {
        const auto frame = engine->status_frame.load(std::memory_order_acquire);
        // Two render blocks cover commands becoming visible between source nodes
        // in one device callback. Offline starts retain the requested exact origin.
        if ((!play && !pause_only) || engine->attached.load(std::memory_order_acquire))
            cutoff = std::max(
                cutoff,
                frame + uint64_t(std::max(engine->options.block_size, engine->device_quantum)) * 2);
        else if (position >= 0 || pause_only)
            cutoff = std::max(cutoff, frame);
    }
    publication->frame = cutoff;
    publication->ready.store(true, std::memory_order_release);
    return NK_OK;
}

void publish_transport_status(DspEngineResource &engine, uint64_t frame) {
    engine.status_sequence.fetch_add(1, std::memory_order_acq_rel);
    engine.status_playing.store(engine.transport.program != nullptr && !engine.transport.paused,
                                std::memory_order_relaxed);
    engine.status_section.store(engine.transport.section, std::memory_order_relaxed);
    engine.status_frame.store(frame, std::memory_order_relaxed);
    engine.status_loops.store(engine.transport.loops, std::memory_order_relaxed);
    engine.status_bpm.store(engine.transport.bpm, std::memory_order_relaxed);
    double position = 0;
    const auto &transport = engine.transport;
    if (transport.program && frame >= transport.start) {
        const auto length = transport.program->sections[transport.section].length;
        position = std::fmod(
            std::max(0.0, static_cast<double>(frame - transport.start) - transport.origin), length);
    }
    if (transport.paused)
        position = transport.paused_position;
    engine.status_section_frame.store(position, std::memory_order_relaxed);
    engine.status_voices.store(
        static_cast<uint32_t>(std::count_if(engine.voices.begin(), engine.voices.end(),
                                            [](const auto &voice) { return voice.active; })),
        std::memory_order_relaxed);
    engine.status_tails.store(
        static_cast<uint32_t>(std::count_if(engine.tails.begin(), engine.tails.end(),
                                            [](const auto &voice) { return voice.active; })),
        std::memory_order_relaxed);
    engine.status_sequence.fetch_add(1, std::memory_order_release);
}

bool note_later(const ScheduledDspEvent &a, const ScheduledDspEvent &b) {
    return a.frame != b.frame ? a.frame > b.frame : a.sequence > b.sequence;
}

// Fixed-capacity scan: cancel future starts without reusing note identities.
void cancel_pending_note(DspEngineResource &engine, uint64_t token) {
    for (uint32_t i = 0; i < engine.note_heap_count;) {
        auto &pending = engine.note_heap[i];
        if (pending.note_token == token && pending.event.kind == NK_AUDIO_DSP_EVENT_NOTE_ON) {
            const auto last = --engine.note_heap_count;
            if (i != last)
                pending = std::move(engine.note_heap[last]);
            engine.note_heap[last].instrument.reset();
        } else
            ++i;
    }
    std::make_heap(engine.note_heap.begin(), engine.note_heap.begin() + engine.note_heap_count,
                   note_later);
}

void consume_runtime_commands(DspEngineResource &engine, uint64_t frame) {
    auto read = engine.transport_read.load(std::memory_order_relaxed);
    const auto write = engine.transport_write.load(std::memory_order_acquire);
    while (read != write) {
        auto &command = engine.transport_commands[read % transport_command_capacity];
        auto &transport = engine.transport;
        if (command.publication) {
            if (!command.publication->ready.load(std::memory_order_acquire))
                break;
            command.frame = command.publication->frame;
        }
        if ((command.kind == TransportCommand::relocate ||
             command.kind == TransportCommand::set_paused) &&
            frame < command.frame)
            break;
        if (command.kind == TransportCommand::queued_note &&
            command.note.generation !=
                engine.scheduled_generation.load(std::memory_order_acquire)) {
            // Invalidated note command: retire its reference below.
        } else if (command.kind == TransportCommand::queued_note) {
            command.note.sequence = command.sequence;
            if (engine.note_heap_count < engine.note_heap.size()) {
                engine.note_heap[engine.note_heap_count++] = std::move(command.note);
                std::push_heap(engine.note_heap.begin(),
                               engine.note_heap.begin() + engine.note_heap_count, note_later);
            } else {
                // Never strand a held note when timed-command capacity is exhausted.
                if (command.note.event.kind == NK_AUDIO_DSP_EVENT_NOTE_OFF) {
                    cancel_pending_note(engine, command.note.note_token);
                    release_note(find_token(engine, command.note.note_token),
                                 command.note.note_token);
                }
                engine.dropped_notes.fetch_add(1, std::memory_order_relaxed);
            }
        } else if (command.kind == TransportCommand::play ||
                   command.kind == TransportCommand::relocate) {
            release_transport_voices(engine);
            transport.program = command.program;
            transport.start = command.frame;
            transport.origin = command.kind == TransportCommand::relocate ? -command.position : 0;
            transport.paused = command.paused;
            transport.paused_position = std::max(0.0, command.position);
            transport.paused_frame = frame;
            transport.section = command.section;
            transport.event = 0;
            transport.pending = UINT32_MAX;
            ++transport.cycle;
            transport.loops = 0;
            transport.bpm = 120;
            transport.has_frame = false;
            engine.active_program.store(command.program->id, std::memory_order_release);
            if (command.kind == TransportCommand::relocate) {
                const auto &section = transport.program->sections[transport.section];
                transport.event = 0;
                while (transport.event < section.events.size() &&
                       section.events[transport.event].value.frame < command.position) {
                    const auto &prepared = section.events[transport.event++];
                    const auto &event = prepared.value;
                    if (event.kind == 2)
                        transport.bpm = event.bpm;
                    if (event.kind == 0 && prepared.end_frame > command.position) {
                        nk_audio_dsp_event note{};
                        note.kind = NK_AUDIO_DSP_EVENT_NOTE_ON;
                        note.note = event.note;
                        note.velocity = event.velocity;
                        const auto token = next_note_token(engine);
                        if (token)
                            (void)start_voice(engine, prepared.instrument, note, token, 0, 0,
                                              transport.cycle, event.note_id);
                    }
                }
                const auto tempo =
                    std::upper_bound(section.tempos.begin(), section.tempos.end(), command.position,
                                     [](double position, const TransportTempo &tempo) {
                                         return position < tempo.frame;
                                     });
                if (tempo != section.tempos.begin())
                    transport.bpm = (tempo - 1)->bpm;
                engine.clock->bpm.store(static_cast<float>(transport.bpm),
                                        std::memory_order_release);
                transport.has_frame = true;
                transport.next_frame = frame;
            }
        } else if (command.kind == TransportCommand::set_paused) {
            if (transport.program && transport.paused != command.paused) {
                if (command.paused) {
                    const auto relative = frame >= transport.start ? frame - transport.start : 0;
                    const auto length = transport.program->sections[transport.section].length;
                    transport.paused_position = std::fmod(
                        std::max(0.0, static_cast<double>(relative) - transport.origin), length);
                    transport.paused_frame = frame;
                } else {
                    // Preserve events, pending transitions and all voice DSP state.
                    transport.start += frame - transport.paused_frame;
                    transport.has_frame = true;
                    transport.next_frame = frame;
                }
                transport.paused = command.paused;
            }
        } else if (command.kind == TransportCommand::stop) {
            release_transport_voices(engine);
            transport.program = nullptr;
            engine.active_program.store(0, std::memory_order_release);
        } else if (transport.program && command.section < transport.program->sections.size()) {
            transport.pending = command.section;
            const auto &section = transport.program->sections[transport.section];
            const auto cutoff = std::max(frame, command.frame);
            transport.boundary = section.length;
            for (const auto bar : section.bars) {
                if (std::floor(transport.origin + bar + 0.5) >=
                    static_cast<double>(cutoff >= transport.start ? cutoff - transport.start : 0)) {
                    transport.boundary = bar;
                    break;
                }
            }
        }
        command.note.instrument.reset();
        if (command.publication) {
            auto *publication = command.publication.get();
            command.publication.reset(); // Control pins own final destruction.
            publication->remaining.fetch_sub(1, std::memory_order_release);
        }
        engine.consumed_command.store(command.sequence, std::memory_order_release);
        ++read;
    }
    engine.transport_read.store(read, std::memory_order_release);
    while (engine.note_heap_count && engine.note_heap[0].frame <= frame) {
        std::pop_heap(engine.note_heap.begin(), engine.note_heap.begin() + engine.note_heap_count,
                      note_later);
        auto note = std::move(engine.note_heap[--engine.note_heap_count]);
        if (note.event.kind == NK_AUDIO_DSP_EVENT_NOTE_OFF && note.note_token)
            cancel_pending_note(engine, note.note_token);
        // Do not replay one-shots whose entire lifetime passed during a source
        // clock gap. Held notes and releases still take effect at this frame.
        if (note.event.kind != NK_AUDIO_DSP_EVENT_NOTE_ON || !note.hold_frames ||
            note.frame + note.hold_frames > frame)
            (void)apply_scheduled_event(engine, note);
        note.instrument.reset();
    }
}

void seek_transport(DspEngineResource &engine, uint64_t relative) {
    auto &transport = engine.transport;
    release_transport_voices(engine);
    for (auto &voice : engine.voices)
        if (voice.active && voice.score_cycle)
            voice.fade_in_remaining = engine.fade_frames;
    for (auto &tail : engine.tails)
        tail.clear();
    if (relative > 9007199254740991ULL) {
        transport.program = nullptr;
        engine.active_program.store(0, std::memory_order_release);
        return;
    }
    if (transport.pending != UINT32_MAX &&
        std::floor(transport.origin + transport.boundary + 0.5) <= relative) {
        transport.origin += transport.boundary;
        transport.section = transport.pending;
        transport.pending = UINT32_MAX;
        ++transport.cycle;
        ++transport.loops;
    }
    const auto &section = transport.program->sections[transport.section];
    const auto distance = static_cast<double>(relative) - transport.origin;
    auto loops = distance > 0 ? static_cast<uint64_t>(std::floor(distance / section.length)) : 0;
    // Account for the same round-half-up rule as ordinary boundary consumption.
    if (loops && std::floor(transport.origin + loops * section.length + 0.5) > relative)
        --loops;
    if (std::floor(transport.origin + (loops + 1) * section.length + 0.5) <= relative)
        ++loops;
    transport.origin += loops * section.length;
    transport.cycle += loops;
    transport.loops += loops;
    const auto first =
        std::lower_bound(section.events.begin(), section.events.end(), relative,
                         [&](const TransportEvent &event, uint64_t frame) {
                             return std::floor(transport.origin + event.value.frame + 0.5) < frame;
                         });
    transport.event = static_cast<uint32_t>(first - section.events.begin());
    const auto tempo =
        std::upper_bound(section.tempos.begin(), section.tempos.end(), relative,
                         [&](uint64_t frame, const TransportTempo &tempo) {
                             return frame < std::floor(transport.origin + tempo.frame + 0.5);
                         });
    if (tempo != section.tempos.begin()) {
        transport.bpm = (tempo - 1)->bpm;
        engine.clock->bpm.store(static_cast<float>(transport.bpm), std::memory_order_release);
    }
}

// Called only after the attached callback gate has closed, or for offline reset.
void stop_runtime(DspEngineResource &engine) {
    for (auto &voice : engine.voices)
        if (voice.active && (voice.note_token || voice.score_cycle))
            release_voice(&voice);
    engine.transport.program = nullptr;
    engine.transport.pending = UINT32_MAX;
    engine.transport.has_frame = false;
    engine.runtime_has_frame = false;
    engine.active_program.store(0, std::memory_order_release);
    for (auto &command : engine.transport_commands) {
        command.note.instrument.reset();
        if (command.publication) {
            auto *publication = command.publication.get();
            command.publication.reset();
            publication->remaining.fetch_sub(1, std::memory_order_release);
        }
    }
    engine.transport_read.store(engine.transport_write.load(std::memory_order_acquire),
                                std::memory_order_release);
    engine.consumed_command.store(engine.next_command - 1, std::memory_order_release);
    for (uint32_t i = 0; i < engine.note_heap_count; ++i)
        engine.note_heap[i].instrument.reset();
    engine.note_heap_count = 0;
    for (auto &event : engine.scheduled_events)
        event.instrument.reset();
    engine.scheduled_read.store(engine.scheduled_write.load(std::memory_order_acquire),
                                std::memory_order_release);
    for (auto &ramp : engine.output_ramps)
        ramp.instrument.reset();
    engine.output_ramp_count = 0;
}

void advance_transport(DspEngineResource &engine, uint64_t frame) {
    if (engine.runtime_has_frame && frame != engine.runtime_next_frame)
        engine.clock_discontinuities.fetch_add(1, std::memory_order_relaxed);
    engine.runtime_has_frame = true;
    engine.runtime_next_frame = frame + 1;
    consume_runtime_commands(engine, frame);
    auto &transport = engine.transport;
    if (!transport.program || transport.paused)
        return;
    if ((!transport.has_frame && frame > transport.start) ||
        (transport.has_frame && frame != transport.next_frame)) {
        if (transport.has_frame && frame < transport.next_frame) {
            release_transport_voices(engine);
            transport.program = nullptr;
            engine.active_program.store(0, std::memory_order_release);
            return;
        }
        if (frame >= transport.start)
            seek_transport(engine, frame - transport.start);
        if (!transport.program)
            return;
    }
    transport.has_frame = true;
    transport.next_frame = frame + 1;
    if (frame < transport.start)
        return;
    const auto relative = frame - transport.start;
    // Prepared programs are validated to have positive lengths and bounded
    // simultaneous events. Ordinary callback progress cannot replay stale time.
    for (;;) {
        const auto &section = transport.program->sections[transport.section];
        const auto stop = transport.pending == UINT32_MAX ? section.length : transport.boundary;
        while (transport.event < section.events.size()) {
            const auto &next = section.events[transport.event].value;
            const auto sample = std::floor(transport.origin + next.frame + 0.5);
            if (next.frame > stop || sample > relative)
                break;
            // Fractional origin changes which exact positions share a sample.
            // Reorder each actual sample so releases precede new note starts.
            std::array<const TransportEvent *, 256> due;
            uint32_t count = 0;
            while (transport.event < section.events.size()) {
                const auto &prepared = section.events[transport.event];
                if (prepared.value.frame > stop ||
                    std::floor(transport.origin + prepared.value.frame + 0.5) != sample)
                    break;
                due[count++] = &prepared;
                ++transport.event;
            }
            std::sort(due.begin(), due.begin() + count, [](const auto *a, const auto *b) {
                const auto rank = [](uint32_t kind) { return kind == 2 ? 0 : kind == 1 ? 1 : 2; };
                if (a->value.kind != b->value.kind)
                    return rank(a->value.kind) < rank(b->value.kind);
                if (a->value.kind == 2)
                    return a->value.frame < b->value.frame;
                return a->value.note_id < b->value.note_id;
            });
            for (uint32_t index = 0; index < count; ++index) {
                const auto &prepared = *due[index];
                const auto &event = prepared.value;
                if (event.kind == 2) {
                    transport.bpm = event.bpm;
                    engine.clock->bpm.store(static_cast<float>(event.bpm),
                                            std::memory_order_release);
                } else if (event.kind == 0) {
                    if (transport.pending == UINT32_MAX || event.frame < stop) {
                        nk_audio_dsp_event note{};
                        note.kind = NK_AUDIO_DSP_EVENT_NOTE_ON;
                        note.note = event.note;
                        note.velocity = event.velocity;
                        const auto token = next_note_token(engine);
                        if (token)
                            (void)start_voice(engine, prepared.instrument, note, token, 0, 0,
                                              transport.cycle, event.note_id);
                    }
                } else {
                    for (auto &voice : engine.voices) {
                        if (!voice.active)
                            continue;
                        if (voice.held_count)
                            release_note(&voice, 0, 0, transport.cycle, event.note_id);
                        else if (voice.score_cycle == transport.cycle &&
                                 voice.score_note == event.note_id)
                            release_voice(&voice);
                    }
                }
            }
        }
        if (std::floor(transport.origin + stop + 0.5) > relative)
            break;
        release_transport_voices(engine);
        transport.origin += stop;
        if (transport.pending != UINT32_MAX) {
            transport.section = transport.pending;
            transport.pending = UINT32_MAX;
        }
        transport.event = 0;
        ++transport.cycle;
        ++transport.loops;
        if (transport.origin > 9007199254740991.0) {
            transport.program = nullptr;
            engine.active_program.store(0, std::memory_order_release);
            break;
        }
    }
}

void consume_attached_events(DspEngineResource &engine, uint64_t frame) noexcept {
    auto read = engine.scheduled_read.load(std::memory_order_relaxed);
    for (;;) {
        const auto write = engine.scheduled_write.load(std::memory_order_acquire);
        const auto generation = engine.scheduled_generation.load(std::memory_order_acquire);
        if (engine.output_generation != generation) {
            reset_voices(engine);
            for (uint32_t i = 0; i < engine.output_ramp_count; ++i)
                engine.output_ramps[i].instrument.reset();
            engine.output_ramp_count = 0;
            engine.output_generation = generation;
        }
        if (read == write)
            break;
        auto &scheduled = engine.scheduled_events[read & scheduled_event_mask];
        if (scheduled.generation != generation) {
            scheduled.instrument.reset();
            ++read;
            continue;
        }
        if (scheduled.frame > frame)
            break;
        if (scheduled.event.kind == NK_AUDIO_DSP_EVENT_PARAMETER_RAMP)
            (void)begin_attached_parameter_ramp(engine, scheduled, frame);
        else
            (void)apply_scheduled_event(engine, scheduled);
        scheduled.instrument.reset(); // Control-side pin owns final destruction.
        ++read;
    }
    engine.scheduled_read.store(read, std::memory_order_release);
}

void render_attached_block(DspEngineResource &engine, uint64_t start_frame,
                           uint32_t frame_count) noexcept {
    const auto sample_count = static_cast<uint64_t>(frame_count) * engine.options.channels;
    std::fill(engine.output_scratch.begin(), engine.output_scratch.begin() + sample_count, 0.0f);
    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        const auto absolute_frame = start_frame + frame;
        expire_mono_notes(engine, absolute_frame);
        consume_attached_events(engine, absolute_frame);
        advance_transport(engine, absolute_frame);
        for (uint32_t index = 0; index < engine.output_ramp_count;) {
            auto &ramp = engine.output_ramps[index];
            if (absolute_frame >= ramp.end_frame) {
                (void)apply_parameter_value(engine, ramp.instrument, ramp.parameter,
                                            ramp.oscillator_index, ramp.end_value);
                const auto last = --engine.output_ramp_count;
                if (index != last)
                    ramp = std::move(engine.output_ramps[last]);
                engine.output_ramps[last].instrument.reset();
                continue;
            }
            const auto progress = static_cast<float>(absolute_frame - ramp.start_frame) /
                                  static_cast<float>(ramp.end_frame - ramp.start_frame);
            (void)apply_parameter_value(
                engine, ramp.instrument, ramp.parameter, ramp.oscillator_index,
                ramp.start_value + (ramp.end_value - ramp.start_value) * progress);
            ++index;
        }
        const auto mixed = mix_voices(engine, absolute_frame);
        for (uint32_t channel = 0; channel < engine.options.channels; ++channel)
            engine
                .output_scratch[static_cast<uint64_t>(frame) * engine.options.channels + channel] =
                engine.options.channels == 1 || channel > 1
                    ? (mixed.left + mixed.right) * 0.5f
                    : (channel == 0 ? mixed.left : mixed.right);
    }
    consume_attached_events(engine, start_frame + frame_count);
    publish_transport_status(engine, start_frame + frame_count);
}

void DspEngineResource::detach() noexcept {
    auto *engine = this;
    std::lock_guard lock(engine->mutex);
    // Total ordering across the gate and reader count prevents a new reader
    // from observing the old gate after detach has observed an empty count.
    engine->attached.store(false, std::memory_order_seq_cst);
    while (engine->output_readers.load(std::memory_order_seq_cst) != 0)
        std::this_thread::yield();
    stop_runtime(*engine);
    publish_transport_status(*engine, engine->output_frame);
    engine->scheduled_read.store(engine->scheduled_write.load(std::memory_order_acquire),
                                 std::memory_order_release);
}

void DspEngineResource::process(float *frames_out, uint64_t frame_count, uint32_t sample_rate,
                                uint32_t channels, uint64_t start_frame) noexcept {
    auto *engine = this;
    engine->output_readers.fetch_add(1, std::memory_order_seq_cst);
    if (!engine->attached.load(std::memory_order_seq_cst) ||
        engine->output_sample_rate != sample_rate || engine->output_channels != channels) {
        engine->output_readers.fetch_sub(1, std::memory_order_seq_cst);
        return;
    }
#ifdef NK_AUDIO_TESTING
    nk::audio_dsp::soundfont_callback_begin();
#endif
    engine->output_frame = start_frame;
    uint64_t output_offset = 0;
    while (output_offset < frame_count) {
        const auto remaining = frame_count - output_offset;
        const auto block = static_cast<uint32_t>(
            std::min<uint64_t>(remaining, static_cast<uint64_t>(engine->options.block_size)));
        render_attached_block(*engine, engine->output_frame, block);
        const auto sample_count = static_cast<uint64_t>(block) * channels;
        auto *destination = frames_out + output_offset * channels;
        for (uint64_t sample = 0; sample < sample_count; ++sample)
            destination[sample] += engine->output_scratch[sample];
        engine->output_frame += block;
        output_offset += block;
    }
#ifdef NK_AUDIO_TESTING
    nk::audio_dsp::soundfont_callback_end();
#endif
    engine->output_readers.fetch_sub(1, std::memory_order_seq_cst);
}

} // namespace

namespace nk::audio_dsp {

nk_result attach_device(nk_audio_dsp_engine engine_handle, uint64_t device_frame,
                        uint32_t sample_rate, uint32_t channels, uint32_t clock_quantum) {
    auto engine = get_engine(engine_handle);
    if (!engine)
        return NK_ERROR_INVALID_HANDLE;
    std::lock_guard lock(engine->mutex);
    if (!engine->alive)
        return invalid_request("audio DSP engine is no longer alive");
    if (engine->options.sample_rate != sample_rate || engine->options.channels != channels)
        return invalid_request("audio DSP engine format does not match the playback device");
    if (engine->attached.load(std::memory_order_acquire))
        return NK_OK;
    // Moving from an offline frame domain to the device starts a fresh native
    // timeline. Prepared programs remain available for an explicit play call.
    stop_runtime(*engine);
    const auto write = engine->scheduled_write.load(std::memory_order_acquire);
    engine->scheduled_read.store(write, std::memory_order_release);
    engine->scheduled_generation.fetch_add(1, std::memory_order_acq_rel);
    engine->output_frame = device_frame;
    engine->output_sample_rate = sample_rate;
    engine->output_channels = channels;
    engine->device_quantum = clock_quantum;
    publish_transport_status(*engine, device_frame);
    engine->output_ramp_count = 0;
    engine->attached.store(true, std::memory_order_release);
    return NK_OK;
}

nk_result require_attached(nk_audio_dsp_engine engine_handle) {
    auto engine = get_engine(engine_handle);
    if (!engine)
        return NK_ERROR_INVALID_HANDLE;
    if (!engine->attached.load(std::memory_order_acquire))
        return invalid_request("audio DSP engine is not attached to the playback device");
    return NK_OK;
}

std::shared_ptr<const TransportClock> transport_clock(nk_audio_dsp_engine engine_handle) {
    auto engine = get_engine(engine_handle);
    return engine ? engine->clock : nullptr;
}

std::shared_ptr<DeviceOutput> device_output(nk_audio_dsp_engine engine_handle) {
    return get_engine(engine_handle);
}

} // namespace nk::audio_dsp

extern "C" {

nk_result NK_CALL nk_audio_dsp_group_play_transport(const nk_audio_dsp_transport_track *tracks,
                                                    uint32_t count, uint32_t section,
                                                    uint64_t start) {
    return nk::core::result_boundary("publishing a native score group", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        return publish_group(tracks, count, section, start, TransportCommand::play);
    });
}
nk_result NK_CALL nk_audio_dsp_group_set_paused(const nk_audio_dsp_transport_track *tracks,
                                                uint32_t count, uint32_t paused) {
    return nk::core::result_boundary("pausing a native score group", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (paused > 1)
            return invalid_argument("invalid transport pause state");
        return publish_group(tracks, count, 0, 0, TransportCommand::set_paused, -1, paused != 0);
    });
}
nk_result NK_CALL nk_audio_dsp_group_seek_transport(const nk_audio_dsp_transport_track *tracks,
                                                    uint32_t count, uint32_t section,
                                                    double position, uint32_t paused) {
    return nk::core::result_boundary("seeking a native score group", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (!std::isfinite(position) || position < 0 || paused > 1)
            return invalid_argument("invalid transport seek");
        return publish_group(tracks, count, section, 0, TransportCommand::relocate, position,
                             paused != 0);
    });
}
nk_result NK_CALL nk_audio_dsp_group_request_section(const nk_audio_dsp_transport_track *tracks,
                                                     uint32_t count, uint32_t section) {
    return nk::core::result_boundary("requesting a native score group section", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        return publish_group(tracks, count, section, 0, TransportCommand::request);
    });
}

nk_result NK_CALL nk_audio_dsp_instrument_create_from_sample(
    nk_audio_dsp_engine handle, const nk_audio_dsp_sample_options *options, const float *samples,
    uint32_t count, nk_audio_dsp_instrument *out_instrument) {
    return nk::core::result_boundary("preparing a PCM instrument", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (!out_instrument)
            return invalid_argument("PCM instrument output is missing");
        *out_instrument = NK_INVALID_HANDLE;
        auto engine = get_engine(handle);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        if (!options || options->struct_size < sizeof(*options) || !std::isfinite(options->gain) ||
            options->gain < 0 || options->gain > 4)
            return invalid_argument("invalid PCM instrument options");
        DspParameters parameters;
        if (options->envelope.struct_size < sizeof(options->envelope))
            return invalid_argument("PCM envelope options are too small");
        parameters.envelope = {options->envelope.attack_seconds, options->envelope.decay_seconds,
                               options->envelope.sustain_level, options->envelope.release_seconds};
        if (!valid_patch_parameters(parameters))
            return invalid_argument("invalid PCM envelope");
        auto provider = nk::audio_dsp::prepare_sample_source(
            *options, samples, count, engine->options.max_voices + 64, engine->options.sample_rate);
        if (!provider)
            return invalid_argument("invalid PCM samples or loop bounds");
        auto instrument = std::make_shared<DspInstrumentResource>();
        instrument->engine = engine;
        instrument->provider = std::move(provider);
        instrument->defaults.gain = instrument->current.gain = options->gain;
        publish_parameters(*instrument);
        std::lock_guard lock(engine->mutex);
        if (!engine->alive)
            return invalid_request("DSP engine is no longer alive");
        const auto made =
            nk::core::handles().insert(nk::core::ResourceType::audio_dsp_instrument, instrument);
        if (made == NK_INVALID_HANDLE)
            return NK_ERROR_OUT_OF_MEMORY;
        engine->instruments.emplace_back(instrument);
        *out_instrument = made;
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_soundfont_load_memory(const uint8_t *bytes, uint32_t byte_count,
                                                 nk_audio_soundfont *out_soundfont) {
    return nk::core::result_boundary("loading a SoundFont bank", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (!out_soundfont)
            return invalid_argument("SoundFont output is missing");
        *out_soundfont = NK_INVALID_HANDLE;
        auto bank = nk::audio_dsp::SoundFontBank::load(bytes, byte_count);
        if (!bank)
            return invalid_argument("invalid or unsupported SF2 bank");
        auto resource = std::make_shared<SoundFontResource>();
        resource->bank = std::move(bank);
        resource->presets.resize(resource->bank->preset_count());
        for (uint32_t i = 0; i < resource->presets.size(); ++i) {
            auto &p = resource->presets[i];
            resource->bank->preset_info(i, p.bank, p.program, p.name);
        }
        const auto handle =
            nk::core::handles().insert(nk::core::ResourceType::audio_soundfont, resource);
        if (handle == NK_INVALID_HANDLE)
            return NK_ERROR_OUT_OF_MEMORY;
        *out_soundfont = handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_soundfont_destroy(nk_audio_soundfont handle) {
    return nk::core::result_boundary("destroying a SoundFont bank", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        return nk::core::handles().erase(handle, nk::core::ResourceType::audio_soundfont)
                   ? NK_OK
                   : NK_ERROR_INVALID_HANDLE;
    });
}

nk_result NK_CALL nk_audio_soundfont_get_preset_count(nk_audio_soundfont handle,
                                                      uint32_t *out_count) {
    return nk::core::result_boundary("reading SoundFont presets", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (!out_count)
            return invalid_argument("SoundFont preset count output is missing");
        const auto resource = std::dynamic_pointer_cast<SoundFontResource>(
            nk::core::handles().get(handle, nk::core::ResourceType::audio_soundfont));
        if (!resource)
            return NK_ERROR_INVALID_HANDLE;
        *out_count = resource->presets.size();
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_soundfont_get_preset_info(nk_audio_soundfont handle, uint32_t index,
                                                     nk_audio_soundfont_preset_info *out_info) {
    return nk::core::result_boundary("reading a SoundFont preset", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (!out_info || out_info->struct_size < sizeof(*out_info))
            return invalid_argument("SoundFont preset info output is missing");
        const auto resource = std::dynamic_pointer_cast<SoundFontResource>(
            nk::core::handles().get(handle, nk::core::ResourceType::audio_soundfont));
        if (!resource)
            return NK_ERROR_INVALID_HANDLE;
        if (index >= resource->presets.size())
            return invalid_argument("SoundFont preset index is invalid");
        const auto &p = resource->presets[index];
        out_info->bank = p.bank;
        out_info->program = p.program;
        out_info->name = p.name.c_str();
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_dsp_instrument_create_from_soundfont(
    nk_audio_dsp_engine engine_handle, nk_audio_soundfont soundfont_handle, uint32_t preset,
    float gain, nk_audio_dsp_instrument *out_instrument) {
    return nk::core::result_boundary("preparing a SoundFont instrument", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (!out_instrument || !std::isfinite(gain) || gain < 0 || gain > 4)
            return invalid_argument("invalid SoundFont instrument options");
        *out_instrument = NK_INVALID_HANDLE;
        const auto engine = get_engine(engine_handle);
        const auto bank = std::dynamic_pointer_cast<SoundFontResource>(
            nk::core::handles().get(soundfont_handle, nk::core::ResourceType::audio_soundfont));
        if (!engine || !bank)
            return NK_ERROR_INVALID_HANDLE;
        // Reserve every possible stealing tail so future policy changes cannot
        // exceed the prepared provider's capacity.
        auto pool = nk::audio_dsp::SoundFontPool::prepare(
            bank->bank, preset, engine->options.max_voices + 64, engine->options.sample_rate);
        if (!pool)
            return invalid_argument("invalid SF2 preset or excessive region layering");
        auto instrument = std::make_shared<DspInstrumentResource>();
        instrument->engine = engine;
        instrument->provider = std::move(pool);
        instrument->defaults.gain = instrument->current.gain = gain;
        publish_parameters(*instrument);
        std::lock_guard lock(engine->mutex);
        if (!engine->alive)
            return invalid_request("audio DSP engine is no longer alive");
        const auto handle =
            nk::core::handles().insert(nk::core::ResourceType::audio_dsp_instrument, instrument);
        if (handle == NK_INVALID_HANDLE)
            return NK_ERROR_OUT_OF_MEMORY;
        engine->instruments.emplace_back(instrument);
        *out_instrument = handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_dsp_engine_create(const nk_audio_dsp_engine_options *options,
                                             nk_audio_dsp_engine *out_engine) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio DSP engine", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (!out_engine)
                return invalid_argument("audio DSP engine output is missing");
            *out_engine = NK_INVALID_HANDLE;
            nk_audio_dsp_engine_options normalized{};
            if (const auto result = normalize_engine_options(options, normalized); result != NK_OK)
                return result;
            auto engine = std::make_shared<DspEngineResource>();
            engine->options = normalized;
            engine->voices.resize(normalized.max_voices);
            engine->output_scratch.resize(static_cast<uint64_t>(normalized.block_size) *
                                          normalized.channels);
            for (auto &voice : engine->voices)
                voice.backend.init(normalized.sample_rate);
            const auto handle =
                nk::core::handles().insert(nk::core::ResourceType::audio_dsp_engine, engine);
            if (handle == NK_INVALID_HANDLE) {
                nk::core::set_error("could not allocate an audio DSP engine handle");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            *out_engine = handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_engine_set_voice_policy(
    nk_audio_dsp_engine handle, const nk_audio_dsp_voice_policy_options *options) {
    return nk::core::result_boundary("configuring DSP voice admission", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        auto engine = get_engine(handle);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        if (!options || options->struct_size < sizeof(*options) || options->steal_enabled > 1 ||
            options->tail_voices > 64 || options->fade_frames == 0 ||
            options->fade_frames > engine->options.sample_rate / 10 ||
            (options->steal_enabled && !options->tail_voices))
            return invalid_argument("invalid DSP voice admission options");
        std::lock_guard lock(engine->mutex);
        if (!engine->alive || engine->attached.load(std::memory_order_acquire))
            return invalid_request("configure DSP voice admission before attaching");
        for (const auto &voice : engine->voices)
            if (voice.active)
                return invalid_request("configure DSP voice admission before playing");
        for (const auto &voice : engine->tails)
            if (voice.active)
                return invalid_request("DSP stealing tails are still active");
        engine->tails.clear();
        engine->tails.resize(options->tail_voices);
        for (auto &tail : engine->tails)
            tail.backend.init(engine->options.sample_rate);
        engine->stealing = options->steal_enabled != 0;
        engine->fade_frames = options->fade_frames;
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_dsp_engine_note_on(nk_audio_dsp_engine handle,
                                              nk_audio_dsp_instrument instrument_handle,
                                              const nk_audio_dsp_note_options *options,
                                              uint64_t *out_note_token) {
    return nk::core::result_boundary("submitting a DSP note", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (!out_note_token)
            return invalid_argument("DSP note token output is missing");
        *out_note_token = 0;
        auto engine = get_engine(handle);
        auto instrument = get_instrument(instrument_handle);
        if (!engine || !instrument)
            return NK_ERROR_INVALID_HANDLE;
        if (!options || options->struct_size < sizeof(*options) || options->note > 127 ||
            !std::isfinite(options->velocity) || options->velocity < 0 || options->velocity > 1 ||
            options->hold_frames > UINT64_MAX - options->start_frame)
            return invalid_argument("invalid DSP note options");
        if (!instrument_belongs_to(instrument, *engine))
            return invalid_request("DSP note instrument belongs to another engine");
        std::lock_guard lock(engine->mutex);
        if (!engine->alive || instrument->retired)
            return invalid_request("DSP instrument retired");
        TransportCommand command;
        command.kind = TransportCommand::queued_note;
        command.note.instrument = std::move(instrument);
        command.note.event.kind = NK_AUDIO_DSP_EVENT_NOTE_ON;
        command.note.event.note = options->note;
        command.note.event.velocity = options->velocity;
        command.note.frame = options->start_frame;
        command.note.generation = engine->scheduled_generation.load(std::memory_order_acquire);
        command.note.hold_frames = options->hold_frames;
        command.note.priority = options->priority;
        command.note.note_token = next_note_token(*engine);
        if (!command.note.note_token)
            return invalid_request("DSP note tokens exhausted");
        const auto token = command.note.note_token;
        if (!enqueue_command(*engine, std::move(command))) {
            engine->dropped_notes.fetch_add(1, std::memory_order_relaxed);
            nk::core::set_error("DSP runtime command queue is full");
            return NK_ERROR_QUEUE_FULL;
        }
        *out_note_token = token;
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_dsp_engine_note_off(nk_audio_dsp_engine handle, uint64_t token,
                                               uint64_t frame) {
    return nk::core::result_boundary("releasing a DSP note", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        auto engine = get_engine(handle);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        if (!token)
            return invalid_argument("DSP note token is zero");
        std::lock_guard lock(engine->mutex);
        if (!engine->alive)
            return invalid_request("DSP engine destroyed");
        TransportCommand command;
        command.kind = TransportCommand::queued_note;
        command.note.event.kind = NK_AUDIO_DSP_EVENT_NOTE_OFF;
        command.note.note_token = token;
        command.note.frame = frame;
        command.note.generation = engine->scheduled_generation.load(std::memory_order_acquire);
        return enqueue_command(*engine, std::move(command)) ? NK_OK : NK_ERROR_QUEUE_FULL;
    });
}

nk_result NK_CALL nk_audio_dsp_engine_prepare_transport(
    nk_audio_dsp_engine handle, const nk_audio_dsp_transport_section *sections,
    uint32_t section_count, const nk_audio_dsp_transport_event *events, uint32_t event_count,
    const double *bars, uint32_t bar_count, uint64_t *out_program_id) {
    return nk::core::result_boundary("preparing native DSP transport", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (!out_program_id)
            return invalid_argument("transport program output is missing");
        *out_program_id = 0;
        auto engine = get_engine(handle);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        if (!sections || !section_count || section_count > 128 || event_count > 65536 ||
            bar_count > 65536 || (event_count && !events) || (bar_count && !bars))
            return invalid_argument("invalid transport arrays");
        auto program = std::make_unique<TransportProgram>();
        for (uint32_t i = 0; i < section_count; ++i) {
            const auto &source = sections[i];
            if (!std::isfinite(source.length_frames) || source.length_frames < 1 ||
                source.length_frames > 9007199254740991.0 || source.event_offset > event_count ||
                source.event_count > event_count - source.event_offset ||
                source.bar_offset > bar_count || source.bar_count > bar_count - source.bar_offset)
                return invalid_argument("invalid transport section bounds");
            TransportSection section;
            section.length = source.length_frames;
            double previous = -1;
            uint32_t simultaneous = 0;
            uint64_t previous_sample = UINT64_MAX;
            std::vector<uint32_t> held;
            for (uint32_t j = 0; j < source.event_count; ++j) {
                const auto &event = events[source.event_offset + j];
                if (!std::isfinite(event.frame) || event.frame < 0 || event.frame < previous ||
                    event.frame > source.length_frames || event.kind > 2 ||
                    (event.kind == 0 && event.frame == source.length_frames))
                    return invalid_argument("invalid transport event position");
                previous = event.frame;
                const auto sample = static_cast<uint64_t>(std::floor(event.frame));
                simultaneous = sample == previous_sample ? simultaneous + 1 : 1;
                previous_sample = sample;
                if (simultaneous > 128)
                    return invalid_argument("transport event burst exceeds callback budget");
                TransportEvent prepared;
                prepared.value = event;
                if (event.kind == 2) {
                    if (!std::isfinite(event.bpm) || event.bpm < 1 || event.bpm > 1000)
                        return invalid_argument("invalid transport tempo");
                    section.tempos.push_back({event.frame, event.bpm});
                } else if (!event.note_id)
                    return invalid_argument("transport note identity is zero");
                else if (event.kind == 0) {
                    if (event.note > 127 || !std::isfinite(event.velocity) || event.velocity < 0 ||
                        event.velocity > 1 ||
                        std::find(held.begin(), held.end(), event.note_id) != held.end())
                        return invalid_argument("invalid or overlapping transport note identity");
                    prepared.instrument = get_instrument(event.instrument);
                    if (!prepared.instrument)
                        return NK_ERROR_INVALID_HANDLE;
                    if (!instrument_belongs_to(prepared.instrument, *engine))
                        return invalid_request("transport instrument belongs to another engine");
                    held.push_back(event.note_id);
                } else {
                    const auto found = std::find(held.begin(), held.end(), event.note_id);
                    if (found == held.end())
                        return invalid_argument("transport release has no paired start");
                    held.erase(found);
                }
                section.events.push_back(std::move(prepared));
            }
            if (!held.empty())
                return invalid_argument("transport contains unreleased notes");
            // Pair note lifetimes off the callback for sustained-note reconstruction on seek.
            std::vector<std::pair<uint32_t, size_t>> starts;
            for (size_t event_index = 0; event_index < section.events.size(); ++event_index) {
                auto &event = section.events[event_index];
                if (event.value.kind == 0)
                    starts.emplace_back(event.value.note_id, event_index);
                else if (event.value.kind == 1) {
                    auto found = std::find_if(starts.begin(), starts.end(), [&](const auto &entry) {
                        return entry.first == event.value.note_id;
                    });
                    if (found != starts.end()) {
                        section.events[found->second].end_frame = event.value.frame;
                        starts.erase(found);
                    }
                }
            }
            previous = -1;
            for (uint32_t j = 0; j < source.bar_count; ++j) {
                const auto bar = bars[source.bar_offset + j];
                if (!std::isfinite(bar) || bar < 0 || bar <= previous || bar > section.length)
                    return invalid_argument("invalid transport bar boundary");
                section.bars.push_back(bar);
                previous = bar;
            }
            program->sections.push_back(std::move(section));
        }
        std::lock_guard lock(engine->mutex);
        if (!engine->alive)
            return invalid_request("DSP engine destroyed");
        collect_resources(*engine);
        if (engine->programs.size() >= 64)
            return invalid_request("too many retained transport programs");
        program->id = engine->next_program++;
        *out_program_id = program->id;
        engine->programs.push_back(std::move(program));
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_dsp_engine_play_transport(nk_audio_dsp_engine handle, uint64_t id,
                                                     uint32_t section, uint64_t frame) {
    return nk::core::result_boundary("starting native DSP transport", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        auto engine = get_engine(handle);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        std::lock_guard lock(engine->mutex);
        if (!engine->alive)
            return invalid_request("DSP engine destroyed");
        for (const auto &program : engine->programs)
            if (program->id == id && !program->retired) {
                if (section >= program->sections.size())
                    return invalid_argument("unknown transport section");
                TransportCommand command;
                command.kind = TransportCommand::play;
                command.program = program.get();
                command.section = section;
                command.frame = frame;
                return enqueue_command(*engine, std::move(command))
                           ? NK_OK
                           : invalid_request("DSP runtime command queue is full");
            }
        return invalid_argument("unknown or retired transport program");
    });
}

nk_result NK_CALL nk_audio_dsp_engine_request_section(nk_audio_dsp_engine handle,
                                                      uint32_t section) {
    return nk::core::result_boundary("requesting native transport section", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        auto engine = get_engine(handle);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        std::lock_guard lock(engine->mutex);
        if (!engine->alive)
            return invalid_request("DSP engine destroyed");
        // The most recently queued program is authoritative even before its
        // first callback. Programs remain pinned while commands reference them.
        TransportProgram *program = nullptr;
        bool queued_stop = false;
        const auto write = engine->transport_write.load(std::memory_order_acquire);
        const auto read = engine->transport_read.load(std::memory_order_acquire);
        for (uint32_t cursor = write; cursor != read;) {
            const auto &command =
                engine->transport_commands[(--cursor) % transport_command_capacity];
            if (command.kind == TransportCommand::stop) {
                queued_stop = true;
                break;
            }
            if (command.kind == TransportCommand::play ||
                command.kind == TransportCommand::relocate) {
                program = command.program;
                break;
            }
        }
        if (!program && !queued_stop) {
            const auto id = engine->active_program.load(std::memory_order_acquire);
            for (const auto &entry : engine->programs)
                if (entry->id == id)
                    program = entry.get();
        }
        if (!program || section >= program->sections.size())
            return invalid_argument("unknown transport section");
        TransportCommand command;
        command.kind = TransportCommand::request;
        command.section = section;
        return enqueue_command(*engine, std::move(command))
                   ? NK_OK
                   : invalid_request("DSP runtime command queue is full");
    });
}

nk_result NK_CALL nk_audio_dsp_engine_stop_transport(nk_audio_dsp_engine handle) {
    return nk::core::result_boundary("stopping native DSP transport", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        auto engine = get_engine(handle);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        std::lock_guard lock(engine->mutex);
        if (!engine->alive)
            return invalid_request("DSP engine destroyed");
        TransportCommand command;
        return enqueue_command(*engine, command)
                   ? NK_OK
                   : invalid_request("DSP runtime command queue is full");
    });
}

nk_result NK_CALL nk_audio_dsp_engine_retire_transport(nk_audio_dsp_engine handle, uint64_t id) {
    return nk::core::result_boundary("retiring native DSP transport", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        auto engine = get_engine(handle);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        std::lock_guard lock(engine->mutex);
        for (const auto &program : engine->programs)
            if (program->id == id) {
                program->retired = true;
                collect_resources(*engine);
                return NK_OK;
            }
        return invalid_argument("unknown transport program");
    });
}

nk_result NK_CALL nk_audio_dsp_engine_collect(nk_audio_dsp_engine handle) {
    return nk::core::result_boundary("collecting retired DSP resources", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        auto engine = get_engine(handle);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        std::lock_guard lock(engine->mutex);
        collect_resources(*engine);
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_dsp_engine_get_voice_counts(nk_audio_dsp_engine handle,
                                                       uint32_t *out_active_voices,
                                                       uint32_t *out_active_tails) {
    return nk::core::result_boundary("reading DSP voice counts", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (!out_active_voices || !out_active_tails)
            return invalid_argument("voice count outputs are required");
        auto engine = get_engine(handle);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        uint32_t voices, tails;
        for (;;) {
            const auto before = engine->status_sequence.load(std::memory_order_acquire);
            if (before & 1)
                continue;
            voices = engine->status_voices.load(std::memory_order_relaxed);
            tails = engine->status_tails.load(std::memory_order_relaxed);
            if (before == engine->status_sequence.load(std::memory_order_acquire))
                break;
        }
        *out_active_voices = voices;
        *out_active_tails = tails;
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_dsp_engine_get_transport_position(nk_audio_dsp_engine handle,
                                                             uint32_t *out_section,
                                                             double *out_frame) {
    return nk::core::result_boundary("reading native DSP transport position", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (!out_section || !out_frame)
            return invalid_argument("Transport position outputs are required");
        auto engine = get_engine(handle);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        for (;;) {
            const auto before = engine->status_sequence.load(std::memory_order_acquire);
            if (before & 1)
                continue;
            const auto section = engine->status_section.load(std::memory_order_relaxed);
            const auto frame = engine->status_section_frame.load(std::memory_order_relaxed);
            if (before != engine->status_sequence.load(std::memory_order_acquire))
                continue;
            *out_section = section;
            *out_frame = frame;
            return NK_OK;
        }
    });
}

nk_result NK_CALL nk_audio_dsp_engine_get_runtime_status(nk_audio_dsp_engine handle,
                                                         nk_audio_dsp_runtime_status *out_status) {
    return nk::core::result_boundary("reading native DSP transport status", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        auto engine = get_engine(handle);
        if (!engine)
            return NK_ERROR_INVALID_HANDLE;
        if (!out_status || out_status->struct_size < sizeof(*out_status))
            return invalid_argument("DSP runtime status output is too small");
        std::lock_guard lock(engine->mutex);
        collect_resources(*engine);
        nk_audio_dsp_runtime_status status{};
        status.struct_size = sizeof(status);
        for (;;) {
            const auto before = engine->status_sequence.load(std::memory_order_acquire);
            if (before & 1)
                continue;
            status.playing = engine->status_playing.load(std::memory_order_relaxed);
            status.section = engine->status_section.load(std::memory_order_relaxed);
            status.frame = engine->status_frame.load(std::memory_order_relaxed);
            status.loops = engine->status_loops.load(std::memory_order_relaxed);
            status.bpm = engine->status_bpm.load(std::memory_order_relaxed);
            if (before == engine->status_sequence.load(std::memory_order_acquire))
                break;
        }
        status.stolen_voices = engine->stolen_voices.load(std::memory_order_relaxed);
        status.dropped_notes = engine->dropped_notes.load(std::memory_order_relaxed);
        status.clock_discontinuities =
            engine->clock_discontinuities.load(std::memory_order_relaxed);
        for (const auto &program : engine->programs)
            if (program->retired)
                ++status.retired_resources;
        for (const auto &instrument : engine->instruments)
            if (instrument->retired)
                ++status.retired_resources;
        *out_status = status;
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_dsp_engine_destroy(nk_audio_dsp_engine engine_handle) {
    return nk::core::result_boundary(
        "unexpected error while destroying an audio DSP engine", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            auto engine = get_engine(engine_handle);
            if (!engine)
                return NK_ERROR_INVALID_HANDLE;
            if (engine->attached.load(std::memory_order_acquire)) {
                if (const auto result = nk_audio_dsp_engine_detach_device(engine_handle);
                    result != NK_OK)
                    return result;
            }
            {
                std::lock_guard lock(engine->mutex);
                engine->alive = false;
                reset_voices(*engine);
            }
            if (!nk::core::handles().erase(engine_handle, nk::core::ResourceType::audio_dsp_engine))
                return NK_ERROR_INVALID_HANDLE;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_engine_get_options(nk_audio_dsp_engine engine_handle,
                                                  nk_audio_dsp_engine_options *out_options) {
    return nk::core::result_boundary(
        "unexpected error while getting audio DSP engine options", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (!out_options)
                return invalid_argument("audio DSP engine options output is missing");
            if (out_options->struct_size < sizeof(nk_audio_dsp_engine_options))
                return invalid_argument("audio DSP engine options output is missing or too small");
            auto engine = get_engine(engine_handle);
            if (!engine)
                return NK_ERROR_INVALID_HANDLE;
            std::lock_guard lock(engine->mutex);
            if (!engine->alive)
                return invalid_request("audio DSP engine is no longer alive");
            *out_options = engine->options;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_engine_get_capabilities(
    nk_audio_dsp_engine engine_handle, nk_audio_dsp_capabilities *out_capabilities) {
    return nk::core::result_boundary(
        "unexpected error while getting audio DSP capabilities", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (!out_capabilities)
                return invalid_argument("audio DSP capabilities output is missing");
            auto engine = get_engine(engine_handle);
            if (!engine)
                return NK_ERROR_INVALID_HANDLE;
            std::lock_guard lock(engine->mutex);
            if (!engine->alive)
                return invalid_request("audio DSP engine is no longer alive");
            *out_capabilities = builtin_capabilities;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_engine_reset(nk_audio_dsp_engine engine_handle) {
    return nk::core::result_boundary(
        "unexpected error while resetting an audio DSP engine", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            auto engine = get_engine(engine_handle);
            if (!engine)
                return NK_ERROR_INVALID_HANDLE;
            std::lock_guard lock(engine->mutex);
            if (!engine->alive)
                return invalid_request("audio DSP engine is no longer alive");
            if (engine->attached.load(std::memory_order_acquire))
                return invalid_request(
                    "detach the audio DSP engine before offline rendering or resetting");
            stop_runtime(*engine);
            reset_voices(*engine);
            engine->offline_frame = 0;
            engine->transport.origin = 0;
            engine->transport.section = 0;
            engine->transport.loops = 0;
            engine->transport.bpm = 120;
            engine->clock->bpm.store(120, std::memory_order_release);
            publish_transport_status(*engine, 0);
            for (auto iterator = engine->instruments.begin();
                 iterator != engine->instruments.end();) {
                auto &instrument = *iterator;
                if (!instrument->retired) {
                    instrument->current = instrument->defaults;
                    publish_parameters(*instrument);
                    ++instrument->parameters_version;
                }
                ++iterator;
            }
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_engine_schedule(nk_audio_dsp_engine engine_handle,
                                               uint64_t start_frame,
                                               const nk_audio_dsp_event *events,
                                               uint32_t event_count) {
    return nk::core::result_boundary(
        "unexpected error while scheduling audio DSP events", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (event_count != 0 && !events)
                return invalid_argument("audio DSP schedule event array is missing");
            auto engine = get_engine(engine_handle);
            if (!engine)
                return NK_ERROR_INVALID_HANDLE;
            std::lock_guard lock(engine->mutex);
            if (!engine->alive)
                return invalid_request("audio DSP engine is no longer alive");
            std::vector<std::shared_ptr<DspInstrumentResource>> instruments;
            instruments.reserve(event_count);
            uint32_t previous_frame = 0;
            for (uint32_t index = 0; index < event_count; ++index) {
                const auto &event = events[index];
                if (event.frame_offset < previous_frame)
                    return invalid_argument("audio DSP scheduled events must be frame sorted");
                if (const auto result = validate_event(
                        *engine, event, std::numeric_limits<uint32_t>::max(), previous_frame);
                    result != NK_OK)
                    return result;
                if (event.kind == NK_AUDIO_DSP_EVENT_NOTE_ON ||
                    event.kind == NK_AUDIO_DSP_EVENT_PARAMETER ||
                    event.kind == NK_AUDIO_DSP_EVENT_PARAMETER_RAMP) {
                    auto instrument = get_instrument(event.instrument);
                    if (!instrument)
                        return NK_ERROR_INVALID_HANDLE;
                    if (!instrument_belongs_to(instrument, *engine))
                        return invalid_request("audio DSP instrument belongs to another engine");
                    instruments.push_back(std::move(instrument));
                } else {
                    instruments.emplace_back();
                }
                previous_frame = event.frame_offset;
            }
            const auto generation = engine->scheduled_generation.load(std::memory_order_acquire);
            auto write = engine->scheduled_write.load(std::memory_order_relaxed);
            const auto read = engine->scheduled_read.load(std::memory_order_acquire);
            if (event_count > scheduled_event_capacity - (write - read))
                return invalid_request("audio DSP schedule queue is full");
            for (uint32_t index = 0; index < event_count; ++index) {
                auto &scheduled = engine->scheduled_events[write & scheduled_event_mask];
                scheduled.frame = start_frame + events[index].frame_offset;
                scheduled.generation = generation;
                scheduled.event = events[index];
                scheduled.instrument = std::move(instruments[index]);
                ++write;
            }
            engine->scheduled_write.store(write, std::memory_order_release);
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_engine_clear_schedule(nk_audio_dsp_engine engine_handle) {
    return nk::core::result_boundary(
        "unexpected error while clearing audio DSP events", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            auto engine = get_engine(engine_handle);
            if (!engine)
                return NK_ERROR_INVALID_HANDLE;
            std::lock_guard lock(engine->mutex);
            if (!engine->alive)
                return invalid_request("audio DSP engine is no longer alive");
            engine->scheduled_generation.fetch_add(1, std::memory_order_acq_rel);
            // Attached queue slots are retired only by the consumer. Rewinding its
            // cursor here would let the producer overwrite a slot still being read.
            if (!engine->attached.load(std::memory_order_acquire))
                engine->scheduled_read.store(
                    engine->scheduled_write.load(std::memory_order_acquire),
                    std::memory_order_release);
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_patch_create(const nk_audio_dsp_patch_options *options,
                                            nk_audio_dsp_patch *out_patch) {
    return nk_audio_dsp_patch_create_extended(options, nullptr, out_patch);
}

nk_result NK_CALL nk_audio_dsp_patch_create_extended(
    const nk_audio_dsp_patch_options *options, const nk_audio_dsp_voice_options *voice_options,
    nk_audio_dsp_patch *out_patch) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio DSP patch", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (!out_patch)
                return invalid_argument("audio DSP patch output is missing");
            *out_patch = NK_INVALID_HANDLE;
            DspParameters parameters;
            if (const auto result = normalize_patch_options(options, parameters); result != NK_OK)
                return result;
            if (const auto result = normalize_voice_options(voice_options, parameters);
                result != NK_OK)
                return result;
            auto patch = std::make_shared<DspPatchResource>();
            patch->parameters = parameters;
            const auto handle =
                nk::core::handles().insert(nk::core::ResourceType::audio_dsp_patch, patch);
            if (handle == NK_INVALID_HANDLE) {
                nk::core::set_error("could not allocate an audio DSP patch handle");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            *out_patch = handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_source_parameter_info(uint32_t kind, uint32_t parameter,
                                                     float *out_minimum, float *out_maximum,
                                                     float *out_default) {
    return nk::core::result_boundary("audio DSP source parameter info", [&]() -> nk_result {
        if (!out_minimum || !out_maximum || !out_default)
            return invalid_argument("missing source parameter output");
        if (!nk::audio_dsp::source_parameter_info(kind, parameter, *out_minimum, *out_maximum,
                                                  *out_default))
            return invalid_argument("unsupported source parameter");
        return NK_OK;
    });
}
nk_result NK_CALL nk_audio_dsp_source_defaults(uint32_t kind,
                                               nk_audio_dsp_source_options *out_options) {
    return nk::core::result_boundary("audio DSP source defaults", [&]() -> nk_result {
        if (!out_options || kind < 1 || kind > NK_AUDIO_DSP_SOURCE_KIND_COUNT)
            return invalid_argument("invalid source kind or output");
        *out_options = {};
        out_options->struct_size = sizeof(*out_options);
        out_options->kind = kind;
        for (uint32_t p = 0; p < NK_AUDIO_DSP_SOURCE_PARAMETER_COUNT; ++p) {
            float lo = 0, hi = 0, value = 0;
            if (nk::audio_dsp::source_parameter_info(kind, p, lo, hi, value))
                out_options->values[p] = value;
        }
        if (kind == NK_AUDIO_DSP_SOURCE_OSCILLATOR_BANK || kind == NK_AUDIO_DSP_SOURCE_HARMONIC)
            out_options->amplitudes[0] = 1;
        return NK_OK;
    });
}
nk_result NK_CALL nk_audio_dsp_patch_create_source(const nk_audio_dsp_patch_options *options,
                                                   const nk_audio_dsp_source_options *source,
                                                   const float *samples, uint32_t sample_count,
                                                   nk_audio_dsp_patch *out_patch) {
    return nk_audio_dsp_patch_create_source_extended(options, nullptr, source, samples,
                                                     sample_count, out_patch);
}

nk_result NK_CALL nk_audio_dsp_patch_create_source_extended(
    const nk_audio_dsp_patch_options *options, const nk_audio_dsp_voice_options *voice_options,
    const nk_audio_dsp_source_options *source, const float *samples, uint32_t sample_count,
    nk_audio_dsp_patch *out_patch) {
    return nk::core::result_boundary("creating audio DSP source patch", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (!out_patch)
            return invalid_argument("missing source patch output");
        *out_patch = NK_INVALID_HANDLE;
        DspParameters parameters;
        if (const auto result = normalize_patch_options(options, parameters); result != NK_OK)
            return result;
        if (!source ||
            !nk::audio_dsp::normalize_source(*source, samples, sample_count, parameters.source))
            return invalid_argument("invalid DSP source configuration or samples");
        if (const auto result = normalize_voice_options(voice_options, parameters); result != NK_OK)
            return result;
        if (parameters.unison.count != 1)
            return invalid_argument("specialized DSP sources require one unison copy");
        if (replacement_source(parameters)) {
            if (parameters.oscillator_count != 1 ||
                parameters.oscillators[0].waveform != NK_AUDIO_DSP_WAVEFORM_SINE ||
                parameters.oscillators[0].wavetable || parameters.oscillators[0].phase != 0)
                return invalid_argument(
                    "specialized source uses one neutral oscillator slot for level and detune");
            for (uint32_t i = 0; i < parameters.route_count; ++i) {
                const auto &route = parameters.routes[i];
                if (route.source == NK_AUDIO_DSP_MODULATION_SOURCE_OSCILLATOR ||
                    route.destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_PHASE ||
                    route.oscillator_index > 1)
                    return invalid_argument("source does not support oscillator operator routes");
            }
        }
        auto patch = std::make_shared<DspPatchResource>();
        patch->parameters = std::move(parameters);
        const auto handle =
            nk::core::handles().insert(nk::core::ResourceType::audio_dsp_patch, patch);
        if (handle == NK_INVALID_HANDLE)
            return NK_ERROR_OUT_OF_MEMORY;
        *out_patch = handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_dsp_patch_destroy(nk_audio_dsp_patch patch_handle) {
    return nk::core::result_boundary(
        "unexpected error while destroying an audio DSP patch", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (!get_patch(patch_handle))
                return NK_ERROR_INVALID_HANDLE;
            if (!nk::core::handles().erase(patch_handle, nk::core::ResourceType::audio_dsp_patch))
                return NK_ERROR_INVALID_HANDLE;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_wavetable_create(const float *samples, uint32_t sample_count,
                                                nk_audio_dsp_wavetable *out_wavetable) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio DSP wavetable", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (!out_wavetable)
                return invalid_argument("audio DSP wavetable output is missing");
            *out_wavetable = NK_INVALID_HANDLE;
            if (!samples || !valid_wavetable_sample_count(sample_count))
                return invalid_argument("audio DSP wavetable samples are invalid");
            for (uint32_t index = 0; index < sample_count; ++index) {
                if (!std::isfinite(samples[index]))
                    return invalid_argument("audio DSP wavetable samples must be finite");
            }
            auto table = nk::audio_dsp::Wavetable::create(samples, sample_count);
            if (!table) {
                nk::core::set_error("could not create an audio DSP wavetable");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            auto resource = std::make_shared<DspWavetableResource>();
            resource->table = std::move(table);
            const auto handle =
                nk::core::handles().insert(nk::core::ResourceType::audio_dsp_wavetable, resource);
            if (handle == NK_INVALID_HANDLE) {
                nk::core::set_error("could not allocate an audio DSP wavetable handle");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            *out_wavetable = handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_wavetable_destroy(nk_audio_dsp_wavetable wavetable_handle) {
    return nk::core::result_boundary(
        "unexpected error while destroying an audio DSP wavetable", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (!get_wavetable(wavetable_handle))
                return NK_ERROR_INVALID_HANDLE;
            if (!nk::core::handles().erase(wavetable_handle,
                                           nk::core::ResourceType::audio_dsp_wavetable))
                return NK_ERROR_INVALID_HANDLE;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_instrument_create_from_patch(
    nk_audio_dsp_engine engine_handle, nk_audio_dsp_patch patch_handle,
    nk_audio_dsp_instrument *out_instrument) {
    return nk::core::result_boundary(
        "unexpected error while creating an instrument from an audio DSP patch",
        [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (!out_instrument)
                return invalid_argument("audio DSP instrument output is missing");
            *out_instrument = NK_INVALID_HANDLE;
            auto engine = get_engine(engine_handle);
            if (!engine)
                return NK_ERROR_INVALID_HANDLE;
            auto patch = get_patch(patch_handle);
            if (!patch)
                return NK_ERROR_INVALID_HANDLE;
            if (const auto result = validate_engine_parameters(patch->parameters, *engine);
                result != NK_OK)
                return result;
            return create_instrument_resource(engine, patch, patch->parameters, out_instrument);
        });
}

nk_result NK_CALL nk_audio_dsp_instrument_create(nk_audio_dsp_engine engine_handle,
                                                 const nk_audio_dsp_instrument_options *options,
                                                 nk_audio_dsp_instrument *out_instrument) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio DSP instrument", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (!out_instrument)
                return invalid_argument("audio DSP instrument output is missing");
            *out_instrument = NK_INVALID_HANDLE;
            auto engine = get_engine(engine_handle);
            if (!engine)
                return NK_ERROR_INVALID_HANDLE;
            DspParameters parameters;
            if (const auto result =
                    normalize_instrument_options(options, engine->options.sample_rate, parameters);
                result != NK_OK)
                return result;
            auto patch = std::make_shared<DspPatchResource>();
            patch->parameters = parameters;
            return create_instrument_resource(engine, patch, parameters, out_instrument);
        });
}

nk_result NK_CALL nk_audio_dsp_instrument_destroy(nk_audio_dsp_instrument instrument_handle) {
    return nk::core::result_boundary(
        "unexpected error while destroying an audio DSP instrument", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            auto instrument = get_instrument(instrument_handle);
            if (!instrument)
                return NK_ERROR_INVALID_HANDLE;
            if (auto engine = instrument->engine.lock()) {
                std::lock_guard lock(engine->mutex);
                instrument->retired = true;
                if (!engine->attached.load(std::memory_order_acquire)) {
                    for (auto &voice : engine->voices)
                        if (voice.instrument.get() == instrument.get())
                            voice.clear();
                    for (auto &voice : engine->tails)
                        if (voice.instrument.get() == instrument.get())
                            voice.clear();
                }
            }
            if (!nk::core::handles().erase(instrument_handle,
                                           nk::core::ResourceType::audio_dsp_instrument))
                return NK_ERROR_INVALID_HANDLE;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_instrument_set_choke_group(nk_audio_dsp_instrument handle,
                                                          uint32_t group) {
    return nk::core::result_boundary("setting DSP choke group", [&]() -> nk_result {
        if (const auto result = enter_dsp(); result != NK_OK)
            return result;
        if (group > 255)
            return invalid_argument("DSP choke group must be 0..255");
        auto instrument = get_instrument(handle);
        if (!instrument)
            return NK_ERROR_INVALID_HANDLE;
        if (instrument->engine.expired())
            return NK_ERROR_INVALID_HANDLE;
        instrument->choke_group.store(group, std::memory_order_relaxed);
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_dsp_instrument_set_parameter(nk_audio_dsp_instrument instrument_handle,
                                                        nk_audio_dsp_parameter parameter,
                                                        float value) {
    return nk::core::result_boundary(
        "unexpected error while setting an audio DSP parameter", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            auto instrument = get_instrument(instrument_handle);
            if (!instrument)
                return NK_ERROR_INVALID_HANDLE;
            auto engine = instrument->engine.lock();
            if (!engine)
                return invalid_request("audio DSP instrument owner is unavailable");
            std::lock_guard lock(engine->mutex);
            if (!engine->alive)
                return invalid_request("audio DSP engine is no longer alive");
            if (instrument->provider && parameter != NK_AUDIO_DSP_PARAMETER_GAIN)
                return invalid_argument("unsupported sampled-instrument DSP parameter");
            auto parameters = control_parameters(*instrument);
            auto result = set_parameter(parameters, parameter, value);
            if (result == NK_OK)
                result = validate_engine_parameters(parameters, *engine);
            if (result == NK_OK && engine->attached.load(std::memory_order_acquire)) {
                TransportCommand command;
                command.kind = TransportCommand::queued_note;
                command.note.event.kind = NK_AUDIO_DSP_EVENT_PARAMETER;
                command.note.event.parameter = parameter;
                command.note.event.value = value;
                command.note.instrument = instrument;
                command.note.generation =
                    engine->scheduled_generation.load(std::memory_order_acquire);
                return enqueue_command(*engine, std::move(command)) ? NK_OK : NK_ERROR_QUEUE_FULL;
            }
            if (result == NK_OK) {
                instrument->current = parameters;
                publish_parameters(*instrument);
            }
            if (result == NK_OK)
                ++instrument->parameters_version;
            return result;
        });
}

nk_result NK_CALL nk_audio_dsp_instrument_set_oscillator_parameter(
    nk_audio_dsp_instrument instrument_handle, uint32_t oscillator_index,
    nk_audio_dsp_parameter parameter, float value) {
    return nk::core::result_boundary(
        "unexpected error while setting an audio DSP oscillator parameter", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            auto instrument = get_instrument(instrument_handle);
            if (!instrument)
                return NK_ERROR_INVALID_HANDLE;
            auto engine = instrument->engine.lock();
            if (!engine)
                return invalid_request("audio DSP instrument owner is unavailable");
            std::lock_guard lock(engine->mutex);
            if (!engine->alive)
                return invalid_request("audio DSP engine is no longer alive");
            if (instrument->provider)
                return invalid_argument("Sampled instrument has no DSP oscillators");
            auto parameters = control_parameters(*instrument);
            auto result = set_oscillator_parameter(parameters, oscillator_index, parameter, value);
            if (result == NK_OK)
                result = validate_engine_parameters(parameters, *engine);
            if (result == NK_OK && engine->attached.load(std::memory_order_acquire)) {
                TransportCommand command;
                command.kind = TransportCommand::queued_note;
                command.note.event.kind = NK_AUDIO_DSP_EVENT_PARAMETER;
                command.note.event.parameter = parameter;
                command.note.event.oscillator_index = oscillator_index;
                command.note.event.value = value;
                command.note.instrument = instrument;
                command.note.generation =
                    engine->scheduled_generation.load(std::memory_order_acquire);
                return enqueue_command(*engine, std::move(command)) ? NK_OK : NK_ERROR_QUEUE_FULL;
            }
            if (result == NK_OK) {
                instrument->current = parameters;
                publish_parameters(*instrument);
            }
            if (result == NK_OK)
                ++instrument->parameters_version;
            return result;
        });
}

nk_result NK_CALL nk_audio_dsp_instrument_get_parameter(nk_audio_dsp_instrument instrument_handle,
                                                        nk_audio_dsp_parameter parameter,
                                                        float *out_value) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio DSP parameter", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (!out_value)
                return invalid_argument("audio DSP parameter output is missing");
            if (!valid_parameter(parameter))
                return invalid_argument("audio DSP parameter is invalid");
            if (valid_oscillator_parameter(parameter))
                return invalid_argument("audio DSP oscillator parameter requires a source index");
            auto instrument = get_instrument(instrument_handle);
            if (!instrument)
                return NK_ERROR_INVALID_HANDLE;
            auto engine = instrument->engine.lock();
            if (!engine)
                return invalid_request("audio DSP instrument owner is unavailable");
            std::lock_guard lock(engine->mutex);
            if (!engine->alive)
                return invalid_request("audio DSP engine is no longer alive");
            if (instrument->provider && parameter != NK_AUDIO_DSP_PARAMETER_GAIN)
                return invalid_argument("unsupported sampled-instrument DSP parameter");
            *out_value = instrument->published[parameter].load(std::memory_order_relaxed);
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_instrument_get_oscillator_parameter(
    nk_audio_dsp_instrument instrument_handle, uint32_t oscillator_index,
    nk_audio_dsp_parameter parameter, float *out_value) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio DSP oscillator parameter", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (!out_value)
                return invalid_argument("audio DSP oscillator parameter output is missing");
            if (!valid_oscillator_parameter(parameter))
                return invalid_argument("audio DSP oscillator parameter is invalid");
            auto instrument = get_instrument(instrument_handle);
            if (!instrument)
                return NK_ERROR_INVALID_HANDLE;
            auto engine = instrument->engine.lock();
            if (!engine)
                return invalid_request("audio DSP instrument owner is unavailable");
            std::lock_guard lock(engine->mutex);
            if (!engine->alive)
                return invalid_request("audio DSP engine is no longer alive");
            if (instrument->provider)
                return invalid_argument("Sampled instrument has no DSP oscillators");
            if (oscillator_index >= instrument->defaults.oscillator_count)
                return invalid_argument("audio DSP oscillator parameter target is invalid");
            *out_value = instrument->published_oscillators[oscillator_index][parameter - 9].load(
                std::memory_order_relaxed);
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_engine_render(nk_audio_dsp_engine engine_handle,
                                             nk_audio_dsp_render_target *target,
                                             const nk_audio_dsp_event *events,
                                             uint32_t event_count) {
    return nk::core::result_boundary(
        "unexpected error while rendering audio DSP", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            if (!target)
                return invalid_argument("audio DSP render target is missing");
            if (target->struct_size < sizeof(nk_audio_dsp_render_target))
                return invalid_argument("audio DSP render target is missing or too small");
            if (!target->samples || target->frame_count == 0)
                return invalid_argument("audio DSP render target is invalid");
            if (event_count != 0 && !events)
                return invalid_argument("audio DSP event array is missing");
            auto engine = get_engine(engine_handle);
            if (!engine)
                return NK_ERROR_INVALID_HANDLE;
            std::lock_guard lock(engine->mutex);
            if (!engine->alive)
                return invalid_request("audio DSP engine is no longer alive");
            if (engine->attached.load(std::memory_order_acquire))
                return invalid_request(
                    "detach the audio DSP engine before offline rendering or resetting");
            if (target->frame_count > engine->options.block_size ||
                target->channels != engine->options.channels)
                return invalid_argument("audio DSP render target does not match the engine");
            const auto sample_count = static_cast<uint64_t>(target->frame_count) *
                                      static_cast<uint64_t>(target->channels);
            if (target->sample_count < sample_count)
                return invalid_argument("audio DSP render target buffer is too small");
            uint32_t previous_frame = 0;
            for (uint32_t index = 0; index < event_count; ++index) {
                if (const auto result =
                        validate_event(*engine, events[index], target->frame_count, previous_frame);
                    result != NK_OK)
                    return result;
                previous_frame = events[index].frame_offset;
            }

            std::fill(target->samples, target->samples + sample_count, 0.0f);
            uint32_t event_index = 0;
            std::vector<ActiveParameterRamp> ramps;
            for (uint32_t frame = 0; frame < target->frame_count; ++frame) {
                expire_mono_notes(*engine, engine->offline_frame + frame);
                for (auto ramp = ramps.begin(); ramp != ramps.end();) {
                    if (const auto result = apply_parameter_ramp(*engine, *ramp, frame);
                        result != NK_OK)
                        return result;
                    if (frame >= ramp->end_frame)
                        ramp = ramps.erase(ramp);
                    else
                        ++ramp;
                }
                consume_attached_events(*engine, engine->offline_frame + frame);
                advance_transport(*engine, engine->offline_frame + frame);
                while (event_index < event_count && events[event_index].frame_offset == frame) {
                    const auto result =
                        events[event_index].kind == NK_AUDIO_DSP_EVENT_PARAMETER_RAMP
                            ? begin_parameter_ramp(*engine, events[event_index], ramps)
                            : apply_event(*engine, events[event_index]);
                    if (result != NK_OK)
                        return result;
                    ++event_index;
                }
                const auto mixed = mix_voices(*engine, engine->offline_frame + frame);
                for (uint32_t channel = 0; channel < target->channels; ++channel)
                    target->samples[static_cast<uint64_t>(frame) * target->channels + channel] =
                        target->channels == 1 || channel > 1
                            ? (mixed.left + mixed.right) * 0.5f
                            : (channel == 0 ? mixed.left : mixed.right);
            }
            engine->offline_frame += target->frame_count;
            publish_transport_status(*engine, engine->offline_frame);
            for (auto ramp = ramps.begin(); ramp != ramps.end();) {
                if (const auto result = apply_parameter_ramp(*engine, *ramp, target->frame_count);
                    result != NK_OK)
                    return result;
                ramp = ramps.erase(ramp);
            }
            while (event_index < event_count) {
                if (const auto result = apply_event(*engine, events[event_index]); result != NK_OK)
                    return result;
                ++event_index;
            }
            return NK_OK;
        });
}

} // extern "C"

extern "C" nk_result NK_CALL nk_audio_dsp_engine_render_buffer(
    nk_audio_dsp_engine engine, void *samples, uint64_t sample_bytes, uint32_t frame_count,
    uint32_t channels, const nk_audio_dsp_event *events, uint32_t event_count) {
    if (sample_bytes % sizeof(float) != 0)
        return NK_ERROR_INVALID_ARGUMENT;
    nk_audio_dsp_render_target target{};
    target.struct_size = sizeof(target);
    target.samples = static_cast<float *>(samples);
    target.sample_count = sample_bytes / sizeof(float);
    target.frame_count = frame_count;
    target.channels = channels;
    return nk_audio_dsp_engine_render(engine, &target, events, event_count);
}

#ifdef NK_AUDIO_TESTING
// Drive a retained DSP source without hardware or graph/UI-thread entry points.
// Tests use this to exercise the callback consumer concurrently with scheduling.
extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_dsp_process(nk_audio_dsp_engine handle,
                                                                   float *samples, uint32_t frames,
                                                                   uint64_t start) {
    auto engine = get_engine(handle);
    if (!engine)
        return NK_ERROR_INVALID_HANDLE;
    engine->process(samples, frames, engine->options.sample_rate, engine->options.channels, start);
    return NK_OK;
}
#endif
