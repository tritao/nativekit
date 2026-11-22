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

static_assert(curl::http2 == CURLE_HTTP2);
static_assert(curl::partial_file == CURLE_PARTIAL_FILE);
static_assert(curl::http_returned_error == CURLE_HTTP_RETURNED_ERROR);
static_assert(curl::ssl_connect_error == CURLE_SSL_CONNECT_ERROR);
static_assert(curl::interface_failed == CURLE_INTERFACE_FAILED);
static_assert(curl::too_many_redirects == CURLE_TOO_MANY_REDIRECTS);
static_assert(curl::send_error == CURLE_SEND_ERROR);
static_assert(curl::recv_error == CURLE_RECV_ERROR);
static_assert(curl::ssl_certproblem == CURLE_SSL_CERTPROBLEM);
static_assert(curl::ssl_cipher == CURLE_SSL_CIPHER);
static_assert(curl::peer_failed_verification == CURLE_PEER_FAILED_VERIFICATION);
static_assert(curl::use_ssl_failed == CURLE_USE_SSL_FAILED);
static_assert(curl::ssl_issuer_error == CURLE_SSL_ISSUER_ERROR);
static_assert(curl::no_connection_available == CURLE_NO_CONNECTION_AVAILABLE);
static_assert(curl::http3 == CURLE_HTTP3);
static_assert(curl::proxy == CURLE_PROXY);

static_assert(curl::opt_writedata == CURLOPT_WRITEDATA);
static_assert(curl::opt_proxy == CURLOPT_PROXY);
static_assert(curl::opt_readdata == CURLOPT_READDATA);
static_assert(curl::opt_writefunction == CURLOPT_WRITEFUNCTION);
static_assert(curl::opt_readfunction == CURLOPT_READFUNCTION);
static_assert(curl::opt_postfields == CURLOPT_POSTFIELDS);
static_assert(curl::opt_httpheader == CURLOPT_HTTPHEADER);
static_assert(curl::opt_headerdata == CURLOPT_HEADERDATA);
static_assert(curl::opt_cookiefile == CURLOPT_COOKIEFILE);
static_assert(curl::opt_sslversion == CURLOPT_SSLVERSION);
static_assert(curl::opt_customrequest == CURLOPT_CUSTOMREQUEST);
static_assert(curl::opt_nobody == CURLOPT_NOBODY);
static_assert(curl::opt_failonerror == CURLOPT_FAILONERROR);
static_assert(curl::opt_upload == CURLOPT_UPLOAD);
static_assert(curl::opt_followlocation == CURLOPT_FOLLOWLOCATION);
static_assert(curl::opt_ssl_verifypeer == CURLOPT_SSL_VERIFYPEER);
static_assert(curl::opt_maxredirs == CURLOPT_MAXREDIRS);
static_assert(curl::opt_headerfunction == CURLOPT_HEADERFUNCTION);
static_assert(curl::opt_ssl_verifyhost == CURLOPT_SSL_VERIFYHOST);
static_assert(curl::opt_share == CURLOPT_SHARE);
static_assert(curl::opt_proxytype == CURLOPT_PROXYTYPE);
static_assert(curl::opt_infilesize_large == CURLOPT_INFILESIZE_LARGE);
static_assert(curl::opt_postfieldsize_large == CURLOPT_POSTFIELDSIZE_LARGE);
static_assert(curl::opt_proxyusername == CURLOPT_PROXYUSERNAME);
static_assert(curl::opt_proxypassword == CURLOPT_PROXYPASSWORD);
static_assert(curl::opt_protocols_str == CURLOPT_PROTOCOLS_STR);
static_assert(curl::opt_redir_protocols_str == CURLOPT_REDIR_PROTOCOLS_STR);

static_assert(curl::proxy_http == CURLPROXY_HTTP);
static_assert(curl::proxy_https == CURLPROXY_HTTPS);
static_assert(curl::proxy_socks5_hostname == CURLPROXY_SOCKS5_HOSTNAME);
static_assert(curl::sslversion_tlsv1_2 == CURL_SSLVERSION_TLSv1_2);
static_assert(curl::sslversion_tlsv1_3 == CURL_SSLVERSION_TLSv1_3);
static_assert(curl::readfunc_abort == CURL_READFUNC_ABORT);

static_assert(curl::info_response_code == CURLINFO_RESPONSE_CODE);
static_assert(curl::info_redirect_count == CURLINFO_REDIRECT_COUNT);
static_assert(curl::info_activesocket == CURLINFO_ACTIVESOCKET);
static_assert(curl::info_content_length_download_t == CURLINFO_CONTENT_LENGTH_DOWNLOAD_T);

static_assert(std::is_same_v<curl::Share, CURLSH>);
static_assert(std::is_same_v<curl::Slist, curl_slist>);
static_assert(sizeof(curl::ShareCode) == sizeof(CURLSHcode));
static_assert(sizeof(curl::LockData) == sizeof(curl_lock_data));
static_assert(curl::share_ok == CURLSHE_OK);
static_assert(curl::shareopt_share == CURLSHOPT_SHARE);
static_assert(curl::shareopt_lockfunc == CURLSHOPT_LOCKFUNC);
static_assert(curl::shareopt_unlockfunc == CURLSHOPT_UNLOCKFUNC);
static_assert(curl::lock_data_cookie == CURL_LOCK_DATA_COOKIE);
static_assert(std::is_same_v<curl::DataCallback, curl_write_callback>);
static_assert(std::is_same_v<curl::DataCallback, curl_read_callback>);
static_assert(curl::global_default == CURL_GLOBAL_DEFAULT);
static_assert(curl::version_ssl == CURL_VERSION_SSL);

static_assert(offsetof(curl::VersionInfo, version_num) ==
              offsetof(curl_version_info_data, version_num));
static_assert(offsetof(curl::VersionInfo, features) == offsetof(curl_version_info_data, features));

static_assert(std::is_same_v<curl::XferInfoCallback, curl_xferinfo_callback>);
static_assert(std::is_same_v<decltype(curl::Api::easy_init), decltype(&curl_easy_init)>);
static_assert(std::is_same_v<decltype(curl::Api::easy_cleanup), decltype(&curl_easy_cleanup)>);
static_assert(std::is_same_v<decltype(curl::Api::slist_append), decltype(&curl_slist_append)>);
static_assert(std::is_same_v<decltype(curl::Api::slist_free_all), decltype(&curl_slist_free_all)>);
static_assert(std::is_same_v<decltype(curl::Api::share_init), decltype(&curl_share_init)>);

/* Every check above is made by the compiler. */
int main() {
    return 0;
}
