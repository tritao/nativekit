#include "nativekit_audio_graph.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_use_offline_device();
extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_render(float *, uint32_t);
namespace {
constexpr uint32_t frames = 256;
std::array<float, frames * 2> output;
float read_energy(int blocks = 1) {
    float sum = 0;
    for (int b = 0; b < blocks; ++b) {
        assert(nk_audio_test_render(output.data(), frames) == NK_OK);
        for (const float sample : output) {
            assert(std::isfinite(sample));
            sum += sample * sample;
        }
    }
    return sum / blocks;
}
}
int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    assert(nk_audio_test_use_offline_device() == NK_OK);
    nk_audio_bus bus = NK_INVALID_HANDLE;
    assert(nk_audio_bus_create(nullptr, &bus) == NK_OK);
    nk_audio_dsp_engine_options options{};
    options.struct_size = sizeof(options);
    options.sample_rate = 48000;
    options.channels = 2;
    nk_audio_dsp_engine synth = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_engine_create(&options, &synth) == NK_OK);
    assert(nk_audio_dsp_engine_set_bus(synth, bus) == NK_ERROR_INVALID_REQUEST);
    assert(nk_audio_dsp_engine_attach_device(synth) == NK_OK);
    assert(nk_audio_dsp_engine_set_bus(synth, bus) == NK_OK);
    nk_audio_dsp_instrument_options instrument_options{};
    instrument_options.struct_size = sizeof(instrument_options);
    instrument_options.gain = 0.2f;
    instrument_options.sustain_level = 1;
    nk_audio_dsp_instrument instrument = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_instrument_create(synth, &instrument_options, &instrument) == NK_OK);
    nk_audio_dsp_event note{};
    note.struct_size = sizeof(note);
    note.kind = NK_AUDIO_DSP_EVENT_NOTE_ON;
    note.instrument = instrument;
    note.voice_id = 1;
    note.note = 69;
    note.velocity = 1;
    assert(nk_audio_dsp_engine_schedule(synth, 0, &note, 1) == NK_OK);
    const auto dry = read_energy(8);
    assert(dry > 1);
    assert(nk_audio_bus_set_volume(bus, 0.5f) == NK_OK);
    read_energy(4); // allow graph cache/fader smoothing to settle
    const auto half = read_energy(8);
    assert(half > dry * 0.20f && half < dry * 0.30f);
    assert(nk_audio_bus_set_muted(bus, 1) == NK_OK);
    read_energy(4);
    assert(read_energy(4) < 0.000001f);
    assert(nk_audio_bus_set_muted(bus, 0) == NK_OK);
    assert(nk_audio_bus_set_volume(bus, 1) == NK_OK);
    read_energy(4);

    nk_audio_bus_effect reverb = NK_INVALID_HANDLE, dynamics = NK_INVALID_HANDLE;
    assert(nk_audio_bus_effect_create_reverb(bus, &reverb) == NK_OK);
    assert(nk_audio_bus_effect_create_dynamics(bus, &dynamics) == NK_OK);
    assert(nk_audio_bus_effect_set_parameter(reverb, NK_AUDIO_EFFECT_PARAMETER_DRY, 0) == NK_OK);
    assert(nk_audio_bus_effect_set_parameter(reverb, NK_AUDIO_EFFECT_PARAMETER_WET, 1) == NK_OK);
    assert(nk_audio_bus_effect_set_parameter(reverb, NK_AUDIO_EFFECT_PARAMETER_DECAY_SECONDS, 0.6f) == NK_OK);
    assert(nk_audio_bus_effect_set_parameter(reverb, NK_AUDIO_EFFECT_PARAMETER_RATIO, 4) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_bus_effect_set_parameter(reverb, NK_AUDIO_EFFECT_PARAMETER_WET,
        std::numeric_limits<float>::infinity()) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_bus_effect_get_parameter(reverb, NK_AUDIO_EFFECT_PARAMETER_WET, nullptr) == NK_ERROR_INVALID_ARGUMENT);
    float wet = 0;
    assert(nk_audio_bus_effect_get_parameter(reverb, NK_AUDIO_EFFECT_PARAMETER_WET, &wet) == NK_OK && wet == 1);
    uint32_t tail = 0, latency = 1;
    assert(nk_audio_bus_effect_get_tail(reverb, &tail) == NK_OK && tail >= 28800 && tail <= 28801);
    assert(nk_audio_bus_effect_get_latency(reverb, &latency) == NK_OK && latency == 0);
    assert(nk_audio_bus_effect_reset(reverb) == NK_OK);
    assert(read_energy(100) > 0.01f);
    // The fader follows effects, so mute silences the complete processed signal.
    assert(nk_audio_bus_set_muted(bus, 1) == NK_OK);
    read_energy(4);
    assert(read_energy(4) < 0.000001f);
    assert(nk_audio_bus_set_muted(bus, 0) == NK_OK);
    read_energy(4);
    assert(read_energy(8) > 0.01f);
    assert(nk_audio_bus_effect_set_position(dynamics, 0) == NK_OK);
    assert(nk_audio_bus_effect_set_enabled(dynamics, 0) == NK_OK);
    assert(nk_audio_bus_effect_set_enabled(dynamics, 1) == NK_OK);
    note.kind = NK_AUDIO_DSP_EVENT_NOTE_OFF;
    uint64_t now = 0;
    assert(nk_audio_get_time_pcm_frames(&now) == NK_OK);
    assert(nk_audio_dsp_engine_schedule(synth, now, &note, 1) == NK_OK);
    read_energy(4);
    assert(read_energy(20) > 0.00001f); // reverb survives synth note end
    read_energy(500);
    assert(read_energy(4) < 0.00000001f);
    assert(nk_audio_bus_effect_destroy(reverb) == NK_OK);
    assert(nk_audio_bus_effect_get_tail(reverb, &tail) == NK_ERROR_INVALID_HANDLE);

    // Routing changes preserve queued notes; destroying a routed bus restores master.
    assert(nk_audio_get_time_pcm_frames(&now) == NK_OK);
    note.kind = NK_AUDIO_DSP_EVENT_NOTE_ON;
    assert(nk_audio_dsp_engine_schedule(synth, now + frames * 4, &note, 1) == NK_OK);
    assert(nk_audio_dsp_engine_set_bus(synth, NK_INVALID_HANDLE) == NK_OK);
    assert(nk_audio_dsp_engine_set_bus(synth, bus) == NK_OK);
    assert(nk_audio_bus_destroy(bus) == NK_OK);
    assert(nk_audio_bus_effect_reset(dynamics) == NK_ERROR_INVALID_HANDLE);
    assert(nk_audio_dsp_engine_set_bus(synth, bus) == NK_ERROR_INVALID_HANDLE);
    read_energy(4);
    assert(read_energy(8) > 1);
    assert(nk_audio_dsp_engine_detach_device(synth) == NK_OK);
    read_energy(4);
    assert(read_energy(4) == 0);
    assert(nk_audio_dsp_instrument_destroy(instrument) == NK_OK);
    assert(nk_audio_dsp_engine_destroy(synth) == NK_OK);
    nk_shutdown();
    std::puts("PASS: offline synth bus routing, volume/mute, effect tails, and lifecycle");
}
