# Platform parity contract

This document defines what NativeKit means by *parity*. Parity is semantic and
platform-appropriate: desktop backends converge with desktop backends, mobile
backends converge with mobile backends, and the browser implements meaningful
Web equivalents. The contract does not require identical native capability
masks on every platform.

The target is that every public API family is either implemented wherever it
makes semantic sense or explicitly classified as not applicable. A missing
implementation is a `Deferred` item, not an implicit platform exception.

This contract covers the core NativeKit platform ABI and its capability bits.
The optional `NativeKit::gpu` and `NativeKit::ui` modules are not capability
families in this table: their rendering, layout, style, and framework behavior
are documented and tested by their respective module suites. The optional
HTTP capability bits are the exception and are included below. They remain
runtime-optional and are advertised only when the selected transport supports
them.

## Contract states

| State | Meaning |
|---|---|
| `Required` | The public API family must be implemented and advertised for this platform. |
| `Platform-specific equivalent` | The semantic operation is required, but its native representation or capability bit may differ. Examples include a mobile host instead of a top-level desktop window and browser permission/Web APIs instead of an OS service. |
| `Not applicable` | The concept has no meaningful operation on this platform. The backend must not advertise the corresponding capability. |
| `Optional` | The operation has a meaningful platform equivalent, but browser/runtime support or permission may be unavailable. The backend advertises it only when the underlying API exists. |
| `Deferred` | A known implementation gap. It is tracked work and must not be described as unsupported due to platform semantics. |

`Deferred` is a temporary state. Before a capability is advertised as
`Required` or `Platform-specific equivalent`, its public operations and shared
behavior tests must be in place. A runtime-dependent component may cause a
required capability to be absent at runtime; the backend must document that
dependency and report the capability accurately, as Windows does for the
optional WebView2 runtime.

## Target contract

This is the intended end-state contract. `WebView` is intentionally
`Not applicable` for browser NativeKit: the browser is already the WebView and
the equivalent is the native DOM.

| Capability family | Linux | Windows | macOS | Android | iOS | Web |
|---|---|---|---|---|---|---|
| Desktop windows | Required | Required | Required | Not applicable | Not applicable | Platform-specific equivalent |
| Extended window API | Required | Required | Required | Not applicable | Not applicable | Platform-specific equivalent |
| WebView | Required | Required | Required | Required | Required | Not applicable (native DOM) |
| Native child views | Required | Required | Required | Required | Required | Platform-specific equivalent (DOM element) |
| Dialogs and resource pickers | Required | Required | Required | Required | Required | Platform-specific equivalent |
| Clipboard | Required | Required | Required | Required | Required | Required |
| Drag/drop | Required | Required | Required | Required | Required | Required |
| Shell/open URI | Required | Required | Required | Required | Required | Required |
| Appearance | Required | Required | Required | Required | Required | Required |
| Notifications | Required | Required | Required | Required | Required | Platform-specific equivalent |
| Input + IME | Required | Required | Required | Required | Required | Required |
| GPU surface | Required | Required | Required | Required | Required | Required |
| Resource I/O | Required | Required | Required | Required | Required | Required |
| Resource sharing | Required | Required | Required | Required | Required | Platform-specific equivalent |
| Accessibility | Required | Required | Required | Required | Required | Required |
| Monitors | Required | Required | Required | Not applicable | Not applicable | Not applicable |
| Joystick/gamepad | Required | Required | Required | Required | Required | Required where feasible |
| Raw sensors | Optional | Optional | Optional | Required | Required | Optional |
| System haptics | Not applicable | Not applicable | Not applicable | Required | Required | Optional |
| Gamepad rumble | Required | Required | Optional | Required | Required | Optional |
| Native window export | Required | Required | Required | Not applicable | Not applicable | Not applicable |
| Native window wrapping | Required | Required | Required | Not applicable | Not applicable | Not applicable |

Two public capability families are orthogonal to the table above:

| Additional family | Linux | Windows | macOS | Android | iOS | Web |
|---|---|---|---|---|---|---|
| Mobile host attachment | Not applicable | Not applicable | Not applicable | Required | Required | Not applicable |
| Cursor + pointer capture | Required | Required | Required | Platform-specific equivalent | Platform-specific equivalent | Platform-specific equivalent |
| Custom window decorations | Required | Required | Required | Not applicable | Not applicable | Not applicable |
| Surface frame callbacks | Required | Required | Required | Required | Required | Required |
| Native child views | Required | Required | Required | Required | Required | Platform-specific equivalent |

The mobile-host row explains why Android and iOS do not advertise
`NK_CAP_WINDOW`: they attach a caller-owned native view instead of creating a
NativeKit-owned top-level window. Pointer capture on touch platforms is a
semantic input equivalent, not a desktop cursor guarantee.

The native-child-view row is the end-state contract. Only GTK implements
`nk_view_*` today; the other backends leave `NK_CAP_NATIVE_VIEW` clear, return
`NK_ERROR_UNSUPPORTED`, and are tracked as deferred in `capability-snapshots.txt`
until each one lands.

## Capability-bit mapping

The public bits are grouped as follows. A family with multiple bits is
complete only when the platform-specific contract says all of those bits are
needed; graphics API bits are alternatives within the GPU family.

| Contract family | Capability bits |
|---|---|
| Desktop windows | `NK_CAP_WINDOW` |
| Mobile host attachment | `NK_CAP_MOBILE_HOST` |
| Extended window geometry | `NK_CAP_WINDOW_GEOMETRY` |
| Window styling | `NK_CAP_WINDOW_STYLING` |
| Custom window decorations | `NK_CAP_WINDOW_CUSTOM_DECORATIONS` |
| WebView | `NK_CAP_WEBVIEW` |
| Native child views | `NK_CAP_NATIVE_VIEW` |
| Dialogs and resource pickers | `nk_dialog_open_resource`, `nk_dialog_save_resource`, `nk_dialog_select_resource_directory`, `nk_dialog_message` |
| Clipboard | `NK_CAP_CLIPBOARD` |
| Drag/drop | `NK_CAP_DRAG_DROP` |
| Shell/open URI | `NK_CAP_SHELL` |
| Appearance | `NK_CAP_SYSTEM_APPEARANCE` |
| Native window export | `NK_CAP_EXPORT_NATIVE_WINDOW` |
| Native window wrapping | `NK_CAP_WRAP_NATIVE_WINDOW` |
| Notifications | `NK_CAP_NOTIFICATION` |
| Input + IME | `NK_CAP_INPUT` |
| Cursor + pointer capture | `NK_CAP_CURSOR`, `NK_CAP_POINTER_CAPTURE` |
| GPU surface | `NK_CAP_OPENGL_SURFACE`, `NK_CAP_OPENGL_ES_SURFACE`, `NK_CAP_VULKAN_SURFACE`, `NK_CAP_D3D11_SURFACE`, `NK_CAP_METAL_SURFACE` |
| Surface frame callbacks | `NK_CAP_SURFACE_FRAME_CALLBACK` |
| Resource sharing | `NK_CAP_RESOURCE_SHARING` |
| Resource I/O | `NK_CAP_RESOURCE_IO` |
| Accessibility | `NK_CAP_ACCESSIBILITY` |
| Monitors | `NK_CAP_MONITOR`, `NK_CAP_MONITOR_FULLSCREEN` |
| Joystick/gamepad | `NK_CAP_JOYSTICK` |
| Raw sensors | `NK_CAP_SENSORS` |
| System haptics | `NK_CAP_HAPTICS` |
| Gamepad rumble | `NK_CAP_GAMEPAD_RUMBLE` |
| Platform/device identity | `NK_CAP_SYSTEM_INFO` |
| Application paths | `NK_CAP_APPLICATION_PATH`, `NK_CAP_APPLICATION_STORAGE` |
| System fonts | `NK_CAP_SYSTEM_FONTS` |
| Keep-awake leases | `NK_CAP_KEEP_AWAKE` |
| Orientation | `NK_CAP_DEVICE_ORIENTATION`, `NK_CAP_DISPLAY_ORIENTATION` |

HTTP networking is a NativeKit capability, but remains optional at runtime.
Backends report `NK_CAP_HTTP_CLIENT` and, where the selected transport can
provide safe bounded pull-backpressure, `NK_CAP_HTTP_STREAMING`. Unsupported
transports leave those bits clear and return `NK_ERROR_UNSUPPORTED`. The
transport matrix and Web buffered-only limitation are specified in
[`0011-nativekit-networking.md`](decisions/0011-nativekit-networking.md).

`NK_CAP_WINDOW_GEOMETRY` and `NK_CAP_WINDOW_STYLING` are checked separately by
the executable baseline because a backend may have implemented geometry while
styling remains deferred. The target `Extended window API` row requires both.
Dialogs and resource pickers are a required API family without a dedicated
capability bit; their resource and message operations are part of each backend's
contract and are exercised by the platform integration suites.

## Current baseline and executable enforcement

The target table is deliberately ahead of the current implementation. Until
each milestone lands, the following gaps remain explicitly `Deferred`:

| Backend | Required or equivalent today | Deferred today |
|---|---|---|
| Linux/GTK | Windows, WebView, URI resource and message dialogs, clipboard, URI clipboard, drag/drop and resource drops, shell, appearance, notifications, input, cursor/capture, geometry, styling, custom window decorations, GPU, resource sharing via URI clipboard, resource I/O, monitors, joystick and evdev rumble, native export, X11 and Wayland native wrapping, accessibility, surface frame callbacks | — |
| Windows | Windows, WebView when WebView2 is available, URI resource and message dialogs, clipboard, URI clipboard, drag/drop and resource drops, shell, appearance, notifications, input, cursor/capture, geometry, styling, custom window decorations, D3D11, resource sharing via URI clipboard, resource I/O, accessibility, monitors, joystick and XInput rumble, native export, Win32 native wrapping, surface frame callbacks | — |
| macOS | Windows, WebView, URI resource and message dialogs, clipboard, URI clipboard, drag/drop and resource drops, shell, appearance, notifications, input, cursor/capture, geometry, styling, custom window decorations, Metal, resource sharing via `NSSharingServicePicker`, resource I/O, accessibility, monitors, joystick, native export, Cocoa native wrapping, surface frame callbacks | — |
| Android | Mobile host, WebView, dialogs, clipboard, drag/drop, shell, appearance, notifications, input, GLES/Vulkan, resource sharing, resource I/O, joystick, sensors, system haptics, controller rumble, accessibility, surface frame callbacks, APK installation path, system fonts | — |
| iOS | Mobile host, WebView, dialogs, Metal, input, clipboard, URI clipboard, host drag/drop, shell, appearance, notifications, resource sharing, resource I/O, joystick, sensors, system haptics, controller haptics where exposed, accessibility, surface frame callbacks | — |
| Web | Window, geometry and CSS-backed styling, clipboard and URI clipboard, drag/drop and resource drops, input, cursor/capture, GLES, shell URL opening, appearance, notifications, Gamepad API, resource picker equivalents, resource sharing via Web Share API, resource I/O, accessibility, surface frame callbacks, optional Device Motion and haptics APIs | — |

The path-shaped iOS system-font and Web application/storage/system-font
capabilities are intentional `Not applicable` exceptions for APIs that return
real filesystem paths; they are not deferred implementations.

The system-information family has platform-specific details that are also part
of the executable contract. Linux reads privacy-safe vendor and product values
from DMI when the kernel exposes them. Windows reads the BIOS manufacturer and
product values and obtains the OS version through `RtlGetVersion`, avoiding
compatibility-manifest-dependent `GetVersionEx` results. macOS reports the
hardware model from `hw.model`. Android reports the APK installation directory
and `/system/fonts` when that directory exists. iOS does not expose a supported
font-directory path, and browser filesystem paths remain unavailable by design.
Web physical orientation is a permission-gated optional equivalent backed by
`DeviceOrientationEvent`; it is advertised only when that browser API exists.
The browser can still deny access or provide no usable sensor reading, in which
case the query remains unknown and no device-orientation events are emitted.

Linux advertises `NK_CAP_KEEP_AWAKE` only when the session's XDG desktop portal
exposes the `Inhibit` interface; applications must continue to treat that bit
as runtime-dependent.

The current Windows joystick adapter uses XInput's standard gamepad model,
including hotplug and normalized canonical state. The macOS and iOS adapters use
GameController's extended and micro gamepad models. Generic HID joystick
enumeration remains a separate follow-up from these platform-native gamepad
equivalents and must not be silently treated as complete parity.

Desktop native-window wrapping is an ownership-safe interoperation primitive.
Win32, Cocoa, and X11 wrappers retain or reference the caller's native object,
but destruction of the NativeKit handle only detaches that reference. The host
continues to own native event dispatch. GTK Wayland wrappers retain the borrowed
display/surface descriptor without attempting to make GTK own or destroy the
`wl_surface`; because GTK3 cannot represent a foreign Wayland surface as a
`GdkWindow`, those wrappers intentionally support descriptor access and
destruction only.

The Web equivalents are browser-mediated: each NativeKit window owns a canvas
and receives its own input, resize, drop, and focus routing. Multiple surfaces
in one window are logical NativeKit surfaces multiplexed onto that canvas's
single browser WebGL context; explicitly shared surfaces retain that context
through a shared lifetime object. `window.open` handles URI shell
opening, `matchMedia` supplies appearance, the Notifications API supplies
notifications, the File System Access API or `<input type=file>` supplies
resource selection, and the Gamepad API supplies controller state. Device
orientation, when available, uses the Device Orientation API after an
explicit permission request; its `beta`/`gamma` sensor values are reduced to
the shared posture enum and remain best-effort. Save and directory selections
retain browser handles behind opaque NativeKit URIs, while
open-file contents use temporary `blob:` URIs and asynchronous fetch. Window
size limits, aspect ratio, resizability, opacity, and mouse passthrough map to
canvas CSS styles; decorations, stacking, and activation remain page-owned.
These APIs can require a user activation, a permission grant, or a secure
context. NativeKit has no path-returning dialog API, so browser path leakage is
not part of the contract.

Raw sensors are advertised on Android, iOS, and Web only when the backend has
its native/browser motion path; sensor enumeration remains authoritative for
physical availability. Desktop backends keep the ABI available but do not
advertise `NK_CAP_SENSORS` without a real adapter. Android and iOS system
haptics use native feedback facilities, and Web uses `navigator.vibrate()`;
controller rumble is separately guarded by `NK_CAP_GAMEPAD_RUMBLE` and may be
unsupported for an individual browser or device actuator.

The baseline is guarded by `platform_parity` in CTest and the shared
`capability_conformance` suite on desktop. The snapshot-backed parity test
checks that:

* every capability currently classified as required or equivalent is present;
* a capability classified as not applicable is absent;
* deferred capabilities are not mistaken for a completed family; and
* no backend advertises a capability bit absent from this contract.

The desktop conformance suite then calls the public operations associated with
each advertised capability and fails if an operation returns
`NK_ERROR_UNSUPPORTED`. Existing Android, Web, and platform-specific native
tests provide the corresponding host or browser behavior coverage; iOS also
has a simulator-hosted runtime test in CI.

An intentional capability change must update this document,
`tests/capability-snapshots.txt`, and the relevant behavior tests in the same
change. This makes the capability mask a reviewable snapshot while the target
table remains the definition of completion.

The fallback stub used when a native desktop dependency is unavailable is not a
platform backend. It is tested separately and advertises only the portable
resource-I/O core.
