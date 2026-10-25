#pragma once

#include <string>

namespace nk::core {

/** Names the calling thread for debuggers, crash dumps and profilers (SetThreadDescription, pthread_setname_np). Linux
    keeps the first 15 bytes, so names start with what tells threads apart. */
void name_current_thread(const char *name) noexcept;
void name_current_thread(const std::string &name) noexcept;

} // namespace nk::core
