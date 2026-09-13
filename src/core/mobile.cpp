#include "nativekit_mobile.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

namespace nk::backend {
#if defined(__ANDROID__)
nk_result mobile_host_attach(const nk_mobile_host_options &options, nk_handle &out_host);
nk_result mobile_host_destroy(nk_handle host);
nk_result mobile_host_set_lifecycle(nk_handle host, nk_mobile_lifecycle_state state);
nk_result mobile_host_dispatch_event(nk_handle host, const nk_mobile_host_event &event);
nk_result mobile_host_set_drop_enabled(nk_handle host, bool enabled);
#endif
} // namespace nk::backend

namespace {
#if !defined(__ANDROID__)
nk_result unsupported() {
    const auto thread = nk::core::require_ui_thread();
    if (thread != NK_OK)
        return thread;
    nk::core::set_error("this NativeKit backend does not support mobile hosts");
    return NK_ERROR_UNSUPPORTED;
}
#endif
} // namespace

extern "C" {

nk_result NK_CALL nk_mobile_host_attach(const nk_mobile_host_options *options,
                                        nk_handle *out_host) {
    return nk::core::result_boundary(
        "unexpected error while attaching a mobile host", [&]() -> nk_result {
            const auto thread = nk::core::require_ui_thread();
            if (thread != NK_OK)
                return thread;
            if (!options || options->struct_size < sizeof(nk_mobile_host_options) || !out_host) {
                nk::core::set_error("mobile host options or output is missing or too small");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            *out_host = NK_INVALID_HANDLE;
#if defined(__ANDROID__)
            return nk::backend::mobile_host_attach(*options, *out_host);
#else
        return unsupported();
#endif
        });
}

nk_result NK_CALL nk_mobile_host_destroy(nk_handle host) {
    return nk::core::result_boundary("unexpected error while destroying a mobile host",
                                     [&]() -> nk_result {
#if defined(__ANDROID__)
                                         return nk::backend::mobile_host_destroy(host);
#else
        (void)host;
        return unsupported();
#endif
                                     });
}

nk_result NK_CALL nk_mobile_host_set_lifecycle(nk_handle host, nk_mobile_lifecycle_state state) {
    return nk::core::result_boundary("unexpected error while updating a mobile host",
                                     [&]() -> nk_result {
#if defined(__ANDROID__)
                                         return nk::backend::mobile_host_set_lifecycle(host, state);
#else
        (void)host;
        (void)state;
        return unsupported();
#endif
                                     });
}

nk_result NK_CALL nk_mobile_host_dispatch_event(nk_handle host, const nk_mobile_host_event *event) {
    return nk::core::result_boundary(
        "unexpected error while dispatching a mobile host event", [&]() -> nk_result {
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (!event || event->struct_size < sizeof(nk_mobile_host_event)) {
                nk::core::set_error("mobile host event is missing or too small");
                return NK_ERROR_INVALID_ARGUMENT;
            }
#if defined(__ANDROID__)
            return nk::backend::mobile_host_dispatch_event(host, *event);
#else
        (void)host;
        return unsupported();
#endif
        });
}

nk_result NK_CALL nk_mobile_host_set_drop_enabled(nk_handle host, uint32_t enabled) {
    return nk::core::result_boundary(
        "unexpected error while updating mobile host drops", [&]() -> nk_result {
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (enabled > 1) {
                nk::core::set_error("mobile host drop state must be zero or one");
                return NK_ERROR_INVALID_ARGUMENT;
            }
#if defined(__ANDROID__)
            return nk::backend::mobile_host_set_drop_enabled(host, enabled != 0);
#else
        (void)host;
        return unsupported();
#endif
        });
}
}
