# NativeKit UI

NativeKit UI is the optional retained UI engine above NativeKit's platform and
surface APIs. Haxeon will own components, application state, reconciliation,
and resolved styles. This module will own the retained render tree, layout,
text, hit testing, accessibility projection, and batched rendering.

The vendored experimental implementation stack is:

- Clay for box layout;
- Skribidi for shaping, bidirectional editing, line layout, and glyph atlases;
- Sokol for GPU submission;
- an internal display list which keeps those dependencies out of the public ABI.

See the repository-level [`vendor/README.md`](../../vendor/README.md) for pinned revisions and
[`docs/integration-plan.md`](docs/integration-plan.md) for the integration
sequence and the policy on adapting versus rewriting upstream code.

All third-party integrations must sit behind private adapters. The public C ABI
will use opaque handles, fixed-width values, versioned structures, and validated
batched transactions. It must not expose Clay, Skribidi, NanoVG, or Sokol types.

The module currently establishes the build, install, ABI-test, and dependency
evaluation seams. Skribidi is linked privately; `skribidi_nanovg` and its
NanoVG-Sokol backend are evaluation and visual-reference code, not the permanent
rendering architecture. NativeKit will replace direct NanoVG-Sokol execution
with a recording adapter and a NativeKit-owned compositor and Sokol backend.
Build it with:

```sh
cmake -S . -B build-ui -GNinja -DNK_BUILD_UI=ON
cmake --build build-ui
ctest --test-dir build-ui --output-on-failure
```

The first implementation milestone is a native-only `Box + Text` vertical
slice before defining the Haxeon reconciler API.
