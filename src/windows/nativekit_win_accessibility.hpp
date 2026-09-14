#pragma once

#include "nativekit.h"
#include "nativekit_accessibility.h"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace nk::windows {

nk_result accessibility_attach(HWND window, nk_handle surface) noexcept;
void accessibility_detach(HWND window) noexcept;
bool accessibility_handle_getobject(HWND window, WPARAM wparam, LPARAM lparam,
                                    LRESULT *out_result) noexcept;
nk_result accessibility_update_tree(nk_handle surface,
                                    const nk_accessibility_update *update) noexcept;
nk_result accessibility_clear_tree(nk_handle surface) noexcept;
nk_result accessibility_set_text_ranges(nk_handle surface, nk_accessibility_node_id node,
                                        const nk_accessibility_text_range *ranges,
                                        uint32_t range_count) noexcept;

} // namespace nk::windows
