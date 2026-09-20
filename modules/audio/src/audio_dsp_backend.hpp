#pragma once

#include "nativekit_audio_dsp.h"

#include <array>
#include <cstdint>
#include <memory>

namespace nk::audio_dsp {

struct OscillatorParameters {
    nk_audio_dsp_waveform waveform = NK_AUDIO_DSP_WAVEFORM_SINE;
    float level = 1.0f;
};

struct NoiseParameters {
    float level = 0.0f;
};

struct EnvelopeParameters {
    float attack_seconds = 0.01f;
    float decay_seconds = 0.1f;
    float sustain_level = 0.8f;
    float release_seconds = 0.1f;
};

struct FilterParameters {
    nk_audio_dsp_filter_type type = NK_AUDIO_DSP_FILTER_NONE;
    float cutoff_hz = 0.0f;
    float resonance = 0.0f;
};

struct LfoParameters {
    nk_audio_dsp_waveform waveform = NK_AUDIO_DSP_WAVEFORM_SINE;
    nk_audio_dsp_lfo_mode mode = NK_AUDIO_DSP_LFO_RETRIGGER;
    float rate_hz = 0.0f;
    float phase = 0.0f;
};

struct ModulationRoute {
    nk_audio_dsp_modulation_source source = NK_AUDIO_DSP_MODULATION_SOURCE_LFO;
    nk_audio_dsp_modulation_destination destination =
        NK_AUDIO_DSP_MODULATION_DESTINATION_PITCH_SEMITONES;
    nk_audio_dsp_modulation_polarity polarity = NK_AUDIO_DSP_MODULATION_BIPOLAR;
    float amount = 0.0f;
};

struct PatchParameters {
    OscillatorParameters oscillator;
    NoiseParameters noise;
    EnvelopeParameters envelope;
    FilterParameters filter;
    float gain = 1.0f;
    LfoParameters lfo;
    std::array<ModulationRoute, NK_AUDIO_DSP_MAX_MODULATION_ROUTES> routes{};
    uint32_t route_count = 0;
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
    void set_parameters(const PatchParameters &parameters) noexcept;
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
