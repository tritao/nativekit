#import <Cocoa/Cocoa.h>

#include "nativekit_menu.h"

#include "core/menu_internal.hpp"
#include "core/runtime.hpp"

#include <string>
#include <unordered_map>

@interface NKMenuActionTarget : NSObject
- (void)activate:(id)sender;
@end

@implementation NKMenuActionTarget
- (void)activate:(id)sender {
    if (![sender isKindOfClass:[NSMenuItem class]])
        return;
    nk::core::menu_item_activated(static_cast<nk_menu_item>([(NSMenuItem *)sender tag]));
}
@end

namespace {

struct MacMenuState {
    nk_menu handle = NK_INVALID_HANDLE;
    __strong NSMenu *menu = nil;
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
        return [[NSString alloc] initWithFormat:@"%c", static_cast<int>(key)];
    if (key >= NK_KEY_0 && key <= NK_KEY_9)
        return [[NSString alloc] initWithFormat:@"%c", static_cast<int>(key)];
    switch (key) {
    case NK_KEY_SPACE:
        return @" ";
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
    default:
        return nil;
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

void set_native_state(NSMenuItem *native, const nk::core::MenuItemResource &item) {
    NSString *title = menu_string(item.label);
    [native setTitle:title ?: @""];
    [native setEnabled:(item.flags & NK_MENU_ITEM_DISABLED) == 0];
    [native setHidden:(item.flags & NK_MENU_ITEM_HIDDEN) != 0];
    [native setState:(item.flags & NK_MENU_ITEM_CHECKED) ? NSControlStateValueOn
                                                         : NSControlStateValueOff];
    NSString *key = shortcut_key(item.shortcut.key);
    [native setKeyEquivalent:key ?: @""];
    [native setKeyEquivalentModifierMask:shortcut_modifiers(item.shortcut.modifiers)];
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
        for (const auto child : menu->children) {
            const auto resource = std::static_pointer_cast<nk::core::MenuItemResource>(
                nk::core::handles().get(child, nk::core::ResourceType::menu_item));
            if (resource)
                add_native_item(resource, state.menu);
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
