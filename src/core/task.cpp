#include "core/task.hpp"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"
#include "core/worker_pool.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::uint32_t default_step_budget_us = 2000;
constexpr std::uint32_t maximum_step_budget_us = 100000;
#if !defined(NK_BACKEND_WEB)
constexpr std::size_t worker_queue_capacity = 256;
#endif
constexpr std::size_t cooperative_queue_capacity = 256;
constexpr std::size_t cooperative_result_budget_us = 2000;

bool terminal_state(nk_task_state state) noexcept {
    return state == NK_TASK_STATE_COMPLETED || state == NK_TASK_STATE_FAILED ||
           state == NK_TASK_STATE_CANCELLED;
}

std::uint64_t budget_ns(std::uint32_t budget_us) noexcept {
    return static_cast<std::uint64_t>(budget_us) * 1000u;
}

bool valid_payload(const void *data, std::uint32_t size) noexcept {
    return size <= NK_TASK_MAX_PAYLOAD && (size == 0 || data != nullptr);
}

} // namespace

namespace nk::core {

class TaskManager;

class Task final : public Resource {
  public:
    Task(std::uint64_t generation, nk_task_execution_mode mode, std::uint32_t step_budget_us,
         nk_task_step_fn step, void *user_data)
        : generation_(generation), mode_(mode), step_budget_us_(step_budget_us), step_(step),
          user_data_(user_data) {}

    void set_handle(nk_task handle) noexcept { handle_ = handle; }
    nk_task handle() const noexcept { return handle_; }
    std::uint64_t generation() const noexcept { return generation_; }
    nk_task_execution_mode mode() const noexcept { return mode_; }

    bool is_destroyed() const noexcept { return destroyed_.load(std::memory_order_acquire); }
    bool is_cancelled() const noexcept { return cancel_requested_.load(std::memory_order_acquire); }
    void request_cancel() noexcept { cancel_requested_.store(true, std::memory_order_release); }

    nk_task_state state() const noexcept {
        std::lock_guard lock(mutex_);
        return state_;
    }

    void mark_destroyed() noexcept {
        destroyed_.store(true, std::memory_order_release);
        request_cancel();
        std::lock_guard lock(mutex_);
        if (!terminal_state(state_))
            state_ = NK_TASK_STATE_CANCELLED;
    }

    void run(TaskManager &manager, const std::shared_ptr<Task> &self) noexcept;

  private:
    friend class TaskManager;

    void emit_progress(std::vector<std::byte> payload, std::uint32_t count) noexcept;
    void emit_terminal(nk_task_state state, nk_result result, std::vector<std::byte> payload,
                       std::uint32_t count) noexcept;
    void set_state(nk_task_state state) noexcept {
        std::lock_guard lock(mutex_);
        if (!terminal_state(state_))
            state_ = state;
    }

    const std::uint64_t generation_;
    const nk_task_execution_mode mode_;
    const std::uint32_t step_budget_us_;
    const nk_task_step_fn step_;
    void *const user_data_;
    nk_task handle_ = NK_INVALID_HANDLE;
    mutable std::mutex mutex_;
    nk_task_state state_ = NK_TASK_STATE_RUNNING;
    std::atomic<bool> cancel_requested_{false};
    std::atomic<bool> destroyed_{false};
    std::atomic<bool> terminal_event_emitted_{false};
    std::atomic<bool> queued_{false};
};

class TaskManager final {
  public:
    nk_result initialize(std::uint64_t generation) noexcept {
        std::lock_guard lock(mutex_);
        if (accepting_)
            return NK_ERROR_ALREADY_INITIALIZED;
        generation_ = generation;
        accepting_ = true;
#if !defined(NK_BACKEND_WEB)
        auto worker_count = std::thread::hardware_concurrency();
        worker_count = std::max(1u, std::min(worker_count, 8u));
        const auto result = workers_.start(worker_count, worker_queue_capacity);
        if (result != NK_OK) {
            accepting_ = false;
            generation_ = 0;
            return result;
        }
#endif
        return NK_OK;
    }

    void shutdown() noexcept {
        std::vector<std::shared_ptr<Task>> tasks;
        {
            std::lock_guard lock(mutex_);
            accepting_ = false;
            generation_ = 0;
            try {
                tasks.reserve(tasks_.size());
                for (const auto &entry : tasks_)
                    tasks.push_back(entry.second);
            } catch (...) {
                for (const auto &entry : tasks_)
                    entry.second->mark_destroyed();
            }
        }

        nk::backend::stop_cooperative_tasks();
        for (auto &task : tasks)
            task->mark_destroyed();
        workers_.shutdown();
        {
            std::lock_guard lock(cooperative_mutex_);
            cooperative_queue_.clear();
        }
        std::lock_guard lock(mutex_);
        tasks_.clear();
    }

    nk_result start_task(const nk_task_options &options, nk_task_step_fn step, void *user_data,
                         nk_task *out_task) noexcept {
        const auto mode = options.execution_mode == NK_TASK_EXECUTION_AUTO ? native_default_mode()
                                                                           : options.execution_mode;
        if (mode != NK_TASK_EXECUTION_BACKGROUND && mode != NK_TASK_EXECUTION_COOPERATIVE) {
            set_error("unknown native task execution mode");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        nk_task handle = NK_INVALID_HANDLE;
        std::shared_ptr<Task> task;
        try {
            std::uint64_t generation = 0;
            {
                std::lock_guard lock(mutex_);
                if (!accepting_ || generation_ == 0) {
                    set_error("NativeKit is not initialized");
                    return NK_ERROR_NOT_INITIALIZED;
                }
                generation = generation_;
            }
            task =
                std::make_shared<Task>(generation, mode, options.step_budget_us, step, user_data);
            handle = handles().insert(ResourceType::task, task);
            if (handle == NK_INVALID_HANDLE) {
                set_error("could not allocate a native task handle");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            task->set_handle(handle);
            {
                std::lock_guard lock(mutex_);
                if (!accepting_ || generation_ != task->generation()) {
                    (void)handles().erase(handle, ResourceType::task);
                    set_error("NativeKit is shutting down");
                    return NK_ERROR_INVALID_REQUEST;
                }
                tasks_.emplace(handle, task);
            }
            const auto result = enqueue(task);
            if (result != NK_OK) {
                task->mark_destroyed();
                {
                    std::lock_guard lock(mutex_);
                    tasks_.erase(handle);
                }
                (void)handles().erase(handle, ResourceType::task);
                set_error("the native task queue is full");
                return result;
            }
            *out_task = handle;
            return NK_OK;
        } catch (const std::bad_alloc &) {
            if (task)
                task->mark_destroyed();
            if (handle != NK_INVALID_HANDLE) {
                std::lock_guard lock(mutex_);
                tasks_.erase(handle);
                (void)handles().erase(handle, ResourceType::task);
            }
            set_error("out of memory while starting a native task");
            return NK_ERROR_OUT_OF_MEMORY;
        } catch (...) {
            if (task)
                task->mark_destroyed();
            if (handle != NK_INVALID_HANDLE) {
                std::lock_guard lock(mutex_);
                tasks_.erase(handle);
                (void)handles().erase(handle, ResourceType::task);
            }
            set_error("unexpected error while starting a native task");
            return NK_ERROR_UNKNOWN;
        }
    }

    std::shared_ptr<Task> lookup(nk_task handle) const noexcept {
        return std::static_pointer_cast<Task>(handles().get(handle, ResourceType::task));
    }

    nk_result cancel(nk_task handle) noexcept {
        const auto task = lookup(handle);
        if (!task) {
            set_error("invalid native task handle");
            return NK_ERROR_INVALID_HANDLE;
        }
        if (task->is_destroyed())
            return NK_ERROR_INVALID_REQUEST;
        if (terminal_state(task->state()))
            return NK_ERROR_INVALID_REQUEST;
        task->request_cancel();
        if (task->state() == NK_TASK_STATE_YIELDED)
            return enqueue(task);
        return NK_OK;
    }

    nk_result destroy(nk_task handle) noexcept {
        const auto task = lookup(handle);
        if (!task) {
            set_error("invalid native task handle");
            return NK_ERROR_INVALID_HANDLE;
        }
        task->mark_destroyed();
        {
            std::lock_guard lock(mutex_);
            tasks_.erase(handle);
        }
        if (!handles().erase(handle, ResourceType::task))
            return NK_ERROR_INVALID_HANDLE;
        return NK_OK;
    }

    void run_cooperative() noexcept {
        const auto deadline =
            Clock::now() + std::chrono::microseconds(cooperative_result_budget_us);
        while (Clock::now() < deadline) {
            std::shared_ptr<Task> task;
            {
                std::lock_guard lock(cooperative_mutex_);
                if (cooperative_queue_.empty())
                    break;
                task = std::move(cooperative_queue_.front());
                cooperative_queue_.pop_front();
            }
            if (!task || task->is_destroyed())
                continue;
            task->queued_.store(false, std::memory_order_release);
            task->run(*this, task);
        }
    }

    bool cooperative_pending() const noexcept {
        std::lock_guard lock(cooperative_mutex_);
        return !cooperative_queue_.empty();
    }

    nk_result enqueue(const std::shared_ptr<Task> &task) noexcept {
        if (!task || task->is_destroyed())
            return NK_ERROR_INVALID_REQUEST;
        {
            std::lock_guard lock(mutex_);
            if (!accepting_ || generation_ == 0)
                return NK_ERROR_INVALID_REQUEST;
        }
        if (task->queued_.exchange(true, std::memory_order_acq_rel))
            return NK_OK;
        try {
            if (task->mode() == NK_TASK_EXECUTION_COOPERATIVE) {
                {
                    std::lock_guard lock(cooperative_mutex_);
                    if (cooperative_queue_.size() >= cooperative_queue_capacity) {
                        task->queued_.store(false, std::memory_order_release);
                        return NK_ERROR_QUEUE_FULL;
                    }
                    cooperative_queue_.push_back(task);
                }
                nk::backend::schedule_cooperative_tasks();
                return NK_OK;
            }
            const auto result = workers_.submit([this, task] {
                task->queued_.store(false, std::memory_order_release);
                if (!task->is_destroyed())
                    task->run(*this, task);
            });
            if (result != NK_OK)
                task->queued_.store(false, std::memory_order_release);
            return result;
        } catch (const std::bad_alloc &) {
            task->queued_.store(false, std::memory_order_release);
            return NK_ERROR_OUT_OF_MEMORY;
        } catch (...) {
            task->queued_.store(false, std::memory_order_release);
            return NK_ERROR_UNKNOWN;
        }
    }

  private:
    friend class Task;

    static nk_task_execution_mode native_default_mode() noexcept {
#if defined(NK_BACKEND_WEB)
        return NK_TASK_EXECUTION_COOPERATIVE;
#else
        return NK_TASK_EXECUTION_BACKGROUND;
#endif
    }

    mutable std::mutex mutex_;
    std::uint64_t generation_ = 0;
    bool accepting_ = false;
    std::unordered_map<nk_task, std::shared_ptr<Task>> tasks_;
    WorkerPool workers_;
    mutable std::mutex cooperative_mutex_;
    std::deque<std::shared_ptr<Task>> cooperative_queue_;
};

TaskManager task_manager;

void Task::emit_progress(std::vector<std::byte> payload, std::uint32_t count) noexcept {
    if (is_destroyed() || !is_runtime_generation(generation_))
        return;
    QueuedEvent event;
    event.kind = NK_EVENT_TASK_PROGRESS;
    event.source = static_cast<nk_handle>(handle_);
    event.result = NK_OK;
    event.data_count = count;
    event.data = std::move(payload);
    (void)push_event(std::move(event));
}

void Task::emit_terminal(nk_task_state state, nk_result result, std::vector<std::byte> payload,
                         std::uint32_t count) noexcept {
    if (is_destroyed() || !is_runtime_generation(generation_) ||
        terminal_event_emitted_.exchange(true, std::memory_order_acq_rel))
        return;
    {
        std::lock_guard lock(mutex_);
        state_ = state;
    }
    QueuedEvent event;
    event.kind = state == NK_TASK_STATE_COMPLETED   ? NK_EVENT_TASK_COMPLETE
                 : state == NK_TASK_STATE_CANCELLED ? NK_EVENT_TASK_CANCELLED
                                                    : NK_EVENT_TASK_FAILED;
    event.source = static_cast<nk_handle>(handle_);
    event.result = result;
    event.data_count = count;
    event.data = std::move(payload);
    (void)push_event(std::move(event));
}

void Task::run(TaskManager &manager, const std::shared_ptr<Task> &self) noexcept {
    if (is_destroyed() || terminal_state(state()))
        return;
    if (is_cancelled()) {
        emit_terminal(NK_TASK_STATE_CANCELLED, NK_ERROR_CANCELLED, {}, 0);
        return;
    }
    set_state(NK_TASK_STATE_RUNNING);

    nk_task_step_context context{};
    context.struct_size = sizeof(context);
    context.state = user_data_;
    context.cancelled = is_cancelled() ? 1u : 0u;
    context.budget_ns = budget_ns(step_budget_us_);
    context.runtime_generation = generation_;

    nk_task_step_output output{};
    output.struct_size = sizeof(output);
    output.result = NK_ERROR_UNKNOWN;
    nk_task_step_result step_result = NK_TASK_STEP_FAILED;
    bool callback_failed = false;
    try {
        step_result = step_(&context, &output);
    } catch (...) {
        callback_failed = true;
    }

    std::vector<std::byte> progress;
    std::vector<std::byte> result_payload;
    try {
        if (!valid_payload(output.progress_data, output.progress_size) ||
            !valid_payload(output.result_data, output.result_size)) {
            emit_terminal(NK_TASK_STATE_FAILED, NK_ERROR_PAYLOAD_TOO_LARGE, {}, 0);
            return;
        }
        if (output.progress_size != 0) {
            const auto *first = static_cast<const std::byte *>(output.progress_data);
            progress.assign(first, first + output.progress_size);
        }
        if (output.result_size != 0) {
            const auto *first = static_cast<const std::byte *>(output.result_data);
            result_payload.assign(first, first + output.result_size);
        }
    } catch (const std::bad_alloc &) {
        emit_terminal(NK_TASK_STATE_FAILED, NK_ERROR_OUT_OF_MEMORY, {}, 0);
        return;
    } catch (...) {
        emit_terminal(NK_TASK_STATE_FAILED, NK_ERROR_UNKNOWN, {}, 0);
        return;
    }

    if (!progress.empty() || output.progress_count != 0)
        emit_progress(std::move(progress), output.progress_count);
    if (is_cancelled()) {
        emit_terminal(NK_TASK_STATE_CANCELLED, NK_ERROR_CANCELLED, std::move(result_payload),
                      output.result_count);
        return;
    }
    if (callback_failed || step_result == NK_TASK_STEP_FAILED) {
        const auto failure = output.result == NK_OK || output.result == NK_PENDING
                                 ? NK_ERROR_UNKNOWN
                                 : output.result;
        emit_terminal(NK_TASK_STATE_FAILED, failure, std::move(result_payload),
                      output.result_count);
        return;
    }
    if (step_result == NK_TASK_STEP_COMPLETE) {
        emit_terminal(NK_TASK_STATE_COMPLETED, NK_OK, std::move(result_payload),
                      output.result_count);
        return;
    }
    if (step_result != NK_TASK_STEP_YIELD) {
        emit_terminal(NK_TASK_STATE_FAILED, NK_ERROR_INVALID_ARGUMENT, {}, 0);
        return;
    }

    set_state(NK_TASK_STATE_YIELDED);
    const auto result = manager.enqueue(self);
    if (result != NK_OK)
        emit_terminal(NK_TASK_STATE_FAILED, result, {}, 0);
}

nk_result task_runtime_initialize(std::uint64_t generation) noexcept {
    return task_manager.initialize(generation);
}

void task_runtime_shutdown() noexcept {
    task_manager.shutdown();
}

void run_cooperative_tasks() noexcept {
    task_manager.run_cooperative();
}

bool cooperative_tasks_pending() noexcept {
    return task_manager.cooperative_pending();
}

} // namespace nk::core

extern "C" {

nk_result NK_CALL nk_task_start(const nk_task_options *options, nk_task_step_fn step,
                                void *user_data, nk_task *out_task) {
    return nk::core::result_boundary(
        "unexpected exception while starting a native task", [&]() -> nk_result {
            nk::core::clear_error();
            if (!options || !out_task || !step) {
                nk::core::set_error("native task options, step, and output are required");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            *out_task = NK_INVALID_HANDLE;
            if (options->struct_size < offsetof(nk_task_options, reserved)) {
                nk::core::set_error("nk_task_options is missing or too small");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (options->step_budget_us > maximum_step_budget_us) {
                nk::core::set_error("native task step budget is too large");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            const auto thread_result =
                nk::core::runtime_generation() == 0 ? NK_ERROR_NOT_INITIALIZED : NK_OK;
            if (thread_result != NK_OK) {
                nk::core::set_error("NativeKit is not initialized");
                return thread_result;
            }
            nk_task_options copy{};
            copy.struct_size = sizeof(copy);
            copy.execution_mode = options->execution_mode;
            copy.step_budget_us = options->step_budget_us;
            if (copy.step_budget_us == 0)
                copy.step_budget_us = default_step_budget_us;
            return nk::core::task_manager.start_task(copy, step, user_data, out_task);
        });
}

nk_result NK_CALL nk_task_cancel(nk_task task) {
    return nk::core::result_boundary("unexpected exception while cancelling a native task",
                                     [&]() -> nk_result {
                                         nk::core::clear_error();
                                         return nk::core::task_manager.cancel(task);
                                     });
}

nk_result NK_CALL nk_task_get_state(nk_task task, nk_task_state *out_state) {
    return nk::core::result_boundary(
        "unexpected exception while reading native task state", [&]() -> nk_result {
            nk::core::clear_error();
            if (!out_state) {
                nk::core::set_error("native task state output is null");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            const auto resource = nk::core::task_manager.lookup(task);
            if (!resource) {
                nk::core::set_error("invalid native task handle");
                return NK_ERROR_INVALID_HANDLE;
            }
            *out_state = resource->state();
            return NK_OK;
        });
}

nk_result NK_CALL nk_task_destroy(nk_task task) {
    return nk::core::result_boundary("unexpected exception while destroying a native task",
                                     [&]() -> nk_result {
                                         nk::core::clear_error();
                                         return nk::core::task_manager.destroy(task);
                                     });
}

} // extern "C"
