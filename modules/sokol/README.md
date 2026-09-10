# NativeKit Sokol module

This isolated experiment uses NativeKit for its window, event loop, OpenGL
context, framebuffer sizing, and presentation. `sokol_gfx.h` owns only rendering
resources and draw submission. It deliberately does not use `sokol_app.h`.

The first milestone is Linux/OpenGL only. Sokol is pinned in `CMakeLists.txt` so
that changes to its source-level API cannot silently change the experiment.

Build and run from the NativeKit repository root:

```sh
cmake -S . -B build-sokol -GNinja -DNK_BUILD_SOKOL=ON
cmake --build build-sokol
./build-sokol/modules/sokol/nativekit_sokol_triangle
```

For a bounded 30-frame run (suitable for Xvfb):

```sh
xvfb-run -a ./build-sokol/modules/sokol/nativekit_sokol_triangle --smoke-test
```

Generate the curated HXI binding for the adapter:

```sh
modules/sokol/tools/check-hxi.sh
```

Run the end-to-end Haxeon triangle test with:

```sh
modules/sokol/tools/test-haxeon.sh
```

The scene is assembled in Haxe from generic buffers, shaders, pipeline
attributes, bindings, and a draw call. There is no triangle-specific operation
in the native adapter. The current test renders an indexed quad, uploads a
vertex-stage uniform block each frame, constructs an RGBA8 checkerboard in Haxe,
and binds its image and sampler independently. Frame state, inline uniform data,
and draws are encoded with the generic `SokolCommandBuffer` Haxe helper and sent
through `nks_submit_commands` in one HXI call. The immediate calls remain
available for simple rendering and debugging.

Renderer, resource, and builder handles are distinct one-word value types in
the public C ABI. Their IDs encode a resource kind, generation, and pool slot;
the raw token stays internal to the adapter. HXI projects each C type to a
distinct nominal Haxe abstract, preventing cross-type calls before runtime.

Measure the HXI call boundary independently of graphics work with:

```sh
modules/sokol/tools/benchmark.sh
```

On the initial x86-64 Linux test machine, one million scalar HXI calls took
317–347 ms across five warm runs (about 317–347 ns per call). A single call
which performed the same million operations in native code took about 0.8 ms.
In the first packed-command benchmark, 10,000 simulated draw records took about
3.33 ms as individual HXI calls and 0.030 ms as one packed submission, including
native command parsing. This measures CPU submission overhead rather than GPU
rendering, but confirms that batching is valuable for draw-heavy scenes.

The GTK backend renders through a `GtkGLArea`. Its framebuffer is not assumed to
be zero: the prototype queries the current draw framebuffer after NativeKit
makes the surface current and passes that value in Sokol's `sg_swapchain`.

Next steps after this native seam is verified:

1. Exercise multiple pipelines and resource sets in a larger rendered scene.
2. Design shared-context surface creation before enabling multiple renderers.
3. Add backend adapters independently, beginning with Vulkan or Metal.
