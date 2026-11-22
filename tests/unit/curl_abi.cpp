#include "net/curl_runtime.hpp"

#include <curl/curl.h>

#include <cstddef>
#include <type_traits>

/* NativeKit loads libcurl at run time from its own declarations; they must match curl's ABI. */

namespace curl = nk::curl;

static_assert(std::is_same_v<curl::Handle, CURL>);
static_assert(std::is_same_v<curl::Offset, curl_off_t>);
static_assert(std::is_same_v<curl::Socket, curl_socket_t>);
static_assert(sizeof(curl::Code) == sizeof(CURLcode));
static_assert(sizeof(curl::Option) == sizeof(CURLoption));
static_assert(sizeof(curl::Info) == sizeof(CURLINFO));

static_assert(curl::ok == CURLE_OK);
static_assert(curl::couldnt_resolve_proxy == CURLE_COULDNT_RESOLVE_PROXY);
static_assert(curl::couldnt_resolve_host == CURLE_COULDNT_RESOLVE_HOST);
static_assert(curl::couldnt_connect == CURLE_COULDNT_CONNECT);
static_assert(curl::operation_timedout == CURLE_OPERATION_TIMEDOUT);
static_assert(curl::aborted_by_callback == CURLE_ABORTED_BY_CALLBACK);
static_assert(curl::again == CURLE_AGAIN);

static_assert(curl::opt_url == CURLOPT_URL);
static_assert(curl::opt_noprogress == CURLOPT_NOPROGRESS);
static_assert(curl::opt_xferinfodata == CURLOPT_XFERINFODATA);
static_assert(curl::opt_cainfo == CURLOPT_CAINFO);
static_assert(curl::opt_capath == CURLOPT_CAPATH);
static_assert(curl::opt_nosignal == CURLOPT_NOSIGNAL);
static_assert(curl::opt_connect_only == CURLOPT_CONNECT_ONLY);
static_assert(curl::opt_timeout_ms == CURLOPT_TIMEOUT_MS);
static_assert(curl::opt_connecttimeout_ms == CURLOPT_CONNECTTIMEOUT_MS);
static_assert(curl::opt_xferinfofunction == CURLOPT_XFERINFOFUNCTION);

static_assert(curl::info_activesocket == CURLINFO_ACTIVESOCKET);
static_assert(curl::global_default == CURL_GLOBAL_DEFAULT);
static_assert(curl::version_ssl == CURL_VERSION_SSL);

static_assert(offsetof(curl::VersionInfo, version_num) ==
              offsetof(curl_version_info_data, version_num));
static_assert(offsetof(curl::VersionInfo, features) == offsetof(curl_version_info_data, features));

static_assert(std::is_same_v<curl::XferInfoCallback, curl_xferinfo_callback>);
static_assert(std::is_same_v<decltype(curl::Api::easy_init), decltype(&curl_easy_init)>);
static_assert(std::is_same_v<decltype(curl::Api::easy_cleanup), decltype(&curl_easy_cleanup)>);

/* Every check above is made by the compiler. */
int main() {
    return 0;
}
