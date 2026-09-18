# ADR 0019: Native resumable task execution

## Status

Accepted for the first native task scheduler milestone.

## Decision

NativeKit exposes a generation-checked `nk_task` handle for resumable native
state machines. A task step receives opaque native state, a cancellation
snapshot, and a bounded execution budget. It returns `YIELD`, `COMPLETE`, or
`FAILED`; progress and terminal result bytes are copied into NativeKit events.

`AUTO` selects the native worker pool on native targets and cooperative
application-executor scheduling on Web. `BACKGROUND` uses the worker pool and
falls back to cooperative scheduling on Web. `COOPERATIVE` always runs bounded
steps on the application executor. Progress events coalesce by task handle;
terminal events are retained even when ordinary event capacity is exhausted.

The public ABI contains no platform thread or queue types. Worker tasks must
not call UI-only APIs. Results return through `NK_EVENT_TASK_*` events and are
consumed on `NK_EXECUTOR_APP`, where an application or Haxeon layer may resume
a future or promise.

The task callback is a native callback contract. NativeKit does not execute
arbitrary managed Haxe functions on worker threads. A Haxeon runtime may add a
worker-safe trampoline, managed-state ownership, and serialization around this
ABI later; Web cooperative Haxe work can run on the application executor.

Shutdown stops acceptance, requests cancellation, joins native workers,
discards queued work, clears cooperative scheduling, and invalidates the
runtime generation before the next generation can receive events.
