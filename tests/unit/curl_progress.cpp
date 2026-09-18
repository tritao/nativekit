#include "net/curl_progress.hpp"

#include <cstdio>
#include <cstdlib>

#define NK_CHECK(expression)                                                                    \
    do {                                                                                       \
        if (!(expression)) {                                                                    \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            std::abort();                                                                       \
        }                                                                                        \
    } while (false)

int main() {
    const auto progress = nk::net::map_curl_progress(100, 25, 80, 7);
    NK_CHECK(progress.downloaded == 25);
    NK_CHECK(progress.download_total == 100);
    NK_CHECK(progress.uploaded == 7);
    NK_CHECK(progress.upload_total == 80);
    return 0;
}
