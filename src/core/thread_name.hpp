#pragma once

namespace nk::core {

/** Names the calling thread for debuggers, crash dumps and profilers (SetThreadDescription,
   pthread_setname_np). Linux keeps the first 15 bytes, so names start with what tells threads
   apart. */
void name_current_thread(const char *name) noexcept;
/** The name from a printf format, built on the stack: a thread can name itself first thing, where
   nothing must throw. */
void name_current_thread_format(const char *format, ...) noexcept;

} // namespace nk::core
