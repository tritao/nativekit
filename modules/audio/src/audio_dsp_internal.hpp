#ifndef NATIVEKIT_AUDIO_DSP_INTERNAL_HPP
#define NATIVEKIT_AUDIO_DSP_INTERNAL_HPP

#include "nativekit_audio_dsp.h"

#include <cstdint>
#include <memory>

namespace nk::audio_dsp {

/* These functions bridge the standalone DSP renderer to NativeKit's device. */
nk_result attach_device(nk_audio_dsp_engine engine, uint64_t device_frame,
                        uint32_t sample_rate, uint32_t channels);
nk_result require_attached(nk_audio_dsp_engine engine);
/** Private typed source boundary; retained by the graph node for callback lifetime. */
class DeviceOutput {
  public:
    virtual ~DeviceOutput() = default;
    virtual void process(float *frames_out, uint64_t frame_count, uint32_t sample_rate,
                         uint32_t channels, uint64_t start_frame) noexcept = 0;
    virtual void detach() noexcept = 0;
};
std::shared_ptr<DeviceOutput> device_output(nk_audio_dsp_engine engine);

} // namespace nk::audio_dsp

#endif /* NATIVEKIT_AUDIO_DSP_INTERNAL_HPP */
