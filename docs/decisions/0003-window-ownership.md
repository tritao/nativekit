# ADR 0003: Window ownership experiment

NativeKit supports NativeKit-owned top-level windows and ownership-safe borrowed
wrapping of existing native windows. NativeKit-owned windows remain the
full-capability path. Win32, Cocoa, and X11 wrappers retain only the references
needed for window interoperation; destroying a wrapper never destroys the host
object and the host retains native event dispatch. GTK Wayland wrappers retain
only the caller's borrowed display/surface descriptor. GTK3 cannot adopt a
foreign `wl_surface` as a `GdkWindow`, so these wrappers deliberately support
native descriptor access and destruction while operations requiring GTK window
ownership remain unsupported.
