#import <Cocoa/Cocoa.h>

#include "nativekit_menu.h"

#include "core/menu_internal.hpp"
#include "core/runtime.hpp"

#include <string>
#include <unordered_map>

namespace {
void perform_native_role(nk_menu_item handle, id sender);
}

@interface NKMenuActionTarget : NSObject
- (void)activate:(id)sender;
@end

@implementation NKMenuActionTarget
- (void)activate:(id)sender {
    if (![sender isKindOfClass:[NSMenuItem class]])
        return;
    const auto handle = static_cast<nk_menu_item>([(NSMenuItem *)sender tag]);
    perform_native_role(handle, sender);
    nk::core::menu_item_activated(handle);
}
@end

namespace {

struct MacMenuState {
    nk_menu handle = NK_INVALID_HANDLE;
    __strong NSMenu *menu = nil;
    __strong NSMenu *application_menu = nil;
    __strong NSMenu *previous = nil;
    __strong id target = nil;
    std::unordered_map<nk_menu_item, NSMenuItem *> items;
};

MacMenuState state;

NSString *menu_string(const std::string &value) {
    return [[NSString alloc] initWithBytes:value.data()
                                    length:value.size()
                                  encoding:NSUTF8StringEncoding];
}

NSString *shortcut_key(nk_key key) {
    if (key >= NK_KEY_A && key <= NK_KEY_Z)
        return [[NSString alloc] initWithFormat:@"%c", static_cast<int>(key + ('a' - 'A'))];
    if (key >= NK_KEY_0 && key <= NK_KEY_9)
        return [[NSString alloc] initWithFormat:@"%c", static_cast<int>(key)];
    if (key >= NK_KEY_F1 && key <= NK_KEY_F24)
        return [[NSString alloc]
            initWithFormat:@"%C", static_cast<unichar>(NSF1FunctionKey + key - NK_KEY_F1)];
    if (key >= NK_KEY_KP_0 && key <= NK_KEY_KP_9)
        return [[NSString alloc] initWithFormat:@"%c", static_cast<int>('0' + key - NK_KEY_KP_0)];
    switch (key) {
    case NK_KEY_SPACE:
        return @" ";
    case NK_KEY_APOSTROPHE:
        return @"'";
    case NK_KEY_COMMA:
        return @",";
    case NK_KEY_MINUS:
        return @"-";
    case NK_KEY_PERIOD:
        return @".";
    case NK_KEY_SLASH:
        return @"/";
    case NK_KEY_SEMICOLON:
        return @";";
    case NK_KEY_EQUAL:
        return @"=";
    case NK_KEY_LEFT_BRACKET:
        return @"[";
    case NK_KEY_BACKSLASH:
        return @"\\";
    case NK_KEY_RIGHT_BRACKET:
        return @"]";
    case NK_KEY_GRAVE_ACCENT:
        return @"`";
    case NK_KEY_TAB:
        return @"\t";
    case NK_KEY_ENTER:
        return @"\r";
    case NK_KEY_ESCAPE:
        return @"\033";
    case NK_KEY_BACKSPACE:
        return @"\b";
    case NK_KEY_LEFT:
        return
            [[NSString alloc] initWithFormat:@"%C", static_cast<unichar>(NSLeftArrowFunctionKey)];
    case NK_KEY_RIGHT:
        return
            [[NSString alloc] initWithFormat:@"%C", static_cast<unichar>(NSRightArrowFunctionKey)];
    case NK_KEY_UP:
        return [[NSString alloc] initWithFormat:@"%C", static_cast<unichar>(NSUpArrowFunctionKey)];
    case NK_KEY_DOWN:
        return
            [[NSString alloc] initWithFormat:@"%C", static_cast<unichar>(NSDownArrowFunctionKey)];
    case NK_KEY_INSERT:
        return [[NSString alloc] initWithFormat:@"%C", static_cast<unichar>(NSInsertFunctionKey)];
    case NK_KEY_DELETE:
        return [[NSString alloc] initWithFormat:@"%C", static_cast<unichar>(NSDeleteFunctionKey)];
    case NK_KEY_PAGE_UP:
        return [[NSString alloc] initWithFormat:@"%C", static_cast<unichar>(NSPageUpFunctionKey)];
    case NK_KEY_PAGE_DOWN:
        return [[NSString alloc] initWithFormat:@"%C", static_cast<unichar>(NSPageDownFunctionKey)];
    case NK_KEY_HOME:
        return [[NSString alloc] initWithFormat:@"%C", static_cast<unichar>(NSHomeFunctionKey)];
    case NK_KEY_END:
        return [[NSString alloc] initWithFormat:@"%C", static_cast<unichar>(NSEndFunctionKey)];
    case NK_KEY_KP_DECIMAL:
        return @".";
    case NK_KEY_KP_DIVIDE:
        return @"/";
    case NK_KEY_KP_MULTIPLY:
        return @"*";
    case NK_KEY_KP_SUBTRACT:
        return @"-";
    case NK_KEY_KP_ADD:
        return @"+";
    case NK_KEY_KP_ENTER:
        return @"\r";
    case NK_KEY_KP_EQUAL:
        return @"=";
    default:
        return nil;
    }
}

nk_menu_shortcut default_role_shortcut(nk_menu_item_role role) {
    switch (role) {
    case NK_MENU_ROLE_PREFERENCES:
        return {NK_KEY_COMMA, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_HIDE:
        return {NK_KEY_H, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_HIDE_OTHERS:
        return {NK_KEY_H, NK_MENU_MOD_PRIMARY | NK_MENU_MOD_ALT};
    case NK_MENU_ROLE_UNDO:
        return {NK_KEY_Z, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_REDO:
        return {NK_KEY_Z, NK_MENU_MOD_PRIMARY | NK_MENU_MOD_SHIFT};
    case NK_MENU_ROLE_CUT:
        return {NK_KEY_X, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_COPY:
        return {NK_KEY_C, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_PASTE:
        return {NK_KEY_V, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_SELECT_ALL:
        return {NK_KEY_A, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_MINIMIZE:
        return {NK_KEY_M, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_QUIT:
        return {NK_KEY_Q, NK_MENU_MOD_PRIMARY};
    default:
        return {NK_KEY_UNKNOWN, 0};
    }
}

NSEventModifierFlags shortcut_modifiers(nk_menu_modifiers modifiers) {
    NSEventModifierFlags result = 0;
    if (modifiers & NK_MENU_MOD_PRIMARY)
        result |= NSEventModifierFlagCommand;
    if (modifiers & NK_MENU_MOD_SHIFT)
        result |= NSEventModifierFlagShift;
    if (modifiers & NK_MENU_MOD_ALT)
        result |= NSEventModifierFlagOption;
    if (modifiers & NK_MENU_MOD_CONTROL)
        result |= NSEventModifierFlagControl;
    return result;
}

void perform_native_role(nk_menu_item handle, id sender) {
    const auto resource = std::static_pointer_cast<nk::core::MenuItemResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::menu_item));
    if (!resource || (resource->flags & NK_MENU_ITEM_DISABLED) ||
        (resource->flags & NK_MENU_ITEM_HIDDEN) || resource->kind == NK_MENU_ITEM_SEPARATOR ||
        resource->kind == NK_MENU_ITEM_SUBMENU)
        return;
    switch (resource->role) {
    case NK_MENU_ROLE_ABOUT:
        [NSApp orderFrontStandardAboutPanel:sender];
        break;
    case NK_MENU_ROLE_PREFERENCES:
        [NSApp sendAction:@selector(showPreferencesWindow:) to:nil from:sender];
        break;
    case NK_MENU_ROLE_HIDE:
        [NSApp hide:sender];
        break;
    case NK_MENU_ROLE_HIDE_OTHERS:
        [NSApp hideOtherApplications:sender];
        break;
    case NK_MENU_ROLE_SHOW_ALL:
        [NSApp unhideAllApplications:sender];
        break;
    case NK_MENU_ROLE_UNDO:
        [NSApp sendAction:@selector(undo:) to:nil from:sender];
        break;
    case NK_MENU_ROLE_REDO:
        [NSApp sendAction:@selector(redo:) to:nil from:sender];
        break;
    case NK_MENU_ROLE_CUT:
        [NSApp sendAction:@selector(cut:) to:nil from:sender];
        break;
    case NK_MENU_ROLE_COPY:
        [NSApp sendAction:@selector(copy:) to:nil from:sender];
        break;
    case NK_MENU_ROLE_PASTE:
        [NSApp sendAction:@selector(paste:) to:nil from:sender];
        break;
    case NK_MENU_ROLE_SELECT_ALL:
        [NSApp sendAction:@selector(selectAll:) to:nil from:sender];
        break;
    case NK_MENU_ROLE_MINIMIZE:
        [NSApp sendAction:@selector(performMiniaturize:) to:nil from:sender];
        break;
    case NK_MENU_ROLE_ZOOM:
        [NSApp sendAction:@selector(performZoom:) to:nil from:sender];
        break;
    case NK_MENU_ROLE_BRING_ALL_TO_FRONT:
        [NSApp arrangeInFront:sender];
        break;
    case NK_MENU_ROLE_NONE:
    case NK_MENU_ROLE_QUIT:
        break;
    }
}

void set_native_state(NSMenuItem *native, const nk::core::MenuItemResource &item) {
    NSString *title = menu_string(item.label);
    [native setTitle:title ?: @""];
    [native setEnabled:(item.flags & NK_MENU_ITEM_DISABLED) == 0];
    [native setHidden:(item.flags & NK_MENU_ITEM_HIDDEN) != 0];
    [native setState:(item.flags & NK_MENU_ITEM_CHECKED) ? NSControlStateValueOn
                                                         : NSControlStateValueOff];
    const auto shortcut =
        item.shortcut.key == NK_KEY_UNKNOWN ? default_role_shortcut(item.role) : item.shortcut;
    NSString *key = shortcut_key(shortcut.key);
    [native setKeyEquivalent:key ?: @""];
    [native setKeyEquivalentModifierMask:shortcut_modifiers(shortcut.modifiers)];
}

NSMenuItem *add_native_item(const std::shared_ptr<nk::core::MenuItemResource> &item,
                            NSMenu *parent) {
    NSMenuItem *native = nil;
    if (item->kind == NK_MENU_ITEM_SEPARATOR) {
        native = [NSMenuItem separatorItem];
    } else {
        NSString *title = menu_string(item->label);
        native = [[NSMenuItem alloc] initWithTitle:title ?: @""
                                            action:@selector(activate:)
                                     keyEquivalent:@""];
        [native setTarget:state.target];
        [native setTag:static_cast<NSInteger>(item->handle)];
        set_native_state(native, *item);
        if (item->kind == NK_MENU_ITEM_SUBMENU) {
            NSString *submenu_title = menu_string(item->label);
            NSMenu *submenu = [[NSMenu alloc] initWithTitle:submenu_title ?: @""];
            [submenu setAutoenablesItems:NO];
            [native setSubmenu:submenu];
        }
    }
    [parent addItem:native];
    state.items[item->handle] = native;
    if (item->kind == NK_MENU_ITEM_SUBMENU) {
        NSMenu *submenu = [native submenu];
        for (const auto child : item->children) {
            const auto resource = std::static_pointer_cast<nk::core::MenuItemResource>(
                nk::core::handles().get(child, nk::core::ResourceType::menu_item));
            if (resource)
                add_native_item(resource, submenu);
        }
    }
    return native;
}

void clear_native_menu(bool restore) {
    if (!state.menu)
        return;
    if (restore && NSApp.mainMenu == state.menu)
        [NSApp setMainMenu:state.previous];
    state.items.clear();
    state.menu = nil;
    state.application_menu = nil;
    state.previous = nil;
    state.target = nil;
    state.handle = NK_INVALID_HANDLE;
}

} // namespace

namespace nk::backend {

nk_result menu_install(const std::shared_ptr<nk::core::MenuResource> &menu) noexcept {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        clear_native_menu(true);
        state.handle = menu->handle;
        NSString *menu_title = menu_string(menu->title);
        state.menu = [[NSMenu alloc] initWithTitle:menu_title ?: @""];
        [state.menu setAutoenablesItems:NO];
        state.previous = NSApp.mainMenu;
        state.target = [[NKMenuActionTarget alloc] init];
        if (!menu->title.empty()) {
            state.application_menu = [[NSMenu alloc] initWithTitle:menu_title ?: @""];
            [state.application_menu setAutoenablesItems:NO];
            NSMenuItem *application_item = [[NSMenuItem alloc] initWithTitle:menu_title ?: @""
                                                                      action:nil
                                                               keyEquivalent:@""];
            [application_item setSubmenu:state.application_menu];
            [state.menu addItem:application_item];
        }
        for (const auto child : menu->children) {
            const auto resource = std::static_pointer_cast<nk::core::MenuItemResource>(
                nk::core::handles().get(child, nk::core::ResourceType::menu_item));
            if (!resource)
                continue;
            NSMenu *parent = state.menu;
            if (state.application_menu && resource->role != NK_MENU_ROLE_NONE)
                parent = state.application_menu;
            add_native_item(resource, parent);
        }
        [NSApp setMainMenu:state.menu];
    }
    return NK_OK;
}

void menu_detach(const std::shared_ptr<nk::core::MenuResource> &menu) noexcept {
    if (!menu || menu->handle != state.handle)
        return;
    @autoreleasepool {
        clear_native_menu(true);
    }
}

nk_result menu_item_added(const std::shared_ptr<nk::core::MenuResource> &menu,
                          const std::shared_ptr<nk::core::MenuItemResource> &item) noexcept {
    if (!menu || menu->handle != state.handle)
        return NK_OK;
    @autoreleasepool {
        NSMenu *parent = state.menu;
        if (item->parent) {
            auto found = state.items.find(item->parent);
            if (found == state.items.end() || !found->second.submenu)
                return NK_ERROR_INVALID_HANDLE;
            parent = found->second.submenu;
        } else if (state.application_menu && item->role != NK_MENU_ROLE_NONE) {
            parent = state.application_menu;
        }
        add_native_item(item, parent);
    }
    return NK_OK;
}

void menu_item_removed(const std::shared_ptr<nk::core::MenuResource> &menu,
                       const std::shared_ptr<nk::core::MenuItemResource> &item) noexcept {
    if (!menu || menu->handle != state.handle)
        return;
    const auto found = state.items.find(item->handle);
    if (found == state.items.end())
        return;
    NSMenuItem *native = found->second;
    NSMenu *parent = native.menu;
    if (parent)
        [parent removeItem:native];
    state.items.erase(found);
}

nk_result menu_item_changed(const std::shared_ptr<nk::core::MenuResource> &menu,
                            const std::shared_ptr<nk::core::MenuItemResource> &item) noexcept {
    if (!menu || menu->handle != state.handle)
        return NK_OK;
    const auto found = state.items.find(item->handle);
    if (found == state.items.end())
        return NK_OK;
    @autoreleasepool {
        set_native_state(found->second, *item);
    }
    return NK_OK;
}

void menu_backend_shutdown() noexcept {
    @autoreleasepool {
        clear_native_menu(true);
    }
}

} // namespace nk::backend
