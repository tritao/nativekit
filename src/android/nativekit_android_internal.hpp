#pragma once

#include "nativekit.h"

struct ANativeWindow;

namespace nk::backend {

nk_result android_vulkan_window(nk_handle surface, ANativeWindow **out_window,
                                bool require_ready);

} // namespace nk::backend
