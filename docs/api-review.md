# Public API review

This review covers the NativeKit 0.1 C ABI after the Android editor-support
work. It records the invariants clients may rely on, corrections made during the
review, and intentionally deferred features. The public headers remain normative;
[`api.md`](api.md) explains contracts shared by multiple modules.

## Review outcome

The API is coherent enough for a real editor integration spike. Its central
model—opaque generation-checked handles, a UI-thread command surface, copied
inputs, queued events, URI-first resources, and versioned structures—works across
desktop and mobile without exposing Android implementation protocols.

The review made these source-compatible, ABI-neutral corrections:

- Dialog operations, flags, message kinds, button masks, and results now have
  named 32-bit ABI types instead of leaking through untyped integer fields.
- Resource permissions, open modes, and stream capabilities now have distinct
  named flag types. Resource views no longer risk being confused with stream
  capability flags.
- Resource output parameters now carry explicit binding-direction annotations.
- Accessibility documentation now correctly describes selection positions as
  absolute document code-point positions.
- The cross-module guide now documents structure versioning, errors, strings,
  booleans, handles versus request IDs, threading exceptions, bounded IME state,
  and the custom-surface accessibility contract.

No numeric constant, structure size, field offset, symbol, or calling convention
changed in this review.

## Stable conventions

| Area | Contract |
|---|---|
| Initialization | One active runtime; the successful `nk_init()` thread becomes the UI thread. |
| Structures | Zero-initialize, set `struct_size`, and leave reserved storage zero. |
| Handles | Opaque, generation checked, non-zero only while the resource is live. |
| Requests | Separate 64-bit asynchronous identities; exactly one terminal event where promised. |
| Strings | UTF-8; input is copied unless explicitly documented otherwise. |
| Event data | Borrowed until `nk_event_release()`; use payload helpers for packed data. |
| Coordinates | Logical pixels unless a field explicitly says framebuffer/device pixels. |
| Text positions | Unicode code points at the C boundary; platform UTF-16 remains private. |
| Resources | URI identity is authoritative; a URI is never implicitly coerced to a path. |
| Capabilities | Compile/backend availability hint; callers still handle runtime failure. |
| Errors | Typed `nk_result` for logic and thread-local `nk_last_error()` for diagnostics. |

## Module boundaries

- `nativekit.h`: runtime, results, handles, requests, and event ownership.
- `nativekit_window.h`: desktop windows, state, capabilities, and explicit native
  interoperation.
- `nativekit_mobile.h`: attachment to caller-owned mobile containers and forwarded
  host lifecycle/events.
- `nativekit_graphics.h`: child rendering surfaces and their native presentation
  lifecycle.
- `nativekit_input.h`: normalized device input, transactional IME state, and
  cursor control.
- `nativekit_accessibility.h`: semantic projection for custom-rendered surfaces.
- `nativekit_resource.h`: URI resources, sharing, durable access, and streams.
- `nativekit_dialog.h`, `nativekit_clipboard.h`, and
  `nativekit_notification.h`: asynchronous operating-system services.
- `nativekit_webview.h`: native child browsers, JSON messaging, evaluation, and
  navigation policy.
- `nativekit_monitor.h`, `nativekit_joystick.h`, and `nativekit_gamepad.h`:
  enumerated hardware resources and normalized state.
- `nativekit_system.h`, `nativekit_time.h`, and `nativekit_vulkan.h`: narrow
  platform services without introducing another object model.

## Lifecycle and ownership audit

Input option strings, arrays, semantic nodes, and text geometry are copied before
their calls return. Event payloads and decoded views remain library-owned. Native
window descriptors are borrowed escape hatches. Vulkan surfaces are the notable
externally owned objects: applications create them through NativeKit but destroy
them explicitly before the Vulkan instance.

Owned desktop windows destroy their owned windows and child resources. Mobile
host destruction releases NativeKit children and references but never destroys
the caller's Activity, view controller, or container. Graphics context sharing
creates an explicit lifetime dependency: the source surface must outlive every
dependent surface.

## Asynchronous audit

Dialogs, clipboard reads, WebView evaluation/navigation decisions, and
notifications use request IDs and terminal events. Starting an operation means it
was accepted, not that the user or operating system completed it. Cancellation
still settles the request where documented. Ordinary input, geometry, lifecycle,
and accessibility events do not carry request IDs.

Clients should dispatch events by `kind` before interpreting `data`, validate
`data_size` for fixed payloads, use decoder helpers for packed payloads, and
release every successfully polled event including `NK_EVENT_NONE`.

## Deliberately deferred

The review does not add outbound Android dragging, platform widget abstractions,
filesystem coercion for provider URIs, or a generic command bus. Those would
expand the API before a real client demonstrates a requirement. The next step is
integration, not another speculative subsystem.

Potential post-integration refinements are additive: richer stylus samples,
additional accessibility roles/actions, and backends for accessibility on other
custom-surface platforms. They should be driven by observed client needs.
