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

typedef struct nk_monitor_geometry {
    uint32_t struct_size;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t work_x;
    int32_t work_y;
    int32_t work_width;
    int32_t work_height;
    int32_t width_mm;
    int32_t height_mm;
    float scale_x;
    float scale_y;
    uint64_t reserved[2];
} nk_monitor_geometry;

typedef struct nk_video_mode {
    uint32_t struct_size;
    int32_t width;
    int32_t height;
    double refresh_rate;
    uint32_t red_bits;
    uint32_t green_bits;
    uint32_t blue_bits;
    uint32_t reserved;
    uint64_t reserved2[2];
} nk_video_mode;

/* ------------------------------------------------------------------------- */
/* Monitor enumeration and queries                                           */
/* ------------------------------------------------------------------------- */

/*
 * Writes connected monitor handles in GDK display order. Pass NULL to query
 * the required count. A missing or undersized array returns
 * NK_ERROR_BUFFER_TOO_SMALL and updates inout_count.
 */
NK_API nk_result NK_CALL nk_monitor_list(nk_handle *monitors, uint32_t *inout_count);
NK_API nk_result NK_CALL nk_monitor_get_primary(nk_handle *out_monitor NK_OUT);
NK_API nk_result NK_CALL nk_monitor_get_name(
    nk_handle monitor, char *buffer NK_OUT_BUFFER(inout_size), uint32_t *inout_size NK_INOUT);
NK_API nk_result NK_CALL nk_monitor_get_geometry(nk_handle monitor,
                                                 nk_monitor_geometry *out_geometry NK_OUT);
NK_API nk_result NK_CALL nk_monitor_get_current_mode(nk_handle monitor,
                                                     nk_video_mode *out_mode NK_OUT);
/*
 * GTK 3 exposes only the current compositor mode, so this currently returns a
 * one-element list.
 */
NK_API nk_result NK_CALL nk_monitor_get_modes(nk_handle monitor, nk_video_mode *modes,
                                              uint32_t *inout_count);

/* ------------------------------------------------------------------------- */
/* Window and monitor integration                                            */
/* ------------------------------------------------------------------------- */

/* An invalid monitor handle leaves fullscreen and restores windowed placement. */
NK_API nk_result NK_CALL nk_window_set_fullscreen_monitor(nk_handle window,
                                                          nk_handle monitor);

#ifdef __cplusplus
}
#endif

#endif
