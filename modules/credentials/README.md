# NativeKit credentials module

`NativeKit::credentials` stores small opaque binary credentials in the current
OS user's protected credential store. It is optional and disabled by default;
enable it with `-DNK_BUILD_CREDENTIALS=ON`.

The portable C API is in `include/nativekit_credentials.h`. Values are limited
to 2048 bytes so callers can rely on the same contract across supported
backends. The service and account strings are lookup labels, not secret data.
The module does not enumerate credentials, write files as a fallback, or expose
application pairing policy. Calls are synchronous and may block, so use them
from a worker thread rather than a UI thread.

Current backends are Windows Credential Manager, macOS/iOS Keychain, and the
freedesktop Secret Service on Linux. Linux builds require the `libsecret-1`
development package; GNOME Keyring and compatible Secret Service providers can
serve requests at runtime. On other targets, calls return
`NK_ERROR_UNSUPPORTED`. Browser storage needs a separate WebCrypto-backed
implementation and is not represented as an OS keyring.

If the operating-system store cannot be reached or denies access, the API
returns an error. It never falls back to plaintext storage.

Build and run the module contract test with the usual NativeKit CMake options
plus `-DNK_BUILD_CREDENTIALS=ON`. The test uses a unique temporary service key
and deletes it after the round trip.
