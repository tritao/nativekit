#pragma once

#include "core/internal_api.hpp"

#include <cstddef>
#include <cstdint>

/*
 * The part of libcurl's stable C ABI that NativeKit uses, declared here so the
 * library builds without curl's development files and loads libcurl the first
 * time it is needed. A system without libcurl reports the capabilities that
 * depend on it as unavailable. The values match curl/curl.h; the curl_abi test
 * compares them with the real header wherever it is installed.
 */

struct curl_slist;

namespace nk::curl {

using Handle = void;
using Share = void;
using Slist = ::curl_slist;
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
constexpr Code http2 = 16;
constexpr Code partial_file = 18;
constexpr Code http_returned_error = 22;
constexpr Code operation_timedout = 28;
constexpr Code ssl_connect_error = 35;
constexpr Code aborted_by_callback = 42;
constexpr Code interface_failed = 45;
constexpr Code too_many_redirects = 47;
constexpr Code send_error = 55;
constexpr Code recv_error = 56;
constexpr Code ssl_certproblem = 58;
constexpr Code ssl_cipher = 59;
constexpr Code peer_failed_verification = 60;
constexpr Code use_ssl_failed = 64;
constexpr Code again = 81;
constexpr Code ssl_issuer_error = 83;
constexpr Code no_connection_available = 89;
constexpr Code http3 = 95;
constexpr Code proxy = 97;

/* CURLoption: type base plus number, as CURLOPT() composes them. */
constexpr Option option_long = 0;
constexpr Option option_pointer = 10000;
constexpr Option option_function = 20000;
constexpr Option option_offset = 30000;

constexpr Option opt_writedata = option_pointer + 1;
constexpr Option opt_url = option_pointer + 2;
constexpr Option opt_proxy = option_pointer + 4;
constexpr Option opt_readdata = option_pointer + 9;
constexpr Option opt_writefunction = option_function + 11;
constexpr Option opt_readfunction = option_function + 12;
constexpr Option opt_postfields = option_pointer + 15;
constexpr Option opt_httpheader = option_pointer + 23;
constexpr Option opt_headerdata = option_pointer + 29;
constexpr Option opt_cookiefile = option_pointer + 31;
constexpr Option opt_sslversion = option_long + 32;
constexpr Option opt_customrequest = option_pointer + 36;
constexpr Option opt_noprogress = option_long + 43;
constexpr Option opt_nobody = option_long + 44;
constexpr Option opt_failonerror = option_long + 45;
constexpr Option opt_upload = option_long + 46;
constexpr Option opt_followlocation = option_long + 52;
constexpr Option opt_xferinfodata = option_pointer + 57;
constexpr Option opt_ssl_verifypeer = option_long + 64;
constexpr Option opt_cainfo = option_pointer + 65;
constexpr Option opt_maxredirs = option_long + 68;
constexpr Option opt_headerfunction = option_function + 79;
constexpr Option opt_ssl_verifyhost = option_long + 81;
constexpr Option opt_capath = option_pointer + 97;
constexpr Option opt_nosignal = option_long + 99;
constexpr Option opt_share = option_pointer + 100;
constexpr Option opt_proxytype = option_long + 101;
constexpr Option opt_infilesize_large = option_offset + 115;
constexpr Option opt_postfieldsize_large = option_offset + 120;
constexpr Option opt_protocols = option_long + 181;
constexpr Option opt_redir_protocols = option_long + 182;
constexpr Option opt_connect_only = option_long + 141;
constexpr Option opt_timeout_ms = option_long + 155;
constexpr Option opt_connecttimeout_ms = option_long + 156;
constexpr Option opt_proxyusername = option_pointer + 175;
constexpr Option opt_proxypassword = option_pointer + 176;
constexpr Option opt_xferinfofunction = option_function + 219;
constexpr Option opt_protocols_str = option_pointer + 318;
constexpr Option opt_redir_protocols_str = option_pointer + 319;

/* CURLOPT_PROXYTYPE and CURLOPT_SSLVERSION values. */
constexpr long proxy_http = 0;
constexpr long proxy_https = 2;
constexpr long proxy_socks5_hostname = 7;
constexpr long sslversion_tlsv1_2 = 6;
constexpr long sslversion_tlsv1_3 = 7;
/* CURLOPT_PROTOCOLS and CURLOPT_REDIR_PROTOCOLS bits. */
constexpr long proto_http = 1 << 0;
constexpr long proto_https = 1 << 1;
/* A read callback's return value that aborts the transfer. */
constexpr std::size_t readfunc_abort = 0x10000000;

/* CURLINFO */
constexpr Info info_long = 0x200000;
constexpr Info info_socket = 0x500000;
constexpr Info info_offset = 0x600000;
constexpr Info info_response_code = info_long + 2;
constexpr Info info_redirect_count = info_long + 20;
constexpr Info info_activesocket = info_socket + 44;
constexpr Info info_content_length_download_t = info_offset + 15;

/* Share interface: CURLSHcode, CURLSHoption, curl_lock_data, curl_lock_access. */
using ShareCode = int;
using ShareOption = int;
using LockData = int;
using LockAccess = int;
constexpr ShareCode share_ok = 0;
constexpr ShareOption shareopt_share = 1;
constexpr ShareOption shareopt_lockfunc = 3;
constexpr ShareOption shareopt_unlockfunc = 4;
constexpr LockData lock_data_cookie = 2;

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
using DataCallback = std::size_t (*)(char *data, std::size_t size, std::size_t count,
                                     void *user_data);
using LockCallback = void (*)(Handle *handle, LockData data, LockAccess access, void *user_data);
using UnlockCallback = void (*)(Handle *handle, LockData data, void *user_data);

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
    Slist *(*slist_append)(Slist *, const char *) = nullptr;
    void (*slist_free_all)(Slist *) = nullptr;
    Share *(*share_init)() = nullptr;
    ShareCode (*share_setopt)(Share *, ShareOption, ...) = nullptr;
    ShareCode (*share_cleanup)(Share *) = nullptr;
};

/**
 * Loads libcurl (libcurl.so.4, or Debian's libcurl-gnutls.so.4) and initializes it once for
 * the life of the process. Returns null when no usable libcurl is installed.
 */
const Api *api() noexcept;

} // namespace nk::curl

namespace nk::core {
/**
 * Test hook, not public API: makes the HTTP backend restrict protocols with the bitmask
 * options it uses for libcurl older than 7.85, whatever version is loaded.
 */
NK_INTERNAL_API void use_curl_protocol_bitmask_for_testing(bool enabled) noexcept;
} // namespace nk::core
