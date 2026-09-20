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
constexpr nk_audio_dsp_capabilities builtin_capabilities =
    NK_AUDIO_DSP_CAPABILITY_OSCILLATOR | NK_AUDIO_DSP_CAPABILITY_ENVELOPE;

using DspParameters = nk::audio_dsp::VoiceParameters;

struct DspEngineResource;

struct DspInstrumentResource final : nk::core::Resource {
    std::weak_ptr<DspEngineResource> engine;
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
    auto resource =
        nk::core::handles().get(handle, nk::core::ResourceType::audio_dsp_instrument);
    if (!resource) {
        nk::core::set_error("invalid audio DSP instrument handle");
        return {};
    }
    return std::dynamic_pointer_cast<DspInstrumentResource>(std::move(resource));
}

bool valid_waveform(nk_audio_dsp_waveform waveform) {
    return waveform <= NK_AUDIO_DSP_WAVEFORM_SQUARE;
}

bool valid_nonnegative_finite(float value) {
    return std::isfinite(value) && value >= 0.0f;
}

bool valid_instrument_parameters(const DspParameters &parameters) {
    return valid_waveform(parameters.waveform) && valid_nonnegative_finite(parameters.gain) &&
           valid_nonnegative_finite(parameters.attack_seconds) &&
           valid_nonnegative_finite(parameters.decay_seconds) &&
           std::isfinite(parameters.sustain_level) && parameters.sustain_level >= 0.0f &&
           parameters.sustain_level <= 1.0f &&
           valid_nonnegative_finite(parameters.release_seconds);
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

nk_result normalize_instrument_options(const nk_audio_dsp_instrument_options *input,
                                        DspParameters &output) {
    output = {};
    if (!input)
        return NK_OK;
    if (input->struct_size < sizeof(nk_audio_dsp_instrument_options))
        return invalid_argument("audio DSP instrument options are missing or too small");
    output.waveform = input->waveform;
    output.gain = input->gain;
    output.attack_seconds = input->attack_seconds;
    output.decay_seconds = input->decay_seconds;
    output.sustain_level = input->sustain_level;
    output.release_seconds = input->release_seconds;
    if (!valid_instrument_parameters(output))
        return invalid_argument("audio DSP instrument parameters are invalid");
    return NK_OK;
}

bool valid_parameter(nk_audio_dsp_parameter parameter) {
    return parameter <= NK_AUDIO_DSP_PARAMETER_RELEASE_SECONDS;
}

nk_result set_parameter(DspParameters &parameters, nk_audio_dsp_parameter parameter, float value) {
    if (!valid_parameter(parameter) || !std::isfinite(value))
        return invalid_argument("audio DSP parameter is invalid");
    switch (parameter) {
    case NK_AUDIO_DSP_PARAMETER_WAVEFORM:
        if (value < 0.0f || value > static_cast<float>(NK_AUDIO_DSP_WAVEFORM_SQUARE) ||
            std::floor(value) != value)
            return invalid_argument("audio DSP waveform parameter is invalid");
        parameters.waveform = static_cast<nk_audio_dsp_waveform>(value);
        break;
    case NK_AUDIO_DSP_PARAMETER_GAIN:
        if (value < 0.0f)
            return invalid_argument("audio DSP gain parameter is invalid");
        parameters.gain = value;
        break;
    case NK_AUDIO_DSP_PARAMETER_ATTACK_SECONDS:
        if (value < 0.0f)
            return invalid_argument("audio DSP attack parameter is invalid");
        parameters.attack_seconds = value;
        break;
    case NK_AUDIO_DSP_PARAMETER_DECAY_SECONDS:
        if (value < 0.0f)
            return invalid_argument("audio DSP decay parameter is invalid");
        parameters.decay_seconds = value;
        break;
    case NK_AUDIO_DSP_PARAMETER_SUSTAIN_LEVEL:
        if (value < 0.0f || value > 1.0f)
            return invalid_argument("audio DSP sustain parameter is invalid");
        parameters.sustain_level = value;
        break;
    case NK_AUDIO_DSP_PARAMETER_RELEASE_SECONDS:
        if (value < 0.0f)
            return invalid_argument("audio DSP release parameter is invalid");
        parameters.release_seconds = value;
        break;
    default:
        return invalid_argument("audio DSP parameter is invalid");
    }
    return NK_OK;
}

float get_parameter(const DspParameters &parameters, nk_audio_dsp_parameter parameter) {
    switch (parameter) {
    case NK_AUDIO_DSP_PARAMETER_WAVEFORM:
        return static_cast<float>(parameters.waveform);
    case NK_AUDIO_DSP_PARAMETER_GAIN:
        return parameters.gain;
    case NK_AUDIO_DSP_PARAMETER_ATTACK_SECONDS:
        return parameters.attack_seconds;
    case NK_AUDIO_DSP_PARAMETER_DECAY_SECONDS:
        return parameters.decay_seconds;
    case NK_AUDIO_DSP_PARAMETER_SUSTAIN_LEVEL:
        return parameters.sustain_level;
    case NK_AUDIO_DSP_PARAMETER_RELEASE_SECONDS:
        return parameters.release_seconds;
    default:
        return 0.0f;
    }
}

bool instrument_belongs_to(const std::shared_ptr<DspInstrumentResource> &instrument,
                           const DspEngineResource &engine) {
    return instrument && instrument->engine.lock().get() == &engine;
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
        voice->clear();
        voice->instrument = std::move(instrument);
        voice->voice_id = event.voice_id;
        voice->note = event.note;
        voice->velocity = event.velocity;
        voice->active = true;
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
        const auto result = set_parameter(instrument->current, event.parameter, event.value);
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
            if (const auto result = set_parameter(parameters, event.parameter, event.value);
                result != NK_OK)
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

nk_result NK_CALL nk_audio_dsp_instrument_create(
    nk_audio_dsp_engine engine_handle, const nk_audio_dsp_instrument_options *options,
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
            if (const auto result = normalize_instrument_options(options, parameters);
                result != NK_OK)
                return result;
            auto instrument = std::make_shared<DspInstrumentResource>();
            instrument->engine = engine;
            instrument->defaults = parameters;
            instrument->current = parameters;
            std::lock_guard lock(engine->mutex);
            if (!engine->alive)
                return invalid_request("audio DSP engine is no longer alive");
            const auto handle = nk::core::handles().insert(
                nk::core::ResourceType::audio_dsp_instrument, instrument);
            if (handle == NK_INVALID_HANDLE) {
                nk::core::set_error("could not allocate an audio DSP instrument handle");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            engine->instruments.emplace_back(instrument);
            *out_instrument = handle;
            return NK_OK;
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
            const auto result = set_parameter(instrument->current, parameter, value);
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
                if (const auto result = validate_event(*engine, events[index], target->frame_count,
                                                       previous_frame);
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
