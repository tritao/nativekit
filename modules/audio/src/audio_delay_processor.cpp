#include "audio_effect_processor.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace nk::audio {
namespace {
class StereoDelay final : public EffectProcessor {
    const uint32_t rate;
    const size_t capacity;
    std::vector<std::array<float, 2>> history;
    size_t position = 0;
    std::atomic<float> wet{0.3f}, dry{1}, feedback{0.35f}, ping_pong{0};
    std::atomic<float> bpm{120}, beats{0}, manual_seconds{0.25f}, delay_seconds{0.25f};
    std::atomic<bool> reset_requested{false};
    double from_delay, to_delay;
    uint32_t fade_position = 0;
    const uint32_t fade_frames;
    bool fading = false;
    float current_wet = 0.3f, current_dry = 1, current_feedback = 0.35f, current_ping = 0;

    std::array<float, 2> read(double delay) const noexcept {
        const auto whole = static_cast<size_t>(delay);
        const float fraction = static_cast<float>(delay - whole);
        const auto a = (position + capacity - whole) % capacity;
        const auto b = (a + capacity - 1) % capacity;
        return {history[a][0] + (history[b][0] - history[a][0]) * fraction,
                history[a][1] + (history[b][1] - history[a][1]) * fraction};
    }
    bool valid_time(float value) const noexcept {
        return std::isfinite(value) && value >= 1.0f / rate && value <= 4;
    }
  public:
    explicit StereoDelay(uint32_t sample_rate)
        : rate(sample_rate), capacity(static_cast<size_t>(rate) * 4 + 2), history(capacity),
          from_delay(rate * 0.25), to_delay(from_delay), fade_frames(std::max(1u, rate / 50)) {}
    bool set_tempo(float tempo, float length) noexcept override {
        if (!std::isfinite(tempo) || tempo < 20 || tempo > 300 ||
            !std::isfinite(length) || length <= 0 || length > 8) return false;
        const auto seconds = 60 * length / tempo;
        if (!valid_time(seconds)) return false;
        bpm.store(tempo); beats.store(length); delay_seconds.store(seconds);
        return true;
    }
    bool set_parameter(nk_audio_effect_parameter parameter, float value) noexcept override {
        if (!std::isfinite(value)) return false;
        switch (parameter) {
        case NK_AUDIO_EFFECT_PARAMETER_WET:
        case NK_AUDIO_EFFECT_PARAMETER_DRY:
        case NK_AUDIO_EFFECT_PARAMETER_PING_PONG:
            if (value < 0 || value > 1) return false;
            if (parameter == NK_AUDIO_EFFECT_PARAMETER_WET) wet.store(value);
            else if (parameter == NK_AUDIO_EFFECT_PARAMETER_DRY) dry.store(value);
            else ping_pong.store(value);
            return true;
        case NK_AUDIO_EFFECT_PARAMETER_FEEDBACK:
            if (value < 0 || value > 0.95f) return false;
            feedback.store(value); return true;
        case NK_AUDIO_EFFECT_PARAMETER_DELAY_SECONDS:
            if (!valid_time(value)) return false;
            manual_seconds.store(value); beats.store(0); delay_seconds.store(value); return true;
        case NK_AUDIO_EFFECT_PARAMETER_BPM:
            if (value < 20 || value > 300) return false;
            if (beats.load() > 0) return set_tempo(value, beats.load());
            bpm.store(value); return true;
        case NK_AUDIO_EFFECT_PARAMETER_DELAY_BEATS:
            if (value == 0) { beats.store(0); delay_seconds.store(manual_seconds.load()); return true; }
            return set_tempo(bpm.load(), value);
        default: return false;
        }
    }
    bool get_parameter(nk_audio_effect_parameter parameter, float &value) const noexcept override {
        switch (parameter) {
        case NK_AUDIO_EFFECT_PARAMETER_WET: value = wet.load(); break;
        case NK_AUDIO_EFFECT_PARAMETER_DRY: value = dry.load(); break;
        case NK_AUDIO_EFFECT_PARAMETER_FEEDBACK: value = feedback.load(); break;
        case NK_AUDIO_EFFECT_PARAMETER_PING_PONG: value = ping_pong.load(); break;
        case NK_AUDIO_EFFECT_PARAMETER_DELAY_SECONDS: value = delay_seconds.load(); break;
        case NK_AUDIO_EFFECT_PARAMETER_BPM: value = bpm.load(); break;
        case NK_AUDIO_EFFECT_PARAMETER_DELAY_BEATS: value = beats.load(); break;
        default: return false;
        }
        return true;
    }
    void request_reset() noexcept override { reset_requested.store(true); }
    uint32_t latency_frames() const noexcept override { return 0; }
    uint32_t tail_frames() const noexcept override {
        const auto gain = feedback.load();
        const double echoes = gain > 0 ? std::ceil(std::log(0.001) / std::log(gain)) : 1;
        return static_cast<uint32_t>(std::ceil(delay_seconds.load() * rate * std::max(1.0, echoes))) + fade_frames;
    }
    void process(const float *input, float *output, uint32_t frames) noexcept override {
        if (reset_requested.exchange(false)) {
            std::fill(history.begin(), history.end(), std::array<float, 2>{});
            position = 0; fading = false; fade_position = 0;
            from_delay = to_delay = static_cast<double>(delay_seconds.load()) * rate;
            current_wet = wet.load(); current_dry = dry.load();
            current_feedback = feedback.load(); current_ping = ping_pong.load();
        }
        const double requested = std::clamp(static_cast<double>(delay_seconds.load()) * rate, 1.0, rate * 4.0);
        const float target_wet = wet.load(), target_dry = dry.load();
        const float target_feedback = feedback.load(), target_ping = ping_pong.load();
        const float smoothing = 1.0f / fade_frames;
        for (uint32_t f = 0; f < frames; ++f) {
            if (!fading && std::abs(requested - to_delay) > 0.0001) {
                from_delay = to_delay; to_delay = requested; fade_position = 0; fading = true;
            }
            auto echo = read(to_delay);
            if (fading) {
                const auto old = read(from_delay);
                const float blend = static_cast<float>(++fade_position) / fade_frames;
                for (int c = 0; c < 2; ++c) echo[c] = old[c] + (echo[c] - old[c]) * blend;
                if (fade_position >= fade_frames) fading = false;
            }
            current_wet += (target_wet - current_wet) * smoothing;
            current_dry += (target_dry - current_dry) * smoothing;
            current_feedback += (target_feedback - current_feedback) * smoothing;
            current_ping += (target_ping - current_ping) * smoothing;
            for (int c = 0; c < 2; ++c) {
                const auto sample = input ? input[f * 2 + c] : 0.0f;
                const auto fed = echo[c] + (echo[1 - c] - echo[c]) * current_ping;
                history[position][c] = sample + current_feedback * fed;
                output[f * 2 + c] = sample * current_dry + echo[c] * current_wet;
                if (std::abs(history[position][c]) < 1e-20f) history[position][c] = 0;
            }
            position = (position + 1) % capacity;
        }
    }
};
} // namespace
std::unique_ptr<EffectProcessor> create_stereo_delay_processor(uint32_t rate, uint32_t channels) {
    if (channels != 2 || rate < 8000 || rate > 384000) return {};
    return std::make_unique<StereoDelay>(rate);
}
} // namespace nk::audio
