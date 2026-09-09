# ADR 0001: Public C ABI

NativeKit exposes only versioned C structures, fixed-width scalar types, and
generation-checked integer handles. C++ classes, exceptions, standard-library
types, wxWidgets types, and native pointers are private implementation details.

All public entry points catch exceptions. Extensible input and output structures
start with `struct_size`. The API version is checked during initialization.

