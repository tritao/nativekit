# Haxeon bindings

`nativekit-abi64.hxi` is the reviewed semantic binding for the public NativeKit
C ABI on supported 64-bit desktop targets. The C headers remain authoritative. The file
records verified portable structure layouts plus ownership and parameter
directions that cannot yet be inferred safely from ordinary C declarations.
Its logical `@library("nativekit")` name resolves to the platform's native
shared-library filename at runtime.

The binding deliberately begins with lifecycle, event polling, diagnostics,
window handles, and monitor-name lookup. `@out`, `@inout`, and
`@out_buffer("size")` keep raw pointers private in the generated module: Haxe
code receives typed result objects, fixed-layout structure values, and managed
variable-length bytes. Extend this reviewed surface alongside integration
coverage; do not expose arbitrary pointers merely because the header importer
can parse them.

## Typed events

`NativeKitEvent.poll()` owns the returned native event. Call `release()` when
inspecting it manually, or prefer `take()`, which decodes and releases it in one
operation. Release is idempotent. Any `Bytes` returned by `payload()` and all
values returned by `decode()` are copies and remain valid after release.
`snapshot()` returns the immutable managed context used by domain decoders.

Known WebView, notification, dialog, clipboard, and drop payloads project to typed
`NativeKitEventValue` cases. Unknown kinds project to `Raw`, preserving metadata
and copied payload data for forward compatibility. Malformed packed payloads
throw instead of allowing out-of-bounds reads.

Transactional custom-editor input projects `NK_EVENT_TEXT_EDIT` to
`TextEdit(source, edit)`. Replacement, selection, and composition positions are
Unicode code-point indices; optional replacement text is copied and strictly
validated as UTF-8 before the native event is released.

HXI constants are projected through `NativeKitConstants`; handwritten bindings
use those symbols rather than repeating numeric C ABI values.

UTF-8 inputs are marked in the authoritative C headers with `NK_UTF8` or
`NK_NULLABLE_UTF8`. The importer projects these annotations to managed Haxe
strings, so the binding import header does not redeclare public functions.

`NativeKitRequests` maps request IDs to one-shot typed completion callbacks. Its
`poll()` method decodes and releases the native event before invoking a matching
handler; `cancel()` only removes local tracking and does not cancel native work.

Run the end-to-end smoke test with:

```sh
tools/test-haxeon.sh
```

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
