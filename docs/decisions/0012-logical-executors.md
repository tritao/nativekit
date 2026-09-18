# ADR 0012: Logical executors and application dispatch

## Status

Accepted. The first implementation binds every executor to the `nk_init()`
thread and adds the dispatch primitive plugins and native callbacks use to get
back onto it.

## Decision

NativeKit names four logical executors instead of threads:

| Executor | Contract |
| --- | --- |
| `NK_EXECUTOR_PLATFORM` | Windows, surfaces, monitors, presentation, input |
| `NK_EXECUTOR_APP` | Application callbacks and event delivery |
| `NK_EXECUTOR_RENDER` | Rendering against an immutable frame target |
| `NK_EXECUTOR_WORKER` | CPU work on arbitrary native threads |

Today `nk_init()` binds `NK_EXECUTOR_PLATFORM`, `NK_EXECUTOR_APP`, and
`NK_EXECUTOR_RENDER` to one thread, which keeps current behavior unchanged while
forcing every new contract to declare the affinity it needs. `NK_EXECUTOR_APP`
is the existing UI thread; `nk_executor_current()` reports `NK_EXECUTOR_WORKER`
on any thread that is not that bound thread.

`nk_dispatch_to_app()` is the only sanctioned path back onto the application
executor. It enqueues a bounded task and wakes the event wait; it never invokes
the task directly, so native callbacks delivered on arbitrary threads cannot
reenter application or language-runtime code from the wrong thread. Tasks run in
a detached batch during the next `nk_poll_event()` on the application thread and
are discarded by `nk_shutdown()`.

Making this split explicit is what allows a later change to move
`NK_EXECUTOR_RENDER` onto its own thread without redefining existing APIs: the
affinity annotations, not the current thread contents, are the contract.

## Consequences

- Existing UI-thread APIs keep their behavior; they now validate through
  `nk_executor()` instead of an ad-hoc thread check.
- New APIs must state which executor owns them.
- Application code that receives native callbacks from arbitrary threads can
  defer work with one bounded queue instead of inventing its own marshalling.
- Frame/render-plan work still executes on the platform thread until a later
  phase splits it; the executor names are already in place for that change.
