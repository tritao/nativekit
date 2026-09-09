#pragma once

#include "core/event_queue.hpp"
#include "core/handle_registry.hpp"

namespace nk::core {

nk_result require_ui_thread() noexcept;
HandleRegistry& handles() noexcept;
nk_result push_event(QueuedEvent event) noexcept;

}

namespace nk::backend {
void pump_events() noexcept;
void shutdown() noexcept;
}

