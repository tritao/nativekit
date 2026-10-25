#include "core/thread_name.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#include <pthread.h>
#endif

#include <cstring>

namespace nk::core {

void name_current_thread(const char *name) noexcept {
    if (!name || !*name)
        return;
#if defined(_WIN32)
    wchar_t wide[128];
    char narrow[128];
    std::strncpy(narrow, name, sizeof(narrow) - 1);
    narrow[sizeof(narrow) - 1] = 0;
    if (MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, 128) > 0)
        SetThreadDescription(GetCurrentThread(), wide);
#elif defined(__APPLE__)
    char narrow[64];
    std::strncpy(narrow, name, sizeof(narrow) - 1);
    narrow[sizeof(narrow) - 1] = 0;
    pthread_setname_np(narrow);
#elif defined(__linux__) || defined(__ANDROID__)
    char narrow[16];
    std::strncpy(narrow, name, sizeof(narrow) - 1);
    narrow[sizeof(narrow) - 1] = 0;
    pthread_setname_np(pthread_self(), narrow);
#endif
}

void name_current_thread(const std::string &name) noexcept {
    name_current_thread(name.c_str());
}

} // namespace nk::core
