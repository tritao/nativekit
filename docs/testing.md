# Testing

The normal CTest suite covers the core ABI and the host platform backend. Linux
GUI tests run under Xvfb when available.

## Windows compatibility smoke tests

`tools/test-wine.sh` cross-builds a static Windows test suite with MinGW and runs
it under Wine using a fresh temporary prefix. It covers DLL-independent C ABI
loading, Win32 window creation, UTF-8 title conversion, visibility, bounds, DPI,
native descriptor export, message pumping, asynchronous file and message dialog
cancellation, known-folder and locale queries, appearance detection, shell input
validation, destruction, and stale handles.

Required commands are `x86_64-w64-mingw32-gcc`,
`x86_64-w64-mingw32-g++`, `wine`, `wineserver`, `cmake`, `ninja`, and `xvfb-run`.

```sh
tools/test-wine.sh
```

The Wine suite is a compatibility layer, not authoritative Windows validation.
Native Windows CI remains required for COM, accessibility, per-monitor DPI,
system integration, and WebView behavior.
