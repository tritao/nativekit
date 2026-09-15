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
and stop controls. Audio calls are UI-thread-only; miniaudio owns the device
and audio callback thread.

Non-looping voices emit `NK_EVENT_AUDIO_VOICE_COMPLETE` when playback reaches
the natural end. The event is queued from miniaudio's audio callback and must
be consumed on the NativeKit UI thread with `nk_poll_event()`. Haxe clients
receive it as `AudioVoiceComplete` through `NativeKitEvents.listen()`.

The module also provides a generated Haxeon ABI interface in
`bindings/nativekit-audio.hxi` and a small typed Haxe facade under
`bindings/haxe/nativekit/audio`. The public facade consists of `Clip`, `Voice`,
`VoiceOptions`, `Bus`, and `Mixer`.

With Haxeon available, run the native and managed smoke test with:

```sh
HAXEON_DIR=/path/to/realtime-haxe modules/audio/tools/test-haxeon.sh
```
