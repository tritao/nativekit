# ADR 0013: Plugin runtime ABI

## Status

Accepted. The first implementation adds the descriptor, host, service, payload,
call, and event contracts in `include/nativekit_plugin.h`. It is deliberately
generic: no Haxeon, Kotlin, or language-runtime type crosses the boundary.

## Decision

Plugins are registered against a NativeKit runtime generation. A descriptor
declares an id plus `create`/`destroy`; `create` receives a versioned
`nk_plugin_host` function table and the instance handle NativeKit allocated for
it. Every instance records the generation that created it, and `nk_shutdown()`
destroys live instances before the runtime, its handles, and its event queue
disappear. Nothing inside the ABI names a host language.

Dispatch is numeric. Plugins register services with a non-zero `service_id` in
one runtime-wide namespace, and calls address `(service_id, method_id)` pairs.
Strings appear only as diagnostics; there are no dictionaries, selectors, or
string hashing on the runtime path.

Payloads are bounded byte spans. NativeKit never interprets payload bytes and
has no JSON, object, or variant model, so generated bindings can lay typed,
fixed-layout records over the span. The plugin control plane is capped by
`NK_PLUGIN_PAYLOAD_MAX` (1 MiB), which keeps high-bandwidth traffic (camera,
video, render output, shared buffers) out of the event stream. Operations that
move bulk data return a NativeKit handle such as `nk_graphics_image` or a
future stream handle in the completion's handle slot.

Calls are asynchronous and always complete through the existing event model:

| Event | Source | Request | Data |
| --- | --- | --- | --- |
| `NK_EVENT_PLUGIN_COMPLETE` | plugin instance | call request id | `nk_plugin_event_data` header and borrowed payload |
| `NK_EVENT_PLUGIN_EVENT` | plugin instance | none | `nk_plugin_event_data` header and borrowed payload |

`nk_plugin_event_query()` decodes the header into `nk_plugin_event_view`, which
carries the instance, service, method, result, request id, transferred handle,
and borrowed payload. `nk_plugin_call()` never invokes plugin code on the
caller's thread; the invocation runs on the executor declared by the service,
which is `NK_EXECUTOR_APP` (aliased to the platform thread) in this phase.

## Consequences

- Plugin lifetimes are generation-bound, so a completion or notification can
  never be attributed to a runtime that no longer owns the instance.
- Applications observe plugin results through the same poll-and-release loop
  they already use for WebViews, HTTP, and dialogs.
- The ABI stays small enough to project into language bindings later; the C
  header is the normative contract until a binding importer can represent the
  host function table.
- Splitting render or worker execution later adds executors to existing
  descriptors instead of changing the plugin contract.
