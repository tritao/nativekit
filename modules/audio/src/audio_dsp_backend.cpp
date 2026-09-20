#include "audio_dsp_backend.hpp"

#include "Control/adsr.h"
#include "Filters/svf.h"
#include "Noise/whitenoise.h"
#include "Synthesis/oscillator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace nk::audio_dsp {

namespace {

constexpr double pi = 3.14159265358979323846264338327950288;

bool is_power_of_two(uint32_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

void set_waveform(daisysp::Oscillator &oscillator, nk_audio_dsp_waveform waveform) {
    switch (waveform) {
    case NK_AUDIO_DSP_WAVEFORM_SINE:
        oscillator.SetWaveform(daisysp::Oscillator::WAVE_SIN);
        break;
    case NK_AUDIO_DSP_WAVEFORM_TRIANGLE:
        oscillator.SetWaveform(daisysp::Oscillator::WAVE_TRI);
        break;
    case NK_AUDIO_DSP_WAVEFORM_SAW:
        oscillator.SetWaveform(daisysp::Oscillator::WAVE_RAMP);
        break;
    case NK_AUDIO_DSP_WAVEFORM_SQUARE:
        oscillator.SetWaveform(daisysp::Oscillator::WAVE_SQUARE);
        break;
    default:
        oscillator.SetWaveform(daisysp::Oscillator::WAVE_SIN);
        break;
    }
}

float clamp_frequency(float frequency, uint32_t sample_rate) {
    return std::clamp(frequency, 1.0f, static_cast<float>(sample_rate) * 0.45f);
}

} // namespace

std::shared_ptr<const Wavetable> Wavetable::create(const float *samples, uint32_t sample_count) {
    if (!samples || sample_count < 32 || sample_count > 4096 || !is_power_of_two(sample_count))
        return {};

    std::vector<double> cosine_coefficients(sample_count / 2 + 1, 0.0);
    std::vector<double> sine_coefficients(sample_count / 2 + 1, 0.0);
    for (uint32_t harmonic = 0; harmonic <= sample_count / 2; ++harmonic) {
        double cosine = 0.0;
        double sine = 0.0;
        for (uint32_t index = 0; index < sample_count; ++index) {
            const auto angle = 2.0 * pi * static_cast<double>(harmonic) * index / sample_count;
            cosine += static_cast<double>(samples[index]) * std::cos(angle);
            sine += static_cast<double>(samples[index]) * std::sin(angle);
        }
        const auto scale =
            harmonic == 0 || harmonic == sample_count / 2 ? 1.0 / sample_count : 2.0 / sample_count;
        cosine_coefficients[harmonic] = cosine * scale;
        sine_coefficients[harmonic] = sine * scale;
    }

    auto table = std::make_shared<Wavetable>();
    table->samples_.reserve(12);
    table->tables_.reserve(12);
    for (uint32_t maximum_harmonic = 1; maximum_harmonic <= sample_count / 2;
         maximum_harmonic *= 2) {
        table->samples_.emplace_back(sample_count);
        auto &band = table->samples_.back();
        for (uint32_t index = 0; index < sample_count; ++index) {
            const auto phase = 2.0 * pi * static_cast<double>(index) / sample_count;
            auto value = cosine_coefficients[0];
            for (uint32_t harmonic = 1; harmonic <= maximum_harmonic; ++harmonic) {
                const auto angle = phase * harmonic;
                value += cosine_coefficients[harmonic] * std::cos(angle) +
                         sine_coefficients[harmonic] * std::sin(angle);
            }
            band[index] = static_cast<float>(value);
        }
        table->tables_.push_back({band.data(), sample_count, maximum_harmonic});
        if (maximum_harmonic == sample_count / 2)
            break;
    }
    return table;
}

struct Voice::Impl {
    struct OscillatorVoice {
        daisysp::Oscillator oscillator;
        daisysp::WavetableOscillator wavetable_oscillator;
        std::shared_ptr<const Wavetable> wavetable;
        float detune_cents = 0.0f;
        float phase = 0.0f;
    };

    std::array<OscillatorVoice, NK_AUDIO_DSP_MAX_OSCILLATORS> oscillators;
    daisysp::Oscillator lfo;
    daisysp::Adsr envelope;
    daisysp::WhiteNoise noise;
    daisysp::Svf filter;
    uint32_t sample_rate = 0;
    float gain = 1.0f;
    float noise_level = 0.0f;
    nk_audio_dsp_filter_type filter_type = NK_AUDIO_DSP_FILTER_NONE;
    float filter_cutoff_hz = 0.0f;
    float filter_resonance = 0.0f;
    LfoParameters lfo_parameters;
    std::array<ModulationRoute, NK_AUDIO_DSP_MAX_MODULATION_ROUTES> routes{};
    std::array<uint32_t, NK_AUDIO_DSP_MAX_OSCILLATORS> oscillator_order{};
    uint32_t route_count = 0;
    uint32_t oscillator_count = 0;
    float base_frequency = 440.0f;
    float velocity = 0.0f;
    bool gate = false;
    bool active = false;
};

Voice::Voice() : impl_(std::make_unique<Impl>()) {}

Voice::~Voice() = default;

Voice::Voice(Voice &&) noexcept = default;

Voice &Voice::operator=(Voice &&) noexcept = default;

void Voice::init(uint32_t sample_rate) noexcept {
    impl_->sample_rate = sample_rate;
    for (auto &oscillator : impl_->oscillators) {
        oscillator.oscillator.Init(static_cast<float>(sample_rate));
        oscillator.wavetable_oscillator.Init(static_cast<float>(sample_rate));
        oscillator.wavetable.reset();
        oscillator.detune_cents = 0.0f;
        oscillator.phase = 0.0f;
    }
    impl_->lfo.Init(static_cast<float>(sample_rate));
    impl_->lfo.SetAmp(1.0f);
    impl_->envelope.Init(static_cast<float>(sample_rate));
    impl_->noise.Init();
    impl_->filter.Init(static_cast<float>(sample_rate));
    impl_->gain = 1.0f;
    impl_->noise_level = 0.0f;
    impl_->filter_type = NK_AUDIO_DSP_FILTER_NONE;
    impl_->filter_cutoff_hz = 0.0f;
    impl_->filter_resonance = 0.0f;
    impl_->lfo_parameters = {};
    impl_->lfo.SetFreq(0.0f);
    impl_->lfo.Reset(0.0f);
    impl_->routes = {};
    impl_->oscillator_order = {};
    impl_->route_count = 0;
    impl_->oscillator_count = 0;
    impl_->base_frequency = 440.0f;
    impl_->velocity = 0.0f;
    impl_->gate = false;
    impl_->active = false;
}

void Voice::set_parameters(const PatchParameters &parameters) noexcept {
    const auto was_active = impl_->active;
    impl_->gain = parameters.gain;
    impl_->oscillator_count = parameters.oscillator_count;
    for (uint32_t index = 0; index < impl_->oscillators.size(); ++index) {
        auto &voice_oscillator = impl_->oscillators[index];
        if (index >= impl_->oscillator_count) {
            voice_oscillator.wavetable.reset();
            voice_oscillator.wavetable_oscillator.SetTables(nullptr, 0);
            voice_oscillator.oscillator.SetAmp(0.0f);
            continue;
        }
        const auto &parameters_oscillator = parameters.oscillators[index];
        voice_oscillator.wavetable = parameters_oscillator.wavetable;
        voice_oscillator.detune_cents = parameters_oscillator.detune_cents;
        voice_oscillator.phase = parameters_oscillator.phase;
        voice_oscillator.wavetable_oscillator.SetTables(
            voice_oscillator.wavetable ? voice_oscillator.wavetable->tables() : nullptr,
            voice_oscillator.wavetable ? voice_oscillator.wavetable->table_count() : 0);
        voice_oscillator.wavetable_oscillator.SetAmp(parameters_oscillator.level);
        voice_oscillator.oscillator.SetAmp(parameters_oscillator.level);
        set_waveform(voice_oscillator.oscillator, parameters_oscillator.waveform);
    }
    impl_->noise_level = parameters.noise.level;
    impl_->filter_type = parameters.filter.type;
    impl_->filter_cutoff_hz = parameters.filter.cutoff_hz;
    impl_->filter_resonance = parameters.filter.resonance;
    impl_->lfo_parameters = parameters.lfo;
    impl_->routes = parameters.routes;
    impl_->route_count = parameters.route_count;
    std::array<bool, NK_AUDIO_DSP_MAX_OSCILLATORS> emitted{};
    for (uint32_t position = 0; position < impl_->oscillator_count; ++position) {
        for (uint32_t candidate = 0; candidate < impl_->oscillator_count; ++candidate) {
            if (emitted[candidate])
                continue;
            auto ready = true;
            for (uint32_t route_index = 0; route_index < impl_->route_count; ++route_index) {
                const auto &route = impl_->routes[route_index];
                if (route.source == NK_AUDIO_DSP_MODULATION_SOURCE_OSCILLATOR &&
                    route.oscillator_index == candidate + 1 &&
                    !emitted[route.source_oscillator_index - 1]) {
                    ready = false;
                    break;
                }
            }
            if (ready) {
                impl_->oscillator_order[position] = candidate;
                emitted[candidate] = true;
                break;
            }
        }
    }
    set_waveform(impl_->lfo, parameters.lfo.waveform);
    impl_->lfo.SetAmp(1.0f);
    impl_->lfo.SetFreq(parameters.lfo.rate_hz);
    if (!was_active)
        impl_->lfo.Reset(parameters.lfo.phase);
    impl_->envelope.SetAttackTime(parameters.envelope.attack_seconds);
    impl_->envelope.SetDecayTime(parameters.envelope.decay_seconds);
    impl_->envelope.SetSustainLevel(parameters.envelope.sustain_level);
    impl_->envelope.SetReleaseTime(parameters.envelope.release_seconds);
    impl_->filter.SetRes(impl_->filter_resonance);
    if (parameters.filter.type == NK_AUDIO_DSP_FILTER_SVF_LOW_PASS &&
        impl_->filter_cutoff_hz > 0.0f)
        impl_->filter.SetFreq(impl_->filter_cutoff_hz);
}

void Voice::note_on(uint32_t note, float velocity) noexcept {
    const auto semitones = static_cast<float>(note) - 69.0f;
    const auto frequency = 440.0f * std::pow(2.0f, semitones / 12.0f);
    impl_->base_frequency = frequency;
    for (uint32_t index = 0; index < impl_->oscillator_count; ++index) {
        auto &oscillator = impl_->oscillators[index];
        const auto detuned_frequency =
            frequency * std::pow(2.0f, oscillator.detune_cents / 1200.0f);
        oscillator.oscillator.SetFreq(clamp_frequency(detuned_frequency, impl_->sample_rate));
        oscillator.oscillator.Reset();
        oscillator.wavetable_oscillator.Reset();
        oscillator.wavetable_oscillator.SetFreq(
            clamp_frequency(detuned_frequency, impl_->sample_rate));
    }
    if (impl_->lfo_parameters.mode == NK_AUDIO_DSP_LFO_RETRIGGER)
        impl_->lfo.Reset(impl_->lfo_parameters.phase);
    impl_->filter.Init(static_cast<float>(impl_->sample_rate));
    impl_->filter.SetRes(impl_->filter_resonance);
    if (impl_->filter_type == NK_AUDIO_DSP_FILTER_SVF_LOW_PASS && impl_->filter_cutoff_hz > 0.0f)
        impl_->filter.SetFreq(impl_->filter_cutoff_hz);
    impl_->envelope.Retrigger(true);
    impl_->velocity = velocity;
    impl_->gate = true;
    impl_->active = true;
}

void Voice::note_off() noexcept {
    if (impl_->active)
        impl_->gate = false;
}

void Voice::reset() noexcept {
    impl_->velocity = 0.0f;
    impl_->gate = false;
    impl_->active = false;
    impl_->lfo.Reset(0.0f);
}

float Voice::process() noexcept {
    if (!impl_->active)
        return 0.0f;
    const auto envelope = impl_->envelope.Process(impl_->gate);
    if (!impl_->gate && !impl_->envelope.IsRunning()) {
        impl_->active = false;
        return 0.0f;
    }
    auto lfo_value = 0.0f;
    for (uint32_t index = 0; index < impl_->route_count; ++index) {
        if (impl_->routes[index].source == NK_AUDIO_DSP_MODULATION_SOURCE_LFO) {
            lfo_value = impl_->lfo.Process();
            break;
        }
    }

    std::array<float, NK_AUDIO_DSP_MAX_OSCILLATORS> pitch_offsets{};
    std::array<float, NK_AUDIO_DSP_MAX_OSCILLATORS> level_offsets{};
    std::array<float, NK_AUDIO_DSP_MAX_OSCILLATORS> phase_offsets{};
    std::array<float, NK_AUDIO_DSP_MAX_OSCILLATORS> frequency_offsets{};
    auto filter_offset = 0.0f;
    auto amplitude_offset = 0.0f;
    const auto add_oscillator_route = [&](std::array<float, NK_AUDIO_DSP_MAX_OSCILLATORS> &values,
                                          const ModulationRoute &route, float value) {
        if (route.oscillator_index == NK_AUDIO_DSP_MODULATION_TARGET_ALL) {
            for (uint32_t oscillator = 0; oscillator < impl_->oscillator_count; ++oscillator)
                values[oscillator] += value;
        } else if (route.oscillator_index <= impl_->oscillator_count) {
            values[route.oscillator_index - 1] += value;
        }
    };
    for (uint32_t index = 0; index < impl_->route_count; ++index) {
        const auto &route = impl_->routes[index];
        if (route.source == NK_AUDIO_DSP_MODULATION_SOURCE_OSCILLATOR)
            continue;
        auto source_value =
            route.source == NK_AUDIO_DSP_MODULATION_SOURCE_LFO ? lfo_value : envelope;
        if (route.polarity == NK_AUDIO_DSP_MODULATION_UNIPOLAR &&
            route.source == NK_AUDIO_DSP_MODULATION_SOURCE_LFO)
            source_value = 0.5f * (source_value + 1.0f);
        else if (route.polarity == NK_AUDIO_DSP_MODULATION_BIPOLAR &&
                 route.source == NK_AUDIO_DSP_MODULATION_SOURCE_ENVELOPE)
            source_value = 2.0f * source_value - 1.0f;
        switch (route.destination) {
        case NK_AUDIO_DSP_MODULATION_DESTINATION_PITCH_SEMITONES:
            add_oscillator_route(pitch_offsets, route, route.amount * source_value);
            break;
        case NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_HZ:
            filter_offset += route.amount * source_value;
            break;
        case NK_AUDIO_DSP_MODULATION_DESTINATION_AMPLITUDE:
            amplitude_offset += route.amount * source_value;
            break;
        case NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_LEVEL:
            add_oscillator_route(level_offsets, route, route.amount * source_value);
            break;
        case NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_PHASE:
            add_oscillator_route(phase_offsets, route, route.amount * source_value);
            break;
        case NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_FREQUENCY_HZ:
            add_oscillator_route(frequency_offsets, route, route.amount * source_value);
            break;
        default:
            break;
        }
    }

    auto frequency = impl_->base_frequency;
    float sample = 0.0f;
    std::array<float, NK_AUDIO_DSP_MAX_OSCILLATORS> oscillator_outputs{};
    for (uint32_t position = 0; position < impl_->oscillator_count; ++position) {
        const auto index = impl_->oscillator_order[position];
        auto &oscillator = impl_->oscillators[index];
        for (uint32_t route_index = 0; route_index < impl_->route_count; ++route_index) {
            const auto &route = impl_->routes[route_index];
            if (route.source != NK_AUDIO_DSP_MODULATION_SOURCE_OSCILLATOR ||
                route.oscillator_index != index + 1)
                continue;
            auto source_value = oscillator_outputs[route.source_oscillator_index - 1];
            if (route.polarity == NK_AUDIO_DSP_MODULATION_UNIPOLAR)
                source_value = 0.5f * (source_value + 1.0f);
            if (route.destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_PHASE)
                phase_offsets[index] += route.amount * source_value;
            else if (route.destination ==
                     NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_FREQUENCY_HZ)
                frequency_offsets[index] += route.amount * source_value;
        }
        auto oscillator_frequency = frequency;
        if (pitch_offsets[index] != 0.0f)
            oscillator_frequency *= std::pow(2.0f, pitch_offsets[index] / 12.0f);
        oscillator_frequency *= std::pow(2.0f, oscillator.detune_cents / 1200.0f);
        const auto detuned_frequency =
            clamp_frequency(oscillator_frequency + frequency_offsets[index], impl_->sample_rate);
        oscillator.wavetable_oscillator.SetFreq(detuned_frequency);
        oscillator.oscillator.SetFreq(detuned_frequency);
        const auto phase_offset = oscillator.phase + phase_offsets[index];
        const auto level = std::max(0.0f, 1.0f + level_offsets[index]);
        auto oscillator_output = 0.0f;
        if (oscillator.wavetable) {
            oscillator_output = oscillator.wavetable_oscillator.Process(phase_offset) * level;
        } else {
            oscillator_output = oscillator.oscillator.Process(phase_offset) * level;
        }
        oscillator_outputs[index] = oscillator_output;
        sample += oscillator_output;
    }
    sample += impl_->noise.Process() * impl_->noise_level;
    if (impl_->filter_type == NK_AUDIO_DSP_FILTER_SVF_LOW_PASS && impl_->filter_cutoff_hz > 0.0f) {
        const auto cutoff = std::clamp(impl_->filter_cutoff_hz + filter_offset, 1.0f,
                                       static_cast<float>(impl_->sample_rate) / 3.0f);
        impl_->filter.SetFreq(cutoff);
        impl_->filter.Process(sample);
        sample = impl_->filter.Low();
    }
    const auto amplitude = impl_->gain * std::max(0.0f, 1.0f + amplitude_offset);
    return sample * amplitude * impl_->velocity * envelope;
}

bool Voice::active() const noexcept {
    return impl_->active;
}

} // namespace nk::audio_dsp
