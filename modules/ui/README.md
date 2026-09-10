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
The reusable NanoVG path API is documented in
[`docs/nanovg-path-preparation.md`](docs/nanovg-path-preparation.md).

All third-party integrations must sit behind private adapters. The public C ABI
will use opaque handles, fixed-width values, versioned structures, and validated
batched transactions. It must not expose Clay, Skribidi, NanoVG, or Sokol types.

The module now has a validated semantic display list, a reusable NanoVG path
preparation adapter, direct Skribidi glyph batches, a NativeKit compositor, and
a NativeKit-owned Sokol backend. Public rendering uses opaque NativeKit UI
resources and `nkui_renderer_render`; NanoVG does not own text or GPU
submission.

Build it with:

```sh
cmake -S . -B build-ui -GNinja -DNK_BUILD_UI=ON
cmake --build build-ui
ctest --test-dir build-ui --output-on-failure
```

Run the interactive public-API showcase with:

```sh
./build-ui/modules/ui/nativekit_ui_showcase
```

The same executable supports `--smoke-test`, which renders 30 frames and exits.
