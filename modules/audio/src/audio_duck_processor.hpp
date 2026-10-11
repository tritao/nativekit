#pragma once
#include "audio_effect_processor.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace nk::audio {
/** Audio-thread detector and gain state; control-thread targets are atomic.
 * Key audio is measured after its bus fader and never mixed into the output.
 */
class DuckProcessor final : public EffectProcessor {
    uint32_t rate, channels, held_frames = 0;
    float gain = 1, held_amount = 0;
    std::array<std::atomic<float>, 6> targets{{-30, 9, 12, 350, 120, 1}};
    std::atomic<bool> reset{false};
    std::atomic<float> published_gain{1};
    static int index(nk_audio_effect_parameter p) noexcept {
        switch (p) {
        case NK_AUDIO_EFFECT_PARAMETER_THRESHOLD_DB: return 0;
        case NK_AUDIO_EFFECT_PARAMETER_DUCK_DEPTH_DB: return 1;
        case NK_AUDIO_EFFECT_PARAMETER_ATTACK_MS: return 2;
        case NK_AUDIO_EFFECT_PARAMETER_RELEASE_MS: return 3;
        case NK_AUDIO_EFFECT_PARAMETER_DUCK_HOLD_MS: return 4;
        case NK_AUDIO_EFFECT_PARAMETER_MIX: return 5;
        default: return -1;
        }
    }
  public:
    DuckProcessor(uint32_t rate, uint32_t channels) : rate(rate), channels(channels) {}
    bool set_parameter(nk_audio_effect_parameter p, float v) noexcept override {
        const int i = index(p);
        if (i < 0 || !std::isfinite(v)) return false;
        const float lo[] = {-60, 0, 1, 10, 0, 0}, hi[] = {0, 24, 500, 5000, 2000, 1};
        if (v < lo[i] || v > hi[i]) return false;
        targets[i].store(v, std::memory_order_relaxed); return true;
    }
    bool get_parameter(nk_audio_effect_parameter p, float &v) const noexcept override {
        const int i = index(p); if (i < 0) return false;
        v = targets[i].load(std::memory_order_relaxed); return true;
    }
    void request_reset() noexcept override { reset.store(true, std::memory_order_release); }
    uint32_t latency_frames() const noexcept override { return 0; }
    uint32_t tail_frames() const noexcept override { return 0; }
    float duck_gain() const noexcept override { return published_gain.load(std::memory_order_relaxed); }
    void process(const float *input, float *output, uint32_t frames) noexcept override {
        process_sidechain(input, nullptr, output, frames);
    }
    void process_sidechain(const float *input, const float *key, float *output, uint32_t frames) noexcept override {
        // Reset detector only: gain returns smoothly, never jumps on a live reset.
        if (reset.exchange(false, std::memory_order_acq_rel)) { held_frames = 0; held_amount = 0; }
        const float threshold = std::pow(10.f, targets[0].load(std::memory_order_relaxed)/20.f);
        const float depth = targets[1].load(std::memory_order_relaxed);
        const float attack = std::exp(-1.f/(rate*targets[2].load(std::memory_order_relaxed)*0.001f));
        const float release = std::exp(-1.f/(rate*targets[3].load(std::memory_order_relaxed)*0.001f));
        const uint32_t hold = static_cast<uint32_t>(rate*targets[4].load(std::memory_order_relaxed)*0.001f);
        const float mix = targets[5].load(std::memory_order_relaxed);
        for (uint32_t f=0; f<frames; ++f) {
            float peak = 0;
            if (key) for (uint32_t c=0;c<channels;++c) {
                float sample = key[f*channels+c];
                if (std::isfinite(sample)) peak=std::max(peak,std::abs(sample));
            }
            // 12 dB above threshold reaches full depth; hold bridges waveform zero crossings.
            const float amount = std::clamp((peak/threshold-1.f)/3.f,0.f,1.f);
            if (amount >= held_amount && amount > 0) { held_amount=amount; held_frames=hold; }
            else if (held_frames) --held_frames;
            else held_amount=amount;
            const float wanted = std::pow(10.f,-depth*held_amount*mix/20.f);
            const float coefficient = wanted < gain ? attack : release;
            gain = wanted+(gain-wanted)*coefficient;
            for (uint32_t c=0;c<channels;++c)
                output[f*channels+c] = input ? input[f*channels+c]*gain : 0;
        }
        published_gain.store(gain,std::memory_order_relaxed);
    }
};
} // namespace nk::audio
