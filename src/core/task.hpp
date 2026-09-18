#pragma once

#include "nativekit_task.h"

#include <cstdint>

namespace nk::core {

/** Starts the task runtime for one NativeKit generation. */
nk_result task_runtime_initialize(std::uint64_t generation) noexcept;

/** Cancels, joins, and discards all task work for the active generation. */
void task_runtime_shutdown() noexcept;

/** Runs a bounded batch of cooperative task steps on the calling app thread. */
void run_cooperative_tasks() noexcept;

/** Reports whether cooperative work remains scheduled. */
bool cooperative_tasks_pending() noexcept;

} // namespace nk::core
