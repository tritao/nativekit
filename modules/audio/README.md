# NativeKit audio module

The optional audio module provides NativeKit's C ABI for sound playback and
mixing on top of the pinned miniaudio submodule. Enable it with
`-DNK_BUILD_AUDIO=ON` after initializing `vendor/miniaudio`.

The primitive API is exposed by `nativekit_audio.h` and the
`NativeKit::audio` target. It owns device lifecycle, clips, voices, transport,
basic voice controls, spatialization, and the engine clock. The higher-level
mixer graph is exposed separately by `nativekit_audio_graph.h` and the
`NativeKit::audio_graph` target, which depends on `NativeKit::audio` and adds
buses, effects, snapshots, concurrency, virtualization, and global mix policy.
Both targets currently link the same NativeKit runtime and miniaudio backend.

The standalone DSP API is exposed by `nativekit_audio_dsp.h` and the
`NativeKit::audio_dsp` target. Create a device-independent `nk_audio_dsp_engine`,
create one or more instruments, submit frame-sorted note and parameter events,
and render interleaved float blocks into caller-owned memory. When live output
is needed, attach the renderer to the process-wide playback device with
`nk_audio_dsp_engine_attach_device()` and queue events against the shared PCM
clock with `nk_audio_dsp_engine_schedule()`. The initial backend provides
pitched sine, triangle, saw, and square oscillators, additive white noise, ADSR
envelopes, and an optional state-variable low-pass filter. Noise level, filter
cutoff, and filter resonance are sample-accurate instrument parameters; a zero
cutoff bypasses the filter. The backend reports these capabilities through
`nk_audio_dsp_engine_get_capabilities()`.
The first backend uses the pinned DaisySP oscillator, noise, ADSR, and SVF
modules; only the translation units needed by this slice are compiled. The
stable NativeKit ABI does not expose DaisySP types, so future DaisySP modules or
other native DSP implementations can be added without changing tracker code.
Wavetable sources are created with `nk_audio_dsp_wavetable_create()` from one
power-of-two source cycle (32 to 4096 float samples). NativeKit builds an
immutable harmonic-limited table bank, and the DaisySP wavetable oscillator
selects the highest safe band for each voice frequency with linear
interpolation. Patches retain their table resources, so a caller may dispose
the source handle immediately after patch creation. The Haxe facade exposes the
same lifecycle through `DspWavetable.fromSamples()` and
`DspWavetable.fromBytes()`.
Use `nk_audio_dsp_patch_create()` to build an immutable reusable patch from up
to four independently tuned oscillator or wavetable sources plus noise,
envelope, and filter components. Sources are mixed additively in array order;
their levels are linear and their tuning offsets are expressed in cents. The
Haxe builder exposes this as `DspPatchBuilder.oscillators` and
`addOscillator()`. Create one or more instruments with
`nk_audio_dsp_instrument_create_from_patch()`. Instruments copy the patch at
creation and remain independently automatable through the existing parameter
events. The legacy flat instrument options are retained as a migration path
while the patch model grows to include additional modulation sources and
destinations.

Patches now also contain one optional LFO and up to eight typed modulation
routes. Routes connect the LFO or the amplitude envelope to global pitch,
filter cutoff (Hz), relative amplitude, or a selected oscillator's pitch,
level, phase, or frequency. A route target of zero applies a control-source
oscillator destination to every source; non-zero targets are one-based
oscillator source indices. LFO routes can preserve their bipolar range or be
mapped to [0, 1]; envelope routes are naturally unipolar and can be centered
when a bipolar destination is useful. `RETRIGGER` resets the LFO phase on each
note-on, while `FREE_RUNNING` preserves phase across note-ons on the same live
voice. Modulation is evaluated per sample inside the voice, so tracker note and
automation timing stays sample-accurate without exposing DaisySP types through
the ABI.

Oscillator outputs can also be modulation sources. These audio-rate operator
routes require a specific one-based source and target oscillator and can add
frequency in Hz (FM) or normalized phase cycles (PM). The patch validator
topologically orders operators and rejects self-routes and cycles, so source
outputs are deterministic and never depend on route declaration order.

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

`AudioBank` provides a named Haxe catalog over a borrowed `ResourceCache` and
event pump. `load()` and `loadAsync()` register `AudioBankEntry` values, which
report loading, ready, and failure state and expose one-shot `onReady` and
`onFailed` callbacks. `AudioBankEntry.createClip()` returns an explicitly
caller-owned `Clip`; unloading the bank entry releases only its cache asset
view, so the clip can be composed into an `AudioCue` or `AudioTrack` and
managed independently. On native desktop backends, asynchronous local-file
URI loads run on NativeKit's bounded worker pool and complete through the same
resource cache events as web fetches.

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
playing voices routed through that bus or any descendant. Route a stopped voice
with `nk_audio_voice_set_bus()` and assign its priority with
`nk_audio_voice_set_priority()` before starting it; larger values win.
`OLDEST`, `QUIETEST`, and `LOWEST_PRIORITY` policies can reclaim an eligible
equal- or lower-priority voice. With
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

Live DSP now enters the miniaudio graph as a source node rather than being
added after the master output. After `DspEngine.attachToDevice()`, call
`setBus(musicBus)` to route it through a bus and its effects; `setBus(null)`
restores direct master routing. Routing changes preserve notes and scheduled
events. Destroying the routed bus restores master routing, and each new device
attachment starts at master. A stopped bus suspends pulls from the synth;
muting or reducing its gain keeps the source timeline running.

Buses apply their volume, mute, and fades after their ordered effect chains,
so these controls also affect effect tails. `Bus.addLowPass()`,
`Bus.addHighPass()`, and `Bus.addDelay()` append effects that can be bypassed,
reordered, and reconfigured through `BusEffect`. Filter cutoffs must be below
Nyquist with orders from 1 through 8; delay frames must be positive, and delay
wet, dry, and decay gains are in the [0, 1] range.

`Bus.addReverb()` adds a stereo algorithmic reverb, and `Bus.addDynamics()`
adds a compressor/expander/gate. They share a private block-processor boundary
with preallocated interleaved/planar buffers, audio-thread state/reset handling,
and smoothed atomic parameter targets. The backend uses Signalsmith Basics
from `tritao/signalsmith-basics`, pinned to the reverb reset/initialization fixes,
and Signalsmith DSP v1.7.1. DaisySP remains the synthesis backend. The embedded
STFX adapter's Boost license is installed alongside the MIT dependency notices.

```haxe
import haxeon.audio.EffectParameter;

var music = Bus.create();
var reverb = music.addReverb();
reverb.setParameter(EffectParameter.Wet, 0.2);
reverb.setParameter(EffectParameter.DecaySeconds, 2.4);
var dynamics = music.addDynamics();
dynamics.setParameter(EffectParameter.ThresholdDb, -20);
dynamics.setParameter(EffectParameter.Ratio, 4);
synth.attachToDevice();
synth.setBus(music);
```

Reverb requires two output channels. Its defaults are room 80 ms, -60 dB decay
3 seconds, wet 0.5 and dry 1. Supported ranges: wet/dry [0, 1], room [10, 200]
ms, decay [0.03, 90] seconds, cutoffs [10, Nyquist) Hz, damping multipliers
[1, 10], and early reflections [0, 2.5]. Dynamics defaults to neutral
compression/expansion, a -80 dB gate, no automatic makeup gain, and no lookahead.
Its ranges are compressor/expander thresholds [-60, 0] dB, compression ratio
[1, 100], attack [1, 50] ms, release [20, 250] ms, makeup [0, 20] dB, gate
threshold [-80, 0] dB, expansion ratio [1, 10], and mix [0, 1]. Unsupported
parameter/effect combinations and non-finite values are rejected.

`BusEffect.parameter()` reads the target value; `reset()` clears processor
state at the next block while preserving those targets. `latencyFrames()` and
`tailFrames()` report processor latency and an estimated -60 dB tail. Both
Signalsmith processors have zero algorithmic latency; reverb continues receiving
silence through the graph after its source ends. External sidechains and
sample-scheduled effect automation remain future additions.

`Bus.addSend(destination, volume)` creates an independent post-fader route to a
shared return bus. Each source supports eight sends in addition to its parent
route; gains are [0, 1], and cycles across parent and send routes are rejected.
Put a wet-only processor on the return (`Dry = 0`, `Wet = 1`) and control the
contribution with `BusSend.setVolume()`. Removing a send preserves return tails.
Destroying either endpoint invalidates the native send; its Haxe wrapper can
still be disposed safely. Sends are owned by their source's `Bus` wrapper.

`Bus.addStereoDelay()` adds NativeKit's stereo interpolated feedback delay.
`setDelayTempo(bpm, beats)` sets quarter-note beat length and tempo together;
the effective delay must fit its four-second buffer. `DelaySeconds` selects
manual timing. `Feedback` is [0, 0.95], and `PingPong` is [0, 1] stereo
cross-feedback. Delay time changes crossfade taps over 20 ms; wet/dry, feedback,
and cross-feedback changes are smoothed. The buffer is prepared before playback;
processing allocates no memory. Defaults: 250 ms, feedback 0.35, wet 0.3, dry 1.
The existing fixed-frame `addDelay()` remains available.

Miniaudio is pinned to `tritao/miniaudio`'s `main` branch, based on upstream
`dev`. The fork caches splitter samples by graph interval so live attachments,
paused returns, and differently sized reads remain aligned. A zero graph
processing-size request selects the default 480-frame quantum (10 ms at 48 kHz);
explicit frame periods select their own quantum, up to 65535 frames. Partial
hardware reads can leave a buffered suffix, so NativeKit preserves the graph's
render clock independently of the public hardware PCM clock. The fork's
standalone splitter regression is included in the audio CTest suite.

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
Haxeon `packages/audio/bindings/nativekit-audio.hxi` and a small typed Haxe facade under
Haxeon `packages/audio/src/haxeon/audio`. The public facade consists of `Clip`, `Voice`, `VoiceOptions`,
`AudioCue`, `AudioCueOptions`, `AudioPlayOptions`, `AudioEmitter`, `Bus`, `BusConcurrencyOptions`,
`AudioTrack`, `AudioTrackOptions`, `AudioTrackPlayer`, `AudioTransitionOptions`, `AudioBank`,
`AudioBankEntry`, `MixSnapshot`, `DeviceOptions`, and `Mixer`. `Mixer` exposes device enumeration and lifecycle controls, while
`Clip.fromAsset()` consumes a ready
`haxeon.platform.resource.ResourceAsset` from the core cache and `Clip.fromStream()`
creates an incremental source for large resources.

`AudioCue` is the first gameplay-oriented layer above the primitive and graph
facades. `AudioCue.fromClip()` handles a single source, while
`AudioCue.fromClips()` adds random or round-robin variant selection, per-play
volume and pitch ranges, and a bounded local pool with drop or oldest-voice
stealing behavior. `AudioPlayOptions` can override volume, pitch, priority, and
source transforms for an individual playback. `AudioEmitter` combines a cue
with a position, direction, and velocity, propagating transform changes to the
emitter's active voices. Cues and emitters return completed or stolen voices to
their pools when the caller polls `NativeKitRuntime.events`; load and stream
failures discard the affected pooled voice. These gameplay objects borrow their
clips, bus, and event pump rather than owning them.

The Haxe facade also exposes the standalone DSP renderer through `DspEngine`,
`DspPatchBuilder`, `DspInstrument`, `DspEvent`, and `DspRenderTarget`. Build an
immutable patch from typed oscillator, noise, envelope, filter, LFO, and
modulation-route options, create independent instruments from it, then render
interleaved float blocks into a managed `haxe.io.Bytes` buffer. Use
`DspPatchBuilder.operatorModulate()` for audio-rate oscillator-to-oscillator
FM/PM routes. `DspEvent` provides note-on, note-off, global parameter, and
oscillator-specific parameter automation and block-bounded linear ramp
constructors; events are validated for frame ordering in Haxe and applied
natively at their exact sample offsets. `DspEngine.attachToDevice()`,
`schedule()`, and `clearSchedule()` expose the live device path.
Use
`DspInstrument.setOscillatorParameter()` and `oscillatorParameter()` for
between-block source edits. Patches are copied into instruments, so a patch can
be disposed after instrument creation and reused to create additional
instruments.

`AudioTrack` is a borrowed long-running source descriptor. `AudioTrackPlayer`
coordinates one current track and one queued track, and uses the shared PCM
clock to schedule independent fade-in and fade-out operations. It supports
immediate replacement, crossfades, delayed starts, natural-end queueing, and
fade-out stops. Call `update()` once per frame after polling events so scheduled
voices can be retired; lifecycle callbacks report starts, ends, failures, and
transition completion or cancellation. It contains no game-state or playlist
policy, leaving those decisions to an engine-level audio director.

Managed bindings, projections, and smoke fixtures live in Haxeon’s
`packages/audio` (`haxeon.audio.*`). From the Haxeon root, use:

```sh
./packages/audio/tools/test-haxeon.sh
```

### Multiple live DSP renderers

Each attached DSP engine owns an independent miniaudio source node and can
route through its own bus. Attaching a second engine preserves the first
engine's routing and schedule. Repeated attachment is idempotent. Detaching
or destroying an engine removes only its source node; destroying a bus restores
all renderers routed through it to master. Sources retain a typed strong
renderer reference through a private interface while attached, and callbacks use it without handle lookup
or allocation. Teardown waits for that renderer's active callback reads before
releasing its source and voice state. All engines use the same device clock.

NativeKit's private white-noise adapter preserves DaisySP's sequence using
unsigned 32-bit multiplication and a defined signed-range conversion. This
avoids the signed-overflow undefined behavior in the vendored WhiteNoise
implementation, including when a voice's noise level is zero.

Schedule producers are serialized independently of the callback. Clearing an
attached queue publishes a new generation; only the audio consumer advances
its read cursor and retires slots. Generation changes reset voices and active
ramps before applying replacement events, so immediate rescheduling cannot
lose new notes to a later reset or overwrite a slot still being read.

Bus gain and fades share one absolute fader. Fade targets are not multiplied
by a previous volume setting. Muting, setting volume, applying mix snapshots,
and clearing scheduled transitions use that same gain path. A fade updates
the bus's configured target gain; an immediate volume change cancels it.

## Specialized source patches

`nk_audio_dsp_patch_create_source()` adds 24 DaisySP source models without
changing the original patch struct or `nk_audio_dsp_patch_create()` behavior.
Start with `nk_audio_dsp_source_defaults()` and use
`nk_audio_dsp_source_parameter_info()` to query supported controls and domains.
Configuration is copied into an immutable patch. The source shares the normal
ADSR, velocity, filter, routing, scheduled events and voice-limit behavior.

| Family | Sources |
| --- | --- |
| FM | FM2, with DaisySP's accumulating phase modulation |
| Physical | StringVoice, KarplusString, ModalVoice, Resonator, Drip |
| Drums | Analog/Synthetic BassDrum, Analog/Synthetic SnareDrum, HiHat |
| Spectral oscillators | Formant, Vosim, Zosc, VariableSaw, VariableShape, OscillatorBank, Harmonic |
| Texture | Grainlet, Particle, Dust, ClockedNoise, four-octave FractalNoise |
| Sampling | GranularPlayer, copied mono PCM, reverse/frozen/time-stretched playback |

Replacement sources use one neutral oscillator slot for level and detune,
including pitch/frequency/level modulation routes. Oscillator waveform/phase
controls and operator routes are unsupported. Noise stays additive.
KarplusString/Resonator receive the existing oscillator/noise mix as excitation,
with an impulse on note-on. Drip has a fixed internal tuning. Source-specific
controls and spectral weights are patch settings, not automation destinations;
ordinary envelope/gain/filter/level/detune automation is unchanged.

Granular PCM must be finite in [-1,1], 2–1048576 mono frames at the engine sample
rate. A patch retains a shared immutable copy; voices allocate no sample memory.
`RootNote` gives the original MIDI pitch, `Speed` sets playback speed, and
`GrainMs` sets grain duration. FM/formant/sync ratios and control domains are
validated; continuous frequency modulation is clamped to the engine's range.
All source state has bounded preallocated storage. Initialization/reset occurs
on note-on and can be more expensive for physical models than basic oscillators.

The private implementation adapts the pinned MIT sources where required:
callback-local random state replaces libc's shared RNG; granular indexing wraps
arbitrary offsets; phasor wrapping handles the endpoint and multiple cycles;
FM2 initializes its cached default carrier/modulator tuning;
KarplusString preserves its curved-bridge mode, compensates actual filter phase
for tuning, and consumes the initial pluck when low-pitch resampling is active;
Drip uses initialized filter values and portable random scaling; analog snare
resonators run linearly to avoid nonlinear SVF divergence on trigger pulses.
Its Q conversion and trigger one-pole filter preserve the natural shell decay,
covered independently of the outer note release.
These changes are documented in `patches/daisysp/README.md`; no LGPL modules or
SF2 decoder are introduced.

`audio_daisy_sources` checks all sources for audible finite output, releases,
invalid configuration and retained PCM lifetime. `audio_daisy_boundaries` checks
supported control limits at MIDI 0/60/69/127 and can run with AddressSanitizer and
UndefinedBehaviorSanitizer. Live device coverage additionally auditions every
source in Beartooth's private null-sink integration check.

## Extended synth voice options

`nk_audio_dsp_patch_create_extended()` and
`nk_audio_dsp_patch_create_source_extended()` take an additional
`nk_audio_dsp_voice_options`. The legacy factories and patch struct layout remain
unchanged; null voice options select legacy defaults. Set both voice and embedded
filter-envelope `struct_size` fields before use.

`unison_count` accepts 0 (default one) or 1–8. Built-in oscillator and wavetable
voices preallocate eight copies, each retaining its own operator modulation graph.
Detune spans ±`unison_detune_cents` (0–100 cents), with copy gains normalized by
count and constant-sum stereo panning controlled by `unison_stereo_spread` (0–1).
Mono output averages L/R; channels beyond L/R receive that average. Specialized
sources require one copy, but support the independent filter ADSR.

`filter_envelope_amount_semitones` (−96 to +96) multiplies base cutoff by
`2^(filter_envelope_level * amount / 12)` before adding cutoff-Hz modulation and
clamping to [1, sample_rate/3]. Zero bypasses the additional envelope processing.
Amplitude and filter envelopes share note gates; amplitude release determines
voice lifetime. These immutable patch controls are not instrument automation
parameters. The engine advertises `STEREO_UNISON` and `FILTER_ENVELOPE` capabilities.

`audio_dsp_voice` tests default compatibility, copy normalization, stereo/mono
mapping, block partitioning, callback/offline parity, filter-envelope independence,
wavetable/operator unison, releases, validation and specialized-source rejection.

## Multiple LFOs and filter-envelope routes

Extended voice options append two optional `additional_lfos` and their count.
The complete original voice-options prefix remains accepted by `struct_size`;
partial extensions are rejected and absent fields are never read. The primary
LFO remains in the original patch layout. Modulation sources `LFO2` and `LFO3`
require the corresponding additional source to be configured. `FILTER_ENVELOPE`
exposes the independent ADSR regardless of the direct sweep amount.

Each used LFO advances once per frame; independent waveforms, phases, rates and
retrigger/free-running modes are retained per voice. Unused sources are not
processed. `FILTER_CUTOFF_SEMITONES` adds to the direct filter-envelope sweep,
clamping the combined logarithmic offset to ±96 semitones before converting
and adding the cutoff-Hz offset. `PAN` clamps its summed offset to [−1, 1] and
attenuates the far output channel after filtering; it never boosts either
channel. Existing patches with no new routes keep their prior sound.

`audio_dsp_voice` also checks independent LFO contributions, shared-source tick
counts, phase modes, zero-sweep filter-envelope routing, stereo balance clamping,
missing-source validation, the v1 voice-options prefix and callback/offline parity.


## Prepared native scores, notes and sampled instruments

`nk_audio_dsp_engine_prepare_transport` copies immutable section/event/bar data
on a preparation thread. Events use fractional frame positions; rendering
carries the fractional phase through loops and section changes. Play/section
commands are consumed on the device clock. Group play/request publishes one
common cutoff to independently routed track engines, rejecting different
section/bar boundaries or clock domains before changing any track. A group
request must identify the current program. The admission cutoff accounts for
both device and DSP processing sizes. Late play commands keep their requested
origin and seek past overdue onsets, preserving the shared track timeline.
Group publication and scalar
parameter snapshots do not take a control mutex in the audio callback.

`nk_audio_dsp_engine_note_on` returns a monotonically increasing 64-bit identity.
A hold deadline and explicit token release are native operations. A release
before an onset cancels it. `set_voice_policy` prepares fixed stealing tails;
allocation prefers released, lower-priority, quiet/old voices and preserves the
outgoing waveform while fading it out. With no available tail/voice budget,
new notes are dropped and counted. The command ring holds 64 commands, the
timed-note heap 512 events; prepared scores allow at most 128 events in each
integer-frame interval and at most 256 after fractional rebasing.

Programs, instruments, PCM and provider resources retain control-side pins.
Retire handles/programs when no longer needed and call `engine_collect` on the
control thread. Callback references can be released without final resource
destruction there. Reset/detach cancel timed commands and native playback; first
attachment starts a fresh device clock domain. Prepared programs remain valid
for explicit replay. Forward source-clock discontinuities skip overdue score
onsets with bounded seeking; reverse clocks stop old programs. Ordinary UI
stalls do not interrupt the native timeline. Runtime status counts clock gaps,
stealing, drops and retained retired resources.

`instrument_create_from_sample` copies finite mono/stereo PCM with root-note
pitch, a validated ADSR and optional looping through release.
`nk_audio_soundfont_load_memory` decodes uncompressed SF2 on a preparation
thread. Preset metadata exposes index, MIDI bank/program and name;
`instrument_create_from_soundfont` prepares independent preallocated playback
slots. Closing the bank handle leaves existing instruments valid. Both use the
same token/release/stealing/routing contract as procedural instruments.

SF2 uses the pinned upstream MIT TinySoundFont submodule with no source changes.
The private adapter bounds RIFF/hydra/sample indices and counter durations,
bounds pre-decode expansion to 4096 regions per preset, 65536 regions per bank
and 4096 presets, limits playback to 64 overlapping layers, and preallocates
all TSF voice storage. SF3, SF2 modulators and region chorus/reverb sends are
unsupported; route mixer sends explicitly. Rendering one sample at a time
preserves exact event and block-partition timing; profile representative banks
at the intended polyphony on target devices. `audio_soundfont` verifies no TSF
allocation or free in device processing with our original sine fixture,
including 32 voices and eight concurrent stealing tails. This does not qualify
arbitrary complex bank fidelity or CPU cost.

Stereo delay's `effect_bind_dsp_tempo` reads an independent shared native BPM
snapshot at effect processing block boundaries. Explicit tempo setting removes
the binding. The snapshot can outlive its engine without retaining DSP voices.
