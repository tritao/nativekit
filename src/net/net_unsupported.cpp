#include "net_backend.hpp"

namespace nk::net {

nk_capabilities capabilities() noexcept {
    return 0;
}

nk_result backend_start(const RequestPtr &) noexcept {
    return NK_ERROR_UNSUPPORTED;
}

void backend_cancel(const RequestPtr &) noexcept {}

void backend_shutdown() noexcept {}

} // namespace nk::net
