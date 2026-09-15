# NativeKit HTTP networking

`NativeKit::net` is an optional asynchronous HTTP module. Enable it with
`NK_BUILD_NET=ON`; on Linux, choose the system libcurl backend explicitly:

```sh
cmake -S . -B build \
  -DNK_BUILD_NET=ON \
  -DNK_USE_SYSTEM_CURL=ON
```

The public contract is `include/nativekit_net.h`. Transport details stay in
private `net_*.cpp` or `net_*.mm` files:

| Platform | Transport |
|---|---|
| Android | `HttpsURLConnection` through JNI |
| iOS/macOS | shared `NSURLSession` adapter boundary |
| Windows | WinHTTP adapter boundary |
| Linux | libcurl |
| Web/WASM | Fetch host bridge boundary |

The Android, Apple, Windows, Linux, and Web transports are implemented behind
their private boundaries. Browser Fetch currently advertises buffered HTTP
only because a synchronous pull reader cannot safely block the browser's main
thread; it does not advertise `NK_CAP_HTTP_STREAMING` until a non-blocking WASM
stream bridge is available.

Responses can be buffered with a caller-supplied size limit or consumed from a
bounded pull stream. All delivery is through NativeKit's event queue; worker
threads never invoke application callbacks.
