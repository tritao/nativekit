#include "nativekit_menu.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/menu_internal.hpp"
#include "core/resource_events.hpp"
#include "core/runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

namespace {

nk_menu active_menu = NK_INVALID_HANDLE;

nk_result enter_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

bool valid_kind(nk_menu_item_kind kind) {
    return kind <= NK_MENU_ITEM_SEPARATOR;
}

bool valid_role(nk_menu_item_role role) {
    return role <= NK_MENU_ROLE_BRING_ALL_TO_FRONT;
}

bool valid_shortcut(const nk_menu_shortcut &shortcut) {
    constexpr nk_menu_modifiers known =
        NK_MENU_MOD_PRIMARY | NK_MENU_MOD_SHIFT | NK_MENU_MOD_ALT | NK_MENU_MOD_CONTROL;
    if (shortcut.key == NK_KEY_UNKNOWN)
        return shortcut.modifiers == 0;
    return (shortcut.modifiers & ~known) == 0;
}

bool valid_menu_options(const nk_menu_options *options) {
    return options && options->struct_size >= sizeof(*options) &&
           nk::platform::valid_utf8(options->title);
}

bool valid_item_options(const nk_menu_item_options *options) {
    if (!options || options->struct_size < sizeof(*options) || !valid_kind(options->kind) ||
        !valid_role(options->role) || !nk::platform::valid_utf8(options->label) ||
        !valid_shortcut(options->shortcut))
        return false;
    if (options->kind == NK_MENU_ITEM_SEPARATOR)
        return options->role == NK_MENU_ROLE_NONE && options->command_id == 0;
    if (!options->label || !*options->label)
        return false;
    if (options->kind == NK_MENU_ITEM_SUBMENU)
        return options->role == NK_MENU_ROLE_NONE && options->command_id == 0 &&
               options->shortcut.key == NK_KEY_UNKNOWN;
    return true;
}

std::shared_ptr<nk::core::MenuResource> menu(nk_menu handle) {
    return std::static_pointer_cast<nk::core::MenuResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::menu));
}

std::shared_ptr<nk::core::MenuItemResource> item(nk_menu_item handle) {
    return std::static_pointer_cast<nk::core::MenuItemResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::menu_item));
}

void erase_item_tree(nk_menu_item handle, const std::shared_ptr<nk::core::MenuResource> &owner) {
    const auto resource = item(handle);
    if (!resource)
        return;
    const auto descendants = resource->children;
    for (const auto child : descendants)
        erase_item_tree(child, owner);
    nk::backend::menu_item_removed(owner, resource);
    nk::core::handles().erase(handle, nk::core::ResourceType::menu_item);
}

} // namespace

namespace nk::core {

nk_result fail(nk_result result, const char *message) {
    set_error(message);
    return result;
}

void menu_item_activated(nk_menu_item handle) noexcept {
    callback_boundary([&] {
        const auto resource = std::static_pointer_cast<MenuItemResource>(
            handles().get(handle, ResourceType::menu_item));
        if (!resource || (resource->flags & NK_MENU_ITEM_DISABLED) ||
            (resource->flags & NK_MENU_ITEM_HIDDEN) || resource->kind == NK_MENU_ITEM_SEPARATOR ||
            resource->kind == NK_MENU_ITEM_SUBMENU)
            return;
        if (resource->kind == NK_MENU_ITEM_CHECKBOX || resource->kind == NK_MENU_ITEM_RADIO) {
            resource->flags ^= NK_MENU_ITEM_CHECKED;
            const auto owner = std::static_pointer_cast<MenuResource>(
                handles().get(resource->menu, ResourceType::menu));
            if (owner)
                (void)backend::menu_item_changed(owner, resource);
        }
        nk_menu_item_activated_event payload{
            resource->command_id, handle,
            static_cast<nk_bool>((resource->flags & NK_MENU_ITEM_CHECKED) != 0), 0};
        QueuedEvent event;
        event.kind = resource->role == NK_MENU_ROLE_QUIT ? NK_EVENT_APPLICATION_QUIT_REQUESTED
                                                         : NK_EVENT_MENU_ITEM_ACTIVATED;
        event.source = handle;
        event.data.resize(sizeof(payload));
        std::memcpy(event.data.data(), &payload, sizeof(payload));
        (void)push_event(std::move(event));
    });
}

} // namespace nk::core

extern "C" {

nk_result NK_CALL nk_menu_create(const nk_menu_options *options, nk_menu *out_menu) {
    return nk::core::result_boundary("unexpected error while creating menu", [&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!out_menu || !valid_menu_options(options))
            return nk::core::fail(NK_ERROR_INVALID_ARGUMENT, "invalid menu options");
        auto resource = std::make_shared<nk::core::MenuResource>();
        if (options->title)
            resource->title = options->title;
        const auto handle = nk::core::handles().insert(nk::core::ResourceType::menu, resource);
        if (!handle)
            return nk::core::fail(NK_ERROR_OUT_OF_MEMORY, "menu handle registry is full");
        resource->handle = handle;
        *out_menu = handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_menu_destroy(nk_menu handle) {
    return nk::core::result_boundary("unexpected error while destroying menu", [&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        const auto resource = menu(handle);
        if (!resource)
            return nk::core::fail(NK_ERROR_INVALID_HANDLE, "invalid or stale menu handle");
        if (active_menu == handle) {
            nk::backend::menu_detach(resource);
            active_menu = NK_INVALID_HANDLE;
        }
        const auto children = resource->children;
        for (const auto child : children)
            erase_item_tree(child, resource);
        nk::core::handles().erase(handle, nk::core::ResourceType::menu);
        return NK_OK;
    });
}

nk_result NK_CALL nk_menu_add_item(nk_menu menu_handle, nk_menu_item parent,
                                   const nk_menu_item_options *options, nk_menu_item *out_item) {
    return nk::core::result_boundary("unexpected error while adding menu item", [&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        const auto owner = menu(menu_handle);
        if (!owner || !out_item)
            return nk::core::fail(NK_ERROR_INVALID_HANDLE, "invalid or stale menu handle");
        if (!valid_item_options(options))
            return nk::core::fail(NK_ERROR_INVALID_ARGUMENT, "invalid menu item options");
        std::shared_ptr<nk::core::MenuItemResource> parent_resource;
        if (parent) {
            parent_resource = item(parent);
            if (!parent_resource || parent_resource->menu != menu_handle ||
                parent_resource->kind != NK_MENU_ITEM_SUBMENU)
                return nk::core::fail(NK_ERROR_INVALID_HANDLE, "invalid menu parent item");
        }
        auto resource = std::make_shared<nk::core::MenuItemResource>();
        resource->menu = menu_handle;
        resource->parent = parent;
        resource->kind = options->kind;
        resource->flags =
            options->flags & (NK_MENU_ITEM_DISABLED | NK_MENU_ITEM_CHECKED | NK_MENU_ITEM_HIDDEN);
        resource->role = options->role;
        resource->command_id = options->command_id;
        resource->label = options->label ? options->label : "";
        resource->shortcut = options->shortcut;
        const auto handle = nk::core::handles().insert(nk::core::ResourceType::menu_item, resource);
        if (!handle)
            return nk::core::fail(NK_ERROR_OUT_OF_MEMORY, "menu item handle registry is full");
        resource->handle = handle;
        if (parent_resource)
            parent_resource->children.push_back(handle);
        else
            owner->children.push_back(handle);
        if (const auto result = nk::backend::menu_item_added(owner, resource); result != NK_OK) {
            if (parent_resource)
                parent_resource->children.pop_back();
            else
                owner->children.pop_back();
            nk::core::handles().erase(handle, nk::core::ResourceType::menu_item);
            return result;
        }
        *out_item = handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_menu_item_remove(nk_menu_item handle) {
    return nk::core::result_boundary(
        "unexpected error while removing menu item", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            const auto resource = item(handle);
            if (!resource)
                return nk::core::fail(NK_ERROR_INVALID_HANDLE, "invalid or stale menu item handle");
            const auto owner = menu(resource->menu);
            if (!owner)
                return nk::core::fail(NK_ERROR_INVALID_HANDLE,
                                      "menu item owner is no longer valid");
            std::shared_ptr<nk::core::MenuItemResource> parent_resource;
            if (resource->parent) {
                parent_resource = item(resource->parent);
                if (!parent_resource)
                    return nk::core::fail(NK_ERROR_INVALID_HANDLE, "invalid menu parent item");
            }
            auto &siblings = parent_resource ? parent_resource->children : owner->children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), handle), siblings.end());
            erase_item_tree(handle, owner);
            return NK_OK;
        });
}

nk_result NK_CALL nk_application_set_menu(nk_menu handle) {
    return nk::core::result_boundary(
        "unexpected error while installing application menu", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (handle == active_menu)
                return NK_OK;
            std::shared_ptr<nk::core::MenuResource> next;
            if (handle) {
                next = menu(handle);
                if (!next)
                    return nk::core::fail(NK_ERROR_INVALID_HANDLE, "invalid or stale menu handle");
            }
            if (next) {
                if (const auto result = nk::backend::menu_install(next); result != NK_OK)
                    return result;
            } else if (active_menu) {
                nk::backend::menu_detach(menu(active_menu));
            }
            if (active_menu && active_menu != handle)
                nk::backend::menu_detach(menu(active_menu));
            active_menu = handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_menu_item_set_label(nk_menu_item handle, const char *label) {
    return nk::core::result_boundary(
        "unexpected error while setting menu item label", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            const auto resource = item(handle);
            const auto owner = resource ? menu(resource->menu) : nullptr;
            if (!resource || !owner)
                return nk::core::fail(NK_ERROR_INVALID_HANDLE, "invalid or stale menu item handle");
            if (!nk::platform::valid_utf8(label) || !label || !*label)
                return nk::core::fail(NK_ERROR_INVALID_ARGUMENT, "invalid menu item label");
            resource->label = label;
            return nk::backend::menu_item_changed(owner, resource);
        });
}

nk_result NK_CALL nk_menu_item_set_enabled(nk_menu_item handle, nk_bool enabled) {
    return nk::core::result_boundary(
        "unexpected error while setting menu item enabled state", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            const auto resource = item(handle);
            const auto owner = resource ? menu(resource->menu) : nullptr;
            if (!resource || !owner)
                return nk::core::fail(NK_ERROR_INVALID_HANDLE, "invalid or stale menu item handle");
            if (enabled)
                resource->flags &= ~NK_MENU_ITEM_DISABLED;
            else
                resource->flags |= NK_MENU_ITEM_DISABLED;
            return nk::backend::menu_item_changed(owner, resource);
        });
}

nk_result NK_CALL nk_menu_item_set_checked(nk_menu_item handle, nk_bool checked) {
    return nk::core::result_boundary(
        "unexpected error while setting menu item checked state", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            const auto resource = item(handle);
            const auto owner = resource ? menu(resource->menu) : nullptr;
            if (!resource || !owner)
                return nk::core::fail(NK_ERROR_INVALID_HANDLE, "invalid or stale menu item handle");
            if (resource->kind != NK_MENU_ITEM_CHECKBOX && resource->kind != NK_MENU_ITEM_RADIO)
                return nk::core::fail(NK_ERROR_INVALID_ARGUMENT,
                                      "only checkable menu items have a state");
            if (checked)
                resource->flags |= NK_MENU_ITEM_CHECKED;
            else
                resource->flags &= ~NK_MENU_ITEM_CHECKED;
            return nk::backend::menu_item_changed(owner, resource);
        });
}

nk_result NK_CALL nk_menu_item_set_visible(nk_menu_item handle, nk_bool visible) {
    return nk::core::result_boundary(
        "unexpected error while setting menu item visibility", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            const auto resource = item(handle);
            const auto owner = resource ? menu(resource->menu) : nullptr;
            if (!resource || !owner)
                return nk::core::fail(NK_ERROR_INVALID_HANDLE, "invalid or stale menu item handle");
            if (visible)
                resource->flags &= ~NK_MENU_ITEM_HIDDEN;
            else
                resource->flags |= NK_MENU_ITEM_HIDDEN;
            return nk::backend::menu_item_changed(owner, resource);
        });
}

nk_result NK_CALL nk_menu_item_set_shortcut(nk_menu_item handle, const nk_menu_shortcut *shortcut) {
    return nk::core::result_boundary(
        "unexpected error while setting menu item shortcut", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            const auto resource = item(handle);
            const auto owner = resource ? menu(resource->menu) : nullptr;
            if (!resource || !owner)
                return nk::core::fail(NK_ERROR_INVALID_HANDLE, "invalid or stale menu item handle");
            if (!shortcut || !valid_shortcut(*shortcut))
                return nk::core::fail(NK_ERROR_INVALID_ARGUMENT, "invalid menu item shortcut");
            resource->shortcut = *shortcut;
            return nk::backend::menu_item_changed(owner, resource);
        });
}
}
