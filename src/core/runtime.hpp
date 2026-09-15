#pragma once

#include "core/event_queue.hpp"
#include "core/handle_registry.hpp"
#include "nativekit_window.h"

#include <cstdint>
#include <chrono>

namespace nk::core {

nk_result require_ui_thread() noexcept;
HandleRegistry &handles() noexcept;
nk_result push_event(QueuedEvent event) noexcept;
nk_request_id next_request_id() noexcept;
std::uint64_t runtime_generation() noexcept;
bool is_runtime_generation(std::uint64_t generation) noexcept;
/** Capabilities supplied by optional modules compiled into this library. */
nk_capabilities optional_capabilities() noexcept;
bool events_pending() noexcept;
std::uint64_t wake_sequence() noexcept;
bool wait_for_wake(std::uint64_t sequence, std::chrono::milliseconds timeout) noexcept;
void wake_events() noexcept;

} // namespace nk::core

namespace nk::backend {
void pump_events() noexcept;
void shutdown() noexcept;
} // namespace nk::backend
