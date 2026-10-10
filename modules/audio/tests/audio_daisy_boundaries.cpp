#include "audio_dsp_backend.hpp"
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
int main() {
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
