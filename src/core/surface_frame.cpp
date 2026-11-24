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
std::uint32_t next_step_serial = 0;
std::unordered_map<nk_surface_frame_step, FrameStep> frame_steps;
constexpr uint32_t max_frame_steps = 16;

struct FrameSubmission {
    FrameTicket ticket;
    std::vector<FrameStep> steps;
    nk_result result = NK_OK;
    bool finished = false;
    std::unique_ptr<FrameSubmission> next;
};

std::unique_ptr<FrameSubmission> completed_frames;
FrameSubmission *completed_tail = nullptr;

void enqueue_completion(std::unique_ptr<FrameSubmission> submission) noexcept {
    auto *tail = submission.get();
    {
        std::lock_guard lock(frame_mutex);
        if (completed_tail)
            completed_tail->next = std::move(submission);
        else
            completed_frames = std::move(submission);
        completed_tail = tail;
    }
    wake_events();
}

void execute_steps(FrameSubmission &submission) noexcept {
    const auto &ticket = submission.ticket;
    FrameTicket latest{};
    if (lookup_frame_ticket(ticket.frame, &latest) && latest.surface_lost) {
        submission.result = NK_ERROR_SURFACE_LOST;
        return;
    }
    if (!ticket.backend.bind) {
        submission.result = NK_ERROR_UNSUPPORTED;
        return;
    }
    submission.result = ticket.backend.bind(ticket);
    if (submission.result != NK_OK)
        return;
    for (const auto &step : submission.steps) {
        submission.result = result_boundary("surface frame step failed", [&] {
            return step.execute(step.payload.get(), ticket);
        });
        if (submission.result != NK_OK) {
            FrameTicket current{};
            if (lookup_frame_ticket(ticket.frame, &current) && current.surface_lost)
                submission.result = NK_ERROR_SURFACE_LOST;
            break;
        }
    }
    if (render_executor_physical() &&
        (ticket.target.api == NK_GRAPHICS_OPENGL || ticket.target.api == NK_GRAPHICS_OPENGL_ES))
        (void)nk_graphics_unbind_frame_target(&ticket.target);
}

void NK_CALL run_frame_submission(void *data) {
    auto &work = *static_cast<std::unique_ptr<FrameSubmission> *>(data);
    execute_steps(*work);
    enqueue_completion(std::move(work));
}

void destroy_frame_submission(void *data) noexcept {
    std::unique_ptr<std::unique_ptr<FrameSubmission>> task(
        static_cast<std::unique_ptr<FrameSubmission> *>(data));
    if (*task) {
        (*task)->result = NK_ERROR_CANCELLED;
        enqueue_completion(std::move(*task));
    }
}


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

void mark_frame_surface_lost(nk_surface_frame frame) noexcept {
    std::lock_guard lock(frame_mutex);
    const auto entry = ticket_by_frame.find(frame);
    if (entry != ticket_by_frame.end())
        entry->second.surface_lost = true;
}

bool take_frame_ticket(nk_surface_frame frame, FrameTicket *out_ticket, bool queue_owned) noexcept {
    if (!out_ticket)
        return false;
    std::lock_guard lock(frame_mutex);
    const auto ticket = ticket_by_frame.find(frame);
    if (ticket == ticket_by_frame.end() || ticket->second.queue_owned != queue_owned)
        return false;
    *out_ticket = ticket->second;
    ticket_by_frame.erase(ticket);
    const auto surface_entry = surface_by_frame.find(frame);
    const nk_surface surface =
        surface_entry == surface_by_frame.end() ? out_ticket->surface : surface_entry->second;
    if (surface_entry != surface_by_frame.end())
        surface_by_frame.erase(surface_entry);
    const auto owner = frame_by_surface.find(surface);
    if (owner != frame_by_surface.end() && owner->second == frame)
        frame_by_surface.erase(owner);
    return true;
}

nk_result register_frame_step(FrameStep step, nk_surface_frame_step *out_step) {
    return result_boundary("could not register surface frame step", [&]() -> nk_result {
        if (const auto affinity = require_executor(NK_EXECUTOR_PLATFORM); affinity != NK_OK)
            return affinity;
        if (!out_step || !step.payload || !step.execute)
            return NK_ERROR_INVALID_ARGUMENT;
        std::lock_guard lock(frame_mutex);
        do {
            ++next_step_serial;
        } while (!next_step_serial || frame_steps.count(next_step_serial));
        frame_steps.emplace(next_step_serial, std::move(step));
        *out_step = next_step_serial;
        return NK_OK;
    });
}

static nk_result finish_queued_frame(const FrameSubmission &submission) noexcept {
    FrameTicket ticket{};
    if (!take_frame_ticket(submission.ticket.frame, &ticket, true))
        return NK_ERROR_INVALID_HANDLE;
    if (ticket.surface_lost || submission.result != NK_OK) {
        if (render_executor_physical() && ticket.backend.cancel)
            (void)ticket.backend.cancel(ticket);
        return ticket.surface_lost ? NK_ERROR_SURFACE_LOST : submission.result;
    }
    return ticket.render_submitted && ticket.backend.finish ? ticket.backend.finish(ticket)
                                                            : nk_surface_present(ticket.surface);
}

static void complete_frame_steps(FrameSubmission &submission) noexcept {
    for (const auto &step : submission.steps)
        if (step.complete)
            step.complete(step.payload.get(), submission.result);
}

void mark_surface_frames_lost(nk_surface surface) noexcept {
    std::lock_guard lock(frame_mutex);
    const auto frame = frame_by_surface.find(surface);
    if (frame == frame_by_surface.end())
        return;
    const auto ticket = ticket_by_frame.find(frame->second);
    if (ticket != ticket_by_frame.end())
        ticket->second.surface_lost = true;
}

bool frame_completions_pending() noexcept {
    std::lock_guard lock(frame_mutex);
    return completed_frames != nullptr;
}

static bool standalone(const FrameSubmission &submission) noexcept {
    return submission.steps.size() == 1 && submission.steps.front().complete;
}

void drain_standalone_frame_completions() noexcept {
    for (;;) {
        std::unique_ptr<FrameSubmission> submission;
        {
            std::lock_guard lock(frame_mutex);
            auto *link = &completed_frames;
            FrameSubmission *previous = nullptr;
            while (*link && !standalone(**link)) {
                previous = link->get();
                link = &previous->next;
            }
            if (!*link)
                return;
            submission = std::move(*link);
            *link = std::move(submission->next);
            if (!*link)
                completed_tail = previous;
        }
        if (!submission->finished) {
            submission->result = finish_queued_frame(*submission);
            complete_frame_steps(*submission);
        }
    }
}

bool poll_frame_completion(nk_event &event) noexcept {
    for (;;) {
        std::unique_ptr<FrameSubmission> submission;
        {
            std::lock_guard lock(frame_mutex);
            if (!completed_frames)
                return false;
            submission = std::move(completed_frames);
            completed_frames = std::move(submission->next);
            if (!completed_frames)
                completed_tail = nullptr;
        }
        if (!submission->finished) {
            submission->result = finish_queued_frame(*submission);
            complete_frame_steps(*submission);
        }
        if (standalone(*submission))
            continue;
        event = {};
        event.struct_size = sizeof(event);
        event.kind = NK_EVENT_SURFACE_FRAME_COMPLETE;
        event.source = submission->ticket.surface;
        event.request_id = submission->ticket.frame;
        event.result = submission->result;
        return true;
    }
}

std::size_t frame_ticket_count() noexcept {
    std::lock_guard lock(frame_mutex);
    return ticket_by_frame.size();
}

bool surface_has_open_frame(nk_surface surface) noexcept {
    std::lock_guard lock(frame_mutex);
    return frame_by_surface.count(surface) != 0;
}

void clear_frame_tickets() noexcept {
    drain_standalone_frame_completions();
    std::vector<FrameTicket> tickets;
    std::unordered_map<nk_surface_frame_step, FrameStep> discarded_steps;
    std::unique_ptr<FrameSubmission> discarded_completions;
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
        discarded_steps.swap(frame_steps);
        discarded_completions = std::move(completed_frames);
        completed_tail = nullptr;
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

nk_result NK_CALL nk_surface_frame_submit(nk_surface_frame frame,
                                           const nk_surface_frame_step *steps, uint32_t count) {
    using namespace nk::core;
    return result_boundary("could not submit surface frame", [&]() -> nk_result {
        if (const auto affinity = require_executor(NK_EXECUTOR_PLATFORM); affinity != NK_OK)
            return affinity;
        if (!steps || !count || count > max_frame_steps)
            return NK_ERROR_INVALID_ARGUMENT;
        auto submission = std::make_unique<FrameSubmission>();
        submission->steps.reserve(count);
        {
            std::lock_guard lock(frame_mutex);
            const auto ticket = ticket_by_frame.find(frame);
            if (ticket == ticket_by_frame.end() || ticket->second.queue_owned)
                return NK_ERROR_INVALID_HANDLE;
            submission->ticket = ticket->second;
            for (uint32_t i = 0; i < count; ++i) {
                for (uint32_t previous = 0; previous < i; ++previous)
                    if (steps[previous] == steps[i])
                        return NK_ERROR_INVALID_ARGUMENT;
                const auto step = frame_steps.find(steps[i]);
                if (step == frame_steps.end())
                    return NK_ERROR_INVALID_HANDLE;
                submission->steps.push_back(step->second);
            }
        }
        const bool threaded = render_executor_physical();
        auto task = threaded ? std::make_unique<std::unique_ptr<FrameSubmission>>() : nullptr;
        {
            std::lock_guard lock(frame_mutex);
            ticket_by_frame.at(frame).queue_owned = true;
        }
        if (threaded) {
            *task = std::move(submission);
            const auto queued = dispatch_to_render(run_frame_submission, task.get(),
                                                    destroy_frame_submission, sizeof(FrameSubmission));
            if (queued != NK_OK) {
                std::lock_guard lock(frame_mutex);
                ticket_by_frame.at(frame).queue_owned = false;
                return queued;
            }
            (void)task.release();
        }
        {
            std::lock_guard lock(frame_mutex);
            for (uint32_t i = 0; i < count; ++i)
                frame_steps.erase(steps[i]);
        }
        if (!threaded) {
            execute_steps(*submission);
            submission->result = finish_queued_frame(*submission);
            complete_frame_steps(*submission);
            submission->finished = true;
            enqueue_completion(std::move(submission));
        }
        return NK_OK;
    });
}

nk_result NK_CALL nk_surface_frame_step_destroy(nk_surface_frame_step step) {
    using namespace nk::core;
    if (const auto affinity = require_executor(NK_EXECUTOR_PLATFORM); affinity != NK_OK)
        return affinity;
    FrameStep removed;
    {
        std::lock_guard lock(frame_mutex);
        const auto entry = frame_steps.find(step);
        if (entry == frame_steps.end())
            return NK_ERROR_INVALID_HANDLE;
        removed = std::move(entry->second);
        frame_steps.erase(entry);
    }
    return NK_OK;
}

} // extern "C"
