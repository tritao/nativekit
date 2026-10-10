#include "nativekit_audio_graph.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_use_offline_device();
extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_render(float *, uint32_t);
namespace {
float energy(int blocks) {
    std::array<float, 512> pcm{};
    float result = 0;
    for (int i = 0; i < blocks; ++i) {
        assert(nk_audio_test_render(pcm.data(), 256) == NK_OK);
        for (float sample : pcm) { assert(std::isfinite(sample)); result += sample * sample; }
    }
    return result / blocks;
}
nk_audio_bus make_bus(nk_audio_bus parent) {
    nk_audio_bus_options options{};
    options.struct_size = sizeof(options); options.parent = parent;
    nk_audio_bus bus = NK_INVALID_HANDLE;
    assert(nk_audio_bus_create(&options, &bus) == NK_OK);
    return bus;
}
}
int main() {
    nk_init_options init{}; init.struct_size = sizeof(init); init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    assert(nk_audio_test_use_offline_device() == NK_OK);
    const auto master = make_bus(NK_INVALID_HANDLE);
    const auto source = make_bus(master), second = make_bus(master), room = make_bus(master);
    nk_audio_dsp_engine_options options{};
    options.struct_size = sizeof(options); options.sample_rate = 48000; options.channels = 2;
    nk_audio_dsp_engine synth = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_engine_create(&options, &synth) == NK_OK);
    assert(nk_audio_dsp_engine_attach_device(synth) == NK_OK);
    assert(nk_audio_dsp_engine_set_bus(synth, source) == NK_OK);
    nk_audio_dsp_instrument_options patch{};
    patch.struct_size = sizeof(patch); patch.gain = 0.1f; patch.sustain_level = 1;
    nk_audio_dsp_instrument instrument = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_instrument_create(synth, &patch, &instrument) == NK_OK);
    nk_audio_dsp_event note{}; note.struct_size = sizeof(note); note.kind = NK_AUDIO_DSP_EVENT_NOTE_ON;
    note.instrument = instrument; note.voice_id = 1; note.note = 69; note.velocity = 1;
    assert(nk_audio_dsp_engine_schedule(synth, 0, &note, 1) == NK_OK);
    const float dry = energy(16);
    assert(dry > 0.1f);
    nk_audio_bus_send send = NK_INVALID_HANDLE, other = NK_INVALID_HANDLE, invalid = NK_INVALID_HANDLE;
    assert(nk_audio_bus_send_create(source, room, 0.5f, &send) == NK_OK);
    assert(nk_audio_bus_send_create(second, room, 0.2f, &other) == NK_OK);
    energy(4);
    const float combined = energy(16);
    assert(combined > dry * 2.20f && combined < dry * 2.30f); // independent dry + half-level return
    assert(nk_audio_bus_stop(room) == NK_OK);
    energy(4);
    assert(std::abs(energy(16) / dry - 1) < 0.03f);
    assert(nk_audio_bus_start(room) == NK_OK);
    energy(4);
    assert(std::abs(energy(16) / dry - 2.25f) < 0.04f);
    // Irregular hardware reads must not rewind the graph's buffered render clock.
    std::array<float, 1026> irregular{};
    float previous = 0;
    bool have_previous = false;
    for (auto frames : {1u, 7u, 63u, 257u, 513u, 1u, 257u}) {
        assert(nk_audio_test_render(irregular.data(), frames) == NK_OK);
        for (uint32_t f = 0; f < frames; ++f) {
            assert(std::isfinite(irregular[f*2]));
            if (have_previous) assert(std::abs(irregular[f*2] - previous) < 0.012f);
            previous = irregular[f*2]; have_previous = true;
        }
    }
    assert(nk_audio_bus_send_set_volume(send, 0) == NK_OK);
    energy(4);
    assert(std::abs(energy(16) / dry - 1) < 0.03f);
    assert(nk_audio_bus_send_create(source, source, 1, &invalid) == NK_ERROR_INVALID_REQUEST);
    assert(nk_audio_bus_send_create(room, source, 1, &invalid) == NK_ERROR_INVALID_REQUEST);
    assert(nk_audio_bus_send_create(master, source, 1, &invalid) == NK_ERROR_INVALID_REQUEST);
    assert(nk_audio_bus_set_parent(room, second) == NK_ERROR_INVALID_REQUEST);
    assert(nk_audio_bus_send_set_volume(send, std::numeric_limits<float>::quiet_NaN()) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_bus_send_set_volume(send, 1.1f) == NK_ERROR_INVALID_ARGUMENT);
    nk_audio_bus destination = NK_INVALID_HANDLE;
    assert(nk_audio_bus_send_get_destination(other, &destination) == NK_OK && destination == room);
    std::array<nk_audio_bus_send, NK_AUDIO_MAX_BUS_SENDS - 1> extra{};
    for (auto &handle : extra) assert(nk_audio_bus_send_create(source, room, 0, &handle) == NK_OK);
    assert(nk_audio_bus_send_create(source, room, 0, &invalid) == NK_ERROR_INVALID_REQUEST);
    assert(nk_audio_bus_send_destroy(extra.back()) == NK_OK);
    assert(nk_audio_bus_send_create(source, room, 0, &extra.back()) == NK_OK); // slot reuse
    for (auto handle : extra) assert(nk_audio_bus_send_destroy(handle) == NK_OK);
    nk_audio_bus_effect reverb = NK_INVALID_HANDLE;
    assert(nk_audio_bus_effect_create_reverb(room, &reverb) == NK_OK);
    assert(nk_audio_bus_effect_set_parameter(reverb, NK_AUDIO_EFFECT_PARAMETER_DRY, 0) == NK_OK);
    assert(nk_audio_bus_effect_set_parameter(reverb, NK_AUDIO_EFFECT_PARAMETER_WET, 1) == NK_OK);
    assert(nk_audio_bus_send_set_volume(send, 0.5f) == NK_OK);
    energy(120);
    // Remove the source contribution entirely: the shared room must keep ringing.
    assert(nk_audio_bus_stop(source) == NK_OK);
    assert(nk_audio_bus_send_destroy(send) == NK_OK);
    energy(4);
    assert(energy(20) > 0.00001f);
    assert(nk_audio_bus_start(source) == NK_OK);
    assert(nk_audio_bus_send_create(source, room, 0.1f, &send) == NK_OK);
    assert(nk_audio_bus_destroy(source) == NK_OK);
    float volume = 0;
    assert(nk_audio_bus_send_get_volume(send, &volume) == NK_ERROR_INVALID_HANDLE);
    assert(nk_audio_bus_send_get_volume(other, &volume) == NK_OK);
    assert(nk_audio_bus_destroy(room) == NK_OK);
    assert(nk_audio_bus_send_get_volume(other, &volume) == NK_ERROR_INVALID_HANDLE);
    assert(nk_audio_bus_destroy(second) == NK_OK);
    assert(nk_audio_bus_destroy(master) == NK_OK);
    assert(nk_audio_dsp_engine_detach_device(synth) == NK_OK);
    assert(nk_audio_dsp_instrument_destroy(instrument) == NK_OK);
    assert(nk_audio_dsp_engine_destroy(synth) == NK_OK);
    nk_shutdown();
    std::puts("PASS: send levels, shared returns, cycle rejection, limits, and lifecycle");
}
