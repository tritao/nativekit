# ADR 0003: Window ownership experiment

NativeKit supports NativeKit-owned top-level windows and ownership-safe borrowed
wrapping of existing native windows. NativeKit-owned windows remain the
full-capability path. Win32, Cocoa, and X11 wrappers retain only the references
needed for window interoperation; destroying a wrapper never destroys the host
object and the host retains native event dispatch. Wayland wrapping remains
deferred until foreign-surface lifecycle and event ownership can be made
explicit.
