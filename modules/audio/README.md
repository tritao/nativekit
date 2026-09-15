# NativeKit audio module

The optional audio module provides NativeKit's C ABI for sound playback and
mixing on top of the pinned miniaudio submodule. Enable it with
`-DNK_BUILD_AUDIO=ON` after initializing `vendor/miniaudio`.

The first API slice supports WAV, FLAC, and MP3 playback from native filesystem
paths, provider-backed URI resources, or caller-provided encoded memory.
`nk_audio_clip` owns a reusable source, while each `nk_audio_voice` has
independent transport and voice controls, so a single clip can play
simultaneously through multiple voices. The original one-shot path is
intentionally expressed by creating a clip and one voice.

`nk_audio_clip_create_from_resource()` accepts the same top-level `nk_resource`
descriptor used by NativeKit dialogs, drops, shares, and clipboard APIs. The
clip retains the URI, while every voice opens its own provider stream and keeps
its decoder attached to that stream. This preserves reusable clips and allows
Android content URIs or other platform resource providers without converting
them to filesystem paths. Resource-backed clips are opened synchronously and
therefore reject `NK_AUDIO_VOICE_ASYNC`.

`nk_audio_clip_create_from_resource_async()` is the asynchronous counterpart
for providers such as browser fetch. It returns a real clip handle immediately
in `NK_AUDIO_CLIP_LOADING` together with a request ID. NativeKit consumes the
matching raw resource completion internally, validates and retains the encoded
audio bytes, then emits `NK_EVENT_AUDIO_CLIP_READY` or
`NK_EVENT_AUDIO_CLIP_LOAD_FAILED`. Query `nk_audio_clip_get_load_state()` and
create voices only after the clip is ready. Providers without asynchronous
loading support return `NK_ERROR_UNSUPPORTED`. Resource-backed voices are
already decoder-backed streams; `NK_AUDIO_VOICE_STREAM` is optional for them
and does not change the provider stream lifetime. Destroying a pending async
clip requests cancellation of its underlying resource load and suppresses any
late completion event.

The process-wide playback device can be enumerated with
`nk_audio_device_get_count()`, `nk_audio_device_get_name()`, and
`nk_audio_device_is_default()`. Apply `nk_audio_device_options` with
`nk_audio_device_configure()` before the first audio API that initializes the
engine; the selected device, output format, period, and automatic-start policy
are then fixed for that runtime generation. Device indices are snapshots, so
applications should enumerate immediately before choosing one. Configuration
after engine initialization returns `NK_ERROR_INVALID_REQUEST`; call
`nk_shutdown()` before configuring a new device generation.

`nk_audio_device_start()`, `nk_audio_device_stop()`, and
`nk_audio_device_restart()` control the device without destroying the mixer
graph. `nk_audio_device_get_state()` exposes the transition state and backend
interruptions. Miniaudio notification callbacks are translated into the
non-droppable `NK_EVENT_AUDIO_DEVICE_STARTED`, `NK_EVENT_AUDIO_DEVICE_STOPPED`,
`NK_EVENT_AUDIO_DEVICE_REROUTED`, `NK_EVENT_AUDIO_DEVICE_INTERRUPTION_BEGAN`,
and `NK_EVENT_AUDIO_DEVICE_INTERRUPTION_ENDED` events. These events are global
and therefore have `NK_INVALID_HANDLE` as their source. Backends do not report
every notification type, so applications should treat restart as an explicit
recovery hook rather than assuming every physical device loss is observable.

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

Buses can also own ordered effect chains. `Bus.addLowPass()`,
`Bus.addHighPass()`, and `Bus.addDelay()` append effects that can be bypassed,
reordered, and reconfigured through `BusEffect`. Filter cutoffs must be below
Nyquist with orders from 1 through 8; delay frames must be positive, and delay
wet, dry, and decay gains are in the [0, 1] range.

Spatialized voices use a right-handed OpenGL-style coordinate system: +X is
right, +Y is up, and -Z is forward. `Mixer` exposes the single process-wide
listener's position, orientation, velocity, cone, and speed of sound. Voices
expose their own position, direction, velocity, absolute/relative positioning,
distance attenuation model, rolloff, gain and distance limits, Doppler factor,
and directional cone. Spatialization is enabled by default per voice and can
  be toggled at runtime; configure the listener and source transforms before
  playback. Vector directions must be finite and non-zero, cone angles are
  radians, and gains are linear [0, 1].

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
`AudioVoiceComplete` through `NativeKitEvents.listen()`. Device notifications
arrive as `AudioDeviceStarted`, `AudioDeviceStopped`, `AudioDeviceRerouted`,
`AudioDeviceInterruptionBegan`, or `AudioDeviceInterruptionEnded`. Asynchronous
clip loads arrive as `AudioClipReady` or `AudioClipLoadFailed`, including the
associated request ID.
Scheduled stops are transport operations and do not emit this natural-end
completion event.

The module also provides a generated Haxeon ABI interface in
`bindings/nativekit-audio.hxi` and a small typed Haxe facade under
`bindings/haxe/nativekit/audio`. The public facade consists of `Clip`, `Voice`,
`VoiceOptions`, `Bus`, `DeviceOptions`, and `Mixer`. `Mixer` exposes device
enumeration and lifecycle controls, while `Clip.fromResourceAsync()` exposes
the asynchronous resource path; `loadRequest()`, `loadState()`, and `isReady()`
mirror the native lifecycle.

With Haxeon available, run the native and managed smoke test with:

```sh
HAXEON_DIR=/path/to/realtime-haxe modules/audio/tools/test-haxeon.sh
```
