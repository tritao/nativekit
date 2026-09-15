# ADR 0012: Core worker pool ownership

NativeKit owns one bounded, lazy-started worker pool for short background jobs
shared by modules. Modules must not create an unbounded thread per resource or
per voice; this keeps streaming and future resource work from multiplying
platform threads.

The pool is an internal C++ facility rather than a public job API. Jobs run
without UI-only NativeKit calls and provide an optional cleanup callback. The
cleanup callback runs after the job, including when queued work is discarded
during shutdown, so resource owners can wait for all callbacks before releasing
their state.

`nk_shutdown()` closes the event generation before stopping the pool, then
joins workers before clearing resource handles. Worker jobs may therefore finish
quietly during shutdown, but cannot publish events into a new runtime or access
resources after their owner has been destroyed.

Audio streaming uses one short decode-page job per voice. The decoder and
provider stream remain voice-owned, while the audio callback only reads from
the per-voice PCM ring and requests more work when capacity is available.
