# ADR 0011: Core resource cache ownership

NativeKit owns URI-resource deduplication in the core resource layer so audio,
graphics, and future modules can share one provider load and one retained byte
representation. Modules should not build parallel URI caches with different
lifetimes or cancellation behavior.

`nk_resource_cache` is a UI-thread-owned map keyed by the exact URI string.
Synchronous loads read complete bytes through the existing resource provider.
Asynchronous loads join an existing entry when possible and publish one
cache-ready or cache-failed event for each live asset view. Cache mutation is
explicit: `remove()` and `clear()` cancel pending loads and remove lookup
entries, but independently owned `nk_resource_asset` views retain their shared
entry and any completed bytes.

Asset state and result queries, URI queries, and byte copies are safe from
worker threads. This allows a module such as audio to decode retained bytes on
NativeKit's shared worker pool without adding a module-specific resource
ownership system.
Asset destruction and cache mutation remain UI-thread operations. An
asynchronously failed entry is retained until explicitly removed, making
retries intentional and preventing accidental duplicate provider requests.
