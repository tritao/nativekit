# WebView2 SDK (subset)

The parts of the Microsoft Edge WebView2 SDK that NativeKit's Windows backend
uses, carried in the repository so configuring needs no network access:

- `include/WebView2.h`: the COM interface declarations.
- `x86/`, `x64/`, `arm64/WebView2Loader.dll`: the loader NativeKit opens at run
  time and installs beside `nativekit.dll`.

They come unmodified from the NuGet package `Microsoft.Web.WebView2`
1.0.4191.47 (SHA-256 of the `.nupkg`:
`f492bbf547d0da329553b6727435b677579b1e9f91cc9e4a1ad029366d5f23d0`), paths
`build/native/include/` and `build/native/<arch>/`. `LICENSE.txt` (BSD-style,
allowing redistribution with this notice) and `NOTICE.txt` are the package's
own and are installed with the loader.

To update, replace these files from a newer package and change the version
above.
