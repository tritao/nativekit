#include "backend.hpp"

namespace nkc {

int32_t set(const Request &, const uint8_t *, uint32_t) {
    set_error("OS-protected credential storage is not implemented on this platform");
    return NK_ERROR_UNSUPPORTED;
}

int32_t get(const Request &, uint8_t *, uint32_t, uint32_t *) {
    set_error("OS-protected credential storage is not implemented on this platform");
    return NK_ERROR_UNSUPPORTED;
}

int32_t erase(const Request &) {
    set_error("OS-protected credential storage is not implemented on this platform");
    return NK_ERROR_UNSUPPORTED;
}

} // namespace nkc
