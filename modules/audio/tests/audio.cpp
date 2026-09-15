#include "nativekit_audio.h"
#include "nativekit_time.h"

#include <cassert>
#include <cstdio>
#include <filesystem>

namespace {

const unsigned char tiny_wav[] = {
    'R', 'I', 'F', 'F', 44, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
    16, 0, 0, 0, 1, 0, 1, 0, 0x40, 0x1f, 0, 0, 0x40, 0x1f, 0, 0,
    1, 0, 8, 0, 'd', 'a', 't', 'a', 8, 0, 0, 0, 128, 128, 128, 128,
    128, 128, 128, 128};

} // namespace

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_audio_clip clip = NK_INVALID_HANDLE;
    assert(nk_audio_clip_create_from_file(nullptr, &clip) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_clip_create_from_memory(nullptr, 0, &clip) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_clip_destroy(NK_INVALID_HANDLE) == NK_ERROR_INVALID_HANDLE);

    nk_audio_voice voice = NK_INVALID_HANDLE;
    assert(nk_audio_voice_destroy(NK_INVALID_HANDLE) == NK_ERROR_INVALID_HANDLE);
    assert(nk_audio_voice_set_volume(NK_INVALID_HANDLE, -1.0f) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_voice_set_pan(NK_INVALID_HANDLE, 2.0f) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_voice_set_pitch(NK_INVALID_HANDLE, 0.0f) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_voice_fade(NK_INVALID_HANDLE, -2.0f, 1.0f, 0) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_set_master_volume(-1.0f) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_get_time_pcm_frames(nullptr) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_get_sample_rate(nullptr) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_bus_fade(NK_INVALID_HANDLE, -2.0f, 1.0f, 0) == NK_ERROR_INVALID_ARGUMENT);

    nk_audio_bus bus = NK_INVALID_HANDLE;
    assert(nk_audio_bus_create(&bus) == NK_OK);
    assert(nk_audio_bus_set_volume(bus, 0.5f) == NK_OK);
    float value = 0;
    assert(nk_audio_bus_get_volume(bus, &value) == NK_OK && value == 0.5f);
    nk_bool state = 0;
    assert(nk_audio_bus_set_muted(bus, 1) == NK_OK);
    assert(nk_audio_bus_is_muted(bus, &state) == NK_OK && state == 1);
    assert(nk_audio_bus_set_muted(bus, 0) == NK_OK);
    assert(nk_audio_bus_is_muted(bus, &state) == NK_OK && state == 0);

    const nk_result clip_result =
        nk_audio_clip_create_from_memory(tiny_wav, sizeof(tiny_wav), &clip);
    if (clip_result == NK_ERROR_UNSUPPORTED) {
        nk_audio_bus_destroy(bus);
        nk_shutdown();
        return 77;
    }
    assert(clip_result == NK_OK);

    nk_audio_voice_options voice_options{};
    voice_options.struct_size = sizeof(voice_options);
    voice_options.flags = 1u << 31;
    assert(nk_audio_voice_create(clip, &voice_options, &voice) == NK_ERROR_INVALID_ARGUMENT);
    voice_options.flags = NK_AUDIO_VOICE_ASYNC;
    assert(nk_audio_voice_create(clip, &voice_options, &voice) == NK_ERROR_INVALID_ARGUMENT);
    voice_options.flags = 0;
    nk_audio_voice completion_voice = NK_INVALID_HANDLE;
    assert(nk_audio_voice_create(clip, &voice_options, &completion_voice) == NK_OK);
    nk_audio_voice_load_state load_state = NK_AUDIO_VOICE_LOADING;
    assert(nk_audio_voice_get_load_state(completion_voice, &load_state) == NK_OK);
    assert(load_state == NK_AUDIO_VOICE_READY);
    nk_audio_voice timed_voice = NK_INVALID_HANDLE;
    voice_options.flags = NK_AUDIO_VOICE_LOOPING;
    assert(nk_audio_voice_create(clip, &voice_options, &timed_voice) == NK_OK);
    voice_options.flags = NK_AUDIO_VOICE_LOOPING;
    voice_options.bus = bus;
    nk_audio_voice first_voice = NK_INVALID_HANDLE;
    nk_audio_voice second_voice = NK_INVALID_HANDLE;
    assert(nk_audio_voice_create(clip, &voice_options, &first_voice) == NK_OK);
    assert(nk_audio_voice_create(clip, &voice_options, &second_voice) == NK_OK);
    assert(first_voice != second_voice);
    assert(nk_audio_clip_destroy(clip) == NK_OK);

    uint32_t sample_rate = 0;
    uint64_t now_frames = 0;
    assert(nk_audio_get_sample_rate(&sample_rate) == NK_OK && sample_rate > 0);
    assert(nk_audio_get_time_pcm_frames(&now_frames) == NK_OK);
    assert(nk_audio_voice_fade(timed_voice, NK_AUDIO_VOLUME_CURRENT, 0.25f,
                               sample_rate / 100) == NK_OK);
    assert(nk_audio_voice_fade_at(timed_voice, 0.25f, 0.5f, sample_rate / 100,
                                  now_frames + sample_rate) == NK_OK);
    assert(nk_audio_voice_schedule_start(timed_voice, now_frames + sample_rate * 5) == NK_OK);
    assert(nk_audio_voice_start(timed_voice) == NK_OK);
    assert(nk_audio_voice_is_playing(timed_voice, &state) == NK_OK && state == 0);
    assert(nk_audio_voice_clear_schedule(timed_voice) == NK_OK);
    assert(nk_audio_voice_stop(timed_voice) == NK_OK);
    assert(nk_audio_voice_start(timed_voice) == NK_OK);
    assert(nk_audio_get_time_pcm_frames(&now_frames) == NK_OK);
    assert(nk_audio_voice_schedule_stop(timed_voice, now_frames + sample_rate * 5) == NK_OK);
    assert(nk_audio_voice_is_playing(timed_voice, &state) == NK_OK && state == 1);
    assert(nk_audio_voice_clear_schedule(timed_voice) == NK_OK);
    assert(nk_audio_voice_is_playing(timed_voice, &state) == NK_OK && state == 1);
    assert(nk_audio_voice_stop(timed_voice) == NK_OK);
    assert(nk_audio_voice_fade(timed_voice, -2.0f, 1.0f, 0) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_voice_fade(timed_voice, 0.0f, -1.0f, 0) == NK_ERROR_INVALID_ARGUMENT);

    assert(nk_audio_voice_start(first_voice) == NK_OK);
    assert(nk_audio_voice_start(second_voice) == NK_OK);
    assert(nk_audio_voice_start(completion_voice) == NK_OK);
    assert(nk_audio_bus_is_playing(bus, &state) == NK_OK && state == 1);
    assert(nk_audio_bus_fade(bus, NK_AUDIO_VOLUME_CURRENT, 0.75f, sample_rate / 100) == NK_OK);
    assert(nk_audio_bus_fade_at(bus, 0.75f, 0.5f, sample_rate / 100,
                                now_frames + sample_rate) == NK_OK);
    assert(nk_audio_get_time_pcm_frames(&now_frames) == NK_OK);
    assert(nk_audio_bus_schedule_stop(bus, now_frames + sample_rate * 5) == NK_OK);
    assert(nk_audio_bus_is_playing(bus, &state) == NK_OK && state == 1);
    assert(nk_audio_bus_clear_schedule(bus) == NK_OK);
    assert(nk_audio_bus_is_playing(bus, &state) == NK_OK && state == 1);
    assert(nk_audio_bus_stop(bus) == NK_OK);
    assert(nk_audio_get_time_pcm_frames(&now_frames) == NK_OK);
    assert(nk_audio_bus_schedule_start(bus, now_frames + sample_rate * 5) == NK_OK);
    assert(nk_audio_bus_start(bus) == NK_OK);
    assert(nk_audio_voice_start(first_voice) == NK_OK);
    assert(nk_audio_bus_is_playing(bus, &state) == NK_OK && state == 0);
    assert(nk_audio_bus_clear_schedule(bus) == NK_OK);
    assert(nk_audio_bus_is_playing(bus, &state) == NK_OK && state == 1);
    assert(nk_audio_voice_set_volume(first_voice, 0.25f) == NK_OK);
    assert(nk_audio_voice_get_volume(first_voice, &value) == NK_OK && value == 0.25f);
    assert(nk_audio_voice_set_pan(first_voice, -0.25f) == NK_OK);
    assert(nk_audio_voice_get_pan(first_voice, &value) == NK_OK && value == -0.25f);
    assert(nk_audio_voice_set_pitch(first_voice, 1.25f) == NK_OK);
    assert(nk_audio_voice_get_pitch(first_voice, &value) == NK_OK && value == 1.25f);
    assert(nk_audio_voice_is_looping(first_voice, &state) == NK_OK && state == 1);
    assert(nk_audio_voice_get_length_seconds(first_voice, &value) == NK_OK && value > 0);
    nk_bool completion_seen = 0;
    for (int attempt = 0; attempt < 20 && !completion_seen; ++attempt) {
        assert(nk_wait_events_timeout(0.05) == NK_OK);
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_AUDIO_VOICE_COMPLETE && event.source == completion_voice)
            completion_seen = 1;
        assert(event.data_size == 0);
        nk_event_release(&event);
    }
    assert(completion_seen == 1);
    assert(nk_audio_voice_destroy(completion_voice) == NK_OK);

    const auto async_path =
        (std::filesystem::temp_directory_path() / "nativekit-audio-async-test.wav").string();
    auto *async_file = std::fopen(async_path.c_str(), "wb");
    assert(async_file != nullptr);
    assert(std::fwrite(tiny_wav, 1, sizeof(tiny_wav), async_file) == sizeof(tiny_wav));
    assert(std::fclose(async_file) == 0);

    nk_audio_clip async_clip = NK_INVALID_HANDLE;
    assert(nk_audio_clip_create_from_file(async_path.c_str(), &async_clip) == NK_OK);
    voice_options.bus = NK_INVALID_HANDLE;
    voice_options.flags = NK_AUDIO_VOICE_ASYNC;
    nk_audio_voice async_decode_voice = NK_INVALID_HANDLE;
    assert(nk_audio_voice_create(async_clip, &voice_options, &async_decode_voice) == NK_OK);
    voice_options.flags = NK_AUDIO_VOICE_ASYNC | NK_AUDIO_VOICE_STREAM;
    nk_audio_voice async_stream_voice = NK_INVALID_HANDLE;
    assert(nk_audio_voice_create(async_clip, &voice_options, &async_stream_voice) == NK_OK);
    assert(nk_audio_clip_destroy(async_clip) == NK_OK);

    nk_bool decode_ready = 0;
    nk_bool stream_ready = 0;
    nk_bool decode_event_seen = 0;
    nk_bool stream_event_seen = 0;
    for (int attempt = 0; attempt < 40 && (!decode_event_seen || !stream_event_seen); ++attempt) {
        assert(nk_audio_voice_get_load_state(async_decode_voice, &load_state) == NK_OK);
        if (load_state == NK_AUDIO_VOICE_READY)
            decode_ready = 1;
        assert(nk_audio_voice_get_load_state(async_stream_voice, &load_state) == NK_OK);
        if (load_state == NK_AUDIO_VOICE_READY)
            stream_ready = 1;

        assert(nk_wait_events_timeout(0.05) == NK_OK);
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_AUDIO_VOICE_READY) {
            assert(event.result == NK_OK);
            if (event.source == async_decode_voice)
                decode_event_seen = 1;
            if (event.source == async_stream_voice)
                stream_event_seen = 1;
        }
        if (event.kind == NK_EVENT_AUDIO_VOICE_LOAD_FAILED)
            assert(false);
        nk_event_release(&event);
    }
    assert(decode_ready == 1 && stream_ready == 1 && decode_event_seen == 1 &&
           stream_event_seen == 1);
    assert(nk_audio_voice_get_load_state(async_decode_voice, &load_state) == NK_OK);
    assert(load_state == NK_AUDIO_VOICE_READY);
    assert(nk_audio_voice_get_load_state(async_stream_voice, &load_state) == NK_OK);
    assert(load_state == NK_AUDIO_VOICE_READY);
    assert(nk_audio_voice_destroy(async_decode_voice) == NK_OK);
    assert(nk_audio_voice_destroy(async_stream_voice) == NK_OK);
    assert(std::remove(async_path.c_str()) == 0);

    assert(nk_audio_voice_destroy(timed_voice) == NK_OK);
    assert(nk_audio_voice_destroy(first_voice) == NK_OK);
    assert(nk_audio_bus_is_playing(bus, &state) == NK_OK && state == 1);
    assert(nk_audio_bus_stop(bus) == NK_OK);
    assert(nk_audio_voice_stop(second_voice) == NK_OK);
    assert(nk_audio_voice_rewind(second_voice) == NK_OK);
    assert(nk_audio_voice_destroy(second_voice) == NK_OK);

    assert(nk_audio_set_master_volume(0.75f) == NK_OK);
    assert(nk_audio_get_master_volume(&value) == NK_OK && value == 0.75f);
    assert(nk_audio_bus_destroy(bus) == NK_OK);

    nk_audio_clip shutdown_clip = NK_INVALID_HANDLE;
    nk_audio_voice shutdown_voice = NK_INVALID_HANDLE;
    assert(nk_audio_clip_create_from_memory(tiny_wav, sizeof(tiny_wav), &shutdown_clip) == NK_OK);
    voice_options.flags = NK_AUDIO_VOICE_LOOPING;
    voice_options.bus = NK_INVALID_HANDLE;
    assert(nk_audio_voice_create(shutdown_clip, &voice_options, &shutdown_voice) == NK_OK);
    assert(nk_audio_voice_start(shutdown_voice) == NK_OK);
    nk_shutdown();
    return 0;
}
