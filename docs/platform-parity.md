# Platform parity contract

This document defines what NativeKit means by *parity*. Parity is semantic and
platform-appropriate: desktop backends converge with desktop backends, mobile
backends converge with mobile backends, and the browser implements meaningful
Web equivalents. The contract does not require identical native capability
masks on every platform.

The target is that every public API family is either implemented wherever it
makes semantic sense or explicitly classified as not applicable. A missing
implementation is a `Deferred` item, not an implicit platform exception.

## Contract states

| State | Meaning |
|---|---|
| `Required` | The public API family must be implemented and advertised for this platform. |
| `Platform-specific equivalent` | The semantic operation is required, but its native representation or capability bit may differ. Examples include a mobile host instead of a top-level desktop window and browser permission/Web APIs instead of an OS service. |
| `Not applicable` | The concept has no meaningful operation on this platform. The backend must not advertise the corresponding capability. |
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
| Dialogs | Required | Required | Required | Required | Required | Platform-specific equivalent |
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
| Native window export | Required | Required | Required | Not applicable | Not applicable | Not applicable |
| Native window wrapping | Required | Required | Required | Not applicable | Not applicable | Not applicable |

Two public capability families are orthogonal to the table above:

| Additional family | Linux | Windows | macOS | Android | iOS | Web |
|---|---|---|---|---|---|---|
| Mobile host attachment | Not applicable | Not applicable | Not applicable | Required | Required | Not applicable |
| Cursor + pointer capture | Required | Required | Required | Platform-specific equivalent | Platform-specific equivalent | Platform-specific equivalent |

The mobile-host row explains why Android and iOS do not advertise
`NK_CAP_WINDOW`: they attach a caller-owned native view instead of creating a
NativeKit-owned top-level window. Pointer capture on touch platforms is a
semantic input equivalent, not a desktop cursor guarantee.

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
| WebView | `NK_CAP_WEBVIEW` |
| Dialogs | `NK_CAP_FILE_DIALOG` |
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
| Resource sharing | `NK_CAP_RESOURCE_SHARING` |
| Resource I/O | `NK_CAP_RESOURCE_IO` |
| Accessibility | `NK_CAP_ACCESSIBILITY` |
| Monitors | `NK_CAP_MONITOR`, `NK_CAP_MONITOR_FULLSCREEN` |
| Joystick/gamepad | `NK_CAP_JOYSTICK` |
| Platform/device identity | `NK_CAP_SYSTEM_INFO` |
| Application paths | `NK_CAP_APPLICATION_PATH`, `NK_CAP_APPLICATION_STORAGE` |
| System fonts | `NK_CAP_SYSTEM_FONTS` |
| Keep-awake leases | `NK_CAP_KEEP_AWAKE` |
| Orientation | `NK_CAP_DEVICE_ORIENTATION`, `NK_CAP_DISPLAY_ORIENTATION` |

The optional `NativeKit::net` module is outside the core platform baseline. When
enabled, it reports `NK_CAP_HTTP_CLIENT` and, where the selected transport can
provide safe bounded pull-backpressure, `NK_CAP_HTTP_STREAMING`. The module's
transport matrix and its Web buffered-only limitation are specified in
[`0011-nativekit-networking.md`](decisions/0011-nativekit-networking.md).

`NK_CAP_WINDOW_GEOMETRY` and `NK_CAP_WINDOW_STYLING` are checked separately by
the executable baseline because a backend may have implemented geometry while
styling remains deferred. The target `Extended window API` row requires both.

## Current baseline and executable enforcement

The target table is deliberately ahead of the current implementation. Until
each milestone lands, the following gaps remain explicitly `Deferred`:

| Backend | Required or equivalent today | Deferred today |
|---|---|---|
| Linux/GTK | Windows, geometry, styling, WebView, dialogs, clipboard, drag/drop, shell, appearance, notifications, input, cursor/capture, GPU, resource sharing via URI clipboard, resource I/O, accessibility, monitors, joystick, native export, X11 native wrapping, platform identity, application paths, fonts, keep-awake, display orientation | Wayland native wrapping, device orientation |
| Windows | Windows, WebView when WebView2 is available, dialogs, clipboard, drag/drop, shell, appearance, notifications, input, cursor/capture, geometry, styling, D3D11, resource sharing via URI clipboard, resource I/O, accessibility, monitors, joystick, native export, Win32 native wrapping, platform identity, application paths, fonts, keep-awake, display orientation | Device orientation |
| macOS | Windows, WebView, dialogs, clipboard, drag/drop, shell, appearance, notifications, input, cursor/capture, geometry, styling, Metal, resource sharing via `NSSharingServicePicker`, resource I/O, accessibility, monitors, joystick, native export, Cocoa native wrapping, platform identity, application paths, fonts, keep-awake, display orientation | Device orientation |
| Android | Mobile host, WebView, dialogs, clipboard, drag/drop, shell, appearance, notifications, input, GLES/Vulkan, resource sharing, resource I/O, joystick, accessibility, platform identity, application storage, keep-awake, device/display orientation | Application install path, system fonts |
| iOS | Mobile host, WebView, dialogs, Metal, input, clipboard, shell, appearance, notifications, accessibility, resource I/O, platform identity, application paths, application storage, keep-awake, device/display orientation | Drag/drop, resource sharing, joystick, system fonts |
| Web | Window, geometry, clipboard and URI clipboard, drag/drop and resource drops, input, cursor/capture, GLES, shell URL opening, appearance, notifications, Gamepad API, resource picker equivalents, resource sharing via Web Share API, resource I/O, accessibility, platform identity, locale, keep-awake, display orientation where browser APIs are available | Styling, dialogs, application paths, application storage, system fonts, device orientation |

The current Windows joystick adapter uses XInput's standard gamepad model,
including hotplug and normalized canonical state. The current macOS adapter
uses GameController's extended and micro gamepad models. Generic HID joystick
enumeration remains a separate follow-up from these platform-native gamepad
equivalents and must not be silently treated as complete parity.

Desktop native-window wrapping is an ownership-safe interoperation primitive.
Win32, Cocoa, and X11 wrappers retain or reference the caller's native object,
but destruction of the NativeKit handle only detaches that reference. The host
continues to own native event dispatch. Linux wrapping is intentionally limited
to X11; Wayland wrapping remains deferred until foreign-surface lifecycle and
event ownership can be made explicit.

The Web equivalents are browser-mediated: `window.open` handles URI shell
opening, `matchMedia` supplies appearance, the Notifications API supplies
notifications, the File System Access API or `<input type=file>` supplies
resource selection, and the Gamepad API supplies controller state. Save and
directory selections retain browser handles behind opaque NativeKit URIs, while
open-file contents use temporary `blob:` URIs and asynchronous fetch. These
APIs can require a user activation, a permission grant, or a secure context.
Web path-based dialogs remain deferred because a browser cannot safely expose a
process-local filesystem path through this URI-first API.

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
