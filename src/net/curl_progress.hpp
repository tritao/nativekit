#pragma once

#include <curl/curl.h>

#include <cstdint>

namespace nk::net {

struct CurlProgressValues {
    std::uint64_t downloaded = 0;
    std::uint64_t download_total = 0;
    std::uint64_t uploaded = 0;
    std::uint64_t upload_total = 0;
};

/* libcurl orders these callback arguments as download total/current followed
 * by upload total/current. Keep that transport order at this boundary and
 * expose NativeKit's current/total event order explicitly. */
inline CurlProgressValues map_curl_progress(curl_off_t download_total, curl_off_t download_current,
                                            curl_off_t upload_total,
                                            curl_off_t upload_current) noexcept {
    return {static_cast<std::uint64_t>(download_current),
            static_cast<std::uint64_t>(download_total), static_cast<std::uint64_t>(upload_current),
            static_cast<std::uint64_t>(upload_total)};
}

} // namespace nk::net
