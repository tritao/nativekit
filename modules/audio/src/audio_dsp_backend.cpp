#include "audio_dsp_backend.hpp"

#include "Control/adsr.h"
#include "Filters/svf.h"
#include "Noise/whitenoise.h"
#include "Synthesis/oscillator.h"

#include <cmath>
#include <cstdint>
#include <utility>

namespace nk::audio_dsp {

struct Voice::Impl {
    daisysp::Oscillator oscillator;
    daisysp::Adsr envelope;
    daisysp::WhiteNoise noise;
    daisysp::Svf filter;
    uint32_t sample_rate = 0;
    float gain = 1.0f;
    float noise_level = 0.0f;
    nk_audio_dsp_filter_type filter_type = NK_AUDIO_DSP_FILTER_NONE;
    float filter_cutoff_hz = 0.0f;
    float filter_resonance = 0.0f;
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
    impl_->envelope.Init(static_cast<float>(sample_rate));
    impl_->noise.Init();
    impl_->filter.Init(static_cast<float>(sample_rate));
    impl_->gain = 1.0f;
    impl_->noise_level = 0.0f;
    impl_->filter_type = NK_AUDIO_DSP_FILTER_NONE;
    impl_->filter_cutoff_hz = 0.0f;
    impl_->filter_resonance = 0.0f;
    impl_->velocity = 0.0f;
    impl_->gate = false;
    impl_->active = false;
}

void Voice::set_parameters(const PatchParameters &parameters) noexcept {
    impl_->gain = parameters.gain;
    impl_->noise_level = parameters.noise.level;
    impl_->filter_type = parameters.filter.type;
    impl_->filter_cutoff_hz = parameters.filter.cutoff_hz;
    impl_->filter_resonance = parameters.filter.resonance;
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
    impl_->oscillator.SetFreq(frequency);
    impl_->oscillator.Reset();
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
}

float Voice::process() noexcept {
    if (!impl_->active)
        return 0.0f;
    const auto envelope = impl_->envelope.Process(impl_->gate);
    if (!impl_->gate && !impl_->envelope.IsRunning()) {
        impl_->active = false;
        return 0.0f;
    }
    auto sample = impl_->oscillator.Process();
    sample += impl_->noise.Process() * impl_->noise_level;
    if (impl_->filter_type == NK_AUDIO_DSP_FILTER_SVF_LOW_PASS && impl_->filter_cutoff_hz > 0.0f) {
        impl_->filter.Process(sample);
        sample = impl_->filter.Low();
    }
    return sample * impl_->gain * impl_->velocity * envelope;
}

bool Voice::active() const noexcept {
    return impl_->active;
}

} // namespace nk::audio_dsp
