#include "audio_instrument_source.hpp"
#include "Control/adsr.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace nk::audio_dsp {
namespace {
class SampleSource final : public InstrumentSource {
    struct Slot {
        daisysp::Adsr envelope;
        double position = 0, step = 1;
        float velocity = 0;
        bool used = false, gate = false, running = false;
    };
    nk_audio_dsp_sample_options options_;
    std::vector<float> samples_;
    std::vector<Slot> slots_;
    uint32_t frames_, output_rate_;

  public:
    SampleSource(const nk_audio_dsp_sample_options &options, const float *samples, uint32_t count,
                 uint32_t voices, uint32_t rate)
        : options_(options), samples_(samples, samples + count), slots_(voices),
          frames_(count / options.channels), output_rate_(rate) {
        for (auto &slot : slots_) {
            slot.envelope.Init(static_cast<float>(rate));
            slot.envelope.SetAttackTime(options.envelope.attack_seconds);
            slot.envelope.SetDecayTime(options.envelope.decay_seconds);
            slot.envelope.SetSustainLevel(options.envelope.sustain_level);
            slot.envelope.SetReleaseTime(options.envelope.release_seconds);
        }
    }
    int acquire(uint32_t note, float velocity) noexcept override {
        for (uint32_t i = 0; i < slots_.size(); ++i)
            if (!slots_[i].used) {
                auto &slot = slots_[i];
                slot.used = slot.gate = slot.running = true;
                slot.position = 0;
                slot.velocity = velocity;
                slot.step =
                    static_cast<double>(options_.sample_rate) / output_rate_ *
                    std::pow(2.0, (static_cast<int>(note) - static_cast<int>(options_.root_note)) /
                                      12.0);
                slot.envelope.Retrigger(true);
                return static_cast<int>(i);
            }
        return -1;
    }
    void release(int index) noexcept override {
        if (index >= 0) {
            auto &slot = slots_[index];
            slot.used = slot.gate = slot.running = false;
        }
    }
    void note_off(int index) noexcept override {
        if (index >= 0)
            slots_[index].gate = false;
    }
    bool active(int index) const noexcept override { return index >= 0 && slots_[index].running; }
    StereoSample process(int index) noexcept override {
        if (!active(index))
            return {};
        auto &slot = slots_[index];
        const bool loop = options_.loop_end_frame > options_.loop_start_frame;
        if (loop && slot.position >= options_.loop_end_frame) {
            const auto length = options_.loop_end_frame - options_.loop_start_frame;
            slot.position =
                options_.loop_start_frame +
                std::fmod(slot.position - options_.loop_start_frame, static_cast<double>(length));
        }
        if (slot.position >= frames_) {
            slot.running = false;
            return {};
        }
        const auto gain = slot.envelope.Process(slot.gate) * slot.velocity;
        if (!slot.gate && !slot.envelope.IsRunning()) {
            slot.running = false;
            return {};
        }
        const auto frame = static_cast<uint32_t>(slot.position);
        auto next = std::min(frame + 1, frames_ - 1);
        if (loop && next >= options_.loop_end_frame)
            next = options_.loop_start_frame;
        const auto fraction = static_cast<float>(slot.position - frame);
        const auto channel = [&](uint32_t c) {
            const auto a = samples_[frame * options_.channels + c];
            const auto b = samples_[next * options_.channels + c];
            return (a + (b - a) * fraction) * gain;
        };
        StereoSample sample{channel(0), channel(options_.channels == 1 ? 0 : 1)};
        slot.position += slot.step;
        return sample;
    }
};
} // namespace
std::shared_ptr<InstrumentSource> prepare_sample_source(const nk_audio_dsp_sample_options &options,
                                                        const float *samples, uint32_t count,
                                                        uint32_t voices, uint32_t rate) {
    if (options.struct_size < sizeof(options) || !samples || !count || count > 64u * 1024 * 1024 ||
        !voices || voices > 8192 || options.sample_rate < 1000 || options.sample_rate > 384000 ||
        options.channels < 1 || options.channels > 2 || count % options.channels ||
        options.root_note > 127 || options.loop_start_frame > options.loop_end_frame ||
        options.loop_end_frame > count / options.channels)
        return {};
    for (uint32_t i = 0; i < count; ++i)
        if (!std::isfinite(samples[i]) || std::abs(samples[i]) > 1)
            return {};
    return std::make_shared<SampleSource>(options, samples, count, voices, rate);
}
} // namespace nk::audio_dsp
