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

`NK_CAP_WINDOW_GEOMETRY` and `NK_CAP_WINDOW_STYLING` are checked separately by
the executable baseline because a backend may have implemented geometry while
styling remains deferred. The target `Extended window API` row requires both.

## Current baseline and executable enforcement

The target table is deliberately ahead of the current implementation. Until
each milestone lands, the following gaps remain explicitly `Deferred`:

| Backend | Required or equivalent today | Deferred today |
|---|---|---|
| Linux/GTK | Windows, geometry, styling, WebView, dialogs, clipboard, drag/drop, shell, appearance, notifications, input, cursor/capture, GPU, resource I/O, monitors, joystick, native export | Resource sharing, accessibility, native wrapping |
| Windows | Windows, WebView when WebView2 is available, dialogs, clipboard, drag/drop, shell, appearance, notifications, input, cursor/capture, geometry, styling, D3D11, resource I/O, accessibility, monitors, joystick, native export | Resource sharing, native wrapping |
| macOS | Windows, WebView, dialogs, clipboard, drag/drop, shell, appearance, notifications, input, cursor/capture, geometry, styling, Metal, resource I/O, monitors, joystick, native export | Resource sharing, accessibility, native wrapping |
| Android | Mobile host, WebView, dialogs, clipboard, drag/drop, shell, appearance, notifications, input, GLES/Vulkan, resource sharing, resource I/O, joystick, accessibility | — |
| iOS | Mobile host, resource I/O | WebView, dialogs, clipboard, drag/drop, shell, appearance, notifications, input, Metal, resource sharing, joystick, accessibility |
| Web | Window, geometry, clipboard, input, cursor/capture, GLES, resource I/O | Styling, dialogs, drag/drop, shell, appearance, notifications, resource sharing, joystick, accessibility |

The current Windows joystick adapter uses XInput's standard gamepad model,
including hotplug and normalized canonical state. The current macOS adapter
uses GameController's extended and micro gamepad models. Generic HID joystick
enumeration remains a separate follow-up from these platform-native gamepad
equivalents and must not be silently treated as complete parity.

The baseline is guarded by `platform_parity` in CTest. It checks that:

* every capability currently classified as required or equivalent is present;
* a capability classified as not applicable is absent;
* deferred capabilities are not mistaken for a completed family; and
* no backend advertises a capability bit absent from this contract.

An intentional capability change must update this document, the per-backend
expectation in `tests/unit/platform_parity.cpp`, and the relevant behavior
tests in the same change. This makes the capability mask a reviewable snapshot
while the target table remains the definition of completion.

The fallback stub used when a native desktop dependency is unavailable is not a
platform backend. It is tested separately and advertises only the portable
resource-I/O core.
