#pragma once

#include "core/event_queue.hpp"
#include "core/handle_registry.hpp"
#include "core/internal_api.hpp"
#include "nativekit_window.h"
#include "nativekit_clipboard.h"
#include "nativekit_file_watch.h"

#include <cstdint>
#include <chrono>

namespace nk::core {

using RuntimeShutdownHook = void (*)() noexcept;

nk_result require_ui_thread() noexcept;
HandleRegistry &handles() noexcept;
nk_result push_event(QueuedEvent event) noexcept;
nk_request_id next_request_id() noexcept;
NK_INTERNAL_API std::uint64_t runtime_generation() noexcept;
NK_INTERNAL_API bool is_runtime_generation(std::uint64_t generation) noexcept;
/** Capabilities supplied by optional modules compiled into this library. */
nk_capabilities optional_capabilities() noexcept;
bool events_pending() noexcept;
std::uint64_t wake_sequence() noexcept;
bool wait_for_wake(std::uint64_t sequence, std::chrono::milliseconds timeout) noexcept;
void wake_events() noexcept;
/** Registers a module-owned cleanup hook invoked during nk_shutdown(). */
NK_INTERNAL_API void register_runtime_shutdown_hook(RuntimeShutdownHook hook) noexcept;
/** Runs registered module cleanup after the render executor has stopped. */
void run_runtime_shutdown_hooks() noexcept;

} // namespace nk::core

namespace nk::backend {
void pump_events() noexcept;
void shutdown() noexcept;
void schedule_cooperative_tasks() noexcept;
void stop_cooperative_tasks() noexcept;
nk_result file_watch_create(const nk_file_watch_options *options,
                            nk_file_watch *out_watch) noexcept;
nk_result file_watch_add_directory(nk_file_watch watch, const char *path,
                                   nk_bool recursive) noexcept;
nk_result file_watch_remove_directory(nk_file_watch watch, const char *path) noexcept;
nk_result file_watch_destroy(nk_file_watch watch) noexcept;
nk_result clipboard_watch_start(const nk_clipboard_watch_options *options,
                                nk_clipboard_watch *out_watch) noexcept;
nk_result clipboard_watch_stop(nk_clipboard_watch watch) noexcept;
} // namespace nk::backend

namespace nk::transport {
nk_capabilities capabilities() noexcept;
void shutdown() noexcept;
} // namespace nk::transport
