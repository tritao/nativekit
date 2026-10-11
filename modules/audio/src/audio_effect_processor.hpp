#pragma once
#include "nativekit_audio_graph.h"
#include "audio_transport_clock.hpp"
#include <cstdint>
#include <memory>

namespace nk::audio {
/** Backend-neutral block processor. Prepare/allocate on the UI thread only.
 * process() is audio-thread-only; parameters/reset requests may be written
 * on the UI thread. Callers keep processing silence to preserve tails. */
class EffectProcessor {
  public:
    virtual ~EffectProcessor() = default;
    virtual void process(const float *input, float *output, uint32_t frames) noexcept = 0;
    virtual void process_sidechain(const float *input, const float *, float *output, uint32_t frames) noexcept { process(input,output,frames); }
    virtual float duck_gain() const noexcept { return 1; }
    virtual bool set_parameter(nk_audio_effect_parameter parameter, float value) noexcept = 0;
    virtual bool get_parameter(nk_audio_effect_parameter parameter, float &value) const noexcept = 0;
    virtual bool set_tempo(float, float) noexcept { return false; }
    virtual bool bind_tempo(std::shared_ptr<const nk::audio_dsp::TransportClock>, float) { return false; }
    virtual void request_reset() noexcept = 0;
    virtual uint32_t latency_frames() const noexcept = 0;
    virtual uint32_t tail_frames() const noexcept = 0;
};
std::unique_ptr<EffectProcessor> create_stereo_delay_processor(uint32_t sample_rate, uint32_t channels);
std::unique_ptr<EffectProcessor> create_effect_processor(nk_audio_effect_type type,
                                                       uint32_t sample_rate, uint32_t channels);
} // namespace nk::audio
