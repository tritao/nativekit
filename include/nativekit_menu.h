#ifndef NATIVEKIT_MENU_H
#define NATIVEKIT_MENU_H

#include "nativekit_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A live application menu tree. */
typedef uint32_t nk_menu NK_HANDLE NK_HANDLE_DESTROY(nk_menu_destroy);
/** A live item owned by an application menu. */
typedef uint32_t nk_menu_item NK_HANDLE NK_HANDLE_DESTROY(nk_menu_item_remove);
/** Application-defined command identity carried by activation events. */
typedef uint64_t nk_menu_command_id;

typedef uint32_t nk_menu_item_kind;
enum NK_ENUM(nk_menu_item_kind) {
    NK_MENU_ITEM_COMMAND = 0,
    NK_MENU_ITEM_CHECKBOX = 1,
    NK_MENU_ITEM_RADIO = 2,
    NK_MENU_ITEM_SUBMENU = 3,
    NK_MENU_ITEM_SEPARATOR = 4
};

typedef uint32_t nk_menu_item_role;
enum NK_ENUM(nk_menu_item_role) {
    NK_MENU_ROLE_NONE = 0,
    NK_MENU_ROLE_ABOUT = 1,
    NK_MENU_ROLE_PREFERENCES = 2,
    NK_MENU_ROLE_QUIT = 3,
    NK_MENU_ROLE_HIDE = 4,
    NK_MENU_ROLE_HIDE_OTHERS = 5,
    NK_MENU_ROLE_SHOW_ALL = 6,
    NK_MENU_ROLE_UNDO = 7,
    NK_MENU_ROLE_REDO = 8,
    NK_MENU_ROLE_CUT = 9,
    NK_MENU_ROLE_COPY = 10,
    NK_MENU_ROLE_PASTE = 11,
    NK_MENU_ROLE_SELECT_ALL = 12,
    NK_MENU_ROLE_MINIMIZE = 13,
    NK_MENU_ROLE_ZOOM = 14,
    NK_MENU_ROLE_BRING_ALL_TO_FRONT = 15
};

typedef uint32_t nk_menu_item_flags;
enum NK_FLAGS(nk_menu_item_flags) {
    NK_MENU_ITEM_DISABLED = 1u << 0,
    NK_MENU_ITEM_CHECKED = 1u << 1,
    NK_MENU_ITEM_HIDDEN = 1u << 2
};

typedef uint32_t nk_menu_modifiers;
enum NK_FLAGS(nk_menu_modifiers) {
    /** The platform's primary shortcut modifier (Command on macOS, Control elsewhere). */
    NK_MENU_MOD_PRIMARY = 1u << 0,
    NK_MENU_MOD_SHIFT = 1u << 1,
    NK_MENU_MOD_ALT = 1u << 2,
    NK_MENU_MOD_CONTROL = 1u << 3
};

typedef struct nk_menu_shortcut {
    /** Printable, navigation, function, or keypad key; unknown clears it. */
    nk_key key;
    nk_menu_modifiers modifiers;
} nk_menu_shortcut;

/** Options used to create an application menu root. */
typedef struct nk_menu_options {
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Optional macOS application-menu title; empty leaves the root unwrapped. */
    const char *title NK_NULLABLE_UTF8;
    uint64_t reserved[2];
} nk_menu_options;

/** Options used to add one item to a menu or submenu. */
typedef struct nk_menu_item_options {
    uint32_t struct_size NK_STRUCT_SIZE;
    nk_menu_item_kind kind;
    nk_menu_item_flags flags;
    nk_menu_item_role role;
    nk_menu_command_id command_id;
    const char *label NK_NULLABLE_UTF8;
    nk_menu_shortcut shortcut;
    uint64_t reserved[2];
} nk_menu_item_options;

/** Payload carried by NK_EVENT_MENU_ITEM_ACTIVATED. */
typedef struct nk_menu_item_activated_event {
    nk_menu_command_id command_id;
    nk_menu_item item;
    nk_bool checked;
    uint32_t reserved;
} nk_menu_item_activated_event;

/** Creates an empty application menu tree. */
NK_API nk_result NK_CALL nk_menu_create(const nk_menu_options *options,
                                        nk_menu *out_menu NK_OUT NK_OWNED);
/** Destroys a menu and all of its items. */
NK_API nk_result NK_CALL nk_menu_destroy(nk_menu menu);
/** Adds an item to the root when parent is zero, or to a submenu item. */
NK_API nk_result NK_CALL nk_menu_add_item(nk_menu menu, nk_menu_item parent,
                                          const nk_menu_item_options *options,
                                          nk_menu_item *out_item NK_OUT NK_OWNED);
/** Removes an item and all of its descendants. */
NK_API nk_result NK_CALL nk_menu_item_remove(nk_menu_item item);
/** Replaces the process application menu; zero detaches the current menu. */
NK_API nk_result NK_CALL nk_application_set_menu(nk_menu menu);
/** Updates item state; all strings and model data remain owned by NativeKit. */
NK_API nk_result NK_CALL nk_menu_item_set_label(nk_menu_item item, const char *label NK_UTF8);
NK_API nk_result NK_CALL nk_menu_item_set_enabled(nk_menu_item item, nk_bool enabled);
NK_API nk_result NK_CALL nk_menu_item_set_checked(nk_menu_item item, nk_bool checked);
NK_API nk_result NK_CALL nk_menu_item_set_visible(nk_menu_item item, nk_bool visible);
NK_API nk_result NK_CALL nk_menu_item_set_shortcut(nk_menu_item item,
                                                   const nk_menu_shortcut *shortcut);

#ifdef __cplusplus
}
#endif

#endif
