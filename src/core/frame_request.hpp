#pragma once

namespace nk::core {

/**
 * Coalescing frame-request state shared by platform graphics surfaces.
 *
 * Continuous surfaces draw whenever the backend schedules a frame; on-demand
 * surfaces draw only while a request is pending. Requests recorded while a
 * frame renders are not lost: begin_frame() consumes exactly the requests that
 * scheduled the frame, so anything recorded afterwards schedules the next one.
 */
class FrameRequestState {
  public:
    /** True when the backend schedules frames without a request. */
    bool continuous() const noexcept { return continuous_; }

    /** Selects continuous or request-driven scheduling. */
    void set_continuous(bool continuous) noexcept { continuous_ = continuous; }

    /** True while at least one request awaits the next frame. */
    bool pending() const noexcept { return pending_; }

    /** True when the backend must run the frame callback for this frame. */
    bool should_draw() const noexcept { return continuous_ || pending_; }

    /** Records one frame request; repeated calls coalesce into one frame. */
    void request() noexcept { pending_ = true; }

    /** Consumes the requests that scheduled the frame that is starting. */
    void begin_frame() noexcept { pending_ = false; }

  private:
    bool pending_ = false;
    bool continuous_ = true;
};

} // namespace nk::core
