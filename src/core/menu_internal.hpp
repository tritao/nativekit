#pragma once

#include "core/handle_registry.hpp"
#include "nativekit_menu.h"

#include <memory>
#include <string>
#include <vector>

namespace nk::core {

struct MenuResource final : Resource {
    nk_menu handle = NK_INVALID_HANDLE;
    std::string title;
    std::vector<nk_menu_item> children;
};

struct MenuItemResource final : Resource {
    nk_menu_item handle = NK_INVALID_HANDLE;
    nk_menu menu = NK_INVALID_HANDLE;
    nk_menu_item parent = NK_INVALID_HANDLE;
    nk_menu_item_kind kind = NK_MENU_ITEM_COMMAND;
    nk_menu_item_flags flags = 0;
    nk_menu_item_role role = NK_MENU_ROLE_NONE;
    nk_menu_command_id command_id = 0;
    std::string label;
    nk_menu_shortcut shortcut{NK_KEY_UNKNOWN, 0};
    std::vector<nk_menu_item> children;
};

void menu_item_activated(nk_menu_item item) noexcept;

} // namespace nk::core

namespace nk::backend {

nk_result menu_install(const std::shared_ptr<nk::core::MenuResource> &menu) noexcept;
void menu_detach(const std::shared_ptr<nk::core::MenuResource> &menu) noexcept;
nk_result menu_item_added(const std::shared_ptr<nk::core::MenuResource> &menu,
                          const std::shared_ptr<nk::core::MenuItemResource> &item) noexcept;
void menu_item_removed(const std::shared_ptr<nk::core::MenuResource> &menu,
                       const std::shared_ptr<nk::core::MenuItemResource> &item) noexcept;
nk_result menu_item_changed(const std::shared_ptr<nk::core::MenuResource> &menu,
                            const std::shared_ptr<nk::core::MenuItemResource> &item) noexcept;
void menu_backend_shutdown() noexcept;

} // namespace nk::backend
