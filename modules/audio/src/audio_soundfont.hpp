#pragma once
#include "audio_instrument_source.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nk::audio_dsp {
#ifdef NK_AUDIO_TESTING
void soundfont_callback_begin() noexcept;
void soundfont_callback_end() noexcept;
#endif
// All parsing, copies, allocation and destruction happen on preparation/control threads.
class SoundFontBank final {
public:
    static std::shared_ptr<SoundFontBank> load(const void *bytes, uint32_t size);
    ~SoundFontBank();
    uint32_t preset_count() const noexcept;
    bool preset_info(uint32_t index, uint32_t &bank, uint32_t &program, std::string &name) const;
private:
    friend class SoundFontPool;
    void *font_ = nullptr;
    mutable std::mutex mutex_;
};
class SoundFontPool final : public InstrumentSource {
public:
    static std::shared_ptr<SoundFontPool> prepare(std::shared_ptr<SoundFontBank> bank,
        uint32_t preset, uint32_t voices, uint32_t sample_rate);
    ~SoundFontPool() override;
    int acquire(uint32_t note, float velocity) noexcept override;
    void release(int slot) noexcept override;
    void note_off(int slot) noexcept override;
    StereoSample process(int slot) noexcept override;
    bool active(int slot) const noexcept override;
private:
    struct Slot { void *font = nullptr; bool used = false; };
    std::shared_ptr<SoundFontBank> bank_;
    std::vector<Slot> slots_;
    uint32_t preset_ = 0;
};
}
