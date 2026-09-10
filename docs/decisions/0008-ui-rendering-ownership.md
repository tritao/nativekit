# ADR 0008: NativeKit owns UI rendering and composition

NativeKit UI needs one ordered rendering model for immediate Canvas calls,
retained widgets, NanoVG paths, Skribidi text, offscreen layers, and future
producers such as 3D and video.

NativeKit owns the canonical semantic 2D display list, compositor, render plan,
GPU resources, and complete Sokol submission path. Canvas transforms, paints,
global alpha, composite modes, clips, layers, resource handles, and ordering are
NativeKit concepts.

NanoVG is a private path and image preparation engine. Its renderer callbacks
record prepared path operations into NativeKit frame storage rather than issuing
GPU commands. Skribidi remains the private authority for shaping, bidi, line and
grapheme breaking, layout, editing, rasterization, and CPU glyph atlases. Its
adapter records direct glyph batches instead of converting glyphs into NanoVG
paths or image-pattern rectangles.

The NativeKit compositor resolves semantic isolation into render targets and
ordered passes. The Sokol backend executes the resulting render plan and is the
only UI subsystem allowed to create Sokol resources or call `sg_*` functions.
NanoVG and Skribidi types never cross the public NativeKit UI ABI.

`nk_surface` continues to mean a platform presentation surface associated with
a NativeKit window. Internal sampleable and offscreen destinations are called
render targets; their IDs are not interchangeable with `nk_surface` handles.

The initial compositor is a linear pass compiler, not a general render graph.
It supports rectangular scissors, source-over composition, and bounded opacity
layers first. Arbitrary clips, destination-dependent blending, filters, and
external render-target producers are added without exposing scheduling details
through the public Canvas API.

The former combined NanoVG/Skribidi example and NanoVG-Sokol backend are not
part of the NativeKit build. NativeKit's path preparation, compositor, and
Sokol backend are the sole runtime path; the recorder remains only as a
compatibility adapter and focused test fixture.
