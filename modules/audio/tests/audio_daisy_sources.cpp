#include "nativekit_audio_dsp.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

static nk_audio_dsp_patch_options patch_options() {
    nk_audio_dsp_patch_options p{};
    p.struct_size = sizeof(p);
    p.oscillator_count = 1;
    p.oscillators[0].struct_size = sizeof(p.oscillators[0]);
    p.oscillators[0].level = .3f;
    p.noise.struct_size = sizeof(p.noise);
    p.envelope.struct_size = sizeof(p.envelope);
    p.envelope.attack_seconds = .001f;
    p.envelope.sustain_level = 1;
    p.envelope.release_seconds = .02f;
    p.filter.struct_size = sizeof(p.filter);
    p.lfo.struct_size = sizeof(p.lfo);
    p.gain = .25f;
    return p;
}
static void render_source(nk_audio_dsp_engine engine, uint32_t kind, float speed = 1) {
    auto p = patch_options();
    nk_audio_dsp_source_options source{};
    assert(nk_audio_dsp_source_defaults(kind, &source) == NK_OK);
    std::vector<float> pcm(97);
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = .7f * std::sin(6.2831853f * i / pcm.size());
    if (kind == NK_AUDIO_DSP_SOURCE_GRANULAR) {
        source.values[NK_AUDIO_DSP_SOURCE_PARAMETER_SPEED] = speed;
        source.values[NK_AUDIO_DSP_SOURCE_PARAMETER_GRAIN_MS] = 1000;
    }
    nk_audio_dsp_patch patch = 0;
    assert(nk_audio_dsp_patch_create_source(
               &p, &source, kind == NK_AUDIO_DSP_SOURCE_GRANULAR ? pcm.data() : nullptr,
               kind == NK_AUDIO_DSP_SOURCE_GRANULAR ? pcm.size() : 0, &patch) == NK_OK);
    nk_audio_dsp_instrument instrument = 0;
    assert(nk_audio_dsp_instrument_create_from_patch(engine, patch, &instrument) == NK_OK);
    assert(nk_audio_dsp_patch_destroy(patch) == NK_OK);
    // The caller's PCM and patch are gone; the instrument must retain its source safely.
    pcm.assign(pcm.size(), std::numeric_limits<float>::quiet_NaN());
    nk_audio_dsp_event event{};
    event.struct_size = sizeof(event);
    event.kind = NK_AUDIO_DSP_EVENT_NOTE_ON;
    event.instrument = instrument;
    event.voice_id = 1;
    event.note = 60;
    event.velocity = .8f;
    float samples[256]{};
    nk_audio_dsp_render_target target{};
    target.struct_size = sizeof(target);
    target.samples = samples;
    target.frame_count = 256;
    target.sample_count = 256;
    target.channels = 1;
    double energy = 0;
    for (int block = 0; block < 200; ++block) {
        assert(nk_audio_dsp_engine_render(engine, &target, block == 0 ? &event : nullptr,
                                          block == 0 ? 1 : 0) == NK_OK);
        for (float sample : samples) {
            assert(std::isfinite(sample));
            assert(std::abs(sample) < 8);
            energy += sample * sample;
        }
    }
    if (!(energy > 1.e-7)) {
        std::fprintf(stderr, "source %u silent: %.9g\n", kind, energy);
        assert(false);
    }
    event = {};
    event.struct_size = sizeof(event);
    event.kind = NK_AUDIO_DSP_EVENT_NOTE_OFF;
    event.voice_id = 1;
    assert(nk_audio_dsp_engine_render(engine, &target, &event, 1) == NK_OK);
    for (int block = 0; block < 20; ++block)
        assert(nk_audio_dsp_engine_render(engine, &target, nullptr, 0) == NK_OK);
    for (float sample : samples)
        assert(sample == 0);
    assert(nk_audio_dsp_instrument_destroy(instrument) == NK_OK);
}
int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    nk_audio_dsp_engine_options options{};
    options.struct_size = sizeof(options);
    options.sample_rate = 48000;
    options.channels = 1;
    options.block_size = 256;
    options.max_voices = 4;
    nk_audio_dsp_engine engine = 0;
    assert(nk_audio_dsp_engine_create(&options, &engine) == NK_OK);
    for (uint32_t kind = 1; kind <= NK_AUDIO_DSP_SOURCE_KIND_COUNT; ++kind)
        render_source(engine, kind);
    for (float speed : {-4.f, -1.f, 0.f, 4.f})
        render_source(engine, NK_AUDIO_DSP_SOURCE_GRANULAR, speed);
    auto p = patch_options();
    nk_audio_dsp_source_options source{};
    nk_audio_dsp_patch patch = 123;
    assert(nk_audio_dsp_source_defaults(NK_AUDIO_DSP_SOURCE_FM2, &source) == NK_OK);
    source.values[NK_AUDIO_DSP_SOURCE_PARAMETER_RATIO] = std::numeric_limits<float>::infinity();
    assert(nk_audio_dsp_patch_create_source(&p, &source, nullptr, 0, &patch) ==
               NK_ERROR_INVALID_ARGUMENT &&
           patch == 0);
    assert(nk_audio_dsp_source_defaults(NK_AUDIO_DSP_SOURCE_FM2, &source) == NK_OK);
    source.values[NK_AUDIO_DSP_SOURCE_PARAMETER_DAMPING] = .5f;
    assert(nk_audio_dsp_patch_create_source(&p, &source, nullptr, 0, &patch) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_dsp_source_defaults(NK_AUDIO_DSP_SOURCE_RESONATOR, &source) == NK_OK);
    source.values[NK_AUDIO_DSP_SOURCE_PARAMETER_RESOLUTION] = 5.5f;
    assert(nk_audio_dsp_patch_create_source(&p, &source, nullptr, 0, &patch) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_dsp_source_defaults(NK_AUDIO_DSP_SOURCE_GRANULAR, &source) == NK_OK);
    assert(nk_audio_dsp_patch_create_source(&p, &source, nullptr, 0, &patch) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_dsp_engine_destroy(engine) == NK_OK);
    nk_shutdown();
    std::puts("DaisySP sources: 24 generators, sample ownership, reverse/frozen/long-grain safety "
              "and release lifecycle passed");
}
