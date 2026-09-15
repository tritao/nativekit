# NativeKit audio module

The optional audio module provides NativeKit's C ABI for sound playback and
mixing on top of the pinned miniaudio submodule. Enable it with
`-DNK_BUILD_AUDIO=ON` after initializing `vendor/miniaudio`.

The first API slice supports WAV, FLAC, and MP3 playback from native filesystem
paths or caller-provided encoded memory. `nk_audio_clip` owns a reusable source,
while each `nk_audio_voice` has independent transport and sound controls, so a
single clip can play simultaneously through multiple voices. The original
`nk_audio_sound_*` API remains available for standalone sounds.

Sounds and voices are generation-checked NativeKit resources and can be routed
through generation-checked mixer buses. Each bus supports volume, mute, start,
and stop controls. Audio calls are UI-thread-only; miniaudio owns the device
and audio callback thread.

The module also provides a generated Haxeon ABI interface in
`bindings/nativekit-audio.hxi` and a small typed Haxe facade under
`bindings/haxe/nativekit/audio`.

With Haxeon available, run the native and managed smoke test with:

```sh
HAXEON_DIR=/path/to/realtime-haxe modules/audio/tools/test-haxeon.sh
```
