#include "nativekit_credentials.h"

#include "backend.hpp"

#include <cstring>
#include <string>

namespace {

thread_local std::string last_error;

bool valid_component(const char *value) {
    if (!value || value[0] == '\0')
        return false;
    const auto size = std::strlen(value);
    if (size > 255)
        return false;
    for (const auto *p = reinterpret_cast<const unsigned char *>(value); *p;) {
        if (*p < 0x80) {
            ++p;
            continue;
        }
        uint32_t codepoint = 0;
        uint32_t remaining = 0;
        if ((*p & 0xe0) == 0xc0) {
            codepoint = *p & 0x1f;
            remaining = 1;
            if (codepoint < 2)
                return false;
        } else if ((*p & 0xf0) == 0xe0) {
            codepoint = *p & 0x0f;
            remaining = 2;
        } else if ((*p & 0xf8) == 0xf0) {
            codepoint = *p & 0x07;
            remaining = 3;
        } else {
            return false;
        }
        ++p;
        for (uint32_t i = 0; i < remaining; ++i, ++p) {
            if ((*p & 0xc0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (*p & 0x3f);
        }
        if ((remaining == 2 && codepoint < 0x800) ||
            (remaining == 3 && codepoint < 0x10000) || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff))
            return false;
    }
    return true;
}

nk_result validate(const char *service, const char *account) {
    if (!valid_component(service) || !valid_component(account)) {
        nkc::set_error("service and account must be non-empty UTF-8 strings of at most 255 bytes");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    return NK_OK;
}

} // namespace

namespace nkc {

void set_error(const char *message) noexcept {
    last_error = message ? message : "credential store operation failed";
}

void clear_error() noexcept {
    last_error.clear();
}

} // namespace nkc

extern "C" {

nk_result NK_CALL nk_credentials_set(const char *service, const char *account,
                                     const uint8_t *secret, uint32_t secret_size) {
    nkc::clear_error();
    const auto valid = validate(service, account);
    if (valid != NK_OK)
        return valid;
    if (!secret || secret_size == 0) {
        nkc::set_error("secret must contain at least one byte");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (secret_size > NK_CREDENTIALS_MAX_SECRET_BYTES) {
        nkc::set_error("secret exceeds the portable 2048-byte limit");
        return NK_ERROR_PAYLOAD_TOO_LARGE;
    }
    return static_cast<nk_result>(nkc::set({service, account}, secret, secret_size));
}

nk_result NK_CALL nk_credentials_get(const char *service, const char *account, uint8_t *secret,
                                     uint32_t *inout_size) {
    nkc::clear_error();
    if (!inout_size) {
        nkc::set_error("inout_size is required");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const uint32_t capacity = *inout_size;
    *inout_size = 0;
    const auto valid = validate(service, account);
    if (valid != NK_OK)
        return valid;
    if (!secret && capacity != 0) {
        nkc::set_error("a null output buffer requires a zero input size");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    return static_cast<nk_result>(nkc::get({service, account}, secret, capacity, inout_size));
}

nk_result NK_CALL nk_credentials_delete(const char *service, const char *account) {
    nkc::clear_error();
    const auto valid = validate(service, account);
    if (valid != NK_OK)
        return valid;
    return static_cast<nk_result>(nkc::erase({service, account}));
}

const char *NK_CALL nk_credentials_last_error(void) {
    return last_error.c_str();
}

} // extern "C"
