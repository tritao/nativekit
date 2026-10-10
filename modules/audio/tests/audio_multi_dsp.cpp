#include "nativekit_audio_graph.h"
#include <array>
#include <cassert>
#include <cmath>
#include <atomic>
#include <thread>

extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_use_offline_device();
extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_render(float *, uint32_t);
extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_dsp_process(nk_audio_dsp_engine, float *, uint32_t, uint64_t);

namespace {
float energy() {
    std::array<float, 257 * 2> block{};
    float sum = 0;
    for (int i = 0; i < 12; ++i) {
        assert(nk_audio_test_render(block.data(), 257) == NK_OK);
        if (i < 4) continue; // Allow graph cache and fader changes to settle.
        for (auto sample : block) {
            assert(std::isfinite(sample));
            sum += sample * sample;
        }
    }
    return sum;
}
void note(nk_audio_dsp_engine engine, nk_audio_dsp_instrument instrument) {
    uint64_t now = 0;
    assert(nk_audio_get_time_pcm_frames(&now) == NK_OK);
    nk_audio_dsp_event event{};
    event.struct_size = sizeof(event);
    event.kind = NK_AUDIO_DSP_EVENT_NOTE_ON;
    event.instrument = instrument;
    event.voice_id = 1; // Voice IDs are engine-local.
    event.note = 69;
    event.velocity = 1;
    assert(nk_audio_dsp_engine_schedule(engine, now, &event, 1) == NK_OK);
}
}
int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    assert(nk_audio_test_use_offline_device() == NK_OK);
    nk_audio_bus a = NK_INVALID_HANDLE, b = NK_INVALID_HANDLE;
    assert(nk_audio_bus_create(nullptr, &a) == NK_OK);
    assert(nk_audio_bus_create(nullptr, &b) == NK_OK);
    nk_audio_dsp_engine_options options{};
    options.struct_size = sizeof(options);
    options.sample_rate = 48000;
    options.channels = 2;
    nk_audio_dsp_engine first = NK_INVALID_HANDLE, second = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_engine_create(&options, &first) == NK_OK);
    assert(nk_audio_dsp_engine_create(&options, &second) == NK_OK);
    assert(nk_audio_dsp_engine_attach_device(first) == NK_OK);
    assert(nk_audio_dsp_engine_attach_device(second) == NK_OK);
    assert(nk_audio_dsp_engine_set_bus(first, a) == NK_OK);
    assert(nk_audio_dsp_engine_reset(first) == NK_ERROR_INVALID_REQUEST);
    assert(nk_audio_dsp_engine_set_bus(second, b) == NK_OK);
    // Duplicate attachment preserves existing routing and queued events.
    assert(nk_audio_dsp_engine_attach_device(first) == NK_OK);
    nk_audio_dsp_instrument_options patch{};
    patch.struct_size = sizeof(patch);
    patch.gain = 0.2f;
    patch.sustain_level = 1;
    nk_audio_dsp_instrument one = NK_INVALID_HANDLE, two = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_instrument_create(first, &patch, &one) == NK_OK);
    assert(nk_audio_dsp_instrument_create(second, &patch, &two) == NK_OK);
    // Clear/requeue while a callback reads slots. The producer must never advance
    // the consumer cursor or overwrite a slot that is still being consumed.
    std::atomic<bool> ready{false}, run{true};
    std::thread callback([&] {
        std::array<float, 64 * 2> samples{};
        uint64_t frame = 0;
        ready.store(true, std::memory_order_release);
        while (run.load(std::memory_order_acquire)) {
            samples.fill(0);
            assert(nk_audio_test_dsp_process(first, samples.data(), 64, frame) == NK_OK);
            for (const auto sample : samples) assert(std::isfinite(sample));
            frame += 64;
        }
    });
    while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    std::array<nk_audio_dsp_event, 32> events{};
    for (auto &event : events) {
        event.struct_size = sizeof(event);
        event.kind = NK_AUDIO_DSP_EVENT_NOTE_ON;
        event.instrument = one;
        event.voice_id = 1;
        event.note = 69;
        event.velocity = 1;
    }
    for (int i = 0; i < 200; ++i) {
        assert(nk_audio_dsp_engine_clear_schedule(first) == NK_OK);
        assert(nk_audio_dsp_engine_schedule(first, 0, events.data(), events.size()) == NK_OK);
    }
    run.store(false, std::memory_order_release);
    callback.join();
    assert(nk_audio_dsp_engine_clear_schedule(first) == NK_OK);
    assert(energy() < 1e-8f);
    // New generation notes must survive the deferred voice reset.
    note(first, one); note(second, two);
    assert(energy() > 1);
    assert(nk_audio_bus_set_volume(a, 0) == NK_OK);
    assert(energy() > 1); // Second renderer has its own route.
    assert(nk_audio_bus_set_volume(b, 0) == NK_OK);
    assert(energy() < 1e-8f);
    assert(nk_audio_bus_set_volume(a, 1) == NK_OK);
    assert(energy() > 1);
    assert(nk_audio_dsp_engine_detach_device(first) == NK_OK);
    assert(energy() < 1e-8f);
    assert(nk_audio_bus_set_volume(b, 1) == NK_OK);
    assert(energy() > 1);
    // Destroy an attached renderer; its graph node must disappear immediately.
    assert(nk_audio_dsp_engine_destroy(second) == NK_OK);
    assert(energy() < 1e-8f);
    assert(nk_audio_dsp_engine_attach_device(first) == NK_OK);
    assert(nk_audio_dsp_engine_set_bus(first, a) == NK_OK);
    note(first, one);
    assert(energy() > 1);
    assert(nk_audio_bus_destroy(a) == NK_OK);
    assert(energy() > 1); // Destroyed destination restores this renderer to master.
    assert(nk_audio_bus_destroy(b) == NK_OK);
    // Shutdown with a renderer still attached exercises graph/handle teardown.
    nk_shutdown();
}
