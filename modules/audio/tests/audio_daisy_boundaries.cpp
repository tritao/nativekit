#include "audio_dsp_backend.hpp"
#include "audio_daisy_source.hpp"
#include "audio_daisy_random.hpp"
#include "PhysicalModeling/KarplusString.h"
#include "Synthesis/oscillator.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace nk::audio_dsp;
static void fm2_zero_index_tracks_carrier_pitch() {
    PatchParameters reference, fm;
    nk_audio_dsp_source_options config{};
    config.struct_size = sizeof(config);
    config.kind = NK_AUDIO_DSP_SOURCE_FM2;
    config.values[NK_AUDIO_DSP_SOURCE_PARAMETER_RATIO] = 2;
    config.values[NK_AUDIO_DSP_SOURCE_PARAMETER_INDEX] = 0;
    assert(normalize_source(config, nullptr, 0, fm.source));
    Voice left, right;
    left.init(48000);
    right.init(48000);
    left.set_parameters(reference);
    right.set_parameters(fm);
    left.note_on(69, 1);
    right.note_on(69, 1);
    // Zero modulation is exactly the sine carrier, including the cached 440Hz/ratio-2 default.
    for (int i = 0; i < 2048; ++i)
        assert(std::abs(left.process() - right.process()) < 0.00001f);
}
static void fm2_matches_two_operator_reference() {
    // Build the reference from two independent oscillators, without Fm2. Daisy's
    // index scales phase additions by 0.2 and accumulates them into the carrier.
    for (const uint32_t rate : {44100u, 48000u, 96000u}) {
        for (const float frequency : {110.0f, 261.625565f, 440.0f}) {
            for (const float ratio : {1.0f, 2.0f, 3.5f, 7.1f}) {
                for (const float index : {0.0f, 1.2f, 5.0f, 9.0f}) {
                    nk_audio_dsp_source_options config{};
                    config.struct_size = sizeof(config);
                    config.kind = NK_AUDIO_DSP_SOURCE_FM2;
                    config.values[NK_AUDIO_DSP_SOURCE_PARAMETER_RATIO] = ratio;
                    config.values[NK_AUDIO_DSP_SOURCE_PARAMETER_INDEX] = index;
                    SourceParameters parameters;
                    assert(normalize_source(config, nullptr, 0, parameters));
                    DaisySource source;
                    source.configure(parameters, rate);
                    daisysp::Oscillator carrier, modulator;
                    carrier.Init(rate); modulator.Init(rate);
                    carrier.SetAmp(1); modulator.SetAmp(1);
                    for (int retrigger = 0; retrigger < 2; ++retrigger) {
                        source.trigger(frequency, 69);
                        carrier.Reset(); modulator.Reset();
                        for (int i = 0; i < 4096; ++i) {
                            const float pitch = i < 2048 ? frequency : frequency * 1.5f;
                            carrier.SetFreq(pitch); modulator.SetFreq(pitch * ratio);
                            const float depth = index * 0.2f;
                            carrier.PhaseAdd(modulator.Process() * depth);
                            const float expected = carrier.Process();
                            const float actual = source.process(pitch, 0);
                            assert(std::isfinite(actual));
                            assert(std::abs(actual - expected) < 0.00001f);
                        }
                    }
                }
            }
        }
    }
}
static SourceParameters karplus_parameters(float nonlinearity, float brightness, float damping) {
    nk_audio_dsp_source_options config{};
    config.struct_size = sizeof(config);
    config.kind = NK_AUDIO_DSP_SOURCE_KARPLUS_STRING;
    config.values[NK_AUDIO_DSP_SOURCE_PARAMETER_NONLINEARITY] = nonlinearity;
    config.values[NK_AUDIO_DSP_SOURCE_PARAMETER_BRIGHTNESS] = brightness;
    config.values[NK_AUDIO_DSP_SOURCE_PARAMETER_DAMPING] = damping;
    SourceParameters parameters;
    assert(normalize_source(config, nullptr, 0, parameters));
    return parameters;
}
static void karplus_bridge_mode_is_not_clamped_to_linear() {
    DaisySource bridge, linear;
    bridge.configure(karplus_parameters(-0.8f, 0.8f, 0.6f), 48000);
    linear.configure(karplus_parameters(0, 0.8f, 0.6f), 48000);
    bridge.trigger(261.625565f, 60); linear.trigger(261.625565f, 60);
    std::vector<float> pluck;
    double difference = 0;
    for (int i = 0; i < 8192; ++i) {
        const float sample = bridge.process(261.625565f, 0);
        const float delta = sample - linear.process(261.625565f, 0);
        assert(std::isfinite(sample));
        difference += delta * delta;
        pluck.push_back(sample);
    }
    assert(difference > 0.00001);
    bridge.trigger(261.625565f, 60);
    for (const float expected : pluck)
        assert(std::abs(bridge.process(261.625565f, 0) - expected) < 0.00001f);
}
static void karplus_matches_direct_string_model() {
    for (const uint32_t rate : {44100u, 48000u, 96000u}) {
        for (const float frequency : {110.0f, 440.0f}) {
            for (const float brightness : {0.2f, 0.5f, 0.85f}) {
                for (const float damping : {0.2f, 0.5f, 0.8f}) {
                    for (const float nonlinearity : {-0.8f, 0.0f, 0.1f, 0.7f, 0.95f}) {
                        DaisySource source;
                        source.configure(karplus_parameters(nonlinearity, brightness, damping), rate);
                        source.trigger(frequency, 60);
                        daisysp::String reference;
                        reference.Init(rate); reference.SetFreq(frequency);
                        reference.SetNonLinearity(nonlinearity);
                        reference.SetBrightness(brightness); reference.SetDamping(damping);
                        uint32_t random_state = 0x9e3779b9u ^ 61u;
                        for (int i = 0; i < 4096; ++i) {
                            const float excitation = std::sin(i * 0.03f);
                            float expected;
                            {
                                DaisyRandomScope scope(random_state);
                                expected = reference.Process(i == 0 ? 1.0f : excitation * 0.02f);
                            }
                            const float actual = source.process(frequency, excitation);
                            assert(std::isfinite(actual));
                            assert(std::abs(actual - expected) < 0.00001f);
                        }
                    }
                }
            }
        }
    }
}
static void karplus_linear_string_tracks_pitch() {
    constexpr double pi = 3.14159265358979323846;
    for (const uint32_t rate : {44100u, 48000u, 96000u}) {
        for (const float frequency : {55.0f, 110.0f, 220.0f, 440.0f, 880.0f}) {
            DaisySource source;
            source.configure(karplus_parameters(0, 0.5f, 0.5f), rate);
            source.trigger(frequency, 60);
            std::vector<double> window;
            double energy = 0;
            const int first = rate / 10;
            const int count = rate / 2;
            for (int i = 0; i < first + count; ++i) {
                const float sample = source.process(frequency, 0);
                assert(std::isfinite(sample));
                energy += sample * sample;
                if (i >= first) {
                    const double hann = 0.5 - 0.5 * std::cos(2 * pi * (i - first) / (count - 1));
                    window.push_back(sample * hann);
                }
            }
            // A low-pitch note must not lose its initial impulse to resampling.
            assert(energy > 0.0001);
            double best_power = -1;
            int best_step = 0;
            for (int step = -80; step <= 80; ++step) {
                const double candidate = frequency * (1 + step * 0.0005);
                const double coefficient = 2 * std::cos(2 * pi * candidate / rate);
                double previous = 0, before_previous = 0;
                for (const double sample : window) {
                    const double next = coefficient * previous - before_previous + sample;
                    before_previous = previous; previous = next;
                }
                const double power = previous * previous + before_previous * before_previous
                                     - coefficient * previous * before_previous;
                if (power > best_power) { best_power = power; best_step = step; }
            }
            // 0.2% is about 3.5 cents; allow the finite analysis window's resolution.
            assert(std::abs(best_step) <= 4);
        }
    }
}
static void karplus_damping_controls_natural_decay() {
    double retention[2]{};
    int mode = 0;
    for (const float damping : {0.2f, 0.8f}) {
        DaisySource source;
        source.configure(karplus_parameters(0, 0.5f, damping), 48000);
        source.trigger(220, 57);
        double early = 0, late = 0;
        for (int i = 0; i < 57600; ++i) {
            const float sample = source.process(220, 0);
            if (i >= 4800 && i < 14400) early += sample * sample;
            if (i >= 48000) late += sample * sample;
        }
        assert(early > 0.0001 && late < early);
        retention[mode++] = late / early;
    }
    // Daisy calls this damping, but higher values retain more string energy.
    assert(retention[1] > retention[0] * 10);
}
static void analog_snare_shell_decays_without_note_off() {
    nk_audio_dsp_source_options config{};
    config.struct_size = sizeof(config);
    config.kind = NK_AUDIO_DSP_SOURCE_ANALOG_SNARE_DRUM;
    for (uint32_t p = 0; p < NK_AUDIO_DSP_SOURCE_PARAMETER_COUNT; ++p) {
        float lo = 0, hi = 0, value = 0;
        if (source_parameter_info(config.kind, p, lo, hi, value)) config.values[p] = value;
    }
    config.values[NK_AUDIO_DSP_SOURCE_PARAMETER_SNAPPY] = 0;
    config.values[NK_AUDIO_DSP_SOURCE_PARAMETER_DECAY] = 0;
    config.values[NK_AUDIO_DSP_SOURCE_PARAMETER_TONE] = .25f;
    PatchParameters patch;
    patch.envelope.sustain_level = 1;
    assert(normalize_source(config, nullptr, 0, patch.source));
    Voice voice;
    voice.init(48000); voice.set_parameters(patch); voice.note_on(55, 1);
    double early_energy = 0, late_energy = 0;
    for (int i = 0; i < 52800; ++i) {
        const float sample = voice.process();
        assert(std::isfinite(sample));
        if (i < 4800) early_energy += sample * sample;
        if (i >= 48000) late_energy += sample * sample;
    }
    // Keep ADSR gated: the physical shell must decay by itself, rather than ring
    // indefinitely until the outer note release masks a saturated Q conversion.
    assert(voice.active());
    assert(early_energy > .0001);
    assert(late_energy < early_energy * .0001);
}
int main() {
    karplus_linear_string_tracks_pitch();
    karplus_damping_controls_natural_decay();
    karplus_bridge_mode_is_not_clamped_to_linear();
    karplus_matches_direct_string_model();
    fm2_matches_two_operator_reference();
    analog_snare_shell_decays_without_note_off();
    fm2_zero_index_tracks_carrier_pitch();
    for (uint32_t kind = 1; kind <= NK_AUDIO_DSP_SOURCE_KIND_COUNT; ++kind) {
        nk_audio_dsp_source_options options{};
        options.struct_size = sizeof(options);
        options.kind = kind;
        for (uint32_t p = 0; p < NK_AUDIO_DSP_SOURCE_PARAMETER_COUNT; ++p) {
            float lo = 0, hi = 0, default_value = 0;
            if (source_parameter_info(kind, p, lo, hi, default_value))
                options.values[p] = default_value;
        }
        if (kind == NK_AUDIO_DSP_SOURCE_OSCILLATOR_BANK || kind == NK_AUDIO_DSP_SOURCE_HARMONIC)
            options.amplitudes[0] = 1;
        std::vector<float> samples(17);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = .5f * std::sin(i * .4f);
        // Combined limits also cover models with no source-specific controls.
        for (int mode : {0, 1, 2}) {
            auto config = options;
            for (uint32_t parameter = 0; parameter < NK_AUDIO_DSP_SOURCE_PARAMETER_COUNT;
                 ++parameter) {
                float lo = 0, hi = 0, value = 0;
                if (source_parameter_info(kind, parameter, lo, hi, value))
                    config.values[parameter] = mode == 0 ? value : mode == 1 ? lo : hi;
            }
            PatchParameters patch;
            assert(normalize_source(
                config, kind == NK_AUDIO_DSP_SOURCE_GRANULAR ? samples.data() : nullptr,
                kind == NK_AUDIO_DSP_SOURCE_GRANULAR ? samples.size() : 0, patch.source));
            for (uint32_t note : {0u, 60u, 69u, 127u}) {
                Voice voice;
                voice.init(48000);
                voice.set_parameters(patch);
                voice.note_on(note, 1);
                for (int i = 0; i < 4096; ++i) {
                    if (!std::isfinite(voice.process())) {
                        std::fprintf(stderr,
                                     "combined limits source=%u mode=%d note=%u sample=%d\n", kind,
                                     mode, note, i);
                        return 1;
                    }
                }
            }
        }
        for (uint32_t parameter = 0; parameter < NK_AUDIO_DSP_SOURCE_PARAMETER_COUNT; ++parameter) {
            float minimum = 0, maximum = 0, value = 0;
            if (!source_parameter_info(kind, parameter, minimum, maximum, value))
                continue;
            for (float boundary : {minimum, maximum}) {
                auto config = options;
                config.values[parameter] = boundary;
                PatchParameters patch;
                patch.envelope.attack_seconds = .001f;
                patch.envelope.sustain_level = 1;
                assert(normalize_source(
                    config, kind == NK_AUDIO_DSP_SOURCE_GRANULAR ? samples.data() : nullptr,
                    kind == NK_AUDIO_DSP_SOURCE_GRANULAR ? samples.size() : 0, patch.source));
                for (uint32_t note : {0u, 60u, 69u, 127u}) {
                    Voice voice;
                    voice.init(48000);
                    voice.set_parameters(patch);
                    voice.note_on(note, 1);
                    for (int i = 0; i < 4096; ++i) {
                        float sample = voice.process();
                        if (!std::isfinite(sample)) {
                            std::fprintf(
                                stderr,
                                "nonfinite source=%u parameter=%u value=%g note=%u sample=%d\n",
                                kind, parameter, boundary, note, i);
                            return 1;
                        }
                    }
                    voice.note_off();
                    for (int i = 0; i < 48000; ++i)
                        assert(std::isfinite(voice.process()));
                    assert(!voice.active());
                }
            }
        }
    }
    std::puts("DaisySP source control boundaries and note extremes passed");
}
