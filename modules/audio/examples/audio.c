#include "nativekit_audio.h"
#include "nativekit_time.h"

#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s AUDIO_FILE\n", argv[0]);
        return 2;
    }

    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK) {
        fprintf(stderr, "nk_init failed: %s\n", nk_last_error());
        return 1;
    }

    nk_audio_clip clip = NK_INVALID_HANDLE;
    const nk_result load_result = nk_audio_clip_create_from_file(argv[1], &clip);
    if (load_result != NK_OK) {
        fprintf(stderr, "audio load failed: %s\n", nk_last_error());
        nk_shutdown();
        return 1;
    }
    nk_audio_voice voice = NK_INVALID_HANDLE;
    if (nk_audio_voice_create(clip, NULL, &voice) != NK_OK) {
        fprintf(stderr, "audio voice creation failed: %s\n", nk_last_error());
        nk_audio_clip_destroy(clip);
        nk_shutdown();
        return 1;
    }
    nk_audio_clip_destroy(clip);
    if (nk_audio_voice_start(voice) != NK_OK) {
        fprintf(stderr, "audio start failed: %s\n", nk_last_error());
        nk_audio_voice_destroy(voice);
        nk_shutdown();
        return 1;
    }

    printf("Playing %s for up to 10 seconds...\n", argv[1]);
    const double deadline = nk_time_seconds() + 10.0;
    while (nk_time_seconds() < deadline) {
        nk_bool at_end = 0;
        if (nk_audio_voice_at_end(voice, &at_end) != NK_OK || at_end)
            break;
        nk_wait_events_timeout(0.05);
        nk_event event = {0};
        event.struct_size = sizeof(event);
        while (nk_poll_event(&event) == NK_OK && event.kind != NK_EVENT_NONE)
            nk_event_release(&event);
    }

    nk_audio_voice_destroy(voice);
    nk_shutdown();
    return 0;
}
