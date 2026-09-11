# ADR 0001: Public C ABI

NativeKit exposes only versioned C structures, fixed-width scalar types, and
generation-checked integer handles. C++ classes, exceptions, standard-library
types, framework-specific types, and native pointers are private implementation details.

Typed subsystem handles use the shared `NK_DECLARE_HANDLE(name)` convention,
or an equivalent local macro in standalone headers such as Sokol's
`NKS_HANDLE`. This keeps each C type distinct while giving those handles the
same four-byte `uint32_t id` representation and the `hxi:handle` annotation
consumed by Haxeon's importer. The established generic `nk_handle` keeps its
scalar `uint32_t` C ABI for source compatibility and carries the same HXI
annotation. Handles are value-copyable identities, not ordinary user-editable
records; their receiving subsystem validates generation, slot, kind, and
lifetime as applicable.

All public entry points catch exceptions. Extensible input and output structures
start with `struct_size`. The API version is checked during initialization.
