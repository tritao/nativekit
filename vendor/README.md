# NativeKit third-party sources

NativeKit uses Git submodules for source dependencies that must build across its
desktop and mobile toolchains. Initialize them after cloning with:

```sh
git submodule update --init
```

The root repository pins every dependency directly; nested dependency fetching
is not used.

| Path | Revision | Purpose |
|---|---|---|
| `sokol` | `5e7dd21eacac60aedc7ea6d80218280d442ce50f` | Shared graphics implementation source |
| `clay` | `e6cc36941ab2af5d81107617039d6f527a1c660b` | Private box layout engine for NativeKit UI |
| `skribidi` | `dee63d6ba76aeddd49dea6d1b2508cf9aa391f46` | Text shaping, layout, editing, and rasterization |
| `harfbuzz` | `ea6a172f84f2cbcfed803b5ae71064c7afb6b5c2` (`11.0.0`) | OpenType shaping |
| `sheenbidi` | `83f77108a2873600283f6da4b326a2dca7a3a7a6` | Unicode bidi processing |
| `libunibreak` | `304585d8e2d63187507368d612c3d5fff1486368` (`libunibreak_6_1`) | Grapheme and line breaking |
| `budouxc` | `a044d49afc654117fac7623fff15bec15943270c` | East Asian word boundaries |
| `nanovg` | `fdf355d1596ea360bc8cf0f6748b73ddc6083acb` | UI path construction and tessellation |

Each submodule retains its upstream license. Build glue in the parent project
must keep third-party targets private and must not expose their types through a
NativeKit ABI.
