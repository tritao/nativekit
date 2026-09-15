#include "nativekit_audio.h"

#include <cassert>

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

    nk_audio_sound sound = NK_INVALID_HANDLE;
    assert(nk_audio_sound_create_from_file(nullptr, nullptr, &sound) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_sound_create_from_memory(nullptr, 0, nullptr, &sound) ==
           NK_ERROR_INVALID_ARGUMENT);

    nk_audio_sound_options options{};
    options.struct_size = sizeof(options);
    options.flags = 1u << 31;
    assert(nk_audio_sound_create_from_memory("x", 1, &options, &sound) ==
           NK_ERROR_INVALID_ARGUMENT);
    options.flags = NK_AUDIO_SOUND_ASYNC;
    assert(nk_audio_sound_create_from_memory("x", 1, &options, &sound) ==
           NK_ERROR_INVALID_ARGUMENT);

    assert(nk_audio_sound_destroy(NK_INVALID_HANDLE) == NK_ERROR_INVALID_HANDLE);
    assert(nk_audio_sound_set_volume(NK_INVALID_HANDLE, -1.0f) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_sound_set_pan(NK_INVALID_HANDLE, 2.0f) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_sound_set_pitch(NK_INVALID_HANDLE, 0.0f) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_set_master_volume(-1.0f) == NK_ERROR_INVALID_ARGUMENT);
    nk_audio_clip clip = NK_INVALID_HANDLE;
    assert(nk_audio_clip_create_from_file(nullptr, &clip) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_clip_create_from_memory(nullptr, 0, &clip) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_clip_destroy(NK_INVALID_HANDLE) == NK_ERROR_INVALID_HANDLE);
    assert(nk_audio_voice_destroy(NK_INVALID_HANDLE) == NK_ERROR_INVALID_HANDLE);

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
    nk_audio_sound_options voice_options{};
    voice_options.struct_size = sizeof(voice_options);
    voice_options.flags = NK_AUDIO_SOUND_LOOPING;
    voice_options.bus = bus;
    nk_audio_voice first_voice = NK_INVALID_HANDLE;
    nk_audio_voice second_voice = NK_INVALID_HANDLE;
    assert(nk_audio_voice_create(clip, &voice_options, &first_voice) == NK_OK);
    assert(nk_audio_voice_create(clip, &voice_options, &second_voice) == NK_OK);
    assert(first_voice != second_voice);
    assert(nk_audio_clip_destroy(clip) == NK_OK);
    assert(nk_audio_voice_start(first_voice) == NK_OK);
    assert(nk_audio_voice_start(second_voice) == NK_OK);
    assert(nk_audio_bus_is_playing(bus, &state) == NK_OK && state == 1);
    assert(nk_audio_voice_set_volume(first_voice, 0.25f) == NK_OK);
    assert(nk_audio_voice_get_volume(first_voice, &value) == NK_OK && value == 0.25f);
    assert(nk_audio_voice_destroy(first_voice) == NK_OK);
    assert(nk_audio_bus_is_playing(bus, &state) == NK_OK && state == 1);
    assert(nk_audio_voice_stop(second_voice) == NK_OK);
    assert(nk_audio_voice_destroy(second_voice) == NK_OK);

    nk_audio_sound_options sound_options{};
    sound_options.struct_size = sizeof(sound_options);
    sound_options.bus = bus;
    const nk_result load_result =
        nk_audio_sound_create_from_memory(tiny_wav, sizeof(tiny_wav), &sound_options, &sound);
    if (load_result == NK_ERROR_UNSUPPORTED) {
        nk_shutdown();
        return 77;
    }
    assert(load_result == NK_OK);
    state = 1;
    assert(nk_audio_sound_is_playing(sound, &state) == NK_OK && state == 0);
    assert(nk_audio_sound_set_volume(sound, 0.5f) == NK_OK);
    assert(nk_audio_sound_set_pan(sound, -0.25f) == NK_OK);
    assert(nk_audio_sound_set_pitch(sound, 1.25f) == NK_OK);
    assert(nk_audio_sound_set_looping(sound, 1) == NK_OK);
    value = 0;
    assert(nk_audio_sound_get_volume(sound, &value) == NK_OK && value == 0.5f);
    assert(nk_audio_sound_get_pan(sound, &value) == NK_OK && value == -0.25f);
    assert(nk_audio_sound_get_pitch(sound, &value) == NK_OK && value == 1.25f);
    assert(nk_audio_sound_is_looping(sound, &state) == NK_OK && state == 1);
    assert(nk_audio_sound_get_length_seconds(sound, &value) == NK_OK && value > 0);
    assert(nk_audio_set_master_volume(0.75f) == NK_OK);
    assert(nk_audio_get_master_volume(&value) == NK_OK && value == 0.75f);
    assert(nk_audio_bus_start(bus) == NK_OK);
    assert(nk_audio_sound_start(sound) == NK_OK);
    assert(nk_audio_bus_is_playing(bus, &state) == NK_OK && state == 1);
    assert(nk_audio_bus_stop(bus) == NK_OK);
    assert(nk_audio_sound_stop(sound) == NK_OK);
    assert(nk_audio_sound_rewind(sound) == NK_OK);
    assert(nk_audio_sound_destroy(sound) == NK_OK);
    assert(nk_audio_bus_destroy(bus) == NK_OK);

    nk_shutdown();
    return 0;
}
