# NativeKit audio module

The optional audio module provides NativeKit's C ABI for sound playback and
mixing on top of the pinned miniaudio submodule. Enable it with
`-DNK_BUILD_AUDIO=ON` after initializing `vendor/miniaudio`.

The first API slice supports WAV, FLAC, and MP3 playback from native filesystem
paths or caller-provided encoded memory. `nk_audio_clip` owns a reusable source,
while each `nk_audio_voice` has independent transport and voice controls, so a
single clip can play simultaneously through multiple voices. The original
one-shot path is intentionally expressed by creating a clip and one voice.

Sounds and voices are generation-checked NativeKit resources and can be routed
through generation-checked mixer buses. Each bus supports volume, mute, start,
and stop controls. The mixer exposes a process-wide PCM-frame clock and sample
rate. Voices can be scheduled against that clock, and can apply immediate or
scheduled linear fades. Scheduling a start still requires an explicit
`nk_audio_voice_start()` call; `nk_audio_voice_clear_schedule()` removes pending
start, stop, and fade transitions. Audio calls are UI-thread-only; miniaudio
owns the device and audio callback thread.

Buses support the same schedule and fade operations for all routed voices. This
allows music transitions and voice/SFX ducking to be coordinated at the mixer
boundary instead of issuing one operation per voice.

File voices created with `NK_AUDIO_VOICE_ASYNC` expose an explicit loading
lifecycle. `nk_audio_voice_get_load_state()` returns `NK_AUDIO_VOICE_LOADING`
until the source reaches its readiness point, then returns
`NK_AUDIO_VOICE_READY`. A stream is ready when its first playable page is
available; a decoded async voice is ready when decoding has completed. If the
load fails, the state is `NK_AUDIO_VOICE_LOAD_FAILED` and
`NK_EVENT_AUDIO_VOICE_LOAD_FAILED` carries the mapped `nk_result` in the event's
`result` field. Ready and failed events are queued once and are not dropped when
the event queue is full. Synchronous voices start in the ready state.

Non-looping voices emit `NK_EVENT_AUDIO_VOICE_COMPLETE` when playback reaches
the natural end. The event is queued from miniaudio's audio callback and must
be consumed on the NativeKit UI thread with `nk_poll_event()`. Haxe clients
receive these as `AudioVoiceReady`, `AudioVoiceLoadFailed`, and
`AudioVoiceComplete` through `NativeKitEvents.listen()`.
Scheduled stops are transport operations and do not emit this natural-end
completion event.

The module also provides a generated Haxeon ABI interface in
`bindings/nativekit-audio.hxi` and a small typed Haxe facade under
`bindings/haxe/nativekit/audio`. The public facade consists of `Clip`, `Voice`,
`VoiceOptions`, `Bus`, and `Mixer`.

With Haxeon available, run the native and managed smoke test with:

```sh
HAXEON_DIR=/path/to/realtime-haxe modules/audio/tools/test-haxeon.sh
```
