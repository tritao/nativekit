#pragma once

#include "core/boundary.hpp"
#include "core/trace.hpp"

#include <utility>

namespace nk::core {

/**
 * Runs one surface frame on PLATFORM: `draw` calls the application's frame callback, with whatever
 * the backend binds before it or presents after it. Backends call this only once they know they
 * will draw, so the trace shows one `nk.frame` zone per drawn frame, whatever triggered it.
 */
template <typename Draw> void run_surface_frame(Draw &&draw) noexcept {
    NK_TRACE_ZONE("nk.frame");
    callback_boundary(std::forward<Draw>(draw));
}

} // namespace nk::core
