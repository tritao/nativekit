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

bool poll_audio_voice_event(nk_event_kind kind, nk_audio_voice source) {
    for (int attempt = 0; attempt < 16; ++attempt) {
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        const bool empty = event.kind == NK_EVENT_NONE;
        const bool match = event.kind == kind && event.source == source;
        if (match) {
            assert(event.flags == 0);
            assert(event.request_id == NK_INVALID_REQUEST_ID);
            assert(event.result == NK_OK);
            assert(event.data_count == 0);
            assert(event.data_size == 0);
        }
        nk_event_release(&event);
        if (match)
            return true;
        if (empty)
            return false;
    }
    return false;
}

} // namespace

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_audio_clip clip = NK_INVALID_HANDLE;
    assert(nk_audio_clip_create_from_file(nullptr, &clip) == NK_ERROR_INVALID_ARGUMENT);
    nk_resource unreadable_resource{};
    unreadable_resource.struct_size = sizeof(unreadable_resource);
    unreadable_resource.uri = "file:///unused.wav";
    assert(nk_audio_clip_create_from_resource(&unreadable_resource, &clip) ==
           NK_ERROR_INVALID_ARGUMENT);
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

    assert(nk_audio_device_configure(nullptr) == NK_ERROR_INVALID_ARGUMENT);
    nk_audio_device_options device_options{};
    device_options.struct_size = sizeof(device_options);
    device_options.period_size_in_frames = 128;
    device_options.period_size_in_milliseconds = 10;
    assert(nk_audio_device_configure(&device_options) == NK_ERROR_INVALID_ARGUMENT);
    device_options.period_size_in_milliseconds = 0;
    device_options.playback_device_index = NK_AUDIO_DEVICE_DEFAULT;
    device_options.sample_rate = 48000;
    device_options.channels = 2;
    device_options.no_auto_start = 1;
    uint32_t device_count = 0;
    assert(nk_audio_device_get_count(&device_count) == NK_OK);
    if (device_count == 0) {
        nk_shutdown();
        return 77;
    }
    uint32_t device_name_size = 0;
    assert(nk_audio_device_get_name(0, nullptr, &device_name_size) == NK_ERROR_BUFFER_TOO_SMALL);
    assert(device_name_size > 0 && device_name_size <= 256);
    char device_name[256]{};
    device_name_size = sizeof(device_name);
    assert(nk_audio_device_get_name(0, device_name, &device_name_size) == NK_OK);
    assert(device_name_size > 0 && device_name[device_name_size - 1] == '\0');
    nk_bool device_is_default = 0;
    assert(nk_audio_device_is_default(0, &device_is_default) == NK_OK);
    assert(nk_audio_device_configure(&device_options) == NK_OK);
    nk_audio_device_state device_state = NK_AUDIO_DEVICE_UNINITIALIZED;
    assert(nk_audio_device_get_state(&device_state) == NK_OK);
    assert(device_state == NK_AUDIO_DEVICE_UNINITIALIZED);
    assert(nk_audio_device_stop() == NK_ERROR_INVALID_REQUEST);
    assert(nk_audio_device_restart() == NK_ERROR_INVALID_REQUEST);
    uint32_t configured_sample_rate = 0;
    assert(nk_audio_device_start() == NK_OK);
    assert(nk_audio_get_sample_rate(&configured_sample_rate) == NK_OK);
    assert(configured_sample_rate == 48000);
    assert(nk_audio_device_get_state(&device_state) == NK_OK);
    assert(device_state == NK_AUDIO_DEVICE_STARTED);
    device_options.sample_rate = 44100;
    assert(nk_audio_device_configure(&device_options) == NK_ERROR_INVALID_REQUEST);
    device_options.sample_rate = 48000;
    assert(nk_audio_device_stop() == NK_OK);
    assert(nk_audio_device_get_state(&device_state) == NK_OK);
    assert(device_state == NK_AUDIO_DEVICE_STOPPED);
    assert(nk_audio_device_restart() == NK_OK);
    assert(nk_audio_device_get_state(&device_state) == NK_OK);
    assert(device_state == NK_AUDIO_DEVICE_STARTED);
    nk_bool started_event = 0;
    nk_bool stopped_event = 0;
    for (int attempt = 0; attempt < 20 && (!started_event || !stopped_event); ++attempt) {
        assert(nk_wait_events_timeout(0.01) == NK_OK);
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_AUDIO_DEVICE_STARTED ||
            event.kind == NK_EVENT_AUDIO_DEVICE_STOPPED) {
            assert(event.source == NK_INVALID_HANDLE);
            started_event |= event.kind == NK_EVENT_AUDIO_DEVICE_STARTED;
            stopped_event |= event.kind == NK_EVENT_AUDIO_DEVICE_STOPPED;
        }
        nk_event_release(&event);
    }
    assert(started_event && stopped_event);

    nk_audio_bus parent_bus = NK_INVALID_HANDLE;
    nk_audio_bus_options bus_options{};
    bus_options.struct_size = sizeof(bus_options);
    bus_options.parent = NK_INVALID_HANDLE;
    assert(nk_audio_bus_create(&bus_options, &parent_bus) == NK_OK);
    nk_audio_bus bus = NK_INVALID_HANDLE;
    bus_options.parent = parent_bus;
    assert(nk_audio_bus_create(&bus_options, &bus) == NK_OK);
    nk_audio_bus queried_parent = NK_INVALID_HANDLE;
    assert(nk_audio_bus_get_parent(bus, &queried_parent) == NK_OK && queried_parent == parent_bus);
    assert(nk_audio_bus_set_parent(bus, bus) == NK_ERROR_INVALID_REQUEST);
    assert(nk_audio_bus_set_parent(bus, NK_INVALID_HANDLE) == NK_OK);
    assert(nk_audio_bus_get_parent(bus, &queried_parent) == NK_OK &&
           queried_parent == NK_INVALID_HANDLE);
    assert(nk_audio_bus_set_parent(bus, parent_bus) == NK_OK);
    nk_audio_bus grandchild_bus = NK_INVALID_HANDLE;
    bus_options.parent = bus;
    assert(nk_audio_bus_create(&bus_options, &grandchild_bus) == NK_OK);
    assert(nk_audio_bus_set_parent(parent_bus, grandchild_bus) == NK_ERROR_INVALID_REQUEST);
    assert(nk_audio_bus_set_volume(bus, 0.5f) == NK_OK);
    float value = 0;
    assert(nk_audio_bus_get_volume(bus, &value) == NK_OK && value == 0.5f);
    nk_bool state = 0;
    assert(nk_audio_bus_set_muted(bus, 1) == NK_OK);
    assert(nk_audio_bus_is_muted(bus, &state) == NK_OK && state == 1);
    assert(nk_audio_bus_set_muted(bus, 0) == NK_OK);
    assert(nk_audio_bus_is_muted(bus, &state) == NK_OK && state == 0);

    nk_audio_mix_snapshot base_snapshot = NK_INVALID_HANDLE;
    nk_audio_mix_snapshot duck_snapshot = NK_INVALID_HANDLE;
    assert(nk_audio_mix_snapshot_create(&base_snapshot) == NK_OK);
    assert(nk_audio_mix_snapshot_get_bus_count(base_snapshot, &device_count) == NK_OK &&
           device_count == 0);
    assert(nk_audio_mix_snapshot_capture_bus(base_snapshot, bus) == NK_OK);
    assert(nk_audio_mix_snapshot_capture_bus(base_snapshot, parent_bus) == NK_OK);
    assert(nk_audio_mix_snapshot_get_bus_count(base_snapshot, &device_count) == NK_OK &&
           device_count == 2);
    assert(nk_audio_mix_snapshot_create(&duck_snapshot) == NK_OK);
    assert(nk_audio_mix_snapshot_set_bus(duck_snapshot, bus, 0.1f, 1) == NK_OK);
    assert(nk_audio_mix_snapshot_get_bus_count(duck_snapshot, &device_count) == NK_OK &&
           device_count == 1);
    assert(nk_audio_mix_snapshot_apply(duck_snapshot, 0) == NK_OK);
    assert(nk_audio_bus_get_volume(bus, &value) == NK_OK && value == 0.1f);
    assert(nk_audio_bus_is_muted(bus, &state) == NK_OK && state == 1);
    assert(nk_audio_mix_snapshot_apply(base_snapshot, 0) == NK_OK);
    assert(nk_audio_bus_get_volume(bus, &value) == NK_OK && value == 0.5f);
    assert(nk_audio_bus_is_muted(bus, &state) == NK_OK && state == 0);
    assert(nk_audio_mix_snapshot_set_bus(duck_snapshot, bus, 0.2f, 0) == NK_OK);
    uint64_t snapshot_time_frames = 0;
    assert(nk_audio_get_time_pcm_frames(&snapshot_time_frames) == NK_OK);
    assert(nk_audio_mix_snapshot_apply_at(duck_snapshot, 1, snapshot_time_frames + 1) == NK_OK);
    assert(nk_audio_mix_snapshot_remove_bus(duck_snapshot, bus) == NK_OK);
    assert(nk_audio_mix_snapshot_clear(base_snapshot) == NK_OK);
    assert(nk_audio_mix_snapshot_get_bus_count(base_snapshot, &device_count) == NK_OK &&
           device_count == 0);
    assert(nk_audio_mix_snapshot_destroy(duck_snapshot) == NK_OK);
    assert(nk_audio_mix_snapshot_destroy(base_snapshot) == NK_OK);

    nk_audio_bus_effect low_pass = NK_INVALID_HANDLE;
    nk_audio_bus_effect high_pass = NK_INVALID_HANDLE;
    nk_audio_bus_effect delay = NK_INVALID_HANDLE;
    assert(nk_audio_bus_effect_create_low_pass(bus, 2000.0f, 2, &low_pass) == NK_OK);
    assert(nk_audio_bus_effect_create_high_pass(bus, 100.0f, 1, &high_pass) == NK_OK);
    assert(nk_audio_bus_effect_create_delay(bus, 64, 0.5f, &delay) == NK_OK);
    nk_audio_effect_type effect_type = NK_AUDIO_EFFECT_DELAY;
    assert(nk_audio_bus_effect_get_type(low_pass, &effect_type) == NK_OK &&
           effect_type == NK_AUDIO_EFFECT_LOW_PASS);
    assert(nk_audio_bus_effect_get_type(high_pass, &effect_type) == NK_OK &&
           effect_type == NK_AUDIO_EFFECT_HIGH_PASS);
    assert(nk_audio_bus_effect_get_type(delay, &effect_type) == NK_OK &&
           effect_type == NK_AUDIO_EFFECT_DELAY);
    uint32_t effect_position = 99;
    assert(nk_audio_bus_effect_get_position(low_pass, &effect_position) == NK_OK &&
           effect_position == 0);
    assert(nk_audio_bus_effect_set_position(high_pass, 0) == NK_OK);
    assert(nk_audio_bus_effect_get_position(high_pass, &effect_position) == NK_OK &&
           effect_position == 0);
    assert(nk_audio_bus_effect_set_position(delay, 1) == NK_OK);
    assert(nk_audio_bus_effect_get_position(delay, &effect_position) == NK_OK &&
           effect_position == 1);
    assert(nk_audio_bus_effect_set_enabled(delay, 0) == NK_OK);
    assert(nk_audio_bus_effect_is_enabled(delay, &state) == NK_OK && state == 0);
    assert(nk_audio_bus_effect_set_enabled(delay, 1) == NK_OK);
    assert(nk_audio_bus_effect_set_low_pass(low_pass, 4000.0f, 4) == NK_OK);
    float cutoff_frequency = 0;
    uint32_t filter_order = 0;
    assert(nk_audio_bus_effect_get_low_pass(low_pass, &cutoff_frequency, &filter_order) == NK_OK);
    assert(cutoff_frequency == 4000.0f && filter_order == 4);
    assert(nk_audio_bus_effect_set_high_pass(high_pass, 250.0f, 2) == NK_OK);
    assert(nk_audio_bus_effect_get_high_pass(high_pass, &cutoff_frequency, &filter_order) == NK_OK);
    assert(cutoff_frequency == 250.0f && filter_order == 2);
    assert(nk_audio_bus_effect_set_low_pass(high_pass, 250.0f, 2) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_bus_effect_set_delay_wet(delay, 0.25f) == NK_OK);
    assert(nk_audio_bus_effect_get_delay_wet(delay, &value) == NK_OK && value == 0.25f);
    assert(nk_audio_bus_effect_set_delay_dry(delay, 0.75f) == NK_OK);
    assert(nk_audio_bus_effect_get_delay_dry(delay, &value) == NK_OK && value == 0.75f);
    assert(nk_audio_bus_effect_set_delay_decay(delay, 0.5f) == NK_OK);
    assert(nk_audio_bus_effect_get_delay_decay(delay, &value) == NK_OK && value == 0.5f);
    assert(nk_audio_bus_effect_set_delay_wet(low_pass, 0.5f) == NK_ERROR_INVALID_ARGUMENT);
    nk_audio_bus_effect invalid_effect = NK_INVALID_HANDLE;
    assert(nk_audio_bus_effect_create_low_pass(bus, 0.0f, 2, &invalid_effect) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_bus_effect_create_low_pass(bus, 2000.0f, 0, &invalid_effect) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_bus_effect_create_delay(bus, 0, 0.5f, &invalid_effect) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_bus_effect_set_delay_decay(delay, 1.5f) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_bus_effect_destroy(high_pass) == NK_OK);
    assert(nk_audio_bus_effect_destroy(delay) == NK_OK);

    nk_audio_vec3 listener_position{4.0f, 2.0f, -3.0f};
    nk_audio_vec3 listener_direction{0.0f, 0.0f, -1.0f};
    nk_audio_vec3 listener_velocity{1.0f, 0.0f, 0.0f};
    nk_audio_vec3 listener_world_up{0.0f, 1.0f, 0.0f};
    nk_audio_vec3 listener_value{};
    assert(nk_audio_listener_set_position(listener_position) == NK_OK);
    assert(nk_audio_listener_get_position(&listener_value) == NK_OK);
    assert(listener_value.x == listener_position.x && listener_value.y == listener_position.y &&
           listener_value.z == listener_position.z);
    assert(nk_audio_listener_set_direction(listener_direction) == NK_OK);
    assert(nk_audio_listener_get_direction(&listener_value) == NK_OK);
    assert(listener_value.x == listener_direction.x && listener_value.y == listener_direction.y &&
           listener_value.z == listener_direction.z);
    assert(nk_audio_listener_set_velocity(listener_velocity) == NK_OK);
    assert(nk_audio_listener_get_velocity(&listener_value) == NK_OK);
    assert(listener_value.x == listener_velocity.x && listener_value.y == listener_velocity.y &&
           listener_value.z == listener_velocity.z);
    assert(nk_audio_listener_set_world_up(listener_world_up) == NK_OK);
    assert(nk_audio_listener_get_world_up(&listener_value) == NK_OK);
    assert(listener_value.x == listener_world_up.x && listener_value.y == listener_world_up.y &&
           listener_value.z == listener_world_up.z);
    assert(nk_audio_listener_set_cone(0.5f, 1.5f, 0.25f) == NK_OK);
    float inner_angle = 0;
    float outer_angle = 0;
    float outer_gain = 0;
    assert(nk_audio_listener_get_cone(&inner_angle, &outer_angle, &outer_gain) == NK_OK);
    assert(inner_angle == 0.5f && outer_angle == 1.5f && outer_gain == 0.25f);
    assert(nk_audio_listener_set_speed_of_sound(340.0f) == NK_OK);
    assert(nk_audio_listener_get_speed_of_sound(&value) == NK_OK && value == 340.0f);
    assert(nk_audio_listener_set_direction({0.0f, 0.0f, 0.0f}) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_listener_set_position({0.0f, 0.0f, 0.0f}) == NK_OK);

    const nk_result clip_result =
        nk_audio_clip_create_from_memory(tiny_wav, sizeof(tiny_wav), &clip);
    if (clip_result == NK_ERROR_UNSUPPORTED) {
        nk_audio_bus_destroy(bus);
        nk_shutdown();
        return 77;
    }
    assert(clip_result == NK_OK);

    nk_audio_clip_load_state clip_load_state = NK_AUDIO_CLIP_LOADING;
    assert(nk_audio_clip_get_load_state(clip, &clip_load_state) == NK_OK);
    assert(clip_load_state == NK_AUDIO_CLIP_READY);

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

    nk_audio_bus_concurrency_options concurrency{};
    concurrency.struct_size = sizeof(concurrency);
    concurrency.max_voices = 1;
    concurrency.steal_policy = NK_AUDIO_VOICE_STEAL_NONE;
    concurrency.virtualize = 0;
    assert(nk_audio_bus_set_concurrency(bus, &concurrency) == NK_OK);
    nk_audio_bus_concurrency_options queried_concurrency{};
    queried_concurrency.struct_size = sizeof(queried_concurrency);
    assert(nk_audio_bus_get_concurrency(bus, &queried_concurrency) == NK_OK);
    assert(queried_concurrency.max_voices == 1 &&
           queried_concurrency.steal_policy == NK_AUDIO_VOICE_STEAL_NONE &&
           queried_concurrency.virtualize == 0);
    queried_concurrency.struct_size = 0;
    assert(nk_audio_bus_get_concurrency(bus, &queried_concurrency) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_voice_set_priority(first_voice, 1) == NK_OK);
    assert(nk_audio_voice_set_priority(second_voice, 2) == NK_OK);
    uint32_t priority = 0;
    assert(nk_audio_voice_get_priority(second_voice, &priority) == NK_OK && priority == 2);
    assert(nk_audio_voice_start(first_voice) == NK_OK);
    assert(nk_audio_voice_start(second_voice) == NK_ERROR_INVALID_REQUEST);
    assert(nk_audio_voice_stop(first_voice) == NK_OK);
    assert(nk_audio_voice_start(second_voice) == NK_OK);
    concurrency.steal_policy = NK_AUDIO_VOICE_STEAL_LOWEST_PRIORITY;
    assert(nk_audio_bus_set_concurrency(bus, &concurrency) == NK_OK);
    assert(nk_audio_voice_stop(second_voice) == NK_OK);
    assert(nk_audio_voice_start(first_voice) == NK_OK);
    assert(nk_audio_voice_start(second_voice) == NK_OK);
    assert(poll_audio_voice_event(NK_EVENT_AUDIO_VOICE_STOLEN, first_voice));
    assert(nk_audio_voice_is_playing(first_voice, &state) == NK_OK && state == 0);
    assert(nk_audio_voice_is_playing(second_voice, &state) == NK_OK && state == 1);
    concurrency.steal_policy = NK_AUDIO_VOICE_STEAL_NONE;
    concurrency.virtualize = 1;
    assert(nk_audio_bus_set_concurrency(bus, &concurrency) == NK_OK);
    assert(nk_audio_voice_stop(second_voice) == NK_OK);
    assert(nk_audio_voice_start(first_voice) == NK_OK);
    assert(nk_audio_voice_start(second_voice) == NK_OK);
    assert(poll_audio_voice_event(NK_EVENT_AUDIO_VOICE_VIRTUALIZED, second_voice));
    assert(nk_audio_voice_is_virtualized(second_voice, &state) == NK_OK && state == 1);
    assert(nk_audio_voice_is_playing(second_voice, &state) == NK_OK && state == 1);
    assert(nk_audio_voice_stop(first_voice) == NK_OK);
    assert(poll_audio_voice_event(NK_EVENT_AUDIO_VOICE_RESUMED, second_voice));
    assert(nk_audio_voice_is_virtualized(second_voice, &state) == NK_OK && state == 0);
    assert(nk_audio_voice_is_playing(second_voice, &state) == NK_OK && state == 1);
    concurrency.max_voices = 0;
    concurrency.virtualize = 0;
    assert(nk_audio_bus_set_concurrency(bus, &concurrency) == NK_OK);
    assert(nk_audio_voice_stop(second_voice) == NK_OK);

    nk_audio_vec3 source_position{8.0f, -1.0f, -6.0f};
    nk_audio_vec3 source_direction{0.0f, 0.0f, 1.0f};
    nk_audio_vec3 source_velocity{-2.0f, 0.0f, 0.5f};
    nk_audio_vec3 source_value{};
    assert(nk_audio_voice_set_spatialization_enabled(first_voice, 1) == NK_OK);
    assert(nk_audio_voice_is_spatialization_enabled(first_voice, &state) == NK_OK && state == 1);
    assert(nk_audio_voice_set_position(first_voice, source_position) == NK_OK);
    assert(nk_audio_voice_get_position(first_voice, &source_value) == NK_OK);
    assert(source_value.x == source_position.x && source_value.y == source_position.y &&
           source_value.z == source_position.z);
    assert(nk_audio_voice_set_direction(first_voice, source_direction) == NK_OK);
    assert(nk_audio_voice_get_direction(first_voice, &source_value) == NK_OK);
    assert(source_value.x == source_direction.x && source_value.y == source_direction.y &&
           source_value.z == source_direction.z);
    assert(nk_audio_voice_set_velocity(first_voice, source_velocity) == NK_OK);
    assert(nk_audio_voice_get_velocity(first_voice, &source_value) == NK_OK);
    assert(source_value.x == source_velocity.x && source_value.y == source_velocity.y &&
           source_value.z == source_velocity.z);
    nk_audio_attenuation_model attenuation_model = NK_AUDIO_ATTENUATION_NONE;
    assert(nk_audio_voice_set_attenuation_model(first_voice, NK_AUDIO_ATTENUATION_LINEAR) == NK_OK);
    assert(nk_audio_voice_get_attenuation_model(first_voice, &attenuation_model) == NK_OK &&
           attenuation_model == NK_AUDIO_ATTENUATION_LINEAR);
    nk_audio_positioning positioning = NK_AUDIO_POSITIONING_ABSOLUTE;
    assert(nk_audio_voice_set_positioning(first_voice, NK_AUDIO_POSITIONING_RELATIVE) == NK_OK);
    assert(nk_audio_voice_get_positioning(first_voice, &positioning) == NK_OK &&
           positioning == NK_AUDIO_POSITIONING_RELATIVE);
    assert(nk_audio_voice_set_rolloff(first_voice, 0.75f) == NK_OK);
    assert(nk_audio_voice_get_rolloff(first_voice, &value) == NK_OK && value == 0.75f);
    assert(nk_audio_voice_set_gain_limits(first_voice, 0.1f, 0.8f) == NK_OK);
    float min_gain = 0;
    float max_gain = 0;
    assert(nk_audio_voice_get_gain_limits(first_voice, &min_gain, &max_gain) == NK_OK);
    assert(min_gain == 0.1f && max_gain == 0.8f);
    assert(nk_audio_voice_set_distance_limits(first_voice, 2.0f, 100.0f) == NK_OK);
    float min_distance = 0;
    float max_distance = 0;
    assert(nk_audio_voice_get_distance_limits(first_voice, &min_distance, &max_distance) == NK_OK);
    assert(min_distance == 2.0f && max_distance == 100.0f);
    assert(nk_audio_voice_set_doppler_factor(first_voice, 0.5f) == NK_OK);
    assert(nk_audio_voice_get_doppler_factor(first_voice, &value) == NK_OK && value == 0.5f);
    assert(nk_audio_voice_set_cone(first_voice, 0.25f, 1.25f, 0.2f) == NK_OK);
    assert(nk_audio_voice_get_cone(first_voice, &inner_angle, &outer_angle, &outer_gain) == NK_OK);
    assert(inner_angle == 0.25f && outer_angle == 1.25f && outer_gain == 0.2f);
    assert(nk_audio_voice_set_directional_attenuation_factor(first_voice, 0.4f) == NK_OK);
    assert(nk_audio_voice_get_directional_attenuation_factor(first_voice, &value) == NK_OK &&
           value == 0.4f);
    assert(nk_audio_voice_set_direction(first_voice, {0.0f, 0.0f, 0.0f}) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_voice_set_attenuation_model(first_voice, 99) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_voice_set_gain_limits(first_voice, 1.0f, 0.5f) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_voice_set_distance_limits(first_voice, 2.0f, 1.0f) ==
           NK_ERROR_INVALID_ARGUMENT);
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

    std::string resource_uri = "file://";
#if defined(_WIN32)
    resource_uri += '/';
    for (auto character : async_path)
        resource_uri += character == '\\' ? '/' : character;
#else
    resource_uri += async_path;
#endif
    nk_resource resource{};
    resource.struct_size = sizeof(resource);
    resource.flags = NK_RESOURCE_READABLE;
    resource.uri = resource_uri.c_str();
    resource.mime_type = "audio/wav";
    resource.display_name = "nativekit-audio-async-test.wav";
    nk_audio_clip unsupported_async_clip = NK_INVALID_HANDLE;
    nk_request_id unsupported_async_request = 123;
    assert(nk_audio_clip_create_from_resource_async(&resource, &unsupported_async_clip,
                                                    &unsupported_async_request) ==
           NK_ERROR_UNSUPPORTED);
    assert(unsupported_async_clip == NK_INVALID_HANDLE);
    assert(unsupported_async_request == NK_INVALID_REQUEST_ID);
    nk_audio_clip resource_clip = NK_INVALID_HANDLE;
    assert(nk_audio_clip_create_from_resource(&resource, &resource_clip) == NK_OK);
    voice_options.bus = NK_INVALID_HANDLE;
    voice_options.flags = 0;
    nk_audio_voice resource_voice = NK_INVALID_HANDLE;
    assert(nk_audio_voice_create(resource_clip, &voice_options, &resource_voice) == NK_OK);
    nk_audio_voice resource_second_voice = NK_INVALID_HANDLE;
    assert(nk_audio_voice_create(resource_clip, &voice_options, &resource_second_voice) == NK_OK);
    assert(nk_audio_voice_get_load_state(resource_voice, &load_state) == NK_OK);
    assert(load_state == NK_AUDIO_VOICE_READY);
    voice_options.flags = NK_AUDIO_VOICE_ASYNC;
    assert(nk_audio_voice_create(resource_clip, &voice_options, &voice) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_voice_destroy(resource_voice) == NK_OK);
    assert(nk_audio_voice_destroy(resource_second_voice) == NK_OK);
    assert(nk_audio_clip_destroy(resource_clip) == NK_OK);

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
    assert(nk_audio_bus_destroy(grandchild_bus) == NK_OK);
    assert(nk_audio_bus_destroy(bus) == NK_OK);
    assert(nk_audio_bus_destroy(parent_bus) == NK_OK);
    assert(nk_audio_bus_effect_get_type(low_pass, &effect_type) == NK_ERROR_INVALID_HANDLE);
    assert(nk_audio_bus_effect_destroy(low_pass) == NK_ERROR_INVALID_HANDLE);

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
