#include "nativekit_graphics.h"

#include "core/boundary.hpp"
#include "core/executor.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace nk::core {
namespace {

/**
 * Backend-independent frame transaction bookkeeping.
 *
 * A token is a serial bound to one surface. Both directions are tracked so
 * nk_surface_present_frame() can find its surface from the token alone and
 * nk_surface_acquire_frame() can reject a second open frame. Entries are
 * dropped when a frame ends, and entries whose surface was destroyed are
 * pruned before the next acquire.
 */
std::mutex frame_mutex;
std::unordered_map<nk_surface, nk_surface_frame> frame_by_surface;
std::unordered_map<nk_surface_frame, nk_surface> surface_by_frame;
std::uint32_t next_frame_serial = 0;

constexpr uint32_t surface_frame_target_min_size =
    static_cast<uint32_t>(offsetof(nk_surface_frame_target, frame) + sizeof(nk_surface_frame));

nk_surface_frame next_frame_token() noexcept {
    ++next_frame_serial;
    if (next_frame_serial == 0)
        ++next_frame_serial;
    return static_cast<nk_surface_frame>(next_frame_serial);
}

bool surface_alive(nk_surface surface) {
    return handles().get(static_cast<nk_handle>(surface), ResourceType::surface) != nullptr;
}

/** Drops bookkeeping for surfaces that no longer exist; caller holds the lock. */
void prune_dead_surfaces() {
    for (auto entry = frame_by_surface.begin(); entry != frame_by_surface.end();) {
        if (surface_alive(entry->first)) {
            ++entry;
            continue;
        }
        surface_by_frame.erase(entry->second);
        entry = frame_by_surface.erase(entry);
    }
}

nk_surface_frame close_frame(nk_surface_frame frame) noexcept {
    std::lock_guard lock(frame_mutex);
    const auto entry = surface_by_frame.find(frame);
    if (entry == surface_by_frame.end())
        return NK_INVALID_HANDLE;
    const nk_surface surface = entry->second;
    surface_by_frame.erase(entry);
    const auto owner = frame_by_surface.find(surface);
    if (owner != frame_by_surface.end() && owner->second == frame)
        frame_by_surface.erase(owner);
    return surface;
}

} // namespace
} // namespace nk::core

extern "C" {

nk_result NK_CALL nk_surface_acquire_frame(nk_surface surface, nk_surface_frame *out_frame,
                                           nk_surface_frame_target *out_target) {
    return nk::core::result_boundary(
        "unexpected exception while acquiring a surface frame", [&]() -> nk_result {
            nk::core::clear_error();
            if (out_frame)
                *out_frame = NK_INVALID_HANDLE;
            /*
             * Frame acquisition belongs to the platform executor; rendering the
             * acquired frame may move to the render executor (ADR 0017).
             */
            if (const auto affinity = nk::core::require_executor(NK_EXECUTOR_PLATFORM);
                affinity != NK_OK)
                return affinity;
            if (surface == NK_INVALID_HANDLE || !out_frame || !out_target) {
                nk::core::set_error("nk_surface_acquire_frame needs a surface, a frame "
                                    "output, and a target output");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (out_target->struct_size < nk::core::surface_frame_target_min_size) {
                nk::core::set_error("nk_surface_frame_target is missing or too small");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            {
                std::lock_guard lock(nk::core::frame_mutex);
                nk::core::prune_dead_surfaces();
                if (nk::core::frame_by_surface.count(surface) != 0) {
                    nk::core::set_error("this surface already has an open frame");
                    return NK_ERROR_INVALID_REQUEST;
                }
            }

            const nk_result prepared = nk_surface_make_current(surface);
            if (prepared != NK_OK)
                return prepared;
            const nk_result targeted = nk_surface_get_frame_target(surface, out_target);
            if (targeted != NK_OK)
                return targeted;

            const auto token = nk::core::next_frame_token();
            {
                std::lock_guard lock(nk::core::frame_mutex);
                const auto inserted = nk::core::frame_by_surface.emplace(surface, token);
                if (!inserted.second) {
                    nk::core::set_error("this surface already has an open frame");
                    return NK_ERROR_INVALID_REQUEST;
                }
                nk::core::surface_by_frame.emplace(token, surface);
            }
            out_target->frame = token;
            *out_frame = token;
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_present_frame(nk_surface_frame frame) {
    return nk::core::result_boundary(
        "unexpected exception while presenting a surface frame", [&]() -> nk_result {
            nk::core::clear_error();
            if (frame == NK_INVALID_HANDLE) {
                nk::core::set_error("nk_surface_frame is not a valid frame token");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (const auto affinity = nk::core::require_executor(NK_EXECUTOR_PLATFORM);
                affinity != NK_OK)
                return affinity;
            const auto surface = nk::core::close_frame(frame);
            if (surface == NK_INVALID_HANDLE) {
                nk::core::set_error("the frame token is not open on this runtime");
                return NK_ERROR_INVALID_HANDLE;
            }
            return nk_surface_present(surface);
        });
}

nk_result NK_CALL nk_surface_cancel_frame(nk_surface_frame frame) {
    return nk::core::result_boundary(
        "unexpected exception while cancelling a surface frame", [&]() -> nk_result {
            nk::core::clear_error();
            if (frame == NK_INVALID_HANDLE) {
                nk::core::set_error("nk_surface_frame is not a valid frame token");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (const auto affinity = nk::core::require_executor(NK_EXECUTOR_PLATFORM);
                affinity != NK_OK)
                return affinity;
            if (nk::core::close_frame(frame) == NK_INVALID_HANDLE) {
                nk::core::set_error("the frame token is not open on this runtime");
                return NK_ERROR_INVALID_HANDLE;
            }
            return NK_OK;
        });
}

} // extern "C"
