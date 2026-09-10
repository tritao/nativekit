# Haxeon bindings

`nativekit-linux-x86_64.hxi` is the reviewed semantic binding for the public
NativeKit C ABI on 64-bit Linux. The C headers remain authoritative. The file
records target-specific structure layouts plus ownership and parameter
directions that cannot yet be inferred safely from ordinary C declarations.

The binding deliberately begins with lifecycle, event polling, diagnostics,
window handles, and monitor-name lookup. `@out`, `@inout`, and
`@out_buffer("size")` keep raw pointers private in the generated module: Haxe
code receives typed result objects, fixed-layout structure values, and managed
variable-length bytes. Extend this reviewed surface alongside integration
coverage; do not expose arbitrary pointers merely because the header importer
can parse them.

Run the end-to-end smoke test with:

```sh
tools/test-haxeon.sh
```

Regenerate the target-specific interface after changing public headers, or
verify that it is current without rewriting it:

```sh
tools/update-haxeon-hxi.sh
tools/update-haxeon-hxi.sh --check
```

It expects the Haxeon checkout at `../realtime-haxe` by default. Override that
with `HAXEON_DIR=/path/to/realtime-haxe`. Wine continues to validate the same C
ABI independently through `tools/test-wine.sh`; producing Windows HashLink
runtime artifacts is outside this Linux-target binding.
