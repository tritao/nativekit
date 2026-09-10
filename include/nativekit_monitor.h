#ifndef NATIVEKIT_MONITOR_H
#define NATIVEKIT_MONITOR_H

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
/* Monitor data types                                                        */
/* ------------------------------------------------------------------------- */

/** Geometry, work area, physical size, and scale of a monitor. */
typedef struct nk_monitor_geometry {
    /** Set to sizeof(nk_monitor_geometry) before passing the structure. */
    uint32_t struct_size;
    /** Monitor origin in logical desktop coordinates. */
    int32_t x;
    /** Monitor origin in logical desktop coordinates. */
    int32_t y;
    /** Monitor width in logical pixels. */
    int32_t width;
    /** Monitor height in logical pixels. */
    int32_t height;
    /** Work-area origin in logical desktop coordinates. */
    int32_t work_x;
    /** Work-area origin in logical desktop coordinates. */
    int32_t work_y;
    /** Work-area width in logical pixels, excluding reserved system areas. */
    int32_t work_width;
    /** Work-area height in logical pixels, excluding reserved system areas. */
    int32_t work_height;
    /** Physical monitor width in millimeters, or zero when unavailable. */
    int32_t width_mm;
    /** Physical monitor height in millimeters, or zero when unavailable. */
    int32_t height_mm;
    /** Horizontal scale from logical pixels to framebuffer pixels. */
    float scale_x;
    /** Vertical scale from logical pixels to framebuffer pixels. */
    float scale_y;
    /** Reserved; set to zero. */
    uint64_t reserved[2];
} nk_monitor_geometry;

/** A display mode reported by a monitor backend. */
typedef struct nk_video_mode {
    /** Set to sizeof(nk_video_mode) before passing the structure. */
    uint32_t struct_size;
    /** Mode width in physical pixels. */
    int32_t width;
    /** Mode height in physical pixels. */
    int32_t height;
    /** Nominal refresh rate in hertz. */
    double refresh_rate;
    /** Red channel precision in bits. */
    uint32_t red_bits;
    /** Green channel precision in bits. */
    uint32_t green_bits;
    /** Blue channel precision in bits. */
    uint32_t blue_bits;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for future mode information; set all elements to zero. */
    uint64_t reserved2[2];
} nk_video_mode;

/* ------------------------------------------------------------------------- */
/* Monitor enumeration and queries                                           */
/* ------------------------------------------------------------------------- */

/**
 * Writes connected monitor handles in GDK display order. Pass NULL to query
 * the required count. A missing or undersized array returns
 * NK_ERROR_BUFFER_TOO_SMALL and updates inout_count.
 */
NK_API nk_result NK_CALL nk_monitor_list(nk_handle *monitors, uint32_t *inout_count);
/** Returns the primary monitor handle. */
NK_API nk_result NK_CALL nk_monitor_get_primary(nk_handle *out_monitor NK_OUT);
/** Copies the monitor name into a caller-owned UTF-8 buffer. */
NK_API nk_result NK_CALL nk_monitor_get_name(
    nk_handle monitor, char *buffer NK_OUT_BUFFER(inout_size), uint32_t *inout_size NK_INOUT);
/** Returns the monitor geometry in logical desktop coordinates. */
NK_API nk_result NK_CALL nk_monitor_get_geometry(nk_handle monitor,
                                                 nk_monitor_geometry *out_geometry NK_OUT);
/** Returns the mode currently selected by the monitor compositor. */
NK_API nk_result NK_CALL nk_monitor_get_current_mode(nk_handle monitor,
                                                     nk_video_mode *out_mode NK_OUT);
/**
 * GTK 3 exposes only the current compositor mode, so this currently returns a
 * one-element list.
 */
NK_API nk_result NK_CALL nk_monitor_get_modes(nk_handle monitor, nk_video_mode *modes,
                                              uint32_t *inout_count);

/* ------------------------------------------------------------------------- */
/* Window and monitor integration                                            */
/* ------------------------------------------------------------------------- */

/** An invalid monitor handle leaves fullscreen and restores windowed placement. */
NK_API nk_result NK_CALL nk_window_set_fullscreen_monitor(nk_handle window,
                                                          nk_handle monitor);

#ifdef __cplusplus
}
#endif

#endif
