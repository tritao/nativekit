#include "nativekit_credentials.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

bool require(bool condition, const char *message) {
    if (condition)
        return true;
    std::fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

} // namespace

int main() {
    const char invalid_utf8[] = {static_cast<char>(0xc0), static_cast<char>(0x80), '\0'};
    const uint8_t one_byte = 1;
    if (!require(nk_credentials_set(invalid_utf8, "account", &one_byte, 1) ==
                     NK_ERROR_INVALID_ARGUMENT,
                 "invalid UTF-8 service was accepted") ||
        !require(nk_credentials_set("service", "account", &one_byte, 0) ==
                     NK_ERROR_INVALID_ARGUMENT,
                 "empty credential was accepted"))
        return 1;

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string service = "org.nativekit.tests." + std::to_string(nonce);
    constexpr const char *account = "binary round trip";
    uint32_t size = 0;
    auto status = nk_credentials_get(service.c_str(), account, nullptr, &size);
    if (status != NK_ERROR_NOT_FOUND) {
        std::fprintf(stderr, "SKIP: credential service is unavailable: %s\n",
                     nk_credentials_last_error());
        return 77;
    }

    const std::array<uint8_t, 5> secret{0x00, 0x42, 0xff, 0x00, 0x7e};
    if (!require(nk_credentials_set(service.c_str(), account, secret.data(), secret.size()) == NK_OK,
                 "store failed"))
        return 1;

    size = 0;
    status = nk_credentials_get(service.c_str(), account, nullptr, &size);
    if (!require(status == NK_ERROR_BUFFER_TOO_SMALL && size == secret.size(),
                 "size query did not report the stored size")) {
        nk_credentials_delete(service.c_str(), account);
        return 1;
    }

    std::array<uint8_t, 4> short_buffer{0xaa, 0xaa, 0xaa, 0xaa};
    size = short_buffer.size();
    status = nk_credentials_get(service.c_str(), account, short_buffer.data(), &size);
    if (!require(status == NK_ERROR_BUFFER_TOO_SMALL && size == secret.size() &&
                     short_buffer == std::array<uint8_t, 4>{0xaa, 0xaa, 0xaa, 0xaa},
                 "short read partially wrote the output buffer")) {
        nk_credentials_delete(service.c_str(), account);
        return 1;
    }

    std::array<uint8_t, 8> output{};
    size = output.size();
    status = nk_credentials_get(service.c_str(), account, output.data(), &size);
    if (!require(status == NK_OK && size == secret.size() &&
                     std::memcmp(output.data(), secret.data(), secret.size()) == 0,
                 "binary value did not round-trip")) {
        nk_credentials_delete(service.c_str(), account);
        return 1;
    }

    if (!require(nk_credentials_delete(service.c_str(), account) == NK_OK,
                 "delete failed") ||
        !require((size = 0, nk_credentials_get(service.c_str(), account, nullptr, &size)) ==
                     NK_ERROR_NOT_FOUND,
                 "deleted credential remained available"))
        return 1;
    return 0;
}
