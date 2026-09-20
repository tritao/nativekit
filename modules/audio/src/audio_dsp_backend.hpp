#pragma once

#include "nativekit_audio_dsp.h"

#include <cstdint>
#include <memory>

namespace nk::audio_dsp {

struct VoiceParameters {
    nk_audio_dsp_waveform waveform = NK_AUDIO_DSP_WAVEFORM_SINE;
    float gain = 1.0f;
    float attack_seconds = 0.01f;
    float decay_seconds = 0.1f;
    float sustain_level = 0.8f;
    float release_seconds = 0.1f;
};

/** Private backend-neutral voice boundary; DaisySP types stay in the .cpp. */
class Voice final {
  public:
    Voice();
    ~Voice();
    Voice(Voice &&) noexcept;
    Voice &operator=(Voice &&) noexcept;
    Voice(const Voice &) = delete;
    Voice &operator=(const Voice &) = delete;

    void init(uint32_t sample_rate) noexcept;
    void set_parameters(const VoiceParameters &parameters) noexcept;
    void note_on(uint32_t note, float velocity) noexcept;
    void note_off() noexcept;
    void reset() noexcept;
    float process() noexcept;
    bool active() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk::audio_dsp
