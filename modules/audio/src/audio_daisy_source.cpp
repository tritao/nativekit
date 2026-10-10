#include "audio_daisy_source.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <variant>
#include <utility>
#include "audio_daisy_random.hpp"
#define rand nk_daisy_rand
#include "Synthesis/fm2.h"
#include "PhysicalModeling/stringvoice.h"
#include "PhysicalModeling/KarplusString.h"
#include "PhysicalModeling/modalvoice.h"
#include "PhysicalModeling/resonator.h"
#include "PhysicalModeling/drip.h"
#include "Drums/analogbassdrum.h"
#include "Drums/synthbassdrum.h"
#include "Drums/analogsnaredrum.h"
#include "Drums/synthsnaredrum.h"
#include "Drums/hihat.h"
#include "Synthesis/formantosc.h"
#include "Synthesis/vosim.h"
#include "Synthesis/zoscillator.h"
#include "Synthesis/variablesawosc.h"
#include "Synthesis/variableshapeosc.h"
#include "Synthesis/oscillatorbank.h"
#include "Synthesis/harmonic_osc.h"
#include "Noise/grainlet.h"
#include "Noise/particle.h"
#include "Noise/dust.h"
#include "Noise/clockednoise.h"
#include "Noise/fractal_noise.h"
#include "Sampling/granularplayer.h"
#undef rand
namespace nk::audio_dsp {
bool source_parameter_info(uint32_t kind, uint32_t parameter, float &minimum, float &maximum,
                           float &default_value) noexcept {
    switch (kind) {
    case NK_AUDIO_DSP_SOURCE_FM2:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_RATIO:
            minimum = 0.01f;
            maximum = 16.0f;
            default_value = 2.0f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_INDEX:
            minimum = 0.0f;
            maximum = 25.0f;
            default_value = 5.0f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_STRING_VOICE:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN:
            minimum = 0;
            maximum = 1;
            default_value = 0;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.8f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_STRUCTURE:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_BRIGHTNESS:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DAMPING:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_KARPLUS_STRING:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_NONLINEARITY:
            minimum = -1.0f;
            maximum = 1.0f;
            default_value = 0.1f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_BRIGHTNESS:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DAMPING:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_MODAL_VOICE:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN:
            minimum = 0;
            maximum = 1;
            default_value = 0;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.8f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_STRUCTURE:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_BRIGHTNESS:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DAMPING:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_RESONATOR:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_POSITION:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.015f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_RESOLUTION:
            minimum = 4.0f;
            maximum = 24.0f;
            default_value = 24.0f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_STRUCTURE:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_BRIGHTNESS:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DAMPING:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_DRIP:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DETTACK:
            minimum = 0.001f;
            maximum = 2.0f;
            default_value = 0.1f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_ANALOG_BASS_DRUM:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN:
            minimum = 0;
            maximum = 1;
            default_value = 0;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.8f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_TONE:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DECAY:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_ATTACK_FM:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SELF_FM:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_SYNTHETIC_BASS_DRUM:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN:
            minimum = 0;
            maximum = 1;
            default_value = 0;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.8f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DIRTINESS:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DECAY:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_FM_AMOUNT:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_FM_DECAY:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_ANALOG_SNARE_DRUM:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN:
            minimum = 0;
            maximum = 1;
            default_value = 0;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.8f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_TONE:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DECAY:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SNAPPY:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_SYNTHETIC_SNARE_DRUM:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN:
            minimum = 0;
            maximum = 1;
            default_value = 0;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.8f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_FM_AMOUNT:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DECAY:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SNAPPY:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_HI_HAT:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN:
            minimum = 0;
            maximum = 1;
            default_value = 0;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.8f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_TONE:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DECAY:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_NOISINESS:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_FORMANT:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO:
            minimum = 0.1f;
            maximum = 16.0f;
            default_value = 2.0f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_PHASE_SHIFT:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_VOSIM:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO:
            minimum = 0.1f;
            maximum = 16.0f;
            default_value = 2.0f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SECOND_FORMANT_RATIO:
            minimum = 0.1f;
            maximum = 16.0f;
            default_value = 3.0f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SHAPE:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_ZOSC:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO:
            minimum = 0.1f;
            maximum = 16.0f;
            default_value = 2.0f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SHAPE:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_MODE:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_VARIABLE_SAW:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_PULSE_WIDTH:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SHAPE:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_VARIABLE_SHAPE:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SYNC_ENABLED:
            minimum = 0;
            maximum = 1;
            default_value = 1;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_PULSE_WIDTH:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SHAPE:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SYNC_RATIO:
            minimum = 0.1f;
            maximum = 16.0f;
            default_value = 2.0f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_OSCILLATOR_BANK:
        switch (parameter) {
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_HARMONIC:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_FIRST_HARMONIC:
            minimum = 1.0f;
            maximum = 16.0f;
            default_value = 1.0f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_GRAINLET:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO:
            minimum = 0.1f;
            maximum = 16.0f;
            default_value = 2.0f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SHAPE:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_BLEED:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_PARTICLE:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_RESONANCE:
            minimum = 0.01f;
            maximum = 0.99f;
            default_value = 0.7f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_RANDOM_RATE:
            minimum = 0.0f;
            maximum = 1000.0f;
            default_value = 10.0f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DENSITY:
            minimum = 0.0001f;
            maximum = 1.0f;
            default_value = 0.3f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SPREAD:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_DUST:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_DENSITY:
            minimum = 0.0001f;
            maximum = 1.0f;
            default_value = 0.3f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_CLOCKED_NOISE:
        switch (parameter) {
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_FRACTAL_NOISE:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_COLOR:
            minimum = 0.0f;
            maximum = 1.0f;
            default_value = 0.5f;
            return true;
        default:
            return false;
        }
    case NK_AUDIO_DSP_SOURCE_GRANULAR:
        switch (parameter) {
        case NK_AUDIO_DSP_SOURCE_PARAMETER_SPEED:
            minimum = -4.0f;
            maximum = 4.0f;
            default_value = 1.0f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_ROOT_NOTE:
            minimum = 0.0f;
            maximum = 127.0f;
            default_value = 60.0f;
            return true;
        case NK_AUDIO_DSP_SOURCE_PARAMETER_GRAIN_MS:
            minimum = 1.0f;
            maximum = 1000.0f;
            default_value = 80.0f;
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}
bool normalize_source(const nk_audio_dsp_source_options &options, const float *samples,
                      uint32_t count, SourceParameters &output) {
    if (options.struct_size != sizeof(options) || options.kind < 1 ||
        options.kind > NK_AUDIO_DSP_SOURCE_KIND_COUNT)
        return false;
    for (uint32_t p = 0; p < NK_AUDIO_DSP_SOURCE_PARAMETER_COUNT; ++p) {
        float lo = 0, hi = 0, value = 0;
        const bool supported = source_parameter_info(options.kind, p, lo, hi, value);
        const float input = options.values[p];
        if (!std::isfinite(input) || (supported ? input < lo || input > hi : input != 0))
            return false;
        if ((p == NK_AUDIO_DSP_SOURCE_PARAMETER_RESOLUTION ||
             p == NK_AUDIO_DSP_SOURCE_PARAMETER_FIRST_HARMONIC ||
             p == NK_AUDIO_DSP_SOURCE_PARAMETER_ROOT_NOTE ||
             p == NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN ||
             p == NK_AUDIO_DSP_SOURCE_PARAMETER_SYNC_ENABLED) &&
            input != std::floor(input))
            return false;
    }
    if (options.kind == NK_AUDIO_DSP_SOURCE_RESONATOR &&
        static_cast<int>(options.values[NK_AUDIO_DSP_SOURCE_PARAMETER_RESOLUTION]) % 4)
        return false;
    const uint32_t amplitude_count = options.kind == NK_AUDIO_DSP_SOURCE_OSCILLATOR_BANK ? 7
                                     : options.kind == NK_AUDIO_DSP_SOURCE_HARMONIC      ? 16
                                                                                         : 0;
    float total = 0;
    for (uint32_t i = 0; i < 16; ++i) {
        const float amp = options.amplitudes[i];
        if (!std::isfinite(amp) || amp < 0 || amp > 1 || (i >= amplitude_count && amp != 0))
            return false;
        total += amp;
    }
    if (amplitude_count && std::abs(total - 1) > 0.0001f)
        return false;
    if (options.kind == NK_AUDIO_DSP_SOURCE_GRANULAR) {
        if (!samples || count < 2 || count > 1048576)
            return false;
        for (uint32_t i = 0; i < count; ++i)
            if (!std::isfinite(samples[i]) || std::abs(samples[i]) > 1)
                return false;
        output.samples = std::make_shared<const std::vector<float>>(samples, samples + count);
    } else if (count)
        return false;
    output.options = options;
    return true;
}
struct DaisySource::Impl {
    using Model = std::variant<
        std::monostate, daisysp::Fm2, daisysp::StringVoice, daisysp::String, daisysp::ModalVoice,
        daisysp::Resonator, daisysp::Drip, daisysp::AnalogBassDrum, daisysp::SyntheticBassDrum,
        daisysp::AnalogSnareDrum, daisysp::SyntheticSnareDrum, daisysp::HiHat<>,
        daisysp::FormantOscillator, daisysp::VosimOscillator, daisysp::ZOscillator,
        daisysp::VariableSawOscillator, daisysp::VariableShapeOscillator, daisysp::OscillatorBank,
        daisysp::HarmonicOscillator<16>, daisysp::GrainletOscillator, daisysp::Particle,
        daisysp::Dust, daisysp::ClockedNoise,
        daisysp::FractalRandomGenerator<daisysp::ClockedNoise, 4>, daisysp::GranularPlayer>;
    Model model;
    SourceParameters parameters;
    uint32_t sample_rate = 48000;
    uint32_t random_state = 1;
    bool trigger = false;
    float value(uint32_t p) const noexcept { return parameters.options.values[p]; }
    void initialize(float frequency) noexcept {
        DaisyRandomScope random(random_state);
        switch (parameters.options.kind) {
        case NK_AUDIO_DSP_SOURCE_FM2: {
            auto &m = model.emplace<daisysp::Fm2>();
            m.Init(sample_rate);
            m.SetRatio(value(NK_AUDIO_DSP_SOURCE_PARAMETER_RATIO));
            m.SetIndex(value(NK_AUDIO_DSP_SOURCE_PARAMETER_INDEX));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_STRING_VOICE: {
            auto &m = model.emplace<daisysp::StringVoice>();
            m.Init(sample_rate);
            m.SetAccent(value(NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT));
            m.SetStructure(value(NK_AUDIO_DSP_SOURCE_PARAMETER_STRUCTURE));
            m.SetBrightness(value(NK_AUDIO_DSP_SOURCE_PARAMETER_BRIGHTNESS));
            m.SetDamping(value(NK_AUDIO_DSP_SOURCE_PARAMETER_DAMPING));
            m.SetSustain(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN) != 0);
            break;
        }
        case NK_AUDIO_DSP_SOURCE_KARPLUS_STRING: {
            auto &m = model.emplace<daisysp::String>();
            m.Init(sample_rate);
            m.SetNonLinearity(value(NK_AUDIO_DSP_SOURCE_PARAMETER_NONLINEARITY));
            m.SetBrightness(value(NK_AUDIO_DSP_SOURCE_PARAMETER_BRIGHTNESS));
            m.SetDamping(value(NK_AUDIO_DSP_SOURCE_PARAMETER_DAMPING));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_MODAL_VOICE: {
            auto &m = model.emplace<daisysp::ModalVoice>();
            m.Init(sample_rate);
            m.SetAccent(value(NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT));
            m.SetStructure(value(NK_AUDIO_DSP_SOURCE_PARAMETER_STRUCTURE));
            m.SetBrightness(value(NK_AUDIO_DSP_SOURCE_PARAMETER_BRIGHTNESS));
            m.SetDamping(value(NK_AUDIO_DSP_SOURCE_PARAMETER_DAMPING));
            m.SetSustain(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN) != 0);
            break;
        }
        case NK_AUDIO_DSP_SOURCE_RESONATOR: {
            auto &m = model.emplace<daisysp::Resonator>();
            m.Init(value(NK_AUDIO_DSP_SOURCE_PARAMETER_POSITION),
                   static_cast<int>(value(NK_AUDIO_DSP_SOURCE_PARAMETER_RESOLUTION)), sample_rate);
            m.SetStructure(value(NK_AUDIO_DSP_SOURCE_PARAMETER_STRUCTURE));
            m.SetBrightness(value(NK_AUDIO_DSP_SOURCE_PARAMETER_BRIGHTNESS));
            m.SetDamping(value(NK_AUDIO_DSP_SOURCE_PARAMETER_DAMPING));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_DRIP: {
            auto &m = model.emplace<daisysp::Drip>();
            m.Init(sample_rate, value(NK_AUDIO_DSP_SOURCE_PARAMETER_DETTACK));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_ANALOG_BASS_DRUM: {
            auto &m = model.emplace<daisysp::AnalogBassDrum>();
            m.Init(sample_rate);
            m.SetAccent(value(NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT));
            m.SetTone(value(NK_AUDIO_DSP_SOURCE_PARAMETER_TONE));
            m.SetDecay(value(NK_AUDIO_DSP_SOURCE_PARAMETER_DECAY));
            m.SetAttackFmAmount(value(NK_AUDIO_DSP_SOURCE_PARAMETER_ATTACK_FM));
            m.SetSelfFmAmount(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SELF_FM));
            m.SetSustain(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN) != 0);
            break;
        }
        case NK_AUDIO_DSP_SOURCE_SYNTHETIC_BASS_DRUM: {
            auto &m = model.emplace<daisysp::SyntheticBassDrum>();
            m.Init(sample_rate);
            m.SetAccent(value(NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT));
            m.SetDirtiness(value(NK_AUDIO_DSP_SOURCE_PARAMETER_DIRTINESS));
            m.SetDecay(value(NK_AUDIO_DSP_SOURCE_PARAMETER_DECAY));
            m.SetFmEnvelopeAmount(value(NK_AUDIO_DSP_SOURCE_PARAMETER_FM_AMOUNT));
            m.SetFmEnvelopeDecay(value(NK_AUDIO_DSP_SOURCE_PARAMETER_FM_DECAY));
            m.SetSustain(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN) != 0);
            break;
        }
        case NK_AUDIO_DSP_SOURCE_ANALOG_SNARE_DRUM: {
            auto &m = model.emplace<daisysp::AnalogSnareDrum>();
            m.Init(sample_rate);
            m.SetAccent(value(NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT));
            m.SetTone(value(NK_AUDIO_DSP_SOURCE_PARAMETER_TONE));
            m.SetDecay(value(NK_AUDIO_DSP_SOURCE_PARAMETER_DECAY));
            m.SetSnappy(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SNAPPY));
            m.SetSustain(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN) != 0);
            break;
        }
        case NK_AUDIO_DSP_SOURCE_SYNTHETIC_SNARE_DRUM: {
            auto &m = model.emplace<daisysp::SyntheticSnareDrum>();
            m.Init(sample_rate);
            m.SetAccent(value(NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT));
            m.SetFmAmount(value(NK_AUDIO_DSP_SOURCE_PARAMETER_FM_AMOUNT));
            m.SetDecay(value(NK_AUDIO_DSP_SOURCE_PARAMETER_DECAY));
            m.SetSnappy(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SNAPPY));
            m.SetSustain(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN) != 0);
            break;
        }
        case NK_AUDIO_DSP_SOURCE_HI_HAT: {
            auto &m = model.emplace<daisysp::HiHat<>>();
            m.Init(sample_rate);
            m.SetAccent(value(NK_AUDIO_DSP_SOURCE_PARAMETER_ACCENT));
            m.SetTone(value(NK_AUDIO_DSP_SOURCE_PARAMETER_TONE));
            m.SetDecay(value(NK_AUDIO_DSP_SOURCE_PARAMETER_DECAY));
            m.SetNoisiness(value(NK_AUDIO_DSP_SOURCE_PARAMETER_NOISINESS));
            m.SetSustain(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SUSTAIN) != 0);
            break;
        }
        case NK_AUDIO_DSP_SOURCE_FORMANT: {
            auto &m = model.emplace<daisysp::FormantOscillator>();
            m.Init(sample_rate);
            m.SetFormantFreq(
                std::clamp(frequency * value(NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO), 1.0f,
                           sample_rate * .45f));
            m.SetPhaseShift(value(NK_AUDIO_DSP_SOURCE_PARAMETER_PHASE_SHIFT));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_VOSIM: {
            auto &m = model.emplace<daisysp::VosimOscillator>();
            m.Init(sample_rate);
            m.SetForm1Freq(
                std::clamp(frequency * value(NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO), 1.0f,
                           sample_rate * .45f));
            m.SetForm2Freq(
                std::clamp(frequency * value(NK_AUDIO_DSP_SOURCE_PARAMETER_SECOND_FORMANT_RATIO),
                           1.0f, sample_rate * .45f));
            m.SetShape(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SHAPE));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_ZOSC: {
            auto &m = model.emplace<daisysp::ZOscillator>();
            m.Init(sample_rate);
            m.SetFormantFreq(
                std::clamp(frequency * value(NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO), 1.0f,
                           sample_rate * .45f));
            m.SetShape(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SHAPE));
            m.SetMode(value(NK_AUDIO_DSP_SOURCE_PARAMETER_MODE));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_VARIABLE_SAW: {
            auto &m = model.emplace<daisysp::VariableSawOscillator>();
            m.Init(sample_rate);
            m.SetPW(value(NK_AUDIO_DSP_SOURCE_PARAMETER_PULSE_WIDTH));
            m.SetWaveshape(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SHAPE));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_VARIABLE_SHAPE: {
            auto &m = model.emplace<daisysp::VariableShapeOscillator>();
            m.Init(sample_rate);
            m.SetSync(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SYNC_ENABLED) != 0);
            m.SetPW(value(NK_AUDIO_DSP_SOURCE_PARAMETER_PULSE_WIDTH));
            m.SetWaveshape(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SHAPE));
            m.SetSyncFreq(std::clamp(frequency * value(NK_AUDIO_DSP_SOURCE_PARAMETER_SYNC_RATIO),
                                     1.0f, sample_rate * .45f));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_OSCILLATOR_BANK: {
            auto &m = model.emplace<daisysp::OscillatorBank>();
            m.Init(sample_rate);
            m.SetAmplitudes(parameters.options.amplitudes);
            break;
        }
        case NK_AUDIO_DSP_SOURCE_HARMONIC: {
            auto &m = model.emplace<daisysp::HarmonicOscillator<16>>();
            m.Init(sample_rate);
            m.SetAmplitudes(parameters.options.amplitudes);
            m.SetFirstHarmIdx(
                static_cast<int>(value(NK_AUDIO_DSP_SOURCE_PARAMETER_FIRST_HARMONIC)));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_GRAINLET: {
            auto &m = model.emplace<daisysp::GrainletOscillator>();
            m.Init(sample_rate);
            m.SetFormantFreq(
                std::clamp(frequency * value(NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO), 1.0f,
                           sample_rate * .45f));
            m.SetShape(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SHAPE));
            m.SetBleed(value(NK_AUDIO_DSP_SOURCE_PARAMETER_BLEED));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_PARTICLE: {
            auto &m = model.emplace<daisysp::Particle>();
            m.Init(sample_rate);
            m.SetGain(1);
            m.SetResonance(value(NK_AUDIO_DSP_SOURCE_PARAMETER_RESONANCE));
            m.SetRandomFreq(value(NK_AUDIO_DSP_SOURCE_PARAMETER_RANDOM_RATE));
            m.SetDensity(value(NK_AUDIO_DSP_SOURCE_PARAMETER_DENSITY));
            m.SetSpread(value(NK_AUDIO_DSP_SOURCE_PARAMETER_SPREAD));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_DUST: {
            auto &m = model.emplace<daisysp::Dust>();
            m.Init();
            m.SetDensity(value(NK_AUDIO_DSP_SOURCE_PARAMETER_DENSITY));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_CLOCKED_NOISE: {
            auto &m = model.emplace<daisysp::ClockedNoise>();
            m.Init(sample_rate);
            break;
        }
        case NK_AUDIO_DSP_SOURCE_FRACTAL_NOISE: {
            auto &m = model.emplace<daisysp::FractalRandomGenerator<daisysp::ClockedNoise, 4>>();
            m.Init(sample_rate);
            m.SetColor(value(NK_AUDIO_DSP_SOURCE_PARAMETER_COLOR));
            break;
        }
        case NK_AUDIO_DSP_SOURCE_GRANULAR: {
            auto &m = model.emplace<daisysp::GranularPlayer>();
            m.Init(const_cast<float *>(parameters.samples->data()),
                   static_cast<int>(parameters.samples->size()), sample_rate);
            break;
        }
        default:
            model.emplace<std::monostate>();
            break;
        }
    }
};
DaisySource::DaisySource() : impl_(std::make_unique<Impl>()) {}
DaisySource::~DaisySource() = default;
void DaisySource::configure(const SourceParameters &p, uint32_t sample_rate) noexcept {
    // Configuration changes do not reset sounding models (gain/envelope updates share this call).
    impl_->parameters = p;
    impl_->sample_rate = sample_rate;
}
void DaisySource::trigger(float frequency, uint32_t note) noexcept {
    impl_->random_state = 0x9e3779b9u ^ (note + 1u);
    impl_->initialize(frequency);
    impl_->trigger = true;
}
float DaisySource::process(float frequency, float excitation) noexcept {
    DaisyRandomScope random(impl_->random_state);
    const bool trigger = std::exchange(impl_->trigger, false);
    switch (impl_->parameters.options.kind) {
    case NK_AUDIO_DSP_SOURCE_FM2: {
        auto &m = std::get<daisysp::Fm2>(impl_->model);
        m.SetFrequency(frequency);
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_STRING_VOICE: {
        auto &m = std::get<daisysp::StringVoice>(impl_->model);
        m.SetFreq(frequency);
        return m.Process(trigger);
    }
    case NK_AUDIO_DSP_SOURCE_KARPLUS_STRING: {
        auto &m = std::get<daisysp::String>(impl_->model);
        m.SetFreq(frequency);
        return m.Process(trigger ? 1.0f : excitation * 0.02f);
    }
    case NK_AUDIO_DSP_SOURCE_MODAL_VOICE: {
        auto &m = std::get<daisysp::ModalVoice>(impl_->model);
        m.SetFreq(frequency);
        return m.Process(trigger);
    }
    case NK_AUDIO_DSP_SOURCE_RESONATOR: {
        auto &m = std::get<daisysp::Resonator>(impl_->model);
        m.SetFreq(frequency);
        return m.Process(trigger ? 1.0f : excitation * 0.02f);
    }
    case NK_AUDIO_DSP_SOURCE_DRIP: {
        auto &m = std::get<daisysp::Drip>(impl_->model);
        return m.Process(trigger);
    }
    case NK_AUDIO_DSP_SOURCE_ANALOG_BASS_DRUM: {
        auto &m = std::get<daisysp::AnalogBassDrum>(impl_->model);
        m.SetFreq(frequency);
        return m.Process(trigger);
    }
    case NK_AUDIO_DSP_SOURCE_SYNTHETIC_BASS_DRUM: {
        auto &m = std::get<daisysp::SyntheticBassDrum>(impl_->model);
        m.SetFreq(frequency);
        return m.Process(trigger);
    }
    case NK_AUDIO_DSP_SOURCE_ANALOG_SNARE_DRUM: {
        auto &m = std::get<daisysp::AnalogSnareDrum>(impl_->model);
        m.SetFreq(frequency);
        return m.Process(trigger);
    }
    case NK_AUDIO_DSP_SOURCE_SYNTHETIC_SNARE_DRUM: {
        auto &m = std::get<daisysp::SyntheticSnareDrum>(impl_->model);
        m.SetFreq(frequency);
        return m.Process(trigger);
    }
    case NK_AUDIO_DSP_SOURCE_HI_HAT: {
        auto &m = std::get<daisysp::HiHat<>>(impl_->model);
        m.SetFreq(frequency);
        return m.Process(trigger);
    }
    case NK_AUDIO_DSP_SOURCE_FORMANT: {
        auto &m = std::get<daisysp::FormantOscillator>(impl_->model);
        m.SetCarrierFreq(frequency);
        m.SetFormantFreq(
            std::clamp(frequency * impl_->value(NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO), 1.0f,
                       impl_->sample_rate * .45f));
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_VOSIM: {
        auto &m = std::get<daisysp::VosimOscillator>(impl_->model);
        m.SetFreq(frequency);
        m.SetForm1Freq(
            std::clamp(frequency * impl_->value(NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO), 1.0f,
                       impl_->sample_rate * .45f));
        m.SetForm2Freq(
            std::clamp(frequency * impl_->value(NK_AUDIO_DSP_SOURCE_PARAMETER_SECOND_FORMANT_RATIO),
                       1.0f, impl_->sample_rate * .45f));
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_ZOSC: {
        auto &m = std::get<daisysp::ZOscillator>(impl_->model);
        m.SetFreq(frequency);
        m.SetFormantFreq(
            std::clamp(frequency * impl_->value(NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO), 1.0f,
                       impl_->sample_rate * .45f));
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_VARIABLE_SAW: {
        auto &m = std::get<daisysp::VariableSawOscillator>(impl_->model);
        m.SetFreq(frequency);
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_VARIABLE_SHAPE: {
        auto &m = std::get<daisysp::VariableShapeOscillator>(impl_->model);
        m.SetFreq(frequency);
        m.SetSyncFreq(std::clamp(frequency * impl_->value(NK_AUDIO_DSP_SOURCE_PARAMETER_SYNC_RATIO),
                                 1.0f, impl_->sample_rate * .45f));
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_OSCILLATOR_BANK: {
        auto &m = std::get<daisysp::OscillatorBank>(impl_->model);
        m.SetFreq(frequency);
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_HARMONIC: {
        auto &m = std::get<daisysp::HarmonicOscillator<16>>(impl_->model);
        m.SetFreq(frequency);
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_GRAINLET: {
        auto &m = std::get<daisysp::GrainletOscillator>(impl_->model);
        m.SetFreq(frequency);
        m.SetFormantFreq(
            std::clamp(frequency * impl_->value(NK_AUDIO_DSP_SOURCE_PARAMETER_FORMANT_RATIO), 1.0f,
                       impl_->sample_rate * .45f));
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_PARTICLE: {
        auto &m = std::get<daisysp::Particle>(impl_->model);
        m.SetSync(trigger);
        m.SetFreq(frequency);
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_DUST: {
        auto &m = std::get<daisysp::Dust>(impl_->model);
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_CLOCKED_NOISE: {
        auto &m = std::get<daisysp::ClockedNoise>(impl_->model);
        m.SetFreq(frequency);
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_FRACTAL_NOISE: {
        auto &m = std::get<daisysp::FractalRandomGenerator<daisysp::ClockedNoise, 4>>(impl_->model);
        m.SetFreq(frequency);
        return m.Process();
    }
    case NK_AUDIO_DSP_SOURCE_GRANULAR: {
        auto &m = std::get<daisysp::GranularPlayer>(impl_->model);
        return m.Process(
            impl_->value(NK_AUDIO_DSP_SOURCE_PARAMETER_SPEED),
            1200.0f *
                std::log2(
                    frequency /
                    (440.0f *
                     std::pow(2.0f, (impl_->value(NK_AUDIO_DSP_SOURCE_PARAMETER_ROOT_NOTE) - 69) /
                                        12.0f))),
            impl_->value(NK_AUDIO_DSP_SOURCE_PARAMETER_GRAIN_MS));
    }
    default:
        return excitation;
    }
}
} // namespace nk::audio_dsp
