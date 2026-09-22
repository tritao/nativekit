# NativeKit third-party sources

NativeKit uses Git submodules for source dependencies that must build across its
desktop and mobile toolchains. Initialize them after cloning with:

```sh
git submodule update --init
```

Each submodule revision is pinned by its gitlink in the NativeKit tree; this
document deliberately does not duplicate those hashes. NativeKit's direct
dependencies do not require recursive fetching. Haxeon and UIKit are sibling
Materia projects rather than NativeKit vendor dependencies.

| Path | Purpose |
|---|---|
| `libwebsockets` | Native WebSocket client/server and platform-default TLS integration |
| `sokol` | Shared graphics implementation source |
| `SDL_GameControllerDB` | Generated controller mapping database source |

Each submodule retains its upstream license. Build glue in the parent project
must keep third-party targets private and must not expose their types through a
NativeKit ABI.
