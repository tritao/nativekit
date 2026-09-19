#include "nativekit_graphics.h"

#include "core/boundary.hpp"
#include "core/executor.hpp"
#include "core/error.hpp"
#include "core/frame_backend.hpp"
#include "core/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

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
std::unordered_map<nk_surface_frame, nk::core::FrameTicket> ticket_by_frame;
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
        ticket_by_frame.erase(entry->second);
        entry = frame_by_surface.erase(entry);
    }
}

nk_result bind_frame_ticket(const FrameTicket &ticket) noexcept {
    return nk_graphics_bind_frame_target(&ticket.target);
}

nk_result submit_frame_ticket(const FrameTicket &ticket) noexcept {
    return nk_frame_backend_submit(&ticket.target);
}

nk_result finish_frame_ticket(const FrameTicket &ticket) noexcept {
    return nk_frame_backend_finish(ticket.surface, &ticket.target);
}

nk_result cancel_frame_ticket(const FrameTicket &ticket) noexcept {
    return nk_frame_backend_finish(ticket.surface, &ticket.target);
}

} // namespace

FrameBackend frame_backend_for_target(const nk_surface_frame_target &) noexcept {
    return {&bind_frame_ticket, &submit_frame_ticket, &finish_frame_ticket, &cancel_frame_ticket};
}

bool lookup_frame_ticket(nk_surface_frame frame, FrameTicket *out_ticket) noexcept {
    if (!out_ticket)
        return false;
    std::lock_guard lock(frame_mutex);
    const auto entry = ticket_by_frame.find(frame);
    if (entry == ticket_by_frame.end())
        return false;
    *out_ticket = entry->second;
    return true;
}

bool mark_frame_render_submitted(nk_surface_frame frame) noexcept {
    std::lock_guard lock(frame_mutex);
    const auto entry = ticket_by_frame.find(frame);
    if (entry == ticket_by_frame.end())
        return false;
    entry->second.render_submitted = true;
    return true;
}

bool take_frame_ticket(nk_surface_frame frame, FrameTicket *out_ticket) noexcept {
    if (!out_ticket)
        return false;
    std::lock_guard lock(frame_mutex);
    const auto ticket = ticket_by_frame.find(frame);
    if (ticket == ticket_by_frame.end())
        return false;
    *out_ticket = ticket->second;
    ticket_by_frame.erase(ticket);
    const auto surface_entry = surface_by_frame.find(frame);
    const nk_surface surface =
        surface_entry == surface_by_frame.end() ? ticket->second.surface : surface_entry->second;
    if (surface_entry != surface_by_frame.end())
        surface_by_frame.erase(surface_entry);
    const auto owner = frame_by_surface.find(surface);
    if (owner != frame_by_surface.end() && owner->second == frame)
        frame_by_surface.erase(owner);
    return true;
}

std::size_t frame_ticket_count() noexcept {
    std::lock_guard lock(frame_mutex);
    return ticket_by_frame.size();
}

void clear_frame_tickets() noexcept {
    std::vector<FrameTicket> tickets;
    {
        std::lock_guard lock(frame_mutex);
        tickets.reserve(ticket_by_frame.size());
        for (const auto &[frame, ticket] : ticket_by_frame) {
            (void)frame;
            tickets.push_back(ticket);
        }
        frame_by_surface.clear();
        surface_by_frame.clear();
        ticket_by_frame.clear();
    }

    /*
     * nk_shutdown() calls this after the render executor has joined but
     * before a platform backend tears down its surfaces.  A render task may
     * have been discarded by the executor, or its completion may still be
     * queued on APP, so the normal present/cancel path cannot be relied on to
     * close the backend frame.  Physical backends use finish for both cases:
     * it releases the acquired drawable/context and applies deferred resize
     * work without presenting an incomplete frame.
     */
    if (!render_executor_physical() || !executor_satisfies(NK_EXECUTOR_PLATFORM))
        return;
    for (const auto &ticket : tickets) {
        if (ticket.backend.finish)
            (void)ticket.backend.finish(ticket);
        else
            (void)nk_frame_backend_finish(ticket.surface, &ticket.target);
    }
}

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
            if (nk::core::render_executor_physical()) {
                const nk_result unbound = nk_graphics_unbind_frame_target(out_target);
                if (unbound != NK_OK)
                    return unbound;
            }

            const auto token = nk::core::next_frame_token();
            out_target->frame = token;
            nk::core::FrameTicket ticket{};
            ticket.surface = surface;
            ticket.frame = token;
            ticket.target = *out_target;
            ticket.binding = nk::core::BackendRenderBinding::from_target(*out_target);
            ticket.backend = nk::core::frame_backend_for_target(*out_target);
            {
                std::lock_guard lock(nk::core::frame_mutex);
                const auto inserted = nk::core::frame_by_surface.emplace(surface, token);
                if (!inserted.second) {
                    nk::core::set_error("this surface already has an open frame");
                    return NK_ERROR_INVALID_REQUEST;
                }
                nk::core::surface_by_frame.emplace(token, surface);
                nk::core::ticket_by_frame.emplace(token, ticket);
            }
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
            nk::core::FrameTicket ticket{};
            if (!nk::core::take_frame_ticket(frame, &ticket)) {
                nk::core::set_error("the frame token is not open on this runtime");
                return NK_ERROR_INVALID_HANDLE;
            }
            if (ticket.render_submitted) {
                if (!ticket.backend.finish) {
                    nk::core::set_error("the frame ticket has no finish operation");
                    return NK_ERROR_UNKNOWN;
                }
                return ticket.backend.finish(ticket);
            }
            return nk_surface_present(ticket.surface);
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
            nk::core::FrameTicket ticket{};
            if (!nk::core::take_frame_ticket(frame, &ticket)) {
                nk::core::set_error("the frame token is not open on this runtime");
                return NK_ERROR_INVALID_HANDLE;
            }
            if (nk::core::render_executor_physical() && ticket.backend.cancel)
                return ticket.backend.cancel(ticket);
            return NK_OK;
        });
}

} // extern "C"
