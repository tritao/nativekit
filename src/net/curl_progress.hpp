#pragma once

#include "net/curl_runtime.hpp"

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
inline CurlProgressValues map_curl_progress(nk::curl::Offset download_total,
                                            nk::curl::Offset download_current,
                                            nk::curl::Offset upload_total,
                                            nk::curl::Offset upload_current) noexcept {
    return {static_cast<std::uint64_t>(download_current),
            static_cast<std::uint64_t>(download_total), static_cast<std::uint64_t>(upload_current),
            static_cast<std::uint64_t>(upload_total)};
}

} // namespace nk::net
