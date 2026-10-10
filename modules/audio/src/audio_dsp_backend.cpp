#include "audio_dsp_backend.hpp"
#include "audio_daisy_source.hpp"

#include "Control/adsr.h"
#include "Filters/svf.h"
#include "Synthesis/oscillator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace nk::audio_dsp {

namespace {

// Adapted from DaisySP WhiteNoise (MIT, Copyright 2020 Electrosmith, Corp).
// Preserve its two's-complement sequence without signed multiplication overflow
// or implementation-defined unsigned-to-signed conversion.
class WrappingWhiteNoise {
  public:
    void Init() noexcept { state_ = 1; }
    float Process() noexcept {
        state_ *= uint32_t{16807};
        const int64_t signed_value = state_ < uint32_t{0x80000000}
            ? static_cast<int64_t>(state_)
            : static_cast<int64_t>(state_) - int64_t{0x100000000};
        return static_cast<float>(signed_value) * 4.6566129e-010f;
    }
  private:
    uint32_t state_ = 1;
};

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
        float level = 1.0f;
        float detune_cents = 0.0f;
        float phase = 0.0f;
        float tuning = 1.0f;
    };

    std::array<std::array<OscillatorVoice, NK_AUDIO_DSP_MAX_OSCILLATORS>, max_unison_copies>
        oscillators;
    UnisonParameters unison;
    std::array<float, max_unison_copies> left_weights{};
    std::array<float, max_unison_copies> right_weights{};
    daisysp::Adsr filter_envelope;
    float filter_envelope_amount = 0.0f;
    daisysp::Svf right_filter;
    DaisySource source;
    bool specialized_source = false;
    bool source_replaces_oscillators = false;
    std::array<daisysp::Oscillator, NK_AUDIO_DSP_MAX_LFOS> lfos;
    std::array<LfoParameters, NK_AUDIO_DSP_MAX_LFOS> lfo_parameters;
    std::array<bool, NK_AUDIO_DSP_MAX_LFOS> lfo_used{};
    bool filter_envelope_used = false;
    daisysp::Adsr envelope;
    WrappingWhiteNoise noise;
    daisysp::Svf filter;
    uint32_t sample_rate = 0;
    float gain = 1.0f;
    float noise_level = 0.0f;
    nk_audio_dsp_filter_type filter_type = NK_AUDIO_DSP_FILTER_NONE;
    float filter_cutoff_hz = 0.0f;
    float filter_resonance = 0.0f;
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
    for (auto &copy : impl_->oscillators) {
        for (auto &oscillator : copy) {
            oscillator.oscillator.Init(static_cast<float>(sample_rate));
            oscillator.wavetable_oscillator.Init(static_cast<float>(sample_rate));
            oscillator.wavetable.reset();
            oscillator.detune_cents = 0.0f;
            oscillator.phase = 0.0f;
        }
    }
    impl_->unison = {};
    impl_->left_weights = {};
    impl_->right_weights = {};
    impl_->left_weights[0] = impl_->right_weights[0] = 1.0f;
    impl_->filter_envelope_amount = 0.0f;
    for (auto &lfo : impl_->lfos) {
        lfo.Init(static_cast<float>(sample_rate));
        lfo.SetAmp(1.0f);
        lfo.SetFreq(0.0f);
        lfo.Reset(0.0f);
    }
    impl_->lfo_used = {};
    impl_->filter_envelope_used = false;
    impl_->envelope.Init(static_cast<float>(sample_rate));
    impl_->filter_envelope.Init(static_cast<float>(sample_rate));
    impl_->right_filter.Init(static_cast<float>(sample_rate));
    impl_->noise.Init();
    impl_->filter.Init(static_cast<float>(sample_rate));
    impl_->gain = 1.0f;
    impl_->noise_level = 0.0f;
    impl_->filter_type = NK_AUDIO_DSP_FILTER_NONE;
    impl_->filter_cutoff_hz = 0.0f;
    impl_->filter_resonance = 0.0f;
    impl_->lfo_parameters = {};
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
    impl_->source.configure(parameters.source, impl_->sample_rate);
    impl_->specialized_source = parameters.source.options.kind != 0;
    impl_->source_replaces_oscillators =
        impl_->specialized_source &&
        parameters.source.options.kind != NK_AUDIO_DSP_SOURCE_KARPLUS_STRING &&
        parameters.source.options.kind != NK_AUDIO_DSP_SOURCE_RESONATOR;
    impl_->gain = parameters.gain;
    impl_->oscillator_count = parameters.oscillator_count;
    impl_->unison = parameters.unison;
    for (uint32_t copy = 0; copy < max_unison_copies; ++copy) {
        const auto position = impl_->unison.count == 1
                                  ? 0.0f
                                  : 2.0f * static_cast<float>(copy) / (impl_->unison.count - 1) - 1.0f;
        impl_->left_weights[copy] = (1.0f - position * impl_->unison.stereo_spread) / impl_->unison.count;
        impl_->right_weights[copy] = (1.0f + position * impl_->unison.stereo_spread) / impl_->unison.count;
        for (uint32_t index = 0; index < NK_AUDIO_DSP_MAX_OSCILLATORS; ++index) {
            auto &voice_oscillator = impl_->oscillators[copy][index];
            if (index >= impl_->oscillator_count || copy >= impl_->unison.count) {
                voice_oscillator.wavetable.reset();
                voice_oscillator.wavetable_oscillator.SetTables(nullptr, 0);
                voice_oscillator.oscillator.SetAmp(0.0f);
                continue;
            }
            const auto &parameters_oscillator = parameters.oscillators[index];
            voice_oscillator.wavetable = parameters_oscillator.wavetable;
            voice_oscillator.level = parameters_oscillator.level;
            voice_oscillator.detune_cents = parameters_oscillator.detune_cents;
            voice_oscillator.phase = parameters_oscillator.phase;
            voice_oscillator.tuning = std::pow(2.0f,
                (parameters_oscillator.detune_cents + position * impl_->unison.detune_cents) / 1200.0f);
            voice_oscillator.wavetable_oscillator.SetTables(
                voice_oscillator.wavetable ? voice_oscillator.wavetable->tables() : nullptr,
                voice_oscillator.wavetable ? voice_oscillator.wavetable->table_count() : 0);
            voice_oscillator.wavetable_oscillator.SetAmp(parameters_oscillator.level);
            voice_oscillator.oscillator.SetAmp(parameters_oscillator.level);
            set_waveform(voice_oscillator.oscillator, parameters_oscillator.waveform);
        }
    }
    impl_->noise_level = parameters.noise.level;
    impl_->filter_type = parameters.filter.type;
    impl_->filter_cutoff_hz = parameters.filter.cutoff_hz;
    impl_->filter_resonance = parameters.filter.resonance;
    impl_->lfo_parameters[0] = parameters.lfo;
    for (uint32_t i = 1; i < NK_AUDIO_DSP_MAX_LFOS; ++i)
        impl_->lfo_parameters[i] = parameters.additional_lfos[i - 1];
    impl_->lfo_used = {};
    impl_->filter_envelope_used = parameters.filter_envelope_amount_semitones != 0;
    impl_->routes = parameters.routes;
    impl_->route_count = parameters.route_count;
    for (uint32_t i = 0; i < impl_->route_count; ++i) {
        switch (impl_->routes[i].source) {
        case NK_AUDIO_DSP_MODULATION_SOURCE_LFO: impl_->lfo_used[0] = true; break;
        case NK_AUDIO_DSP_MODULATION_SOURCE_LFO2: impl_->lfo_used[1] = true; break;
        case NK_AUDIO_DSP_MODULATION_SOURCE_LFO3: impl_->lfo_used[2] = true; break;
        case NK_AUDIO_DSP_MODULATION_SOURCE_FILTER_ENVELOPE:
            impl_->filter_envelope_used = true; break;
        default: break;
        }
    }
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
    for (uint32_t i = 0; i < NK_AUDIO_DSP_MAX_LFOS; ++i) {
        const auto &options = impl_->lfo_parameters[i];
        auto &lfo = impl_->lfos[i];
        set_waveform(lfo, options.waveform);
        lfo.SetAmp(1.0f);
        lfo.SetFreq(options.rate_hz);
        if (!was_active) lfo.Reset(options.phase);
    }
    impl_->envelope.SetAttackTime(parameters.envelope.attack_seconds);
    impl_->envelope.SetDecayTime(parameters.envelope.decay_seconds);
    impl_->envelope.SetSustainLevel(parameters.envelope.sustain_level);
    impl_->envelope.SetReleaseTime(parameters.envelope.release_seconds);
    impl_->filter_envelope.SetAttackTime(parameters.filter_envelope.attack_seconds);
    impl_->filter_envelope.SetDecayTime(parameters.filter_envelope.decay_seconds);
    impl_->filter_envelope.SetSustainLevel(parameters.filter_envelope.sustain_level);
    impl_->filter_envelope.SetReleaseTime(parameters.filter_envelope.release_seconds);
    impl_->filter_envelope_amount = parameters.filter_envelope_amount_semitones;
    impl_->right_filter.SetRes(impl_->filter_resonance);
    impl_->filter.SetRes(impl_->filter_resonance);
    if (parameters.filter.type == NK_AUDIO_DSP_FILTER_SVF_LOW_PASS &&
        impl_->filter_cutoff_hz > 0.0f)
        impl_->filter.SetFreq(impl_->filter_cutoff_hz);
}

void Voice::note_on(uint32_t note, float velocity) noexcept {
    const auto semitones = static_cast<float>(note) - 69.0f;
    const auto frequency = 440.0f * std::pow(2.0f, semitones / 12.0f);
    impl_->base_frequency = frequency;
    impl_->source.trigger(clamp_frequency(frequency, impl_->sample_rate), note);
    for (uint32_t copy = 0; copy < impl_->unison.count; ++copy) {
        for (uint32_t index = 0; index < impl_->oscillator_count; ++index) {
            auto &oscillator = impl_->oscillators[copy][index];
            const auto detuned_frequency = frequency * oscillator.tuning;
            oscillator.oscillator.SetFreq(clamp_frequency(detuned_frequency, impl_->sample_rate));
            oscillator.oscillator.Reset();
            oscillator.wavetable_oscillator.Reset();
            oscillator.wavetable_oscillator.SetFreq(
                clamp_frequency(detuned_frequency, impl_->sample_rate));
        }
    }
    for (uint32_t i = 0; i < NK_AUDIO_DSP_MAX_LFOS; ++i)
        if (impl_->lfo_parameters[i].mode == NK_AUDIO_DSP_LFO_RETRIGGER)
            impl_->lfos[i].Reset(impl_->lfo_parameters[i].phase);
    impl_->filter.Init(static_cast<float>(impl_->sample_rate));
    impl_->right_filter.Init(static_cast<float>(impl_->sample_rate));
    impl_->right_filter.SetRes(impl_->filter_resonance);
    impl_->filter.SetRes(impl_->filter_resonance);
    if (impl_->filter_type == NK_AUDIO_DSP_FILTER_SVF_LOW_PASS && impl_->filter_cutoff_hz > 0.0f)
        impl_->filter.SetFreq(impl_->filter_cutoff_hz);
    impl_->envelope.Retrigger(true);
    impl_->filter_envelope.Retrigger(true);
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
    for (auto &lfo : impl_->lfos) lfo.Reset(0.0f);
}

float Voice::process() noexcept {
    const auto sample = process_stereo();
    return (sample.left + sample.right) * 0.5f;
}

StereoSample Voice::process_stereo() noexcept {
    if (!impl_->active)
        return {};
    const auto envelope = impl_->envelope.Process(impl_->gate);
    if (!impl_->gate && !impl_->envelope.IsRunning()) {
        impl_->active = false;
        return {};
    }
    const auto filter_envelope = impl_->filter_envelope_used
                                     ? impl_->filter_envelope.Process(impl_->gate) : 0.0f;
    std::array<float, NK_AUDIO_DSP_MAX_LFOS> lfo_values{};
    for (uint32_t i = 0; i < NK_AUDIO_DSP_MAX_LFOS; ++i)
        if (impl_->lfo_used[i]) lfo_values[i] = impl_->lfos[i].Process();

    std::array<float, NK_AUDIO_DSP_MAX_OSCILLATORS> pitch_offsets{};
    std::array<float, NK_AUDIO_DSP_MAX_OSCILLATORS> level_offsets{};
    std::array<float, NK_AUDIO_DSP_MAX_OSCILLATORS> phase_offsets{};
    std::array<float, NK_AUDIO_DSP_MAX_OSCILLATORS> frequency_offsets{};
    auto filter_offset = 0.0f;
    auto filter_semitones = filter_envelope * impl_->filter_envelope_amount;
    auto pan_offset = 0.0f;
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
        auto source_value = 0.0f;
        const auto is_envelope = route.source == NK_AUDIO_DSP_MODULATION_SOURCE_ENVELOPE ||
                                route.source == NK_AUDIO_DSP_MODULATION_SOURCE_FILTER_ENVELOPE;
        switch (route.source) {
        case NK_AUDIO_DSP_MODULATION_SOURCE_LFO: source_value = lfo_values[0]; break;
        case NK_AUDIO_DSP_MODULATION_SOURCE_LFO2: source_value = lfo_values[1]; break;
        case NK_AUDIO_DSP_MODULATION_SOURCE_LFO3: source_value = lfo_values[2]; break;
        case NK_AUDIO_DSP_MODULATION_SOURCE_FILTER_ENVELOPE: source_value = filter_envelope; break;
        default: source_value = envelope; break;
        }
        if (route.polarity == NK_AUDIO_DSP_MODULATION_UNIPOLAR && !is_envelope)
            source_value = 0.5f * (source_value + 1.0f);
        else if (route.polarity == NK_AUDIO_DSP_MODULATION_BIPOLAR && is_envelope)
            source_value = 2.0f * source_value - 1.0f;
        switch (route.destination) {
        case NK_AUDIO_DSP_MODULATION_DESTINATION_PITCH_SEMITONES:
            add_oscillator_route(pitch_offsets, route, route.amount * source_value);
            break;
        case NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_HZ:
            filter_offset += route.amount * source_value;
            break;
        case NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_SEMITONES:
            filter_semitones += route.amount * source_value;
            break;
        case NK_AUDIO_DSP_MODULATION_DESTINATION_PAN:
            pan_offset += route.amount * source_value;
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

    const auto frequency = impl_->base_frequency;
    // Pitch modulation is shared by the copies; evaluate its exponential once
    // per oscillator each frame rather than once for every detuned copy.
    std::array<float, NK_AUDIO_DSP_MAX_OSCILLATORS> pitched_frequencies{};
    for (uint32_t i = 0; i < impl_->oscillator_count && !impl_->source_replaces_oscillators; ++i) {
        pitched_frequencies[i] = frequency;
        if (pitch_offsets[i] != 0.0f)
            pitched_frequencies[i] *= std::pow(2.0f, pitch_offsets[i] / 12.0f);
    }
    StereoSample stereo;
    for (uint32_t copy = 0; copy < impl_->unison.count; ++copy) {
        float sample = 0.0f;
        auto copy_phase_offsets = phase_offsets;
        auto copy_frequency_offsets = frequency_offsets;
        std::array<float, NK_AUDIO_DSP_MAX_OSCILLATORS> oscillator_outputs{};
        for (uint32_t position = 0;
             position < impl_->oscillator_count && !impl_->source_replaces_oscillators; ++position) {
            const auto index = impl_->oscillator_order[position];
            auto &oscillator = impl_->oscillators[copy][index];
            for (uint32_t route_index = 0; route_index < impl_->route_count; ++route_index) {
                const auto &route = impl_->routes[route_index];
                if (route.source != NK_AUDIO_DSP_MODULATION_SOURCE_OSCILLATOR ||
                    route.oscillator_index != index + 1)
                    continue;
                auto source_value = oscillator_outputs[route.source_oscillator_index - 1];
                if (route.polarity == NK_AUDIO_DSP_MODULATION_UNIPOLAR)
                    source_value = 0.5f * (source_value + 1.0f);
                if (route.destination == NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_PHASE)
                    copy_phase_offsets[index] += route.amount * source_value;
                else if (route.destination ==
                         NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_FREQUENCY_HZ)
                    copy_frequency_offsets[index] += route.amount * source_value;
            }
            const auto oscillator_frequency = pitched_frequencies[index] * oscillator.tuning;
            const auto detuned_frequency = clamp_frequency(
                oscillator_frequency + copy_frequency_offsets[index], impl_->sample_rate);
            oscillator.wavetable_oscillator.SetFreq(detuned_frequency);
            oscillator.oscillator.SetFreq(detuned_frequency);
            const auto phase_offset = oscillator.phase + copy_phase_offsets[index];
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
        stereo.left += sample * impl_->left_weights[copy];
        stereo.right += sample * impl_->right_weights[copy];
    }
    float sample = stereo.left;
    const float noise_sample = impl_->noise.Process() * impl_->noise_level;
    if (!impl_->source_replaces_oscillators)
        sample += noise_sample;
    stereo.right += noise_sample;
    if (impl_->specialized_source) {
        const float detune =
            impl_->source_replaces_oscillators ? impl_->oscillators[0][0].detune_cents : 0.0f;
        const float source_frequency = clamp_frequency(
            frequency * std::pow(2.0f, (pitch_offsets[0] * 100 + detune) / 1200.0f) +
                frequency_offsets[0],
            impl_->sample_rate);
        sample = impl_->source.process(source_frequency, sample);
        if (impl_->source_replaces_oscillators)
            sample =
                sample * impl_->oscillators[0][0].level * std::max(0.0f, 1.0f + level_offsets[0]) +
                noise_sample;
    }
    if (impl_->specialized_source)
        stereo.right = sample;
    const auto is_stereo = impl_->unison.count > 1 && impl_->unison.stereo_spread > 0.0f;
    if (impl_->filter_type == NK_AUDIO_DSP_FILTER_SVF_LOW_PASS && impl_->filter_cutoff_hz > 0.0f) {
        const auto envelope_ratio = filter_semitones == 0.0f ? 1.0f :
            std::pow(2.0f, std::clamp(filter_semitones, -96.0f, 96.0f) / 12.0f);
        const auto cutoff = std::clamp(impl_->filter_cutoff_hz * envelope_ratio + filter_offset,
                                      1.0f, static_cast<float>(impl_->sample_rate) / 3.0f);
        impl_->filter.SetFreq(cutoff);
        impl_->filter.Process(sample);
        sample = impl_->filter.Low();
        if (is_stereo) {
            impl_->right_filter.SetFreq(cutoff);
            impl_->right_filter.Process(stereo.right);
            stereo.right = impl_->right_filter.Low();
        }
    }
    const auto amplitude = impl_->gain * std::max(0.0f, 1.0f + amplitude_offset);
    const auto pan = std::clamp(pan_offset, -1.0f, 1.0f);
    return {sample * amplitude * impl_->velocity * envelope * (1.0f - std::max(0.0f, pan)),
            (is_stereo ? stereo.right : sample) * amplitude * impl_->velocity * envelope *
                (1.0f + std::min(0.0f, pan))};
}

bool Voice::active() const noexcept {
    return impl_->active;
}

} // namespace nk::audio_dsp
