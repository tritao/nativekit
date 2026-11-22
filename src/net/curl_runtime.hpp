#pragma once

#include <cstddef>
#include <cstdint>

/*
 * The part of libcurl's stable C ABI that NativeKit uses, declared here so the
 * library builds without curl's development files and loads libcurl the first
 * time it is needed. A system without libcurl reports the capabilities that
 * depend on it as unavailable. The values match curl/curl.h; the curl_abi test
 * compares them with the real header wherever it is installed.
 */

namespace nk::curl {

using Handle = void;
using Code = int;
using Option = int;
using Info = int;
using Offset = std::int64_t;
using Socket = int;

/* CURLcode */
constexpr Code ok = 0;
constexpr Code couldnt_resolve_proxy = 5;
constexpr Code couldnt_resolve_host = 6;
constexpr Code couldnt_connect = 7;
constexpr Code operation_timedout = 28;
constexpr Code aborted_by_callback = 42;
constexpr Code again = 81;

/* CURLoption: type base plus number, as CURLOPT() composes them. */
constexpr Option option_long = 0;
constexpr Option option_pointer = 10000;
constexpr Option option_function = 20000;
constexpr Option option_offset = 30000;

constexpr Option opt_url = option_pointer + 2;
constexpr Option opt_noprogress = option_long + 43;
constexpr Option opt_xferinfodata = option_pointer + 57;
constexpr Option opt_cainfo = option_pointer + 65;
constexpr Option opt_capath = option_pointer + 97;
constexpr Option opt_nosignal = option_long + 99;
constexpr Option opt_connect_only = option_long + 141;
constexpr Option opt_timeout_ms = option_long + 155;
constexpr Option opt_connecttimeout_ms = option_long + 156;
constexpr Option opt_xferinfofunction = option_function + 219;

/* CURLINFO */
constexpr Info info_socket = 0x500000;
constexpr Info info_activesocket = info_socket + 44;

/* curl_global_init flags: CURL_GLOBAL_DEFAULT. */
constexpr long global_default = 3;
/* curl_version_info_data.features bits. */
constexpr int version_ssl = 1 << 2;

/** The leading, never-reordered fields of curl_version_info_data. */
struct VersionInfo {
    int age;
    const char *version;
    unsigned int version_num;
    const char *host;
    int features;
};

using XferInfoCallback = int (*)(void *user_data, Offset download_total, Offset downloaded,
                                 Offset upload_total, Offset uploaded);

/** libcurl entry points, resolved once from the installed library. */
struct Api {
    unsigned int version_num = 0;
    int features = 0;

    Handle *(*easy_init)() = nullptr;
    Code (*easy_setopt)(Handle *, Option, ...) = nullptr;
    Code (*easy_perform)(Handle *) = nullptr;
    Code (*easy_getinfo)(Handle *, Info, ...) = nullptr;
    Code (*easy_recv)(Handle *, void *, std::size_t, std::size_t *) = nullptr;
    Code (*easy_send)(Handle *, const void *, std::size_t, std::size_t *) = nullptr;
    void (*easy_cleanup)(Handle *) = nullptr;
};

/**
 * Loads libcurl (libcurl.so.4, or Debian's libcurl-gnutls.so.4) and initializes it once for
 * the life of the process. Returns null when no usable libcurl is installed.
 */
const Api *api() noexcept;

} // namespace nk::curl
