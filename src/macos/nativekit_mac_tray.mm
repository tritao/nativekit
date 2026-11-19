#import <Cocoa/Cocoa.h>

#include "nativekit_tray.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/event_queue.hpp"
#include "core/handle_registry.hpp"
#include "core/menu_internal.hpp"
#include "core/runtime.hpp"
#include "macos/nativekit_mac_application.hpp"

#include <memory>
#include <string>

/*
 * macOS trays are NSStatusItems in the menu bar. A click on one asks the application for its
 * menu (NK_EVENT_TRAY_MENU_REQUESTED), the platform convention for status items, and
 * nk_tray_show_menu opens that menu under the item on the next event pump.
 *
 * AppKit runs menu tracking on the main thread only, so unlike Windows the application's event
 * loop does not run while a tray menu is open; it resumes when the menu closes.
 */

@interface NKTrayButtonTarget : NSObject
@property(nonatomic, assign) nk_tray tray;
- (void)clicked:(id)sender;
@end

@interface NKTrayMenuTarget : NSObject
- (void)activate:(id)sender;
@end

namespace {

struct MacTrayResource final : nk::core::Resource {
    nk_tray handle = NK_INVALID_HANDLE;
    __strong NSStatusItem *item = nil;
    __strong NKTrayButtonTarget *target = nil;
};

// The one open tray menu and the target its items call; menus open one at a time.
__strong NSMenu *open_menu = nil;
__strong NKTrayMenuTarget *open_menu_target = nil;
nk_tray open_menu_tray = NK_INVALID_HANDLE;

std::shared_ptr<MacTrayResource> get_tray(nk_tray handle) {
    return std::static_pointer_cast<MacTrayResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::tray));
}

nk_result invalid_tray_handle() {
    nk::core::set_error("invalid or stale tray handle");
    return NK_ERROR_INVALID_HANDLE;
}

NSString *utf8(const char *value) {
    if (!value || !*value)
        return nil;
    return [NSString stringWithUTF8String:value];
}

/** A status-bar-sized image: the given icon file, or a generic terminal symbol. */
NSImage *tray_image(const char *path) {
    NSString *file = utf8(path);
    NSImage *image = file ? [[NSImage alloc] initWithContentsOfFile:file] : nil;
    if (!image) {
        image = [NSImage imageWithSystemSymbolName:@"terminal"
                          accessibilityDescription:@"NativeKit status item"];
        [image setTemplate:YES]; // `template` is a C++ keyword
    }
    image.size = NSMakeSize(18, 18);
    return image;
}

NSMenuItem *native_item(nk_menu_item handle, NKTrayMenuTarget *target) {
    const auto item = std::static_pointer_cast<nk::core::MenuItemResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::menu_item));
    if (!item || (item->flags & NK_MENU_ITEM_HIDDEN))
        return nil;
    if (item->kind == NK_MENU_ITEM_SEPARATOR)
        return [NSMenuItem separatorItem];
    NSString *title = [[NSString alloc] initWithBytes:item->label.data()
                                               length:item->label.size()
                                             encoding:NSUTF8StringEncoding];
    if (!title)
        title = @"";
    NSMenuItem *native = [[NSMenuItem alloc] initWithTitle:title
                                                    action:@selector(activate:)
                                             keyEquivalent:@""];
    native.target = target;
    native.tag = static_cast<NSInteger>(item->handle);
    native.enabled = (item->flags & NK_MENU_ITEM_DISABLED) == 0;
    native.state = (item->flags & NK_MENU_ITEM_CHECKED) ? NSControlStateValueOn
                                                        : NSControlStateValueOff;
    if (item->kind == NK_MENU_ITEM_SUBMENU) {
        NSMenu *submenu = [[NSMenu alloc] initWithTitle:title];
        submenu.autoenablesItems = NO;
        for (const auto child : item->children)
            if (NSMenuItem *added = native_item(child, target))
                [submenu addItem:added];
        native.submenu = submenu;
        native.action = nil;
    }
    return native;
}

void close_open_menu() {
    [open_menu cancelTrackingWithoutAnimation];
    open_menu = nil;
    open_menu_target = nil;
    open_menu_tray = NK_INVALID_HANDLE;
}

void emit_tray(nk_event_kind kind, nk_tray handle) noexcept {
    nk::core::callback_boundary([&] {
        nk::core::QueuedEvent event;
        event.kind = kind;
        event.source = handle;
        nk::core::push_event(std::move(event));
    });
}

} // namespace

@implementation NKTrayButtonTarget
- (void)clicked:(id)sender {
    (void)sender;
    emit_tray(NK_EVENT_TRAY_MENU_REQUESTED, self.tray);
}
@end

@implementation NKTrayMenuTarget
- (void)activate:(id)sender {
    if (![sender isKindOfClass:[NSMenuItem class]])
        return;
    nk::core::menu_item_activated(static_cast<nk_menu_item>([(NSMenuItem *)sender tag]));
}
@end

extern "C" {

nk_result NK_CALL nk_tray_create(const nk_tray_options *options, nk_tray *out_tray) {
    return nk::core::result_boundary("unexpected error while creating tray", [&]() -> nk_result {
        if (out_tray)
            *out_tray = NK_INVALID_HANDLE;
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        if (!options || !out_tray || options->struct_size < sizeof(nk_tray_options)) {
            nk::core::set_error("tray options and output handle are required");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        @autoreleasepool {
            [NSApplication sharedApplication];
            // A tray-only process lives in the menu bar without a Dock icon; a process that
            // already opened windows keeps its regular policy.
            if (NSApp.activationPolicy == NSApplicationActivationPolicyProhibited)
                [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
            nk::macos::launch_application();
            auto tray = std::make_shared<MacTrayResource>();
            tray->item =
                [NSStatusBar.systemStatusBar statusItemWithLength:NSSquareStatusItemLength];
            if (!tray->item || !tray->item.button) {
                nk::core::set_error("the macOS menu bar rejected the status item");
                return NK_ERROR_UNSUPPORTED;
            }
            tray->item.button.image = tray_image(options->icon);
            tray->item.button.toolTip = utf8(options->tooltip);
            tray->target = [NKTrayButtonTarget new];
            tray->item.button.target = tray->target;
            tray->item.button.action = @selector(clicked:);
            [tray->item.button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
            tray->handle = nk::core::handles().insert(nk::core::ResourceType::tray, tray);
            tray->target.tray = tray->handle;
            *out_tray = tray->handle;
        }
        return NK_OK;
    });
}

nk_result NK_CALL nk_tray_destroy(nk_tray handle) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    const auto tray = get_tray(handle);
    if (!tray)
        return invalid_tray_handle();
    @autoreleasepool {
        if (open_menu_tray == handle)
            close_open_menu();
        [NSStatusBar.systemStatusBar removeStatusItem:tray->item];
        tray->item = nil;
        tray->target = nil;
    }
    nk::core::handles().erase(handle, nk::core::ResourceType::tray);
    return NK_OK;
}

nk_result NK_CALL nk_tray_set_tooltip(nk_tray handle, const char *tooltip) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    const auto tray = get_tray(handle);
    if (!tray)
        return invalid_tray_handle();
    NSString *text = utf8(tooltip);
    if (tooltip && *tooltip && !text) {
        nk::core::set_error("tray text is not valid UTF-8");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    tray->item.button.toolTip = text;
    return NK_OK;
}

nk_result NK_CALL nk_tray_show_menu(nk_tray handle, nk_menu menu) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    const auto tray = get_tray(handle);
    if (!tray)
        return invalid_tray_handle();
    if (open_menu)
        return NK_OK; // one tray menu at a time
    const auto source = std::static_pointer_cast<nk::core::MenuResource>(
        nk::core::handles().get(menu, nk::core::ResourceType::menu));
    if (!source) {
        nk::core::set_error("invalid or stale menu handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    @autoreleasepool {
        // The menu is copied now, so the caller may destroy its own after this returns.
        open_menu_target = [NKTrayMenuTarget new];
        open_menu = [[NSMenu alloc] initWithTitle:@""];
        open_menu.autoenablesItems = NO;
        for (const auto child : source->children)
            if (NSMenuItem *added = native_item(child, open_menu_target))
                [open_menu addItem:added];
        open_menu_tray = handle;
        NSMenu *shown = open_menu;
        NSStatusBarButton *button = tray->item.button;
        // Open it from the next pump, not inside this call, so the call returns at once.
        dispatch_async(dispatch_get_main_queue(), ^{
            if (open_menu == shown && button.window)
                [shown popUpMenuPositioningItem:nil
                                     atLocation:NSMakePoint(0, NSHeight(button.bounds) + 4)
                                         inView:button];
            if (open_menu == shown)
                close_open_menu();
            nk::core::wake_events();
        });
    }
    return NK_OK;
}

} // extern "C"
