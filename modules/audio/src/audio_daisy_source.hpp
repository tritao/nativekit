#pragma once
#include "audio_dsp_backend.hpp"
#include <memory>
namespace nk::audio_dsp {
class DaisySource final {
  public:
    DaisySource();
    ~DaisySource();
    void configure(const SourceParameters &parameters, uint32_t sample_rate) noexcept;
    void trigger(float frequency, uint32_t note) noexcept;
    float process(float frequency, float excitation) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nk::audio_dsp
