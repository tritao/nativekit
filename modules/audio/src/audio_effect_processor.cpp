#include "audio_effect_processor.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include "signalsmith-basics/reverb.h"
#include "signalsmith-basics/dynamics.h"

namespace nk::audio {
namespace {
constexpr uint32_t block_size = 256;
using Parameter = stfx::LibraryParam<double>;

// Stereo reverb is deliberately restricted to stereo buses. Dynamics supports
// every configured device channel. No resizing or allocation occurs in process.
template<class Effect> class Processor : public EffectProcessor {
  protected:
    Effect effect;
    const uint32_t rate, channels;
    std::vector<float> input_storage, output_storage;
    std::vector<float *> inputs, outputs;
    std::atomic<bool> reset_requested{false};
    virtual Parameter *parameter(nk_audio_effect_parameter kind) noexcept = 0;
    virtual bool range(nk_audio_effect_parameter kind, float value) const noexcept = 0;
    virtual float to_backend(nk_audio_effect_parameter, float value) const noexcept { return value; }
    virtual float from_backend(nk_audio_effect_parameter, float value) const noexcept { return value; }
  public:
    Processor(uint32_t sample_rate, uint32_t channel_count)
        : rate(sample_rate), channels(channel_count),
          input_storage(channels * block_size), output_storage(channels * block_size),
          inputs(channels), outputs(channels) {
        for (uint32_t c = 0; c < channels; ++c) {
            inputs[c] = input_storage.data() + c * block_size;
            outputs[c] = output_storage.data() + c * block_size;
        }
    }
    bool prepare() { return effect.configure(rate, block_size, channels); }
    void request_reset() noexcept override { reset_requested.store(true, std::memory_order_release); }
    bool set_parameter(nk_audio_effect_parameter kind, float value) noexcept override {
        if (!std::isfinite(value) || !range(kind, value)) return false;
        auto *target = parameter(kind);
        if (!target) return false;
        *target = to_backend(kind, value);
        return true;
    }
    bool get_parameter(nk_audio_effect_parameter kind, float &value) const noexcept override {
        // Lookup itself doesn't mutate the processor; LibraryParam reads atomically.
        auto *target = const_cast<Processor *>(this)->parameter(kind);
        if (!target) return false;
        value = from_backend(kind, static_cast<double>(*target));
        return true;
    }
    void process(const float *input, float *output, uint32_t frames) noexcept override {
        if (reset_requested.exchange(false, std::memory_order_acq_rel)) effect.reset();
        for (uint32_t offset = 0; offset < frames;) {
            const auto count = std::min(block_size, frames - offset);
            for (uint32_t c = 0; c < channels; ++c)
                for (uint32_t f = 0; f < count; ++f)
                    inputs[c][f] = input ? input[(offset + f) * channels + c] : 0.0f;
            effect.process(inputs.data(), outputs.data(), count);
            for (uint32_t c = 0; c < channels; ++c)
                for (uint32_t f = 0; f < count; ++f)
                    output[(offset + f) * channels + c] = outputs[c][f];
            offset += count;
        }
    }
    uint32_t latency_frames() const noexcept override { return 0; }
    uint32_t tail_frames() const noexcept override { return 0; }
};

class Reverb final : public Processor<signalsmith::basics::ReverbFloat> {
    Parameter *parameter(nk_audio_effect_parameter kind) noexcept override {
        switch (kind) {
        case NK_AUDIO_EFFECT_PARAMETER_WET: return &effect.wet;
        case NK_AUDIO_EFFECT_PARAMETER_DRY: return &effect.dry;
        case NK_AUDIO_EFFECT_PARAMETER_ROOM_MS: return &effect.roomMs;
        case NK_AUDIO_EFFECT_PARAMETER_DECAY_SECONDS: return &effect.rt20;
        case NK_AUDIO_EFFECT_PARAMETER_LOW_CUT_HZ: return &effect.lowCutHz;
        case NK_AUDIO_EFFECT_PARAMETER_HIGH_CUT_HZ: return &effect.highCutHz;
        case NK_AUDIO_EFFECT_PARAMETER_LOW_DAMPING: return &effect.lowDampRate;
        case NK_AUDIO_EFFECT_PARAMETER_HIGH_DAMPING: return &effect.highDampRate;
        case NK_AUDIO_EFFECT_PARAMETER_EARLY_REFLECTIONS: return &effect.early;
        default: return nullptr;
        }
    }
    bool range(nk_audio_effect_parameter kind, float v) const noexcept override {
        switch (kind) {
        case NK_AUDIO_EFFECT_PARAMETER_WET:
        case NK_AUDIO_EFFECT_PARAMETER_DRY: return v >= 0 && v <= 1;
        case NK_AUDIO_EFFECT_PARAMETER_ROOM_MS: return v >= 10 && v <= 200;
        case NK_AUDIO_EFFECT_PARAMETER_DECAY_SECONDS: return v >= 0.03f && v <= 90;
        case NK_AUDIO_EFFECT_PARAMETER_LOW_CUT_HZ:
        case NK_AUDIO_EFFECT_PARAMETER_HIGH_CUT_HZ: return v >= 10 && v < rate * 0.5f;
        case NK_AUDIO_EFFECT_PARAMETER_LOW_DAMPING:
        case NK_AUDIO_EFFECT_PARAMETER_HIGH_DAMPING: return v >= 1 && v <= 10;
        case NK_AUDIO_EFFECT_PARAMETER_EARLY_REFLECTIONS: return v >= 0 && v <= 2.5f;
        default: return false;
        }
    }
    float to_backend(nk_audio_effect_parameter kind, float v) const noexcept override {
        return kind == NK_AUDIO_EFFECT_PARAMETER_DECAY_SECONDS ? v / 3 : v;
    }
    float from_backend(nk_audio_effect_parameter kind, float v) const noexcept override {
        return kind == NK_AUDIO_EFFECT_PARAMETER_DECAY_SECONDS ? v * 3 : v;
    }
  public:
    Reverb(uint32_t rate, uint32_t channels) : Processor(rate, channels) {
        effect.lowCutHz = std::min(80.0, rate * 0.4);
        effect.highCutHz = std::min(12000.0, rate * 0.4);
    }
    uint32_t tail_frames() const noexcept override {
        return static_cast<uint32_t>(std::ceil(rate * static_cast<double>(effect.rt20) * 3));
    }
};

class Dynamics final : public Processor<signalsmith::basics::DynamicsFloat> {
    Parameter *parameter(nk_audio_effect_parameter kind) noexcept override {
        switch (kind) {
        case NK_AUDIO_EFFECT_PARAMETER_THRESHOLD_DB: return &effect.compressor.limitDb;
        case NK_AUDIO_EFFECT_PARAMETER_RATIO: return &effect.compressor.invRatio;
        case NK_AUDIO_EFFECT_PARAMETER_ATTACK_MS: return &effect.timing.attackMs;
        case NK_AUDIO_EFFECT_PARAMETER_RELEASE_MS: return &effect.timing.releaseMs;
        case NK_AUDIO_EFFECT_PARAMETER_MAKEUP_DB: return &effect.compressor.makeupDb;
        case NK_AUDIO_EFFECT_PARAMETER_GATE_THRESHOLD_DB: return &effect.gate.limitDb;
        case NK_AUDIO_EFFECT_PARAMETER_EXPANDER_THRESHOLD_DB: return &effect.expander.limitDb;
        case NK_AUDIO_EFFECT_PARAMETER_EXPANDER_RATIO: return &effect.expander.ratio;
        case NK_AUDIO_EFFECT_PARAMETER_MIX: return &effect.mix;
        default: return nullptr;
        }
    }
    bool range(nk_audio_effect_parameter kind, float v) const noexcept override {
        switch (kind) {
        case NK_AUDIO_EFFECT_PARAMETER_THRESHOLD_DB:
        case NK_AUDIO_EFFECT_PARAMETER_EXPANDER_THRESHOLD_DB: return v >= -60 && v <= 0;
        case NK_AUDIO_EFFECT_PARAMETER_GATE_THRESHOLD_DB: return v >= -80 && v <= 0;
        case NK_AUDIO_EFFECT_PARAMETER_RATIO: return v >= 1 && v <= 100;
        case NK_AUDIO_EFFECT_PARAMETER_ATTACK_MS: return v >= 1 && v <= 50;
        case NK_AUDIO_EFFECT_PARAMETER_RELEASE_MS: return v >= 20 && v <= 250;
        case NK_AUDIO_EFFECT_PARAMETER_MAKEUP_DB: return v >= 0 && v <= 20;
        case NK_AUDIO_EFFECT_PARAMETER_EXPANDER_RATIO: return v >= 1 && v <= 10;
        case NK_AUDIO_EFFECT_PARAMETER_MIX: return v >= 0 && v <= 1;
        default: return false;
        }
    }
    float to_backend(nk_audio_effect_parameter kind, float v) const noexcept override {
        return kind == NK_AUDIO_EFFECT_PARAMETER_RATIO ? 1 / v : v;
    }
    float from_backend(nk_audio_effect_parameter kind, float v) const noexcept override {
        return kind == NK_AUDIO_EFFECT_PARAMETER_RATIO ? 1 / v : v;
    }
  public:
    Dynamics(uint32_t rate, uint32_t channels) : Processor(rate, channels) {
        effect.compressor.invRatio = 1;
        effect.compressor.autoGain = 0;
    }
};
} // namespace
std::unique_ptr<EffectProcessor> create_effect_processor(nk_audio_effect_type type,
                                                       uint32_t rate, uint32_t channels) {
    if (rate < 8000 || rate > 384000 || channels == 0 || channels > 32) return {};
    if (type == NK_AUDIO_EFFECT_REVERB && channels == 2) {
        auto result = std::make_unique<Reverb>(rate, channels);
        if (result->prepare()) return result;
    } else if (type == NK_AUDIO_EFFECT_DYNAMICS) {
        auto result = std::make_unique<Dynamics>(rate, channels);
        if (result->prepare()) return result;
    }
    return {};
}
} // namespace nk::audio
