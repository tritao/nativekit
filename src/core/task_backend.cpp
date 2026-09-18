#include "core/runtime.hpp"

#if !defined(NK_BACKEND_WEB)
namespace nk::backend {

void schedule_cooperative_tasks() noexcept {}
void stop_cooperative_tasks() noexcept {}

} // namespace nk::backend
#endif
