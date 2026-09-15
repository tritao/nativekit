# NativeKit audio module

The optional audio module provides NativeKit's C ABI for sound playback and
mixing on top of the pinned miniaudio submodule. Enable it with
`-DNK_BUILD_AUDIO=ON` after initializing `vendor/miniaudio`.

The first API slice supports WAV, FLAC, and MP3 playback from native filesystem
paths or caller-provided encoded memory. Sound handles are generation-checked
NativeKit resources. Audio calls are UI-thread-only; miniaudio owns the device
and audio callback thread.
