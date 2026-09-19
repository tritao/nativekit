#pragma once

#include "nativekit_graphics.h"
#include "core/internal_api.hpp"

#include <cstdint>

namespace nk::core {

/**
 * Stable backend binding carried with an acquired frame ticket.
 *
 * The values are borrowed from the platform surface and are valid for the
 * lifetime of the ticket. Keeping them in a separate internal type makes it
 * explicit that RENDER receives a complete binding and must not rediscover
 * one through nk_surface_* calls.
 */
struct BackendRenderBinding {
    nk_graphics_api api = 0;
    nk_graphics_device device{};
    int32_t width = 0;
    int32_t height = 0;
    uint64_t native_target = 0;
    uint64_t native_device = 0;
    uint64_t native_context = 0;
    uint64_t native_depth_stencil_target = 0;
    uint64_t native_present_target = 0;

    static BackendRenderBinding from_target(const nk_surface_frame_target &target) noexcept {
        return {target.api,
                target.device,
                target.width,
                target.height,
                target.native_target,
                target.native_device,
                target.native_context,
                target.native_depth_stencil_target,
                target.native_present_target};
    }
};

struct FrameTicket;

/** Backend-specific operations used by a physical render executor. */
struct FrameBackend {
    using Operation = nk_result (*)(const FrameTicket &) noexcept;

    Operation bind = nullptr;
    Operation submit = nullptr;
    Operation finish = nullptr;
    Operation cancel = nullptr;
};

/**
 * The immutable handoff from PLATFORM to RENDER.
 *
 * `surface` and `frame` identify the lifecycle token owned by PLATFORM;
 * `binding` is the complete render-side snapshot. The target is retained as
 * well so callers that still speak the C ABI can pass it to nkgpu_* without
 * rebuilding a descriptor.
 */
struct FrameTicket {
    nk_surface surface = NK_INVALID_HANDLE;
    nk_surface_frame frame = NK_INVALID_HANDLE;
    nk_surface_frame_target target{};
    BackendRenderBinding binding{};
    FrameBackend backend{};
    bool render_submitted = false;
};

/** Copies an open platform-owned ticket for render-side scheduling. */
NK_INTERNAL_API bool lookup_frame_ticket(nk_surface_frame frame, FrameTicket *out_ticket) noexcept;
NK_INTERNAL_API FrameBackend
frame_backend_for_target(const nk_surface_frame_target &target) noexcept;
NK_INTERNAL_API bool mark_frame_render_submitted(nk_surface_frame frame) noexcept;
NK_INTERNAL_API bool take_frame_ticket(nk_surface_frame frame, FrameTicket *out_ticket) noexcept;
NK_INTERNAL_API void clear_frame_tickets() noexcept;

} // namespace nk::core

/* Backend hooks used by RENDER for APIs whose presentation is tied to submit. */
extern "C" {
/* Bind/unbind a backend context without rediscovering a surface on RENDER. */
NK_INTERNAL_API nk_result NK_CALL
nk_graphics_bind_frame_target(const nk_surface_frame_target *target);
NK_INTERNAL_API nk_result NK_CALL
nk_graphics_unbind_frame_target(const nk_surface_frame_target *target);
NK_INTERNAL_API nk_result NK_CALL nk_frame_backend_submit(const nk_surface_frame_target *target);
NK_INTERNAL_API nk_result NK_CALL nk_frame_backend_finish(nk_surface surface,
                                                          const nk_surface_frame_target *target);
}
