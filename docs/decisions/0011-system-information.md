# ADR 0011: System information, application paths, and orientation

NativeKit exposes global platform/runtime information from
`nativekit_system.h` and per-display information from `nativekit_monitor.h`.
This split is intentional: desktop systems can have multiple displays, while
mobile hosts normally expose one application display.

The API is additive to the existing C ABI. `nk_init_options` keeps its original
prefix valid and app identity fields are read only when covered by
`struct_size`. Application-specific storage is never substituted for the
legacy global `DATA`, `CONFIG`, or `CACHE` directories.

Device identity is limited to vendor and model and may be empty. Filesystem
paths are returned only for real path-backed locations. Android packaged assets
and provider URIs remain resource-API concerns, and mobile system font
directories are explicitly unsupported.

Screen-timeout prevention uses independent keep-awake leases rather than a
shared boolean. Backend assertions are enabled on the first lease and removed
after the last lease or during shutdown, so unrelated callers cannot disable
one another.

Orientation queries distinguish physical device posture from display
presentation. Orientation events carry the shared payload, are deduplicated,
and are coalesced by the event queue. Requests to lock orientation are outside
this milestone.
