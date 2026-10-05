#ifndef NATIVEKIT_AUDIO_DSP_INTERNAL_HPP
#define NATIVEKIT_AUDIO_DSP_INTERNAL_HPP

#include "nativekit_audio_dsp.h"

#include <cstdint>

namespace nk::audio_dsp {

/* These functions bridge the standalone DSP renderer to NativeKit's device. */
nk_result attach_device(nk_audio_dsp_engine engine, uint64_t device_frame,
                        uint32_t sample_rate, uint32_t channels);
nk_result detach_device(nk_audio_dsp_engine engine);
void detach_device() noexcept;
void process_device_output(float *frames_out, uint64_t frame_count, uint32_t sample_rate,
                           uint32_t channels) noexcept;

} // namespace nk::audio_dsp

#endif /* NATIVEKIT_AUDIO_DSP_INTERNAL_HPP */
