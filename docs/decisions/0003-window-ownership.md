# ADR 0003: Window ownership experiment

NativeKit will support NativeKit-owned top-level windows and borrowed wrapping of
existing native windows. NativeKit-owned windows are the intended full-capability
path. Wrapped-window capabilities remain experimental until SDL coexistence is
validated separately on Win32, Cocoa, X11, and Wayland.

