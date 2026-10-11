#pragma once
#include "audio_dsp_backend.hpp"
namespace nk::audio_dsp {
/** Prepared instrument renderer. Slots and sample storage are allocated before publication.
 * Slot operations belong exclusively to the owning engine's render thread.
 * The engine pins the owner until release() drops every voice reference.
 */
class InstrumentSource {
public:
    virtual ~InstrumentSource() = default;
    virtual int acquire(uint32_t note, float velocity) noexcept = 0;
    virtual void release(int slot) noexcept = 0;
    virtual void note_off(int slot) noexcept = 0;
    virtual StereoSample process(int slot) noexcept = 0;
    virtual bool active(int slot) const noexcept = 0;
};
std::shared_ptr<InstrumentSource> prepare_sample_source(
    const nk_audio_dsp_sample_options &options, const float *samples,
    uint32_t count, uint32_t voices, uint32_t output_rate);
}
