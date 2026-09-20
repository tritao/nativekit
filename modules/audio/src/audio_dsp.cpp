#include "nativekit_audio_dsp.h"

#include "audio_dsp_backend.hpp"
#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
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
    NK_AUDIO_DSP_CAPABILITY_LFO | NK_AUDIO_DSP_CAPABILITY_FILTER |
    NK_AUDIO_DSP_CAPABILITY_MODULATION;

using DspParameters = nk::audio_dsp::PatchParameters;

struct DspEngineResource;
struct DspPatchResource;
struct DspWavetableResource;

struct DspPatchResource final : nk::core::Resource {
    DspParameters parameters;
};

struct DspWavetableResource final : nk::core::Resource {
    std::shared_ptr<const nk::audio_dsp::Wavetable> table;
};

struct DspInstrumentResource final : nk::core::Resource {
    std::weak_ptr<DspEngineResource> engine;
    std::shared_ptr<DspPatchResource> patch;
    DspParameters defaults;
    DspParameters current;
    uint64_t parameters_version = 1;
};

struct DspVoice {
    nk::audio_dsp::Voice backend;
    std::shared_ptr<DspInstrumentResource> instrument;
    uint32_t voice_id = 0;
    uint32_t note = 0;
    float velocity = 0.0f;
    uint64_t parameters_version = 0;
    bool active = false;

    void clear() noexcept {
        backend.reset();
        instrument.reset();
        voice_id = 0;
        note = 0;
        velocity = 0.0f;
        parameters_version = 0;
        active = false;
    }
};

struct DspEngineResource final : nk::core::Resource {
    nk_audio_dsp_engine_options options{};
    std::mutex mutex;
    std::vector<DspVoice> voices;
    std::vector<std::weak_ptr<DspInstrumentResource>> instruments;
    bool alive = true;
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
           source == NK_AUDIO_DSP_MODULATION_SOURCE_ENVELOPE;
}

bool valid_modulation_destination(nk_audio_dsp_modulation_destination destination) {
    return destination == NK_AUDIO_DSP_MODULATION_DESTINATION_PITCH_SEMITONES ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_HZ ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_AMPLITUDE ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_LEVEL ||
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_PHASE;
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
           destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_PHASE;
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
             route.oscillator_index != NK_AUDIO_DSP_MODULATION_TARGET_ALL))
            return false;
    }
    return true;
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

nk_result set_oscillator_parameter(DspParameters &parameters, uint32_t oscillator_index,
                                   nk_audio_dsp_parameter parameter, float value) {
    if (!valid_oscillator_parameter(parameter) || oscillator_index >= parameters.oscillator_count ||
        !std::isfinite(value))
        return invalid_argument("audio DSP oscillator parameter target is invalid");
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
    voice.backend.set_parameters(voice.instrument->current);
    voice.parameters_version = voice.instrument->parameters_version;
}

float voice_sample(DspVoice &voice) noexcept {
    if (!voice.active || !voice.instrument)
        return 0.0f;
    sync_voice_parameters(voice);
    const auto result = voice.backend.process();
    voice.active = voice.backend.active();
    return result;
}

DspVoice *find_voice(DspEngineResource &engine, uint32_t voice_id) {
    for (auto &voice : engine.voices) {
        if (voice.active && voice.voice_id == voice_id)
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

nk_result apply_event(DspEngineResource &engine, const nk_audio_dsp_event &event) {
    switch (event.kind) {
    case NK_AUDIO_DSP_EVENT_NOTE_ON: {
        auto instrument = get_instrument(event.instrument);
        if (!instrument)
            return NK_ERROR_INVALID_HANDLE;
        if (!instrument_belongs_to(instrument, engine))
            return invalid_request("audio DSP instrument belongs to another engine");
        auto *voice = find_voice(engine, event.voice_id);
        if (!voice)
            voice = find_free_voice(engine);
        if (!voice) {
            nk::core::set_error("audio DSP voice limit reached");
            return NK_ERROR_QUEUE_FULL;
        }
        const auto instrument_changed = voice->instrument.get() != instrument.get();
        if (!voice->active)
            voice->clear();
        voice->instrument = std::move(instrument);
        voice->voice_id = event.voice_id;
        voice->note = event.note;
        voice->velocity = event.velocity;
        voice->active = true;
        if (instrument_changed)
            voice->parameters_version = 0;
        sync_voice_parameters(*voice);
        voice->backend.note_on(event.note, event.velocity);
        return NK_OK;
    }
    case NK_AUDIO_DSP_EVENT_NOTE_OFF: {
        auto *voice = find_voice(engine, event.voice_id);
        if (!voice)
            return NK_OK;
        voice->backend.note_off();
        return NK_OK;
    }
    case NK_AUDIO_DSP_EVENT_PARAMETER: {
        auto instrument = get_instrument(event.instrument);
        if (!instrument)
            return NK_ERROR_INVALID_HANDLE;
        if (!instrument_belongs_to(instrument, engine))
            return invalid_request("audio DSP instrument belongs to another engine");
        auto parameters = instrument->current;
        auto result = valid_oscillator_parameter(event.parameter)
                          ? set_oscillator_parameter(parameters, event.oscillator_index,
                                                     event.parameter, event.value)
                          : set_parameter(parameters, event.parameter, event.value);
        if (result == NK_OK)
            result = validate_engine_parameters(parameters, engine);
        if (result == NK_OK)
            instrument->current = parameters;
        if (result == NK_OK)
            ++instrument->parameters_version;
        return result;
    }
    default:
        return invalid_argument("audio DSP event kind is invalid");
    }
}

nk_result validate_event(const DspEngineResource &engine, const nk_audio_dsp_event &event,
                         uint32_t frame_count, uint32_t previous_frame) {
    if (event.struct_size < sizeof(nk_audio_dsp_event))
        return invalid_argument("audio DSP event is missing or too small");
    if (event.frame_offset > frame_count || event.frame_offset < previous_frame)
        return invalid_argument("audio DSP events must be frame sorted within the block");
    if (event.kind != NK_AUDIO_DSP_EVENT_PARAMETER && event.voice_id == 0)
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
    default:
        return invalid_argument("audio DSP event kind is invalid");
    }
    if (event.kind != NK_AUDIO_DSP_EVENT_NOTE_OFF) {
        auto instrument = get_instrument(event.instrument);
        if (!instrument)
            return NK_ERROR_INVALID_HANDLE;
        if (!instrument_belongs_to(instrument, engine))
            return invalid_request("audio DSP instrument belongs to another engine");
        if (event.kind == NK_AUDIO_DSP_EVENT_PARAMETER) {
            auto parameters = instrument->current;
            const auto result = valid_oscillator_parameter(event.parameter)
                                    ? set_oscillator_parameter(parameters, event.oscillator_index,
                                                               event.parameter, event.value)
                                    : set_parameter(parameters, event.parameter, event.value);
            if (result != NK_OK)
                return result;
            if (const auto result = validate_engine_parameters(parameters, engine); result != NK_OK)
                return result;
        }
    }
    return NK_OK;
}

void reset_voices(DspEngineResource &engine) {
    for (auto &voice : engine.voices)
        voice.clear();
}

} // namespace

extern "C" {

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

nk_result NK_CALL nk_audio_dsp_engine_destroy(nk_audio_dsp_engine engine_handle) {
    return nk::core::result_boundary(
        "unexpected error while destroying an audio DSP engine", [&]() -> nk_result {
            if (const auto result = enter_dsp(); result != NK_OK)
                return result;
            auto engine = get_engine(engine_handle);
            if (!engine)
                return NK_ERROR_INVALID_HANDLE;
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
            reset_voices(*engine);
            for (auto iterator = engine->instruments.begin();
                 iterator != engine->instruments.end();) {
                if (auto instrument = iterator->lock()) {
                    instrument->current = instrument->defaults;
                    ++instrument->parameters_version;
                    ++iterator;
                } else {
                    iterator = engine->instruments.erase(iterator);
                }
            }
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_dsp_patch_create(const nk_audio_dsp_patch_options *options,
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
                for (auto &voice : engine->voices) {
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
            auto parameters = instrument->current;
            auto result = set_parameter(parameters, parameter, value);
            if (result == NK_OK)
                result = validate_engine_parameters(parameters, *engine);
            if (result == NK_OK)
                instrument->current = parameters;
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
            auto parameters = instrument->current;
            auto result = set_oscillator_parameter(parameters, oscillator_index, parameter, value);
            if (result == NK_OK)
                result = validate_engine_parameters(parameters, *engine);
            if (result == NK_OK)
                instrument->current = parameters;
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
            *out_value = get_parameter(instrument->current, parameter);
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
            if (oscillator_index >= instrument->current.oscillator_count)
                return invalid_argument("audio DSP oscillator parameter target is invalid");
            *out_value = get_oscillator_parameter(instrument->current, oscillator_index, parameter);
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
            for (uint32_t frame = 0; frame < target->frame_count; ++frame) {
                while (event_index < event_count && events[event_index].frame_offset == frame) {
                    if (const auto result = apply_event(*engine, events[event_index]);
                        result != NK_OK)
                        return result;
                    ++event_index;
                }
                float mixed = 0.0f;
                for (auto &voice : engine->voices)
                    mixed += voice_sample(voice);
                for (uint32_t channel = 0; channel < target->channels; ++channel)
                    target->samples[static_cast<uint64_t>(frame) * target->channels + channel] =
                        mixed;
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
