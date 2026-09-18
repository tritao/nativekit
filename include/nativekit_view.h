#ifndef NATIVEKIT_VIEW_H
#define NATIVEKIT_VIEW_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit.h"

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Native child views                                                        */
/* ------------------------------------------------------------------------- */

/**
 * A NativeKit-owned native child view.
 *
 * A view is the general form of the native children a window already hosts,
 * such as a WebView: NativeKit owns its lifecycle and placement, and the
 * application owns its content through the borrowed platform handle returned
 * by nk_view_get_native(). Geometry, visibility, and clipping are pending
 * state that only becomes the committed state when nk_view_commit() publishes
 * it, so a frame's layout changes apply as one unit instead of one setter at a
 * time.
 */
typedef uint32_t nk_view NK_HANDLE NK_HANDLE_DESTROY(nk_view_destroy);

/** Creation flags for nk_view_create(). */
typedef uint32_t nk_view_flags;
enum NK_FLAGS(nk_view_flags) {
    /** Create the view hidden until nk_view_set_visible() shows it. */
    NK_VIEW_HIDDEN = 1u << 0
};

/** Options used to create a native child view. */
typedef struct nk_view_options {
    /** Set to sizeof(nk_view_options) before calling nk_view_create. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Bitwise OR of NK_VIEW_* flags. */
    nk_view_flags flags;
    /** Initial left edge in the parent's logical coordinates. */
    int32_t x;
    /** Initial top edge in the parent's logical coordinates. */
    int32_t y;
    /** Initial logical width; must be positive. */
    int32_t width;
    /** Initial logical height; must be positive. */
    int32_t height;
    /** Reserved for future options; initialize to zero. */
    uint64_t reserved[2];
} nk_view_options;

/** Committed logical-pixel rectangle of a native child view. */
typedef struct nk_view_bounds {
    /** Set to sizeof(nk_view_bounds) before calling nk_view_get_bounds. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Committed left edge in the parent's logical coordinates. */
    int32_t x;
    /** Committed top edge in the parent's logical coordinates. */
    int32_t y;
    /** Committed logical width. */
    int32_t width;
    /** Committed logical height. */
    int32_t height;
    /** Reserved for future use; initialize to zero. */
    uint64_t reserved[2];
} nk_view_bounds;

/** Discriminator identifying the platform representation in nk_native_view. */
typedef uint32_t nk_native_view_kind;

enum NK_ENUM(nk_native_view_kind) {
    /** No platform representation is available. */
    NK_NATIVE_VIEW_UNKNOWN = 0,
    /** The view value is a GtkWidget*. */
    NK_NATIVE_VIEW_GTK = 1,
    /** The view value is a Win32 HWND. */
    NK_NATIVE_VIEW_WIN32 = 2,
    /** The view value is a Cocoa NSView*. */
    NK_NATIVE_VIEW_COCOA = 3,
    /** The view value is an Android View reference. */
    NK_NATIVE_VIEW_ANDROID = 4,
    /** The view value is an iOS UIView*. */
    NK_NATIVE_VIEW_IOS = 5,
    /** The view value identifies a DOM element created by the browser host. */
    NK_NATIVE_VIEW_DOM = 6
};

/** Borrowed platform handles for a NativeKit native child view. */
typedef struct nk_native_view {
    /** Set to sizeof(nk_native_view) before passing this structure to NativeKit. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Identifies which platform-specific field is populated. */
    nk_native_view_kind kind;
    /** Reserved for future platform-neutral interoperation flags; initialize to zero. */
    uint32_t flags;
    /** Reserved for future use; initialize to zero. */
    uint32_t reserved;
    /** Platform view handle; NULL when the backend has none. */
    uintptr_t view;
    /** Reserved pointer-sized values; initialize to zero. */
    uintptr_t reserved2[3];
} nk_native_view;

/* ------------------------------------------------------------------------- */
/* Native child view APIs                                                    */
/* ------------------------------------------------------------------------- */

/**
 * Creates a native child view inside `parent`.
 *
 * Desktop backends accept an nk_window parent and mobile backends accept an
 * nk_mobile_host; zero is accepted where the backend supports an unparented
 * view. Bounds are logical pixels and width and height must be positive. The
 * view starts at its initial bounds and is visible unless NK_VIEW_HIDDEN is
 * set. On success, `out_view` receives a non-zero generation-checked handle
 * owned by NativeKit and destroyed with its parent.
 */
NK_API nk_result NK_CALL nk_view_create(nk_handle parent, const nk_view_options *options,
                                        nk_view *out_view NK_OUT NK_OWNED);

/**
 * Destroys a native child view and detaches it from its parent.
 *
 * The caller's platform widgets are not destroyed, only the NativeKit-owned
 * container that hosts them. The handle becomes invalid after this call and
 * must not be reused.
 */
NK_API nk_result NK_CALL nk_view_destroy(nk_view view);

/**
 * Returns the borrowed platform view the application populates.
 *
 * The descriptor is valid only while the view is alive, and its values must
 * never be freed by callers. This is an explicit interoperability escape
 * hatch, not a portable resource.
 */
NK_API nk_result NK_CALL nk_view_get_native(nk_view view, nk_native_view *out_native NK_OUT);

/**
 * Records the view's logical-pixel rectangle as pending state.
 *
 * Width and height must be positive. The committed rectangle does not change
 * until nk_view_commit() publishes every pending change.
 */
NK_API nk_result NK_CALL nk_view_set_bounds(nk_view view, int32_t x, int32_t y, int32_t width,
                                            int32_t height);

/** Records whether the view is visible as pending state. */
NK_API nk_result NK_CALL nk_view_set_visible(nk_view view, nk_bool visible);

/**
 * Records the clip rectangle as pending state.
 *
 * When a clip is enabled the view may only occupy the intersection of its
 * bounds and the clip rectangle, both in the parent's logical coordinates, so
 * a view that straddles its ancestor's clip boundary is constrained rather
 * than drawn outside it. Width and height must be positive when enabling.
 * Disabling the clip restores the full bounds.
 */
NK_API nk_result NK_CALL nk_view_set_clip(nk_view view, nk_bool enabled, int32_t x, int32_t y,
                                          int32_t width, int32_t height);

/**
 * Publishes every pending change and makes it the committed state.
 *
 * Call this once per frame after updating one or more views so a layout change
 * applies as a unit: the platform updates each view before the next frame is
 * presented, and nk_view_get_bounds() reports the committed rectangle
 * afterwards. Committing without pending changes is a no-op.
 */
NK_API nk_result NK_CALL nk_view_commit(nk_view view);

/**
 * Fills in the committed logical-pixel rectangle.
 *
 * This reports the published state, not pending edits made since the last
 * nk_view_commit().
 */
NK_API nk_result NK_CALL nk_view_get_bounds(nk_view view, nk_view_bounds *out_bounds NK_OUT);

#ifdef __cplusplus
}
#endif

#endif
