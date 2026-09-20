#include "core/menu_internal.hpp"

#include "core/error.hpp"
#include "core/runtime.hpp"

#if !defined(NK_BACKEND_MACOS) && !defined(NK_BACKEND_GTK)
namespace nk::backend {

nk_result menu_install(const std::shared_ptr<nk::core::MenuResource> &) noexcept {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::core::set_error("native application menus are not implemented by this backend");
    return NK_ERROR_UNSUPPORTED;
}

void menu_detach(const std::shared_ptr<nk::core::MenuResource> &) noexcept {}

nk_result menu_item_added(const std::shared_ptr<nk::core::MenuResource> &,
                          const std::shared_ptr<nk::core::MenuItemResource> &) noexcept {
    return NK_OK;
}

void menu_item_removed(const std::shared_ptr<nk::core::MenuResource> &,
                       const std::shared_ptr<nk::core::MenuItemResource> &) noexcept {}

nk_result menu_item_changed(const std::shared_ptr<nk::core::MenuResource> &,
                            const std::shared_ptr<nk::core::MenuItemResource> &) noexcept {
    return NK_OK;
}

void menu_backend_shutdown() noexcept {}

} // namespace nk::backend
#endif
