# ADR 0018: Native child views

## Status

Partially implemented. The GTK backend implements the API below; Windows,
macOS, Android, iOS, and the Web backend return `NK_ERROR_UNSUPPORTED` and
report `NK_CAP_NATIVE_VIEW` as deferred until their implementations land.

## Context

Windows already host native children: `nk_webview_*` creates a platform WebView
inside a window's container, and the UI layer resolves a rectangle for
content it does not paint itself (`LayoutMeasuredContent` drives "canvas,
native-view, and editor surfaces"). What was missing is the general form of
that relationship: a NativeKit-owned native child view whose lifecycle and
placement NativeKit owns while the application owns the content.

Two constraints shape the design:

1. A native child is placed by the platform window system, not by the GPU
   compositor, so its geometry cannot be interpolated: whatever rectangle is
   set is what the platform shows.
2. A layout change usually touches several views at once. Applying setters one
   at a time exposes intermediate layouts to the platform, which is exactly the
   partially-updated frame that ADR 0015's frame transactions exist to avoid
   for surfaces.

## Decision

Add `nativekit_view.h` with a `nk_view` handle:

```
nk_view_create(parent, options, &view)   parent is a window or mobile host
nk_view_destroy(view)
nk_view_get_native(view, &native)        borrowed platform view for app content
nk_view_set_bounds(view, x, y, w, h)     pending
nk_view_set_visible(view, visible)       pending
nk_view_set_clip(view, on, x, y, w, h)   pending
nk_view_commit_parent(parent)            publishes the whole child set atomically
nk_view_commit(view)                     compatibility entry point for that parent set
nk_view_get_bounds(view, &bounds)        committed rectangle
```

Bounds, visibility, and clipping are *pending* state. `nk_view_commit_parent()`
copies and applies all pending child state as one parent transaction, and
`nk_view_commit()` is an equivalent compatibility entry point when a view is
available. `nk_view_get_bounds()` reports the committed rectangle rather than
pending edits, so a caller can see exactly what the platform has been told. An
enabled clip constrains the rectangle the view
may occupy to the intersection of its bounds and the clip rectangle, both in
the parent's logical coordinates; a fully clipped view is hidden rather than
allocated a zero-size rectangle.

`nk_view_get_native()` returns a borrowed platform descriptor
(`nk_native_view`) so the application can parent its own widget, view, or DOM
element. The descriptor is an interoperability escape hatch with the same
ownership rules as `nk_window_get_native()`; NativeKit never destroys the
caller's content, only the container it hosts.

The feature is advertised through `NK_CAP_NATIVE_VIEW`, so backends that do not
implement it stay discoverable instead of failing at first use.

## Consequences

NativeKit owns view lifecycle: destroying a window destroys its views, and
destroying a view detaches it without destroying caller content. Views are
created from a window (or mobile host) handle, matching `nk_webview_create`, so
the existing child-container plumbing is shared rather than duplicated.

Commit is per view rather than tied to an `nk_surface_frame` token. Frame
tokens name surfaces, and a view is parented to a window that may own several
surfaces, so binding a view to a token would require the view to choose one.
The platform applies committed geometry before the next presented frame, which
is the property the transaction model actually needs.

## Open questions

1. Content clipping: the intersection rule constrains the rectangle a view
   occupies. Clipping a view's own rendering to a larger clip rectangle
   (partial display of an oversized child) is backend-specific and deferred.
2. Input and focus routing between a native child and NativeKit input is
   currently the platform's own business, because the child is a real native
   widget in the hierarchy.
3. Stacking relative to graphics surfaces is platform-defined today; a
   portable z-order contract would need a separate decision.

## Migration

1. GTK: a `GtkFixed` container per view, placed in the parent window's fixed
   container, with commit applying the intersection rectangle. Done; covered by
   `gtk_native_view`.
2. Windows, macOS, Android, iOS, and Web: child HWND, NSView, View, UIView, and
   DOM element respectively, each advertised only once implemented.
