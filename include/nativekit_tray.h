#ifndef NATIVEKIT_TRAY_H
#define NATIVEKIT_TRAY_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit_menu.h"

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Tray icons                                                                */
/* ------------------------------------------------------------------------- */

/**
 * A status icon in the desktop notification area (the Windows system tray).
 * Only the Windows backend implements trays; other backends return
 * NK_ERROR_UNSUPPORTED. Events carry the tray handle as their source:
 * NK_EVENT_TRAY_ACTIVATED for a primary click (flags is 1 for a double click)
 * and NK_EVENT_TRAY_MENU_REQUESTED for a secondary click or the keyboard
 * context-menu gesture. The application answers a menu request by building a
 * menu and calling nk_tray_show_menu, so the menu reflects current state.
 */
typedef uint32_t nk_tray NK_HANDLE NK_HANDLE_DESTROY(nk_tray_destroy);

/** Options for nk_tray_create. */
typedef struct nk_tray_options {
    /** Set to sizeof(nk_tray_options) before passing the structure. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Reserved; set to zero. */
    uint32_t flags;
    /** Optional UTF-8 hover text; the platform may truncate it (Windows keeps 127 characters). */
    const char *tooltip NK_NULLABLE_UTF8;
    /** Optional absolute path of an icon file (.ico on Windows); empty uses a generic icon. */
    const char *icon NK_NULLABLE_UTF8;
    /** Reserved for future options; set all elements to zero. */
    uint64_t reserved[2];
} nk_tray_options;

/** Adds a tray icon. Must be called on the UI thread. */
NK_API nk_result NK_CALL nk_tray_create(const nk_tray_options *options,
                                        nk_tray *out_tray NK_OUT NK_OWNED);
/** Removes the tray icon. */
NK_API nk_result NK_CALL nk_tray_destroy(nk_tray tray);
/** Replaces the hover text. */
NK_API nk_result NK_CALL nk_tray_set_tooltip(nk_tray tray, const char *tooltip NK_UTF8);
/**
 * Shows `menu` as a popup at the pointer and returns once it closes. A chosen
 * item is reported as NK_EVENT_MENU_ITEM_ACTIVATED, as for application menus.
 * The menu is not retained; the caller may destroy it after this returns.
 */
NK_API nk_result NK_CALL nk_tray_show_menu(nk_tray tray, nk_menu menu);

#ifdef __cplusplus
}
#endif

#endif
