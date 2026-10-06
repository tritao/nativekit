#ifndef NATIVEKIT_CREDENTIALS_BACKEND_HPP
#define NATIVEKIT_CREDENTIALS_BACKEND_HPP

#include "nativekit_credentials.h"

#include <cstdint>

namespace nkc {

struct Request {
    const char *service;
    const char *account;
};

int32_t set(const Request &request, const uint8_t *secret, uint32_t secret_size);
int32_t get(const Request &request, uint8_t *secret, uint32_t capacity,
            uint32_t *out_secret_size);
int32_t erase(const Request &request);

void set_error(const char *message) noexcept;
void clear_error() noexcept;

} // namespace nkc

#endif
