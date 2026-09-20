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

namespace nk::audio_dsp {

struct Voice::Impl {
    daisysp::Oscillator oscillator;
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
    uint32_t route_count = 0;
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
    impl_->oscillator.Init(static_cast<float>(sample_rate));
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
    impl_->route_count = 0;
    impl_->base_frequency = 440.0f;
    impl_->velocity = 0.0f;
    impl_->gate = false;
    impl_->active = false;
}

void Voice::set_parameters(const PatchParameters &parameters) noexcept {
    const auto was_active = impl_->active;
    impl_->gain = parameters.gain;
    impl_->noise_level = parameters.noise.level;
    impl_->filter_type = parameters.filter.type;
    impl_->filter_cutoff_hz = parameters.filter.cutoff_hz;
    impl_->filter_resonance = parameters.filter.resonance;
    impl_->lfo_parameters = parameters.lfo;
    impl_->routes = parameters.routes;
    impl_->route_count = parameters.route_count;
    switch (parameters.oscillator.waveform) {
    case NK_AUDIO_DSP_WAVEFORM_SINE:
        impl_->oscillator.SetWaveform(daisysp::Oscillator::WAVE_SIN);
        break;
    case NK_AUDIO_DSP_WAVEFORM_TRIANGLE:
        impl_->oscillator.SetWaveform(daisysp::Oscillator::WAVE_TRI);
        break;
    case NK_AUDIO_DSP_WAVEFORM_SAW:
        impl_->oscillator.SetWaveform(daisysp::Oscillator::WAVE_RAMP);
        break;
    case NK_AUDIO_DSP_WAVEFORM_SQUARE:
        impl_->oscillator.SetWaveform(daisysp::Oscillator::WAVE_SQUARE);
        break;
    default:
        impl_->oscillator.SetWaveform(daisysp::Oscillator::WAVE_SIN);
        break;
    }
    switch (parameters.lfo.waveform) {
    case NK_AUDIO_DSP_WAVEFORM_SINE:
        impl_->lfo.SetWaveform(daisysp::Oscillator::WAVE_SIN);
        break;
    case NK_AUDIO_DSP_WAVEFORM_TRIANGLE:
        impl_->lfo.SetWaveform(daisysp::Oscillator::WAVE_TRI);
        break;
    case NK_AUDIO_DSP_WAVEFORM_SAW:
        impl_->lfo.SetWaveform(daisysp::Oscillator::WAVE_RAMP);
        break;
    case NK_AUDIO_DSP_WAVEFORM_SQUARE:
        impl_->lfo.SetWaveform(daisysp::Oscillator::WAVE_SQUARE);
        break;
    default:
        impl_->lfo.SetWaveform(daisysp::Oscillator::WAVE_SIN);
        break;
    }
    impl_->lfo.SetAmp(1.0f);
    impl_->lfo.SetFreq(parameters.lfo.rate_hz);
    if (!was_active)
        impl_->lfo.Reset(parameters.lfo.phase);
    impl_->oscillator.SetAmp(parameters.oscillator.level);
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
    impl_->oscillator.SetFreq(frequency);
    impl_->oscillator.Reset();
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

    auto pitch_offset = 0.0f;
    auto filter_offset = 0.0f;
    auto amplitude_offset = 0.0f;
    for (uint32_t index = 0; index < impl_->route_count; ++index) {
        const auto &route = impl_->routes[index];
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
            pitch_offset += route.amount * source_value;
            break;
        case NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_HZ:
            filter_offset += route.amount * source_value;
            break;
        case NK_AUDIO_DSP_MODULATION_DESTINATION_AMPLITUDE:
            amplitude_offset += route.amount * source_value;
            break;
        default:
            break;
        }
    }

    if (pitch_offset != 0.0f) {
        const auto frequency = impl_->base_frequency * std::pow(2.0f, pitch_offset / 12.0f);
        impl_->oscillator.SetFreq(
            std::clamp(frequency, 1.0f, static_cast<float>(impl_->sample_rate) * 0.45f));
    }
    auto sample = impl_->oscillator.Process();
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
