# Haxeon bindings

`nativekit.hxi` is the reviewed semantic binding for the public NativeKit
C ABI on supported 64-bit desktop targets. The C headers remain authoritative. The file
records verified portable structure layouts plus ownership and parameter
directions that cannot yet be inferred safely from ordinary C declarations.
Its logical `@library("nativekit")` name resolves to the platform's native
shared-library filename at runtime.

`nativekit.hxmap` is the separate Haxe projection policy. It contains only
Haxe-facing naming rules; it does not alter C symbols, structure layouts, or
ownership contracts. Other bindings can reuse the generic prefix rules and
provide small manifests only for library-specific exceptions.

The `nk_result` projection policy preserves raw result-returning functions and
generates a matching `*_checked` helper. For example,
`NativeKit.nk_window_show(...)` remains available while
`NativeKit.nk_window_show_checked(...)` throws a structured `NativeKitError`
with `result`, `operation`, and `diagnostic` fields. Functions with annotated
output parameters return their typed output values from the checked helper.

The binding deliberately begins with lifecycle, event polling, diagnostics,
window handles, and monitor-name lookup. `@out`, `@inout`, and
`@out_buffer("size")` keep raw pointers private in the generated module: Haxe
code receives typed result objects, fixed-layout structure values, and managed
variable-length bytes. Extend this reviewed surface alongside integration
coverage; do not expose arbitrary pointers merely because the header importer
can parse them.

## Typed events

`NativeKitRuntime.events` is the managed owner of the native event queue. Each
`events.poll()` performs one native poll, decodes and releases that event, routes
request completions, then notifies event listeners. It returns `false` when the
queue is empty, so an application can drain events with `while (events.poll())`.
The low-level `NativeKitEvent` wrapper still provides `release()`, `take()`,
`payload()`, and `snapshot()` for code that already owns a raw event value.

Known WebView, notification, dialog, clipboard, and drop payloads project to typed
`NativeKitEventValue` cases. Unknown kinds project to `Raw`, preserving metadata
and copied payload data for forward compatibility. Malformed packed payloads
throw instead of allowing out-of-bounds reads.

Transactional custom-editor input projects `NK_EVENT_TEXT_EDIT` to
`TextEdit(source, edit)`. Replacement, selection, and composition positions are
Unicode code-point indices; optional replacement text is copied and strictly
validated as UTF-8 before the native event is released.

Fixed-width value domains marked with `NK_ENUM` in the public headers project
as concise PascalCase Haxe enum abstracts such as `Result` and `EventKind`, with
PascalCase members such as `Ok` and `WindowClose`. The raw `nk_*` types and
`NK_*` spellings remain in the C/HXI interface, while managed Haxe code uses
the concise projected types and typed enum abstracts.

UTF-8 inputs are marked in the authoritative C headers with `NK_UTF8` or
`NK_NULLABLE_UTF8`. The importer projects these annotations to managed Haxe
strings, so the binding import header does not redeclare public functions.

`NativeKitRequests` is a request registry owned by the event pump; it never polls
the native queue. Access it as `runtime.events.requests`. The Haxe UI
`NativeInputAdapter` can attach to the same pump with `attach(events)` and stop
receiving events with `detach()`.

Request IDs map to one-shot typed completion callbacks.
Callbacks receive `Success(value)`, `Cancelled`, or `Failure(result, message)`;
dialogs use `Cancelled` when dismissed. Expected asynchronous failures are
delivered as values instead of being thrown from event polling. Synchronous
failures to start a request still throw with the NativeKit diagnostic. The pump
releases each event before invoking request handlers or application listeners;
`cancel()` only removes local tracking and does not cancel native work.

Run the end-to-end smoke test with:

```sh
tools/test-haxeon.sh
```

Compiler invocations using the HXI interface should load the matching
projection manifest with `--ffi-projection`.

The test first runs `tools/audit-haxeon-abi.sh`, which imports the same public
header for Linux x86-64, Windows x86-64, and both macOS 64-bit architectures.
It fails if declarations, constants, calling conventions, or structure layouts
drift, or if the public ABI uses target-dependent C scalar types.

Regenerate the canonical interface after changing public headers, or
verify that it is current without rewriting it:

```sh
tools/update-haxeon-hxi.sh
tools/update-haxeon-hxi.sh --check
```

It expects the Haxeon checkout at `../realtime-haxe` by default. Override that
with `HAXEON_DIR=/path/to/realtime-haxe`. The canonical file is written only
after Linux, Windows, and both macOS 64-bit models compare successfully. Wine
continues to validate the same C ABI independently through `tools/test-wine.sh`.
