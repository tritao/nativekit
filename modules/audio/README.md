# NativeKit audio module

The optional audio module provides NativeKit's C ABI for sound playback and
mixing on top of the pinned miniaudio submodule. Enable it with
`-DNK_BUILD_AUDIO=ON` after initializing `vendor/miniaudio`.

The first API slice supports WAV, FLAC, and MP3 playback from native filesystem
paths, cached URI assets, or caller-provided encoded memory.
`nk_audio_clip` owns a reusable source, while each `nk_audio_voice` has
independent transport and voice controls, so a single clip can play
simultaneously through multiple voices. The original one-shot path is
intentionally expressed by creating a clip and one voice.

Use the core `ResourceCache` to load a top-level `nk_resource` descriptor. Once
its `nk_resource_asset` is ready, pass that handle to
`nk_audio_clip_create_from_asset()` (or Haxe `Clip.fromAsset()`). Audio retains
the cache's immutable encoded byte storage, so the asset and cache may be
released after clip creation. Resource cache ready and failure events are the
single loading lifecycle for URI-backed audio; attempting to create a clip
before the asset is ready returns `NK_ERROR_INVALID_REQUEST`.

For music and other large resources, use `nk_audio_clip_create_from_stream()`
(or Haxe `Clip.fromStream()`). This validates the resource synchronously but
does not retain its complete encoded contents. Each voice opens an independent
provider stream and decoder, so the same streaming clip can be played by
multiple voices without sharing stream position. Provider reads happen through
NativeKit's bounded core worker pool and the audio callback only consumes the
decoded PCM ring. `NK_AUDIO_VOICE_ASYNC` reports readiness when the first
decoded page is available; provider read and seek failures emit
`NK_EVENT_AUDIO_VOICE_STREAM_FAILED` with the provider's `nk_result`.

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
through generation-checked mixer buses. Buses form a directed tree: each bus
can route through another bus or directly to the master endpoint. A bus's
volume, mute state, and effects therefore apply to all descendant voices.
Reparenting rejects cycles and cross-engine buses. Destroying a parent handle
does not invalidate descendant audio; the shared native parent remains alive
while descendants still reference it.

Each bus supports volume, mute, start, and stop controls. The mixer exposes a
process-wide PCM-frame clock and sample rate. Voices can be scheduled against
that clock, and can apply immediate or scheduled linear fades. Scheduling a
start still requires an explicit `nk_audio_voice_start()` call;
`nk_audio_voice_clear_schedule()` removes pending start, stop, and fade
transitions. Audio calls are UI-thread-only; miniaudio owns the device and
audio callback thread.

Buses support the same schedule and fade operations for all routed voices. This
allows music transitions and voice/SFX ducking to be coordinated at the mixer
boundary instead of issuing one operation per voice.

Voice concurrency is configured per bus subtree with
`nk_audio_bus_set_concurrency()`. A non-zero `max_voices` counts all currently
playing voices routed through that bus or any descendant. New voices carry a
priority (larger values win); `OLDEST`, `QUIETEST`, and `LOWEST_PRIORITY`
policies can reclaim an eligible equal- or lower-priority voice. With
`STEAL_NONE`, a full bus returns `NK_ERROR_INVALID_REQUEST` unless
`virtualize` is enabled. Virtualized voices remain logically playing, advance
against the process-wide audio clock, and are promoted automatically when a
slot opens; `nk_audio_voice_is_virtualized()` exposes that state. Stopping a
voice or a bus immediately makes room for the highest-priority virtual voice.
Concurrency transitions emit `NK_EVENT_AUDIO_VOICE_STOLEN`,
`NK_EVENT_AUDIO_VOICE_VIRTUALIZED`, and `NK_EVENT_AUDIO_VOICE_RESUMED`; each
uses the affected voice in `source` and has no payload. A stolen voice does
not emit natural-end completion.

`nk_audio_mix_snapshot` stores explicit bus volume and mute targets. Capture a
bus's current mix with `nk_audio_mix_snapshot_capture_bus()`, or define a
target directly with `nk_audio_mix_snapshot_set_bus()`, then apply the snapshot
immediately or over a shared PCM-frame fade. `nk_audio_mix_snapshot_apply_at()`
schedules the transition against the same process-wide clock. Keeping a base
snapshot and a lower-volume target snapshot makes dialogue ducking, pause-menu
mixes, and cutscene transitions reversible without manually restoring each bus.
Snapshots reference buses weakly; destroying a bus removes its target when the
snapshot is queried or applied.

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
`AudioVoiceComplete`, `AudioVoiceStolen`, `AudioVoiceVirtualized`,
`AudioVoiceResumed`, or `AudioVoiceStreamFailed` through
`NativeKitEvents.listen()`. Device notifications arrive as
`AudioDeviceStarted`, `AudioDeviceStopped`, `AudioDeviceRerouted`,
`AudioDeviceInterruptionBegan`, or `AudioDeviceInterruptionEnded`.
Scheduled stops are transport operations and do not emit this natural-end
completion event.

The module also provides a generated Haxeon ABI interface in
`bindings/nativekit-audio.hxi` and a small typed Haxe facade under
`bindings/haxe/nativekit/audio`. The public facade consists of `Clip`, `Voice`, `VoiceOptions`,
`Bus`, `BusConcurrencyOptions`, `MixSnapshot`, `DeviceOptions`, and `Mixer`. `Mixer` exposes device
enumeration and lifecycle controls, while `Clip.fromAsset()` consumes a ready
`nativekit.resource.ResourceAsset` from the core cache and `Clip.fromStream()`
creates an incremental source for large resources.

With Haxeon available, run the native and managed smoke test with:

```sh
HAXEON_DIR=/path/to/realtime-haxe modules/audio/tools/test-haxeon.sh
```
