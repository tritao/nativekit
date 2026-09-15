# NativeKit third-party sources

NativeKit uses Git submodules for source dependencies that must build across its
desktop and mobile toolchains. Initialize them after cloning with:

```sh
git submodule update --init
```

Each dependency revision is pinned by its gitlink in the NativeKit tree; this
document deliberately does not duplicate those hashes. NativeKit's direct
dependencies do not require recursive fetching. The optional Haxeon submodule
is skipped by default because it is a large build tool with its own
dependencies; the web CI job initializes it and the public dependencies it
needs explicitly.

| Path | Purpose |
|---|---|
| `sokol` | Shared graphics implementation source |
| `clay` | Private box layout engine with NativeKit's external paragraph-layout seam |
| `skribidi` | Text shaping, layout, editing, and rasterization |
| `harfbuzz` | OpenType shaping |
| `sheenbidi` | Unicode bidi processing |
| `libunibreak` | Grapheme and line breaking |
| `budouxc` | East Asian word boundaries |
| `nanovg` | UI path construction and tessellation |
| `haxeon` | Optional Haxe compiler and WebAssembly toolchain |
| `miniaudio` | Cross-platform audio device and playback implementation |

Initialize Haxeon when working on its bindings or browser integration:

```sh
git -c submodule.vendor/haxeon.update=checkout submodule update --init --depth=1 --recursive vendor/haxeon
```

Each submodule retains its upstream license. Build glue in the parent project
must keep third-party targets private and must not expose their types through a
NativeKit ABI.
