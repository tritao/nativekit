#include "nativekit_audio_dsp.h"

#include <cassert>
#include <cmath>
#include <cstdint>

namespace {

bool contains_signal(const float *samples, uint32_t count) {
    for (uint32_t index = 0; index < count; ++index) {
        if (std::abs(samples[index]) > 0.0001f)
            return true;
    }
    return false;
}

bool all_silent(const float *samples, uint32_t count) {
    for (uint32_t index = 0; index < count; ++index) {
        if (std::abs(samples[index]) > 0.0001f)
            return false;
    }
    return true;
}

} // namespace

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_audio_dsp_engine_options engine_options{};
    engine_options.struct_size = sizeof(engine_options);
    engine_options.sample_rate = 48000;
    engine_options.channels = 1;
    engine_options.block_size = 64;
    engine_options.max_voices = 4;

    nk_audio_dsp_engine engine = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_engine_create(&engine_options, &engine) == NK_OK);
    assert(engine != NK_INVALID_HANDLE);

    nk_audio_dsp_engine_options normalized{};
    normalized.struct_size = sizeof(normalized);
    assert(nk_audio_dsp_engine_get_options(engine, &normalized) == NK_OK);
    assert(normalized.sample_rate == 48000 && normalized.channels == 1 &&
           normalized.block_size == 64 && normalized.max_voices == 4);

    nk_audio_dsp_capabilities capabilities = 0;
    assert(nk_audio_dsp_engine_get_capabilities(engine, &capabilities) == NK_OK);
    assert((capabilities & NK_AUDIO_DSP_CAPABILITY_OSCILLATOR) != 0);
    assert((capabilities & NK_AUDIO_DSP_CAPABILITY_NOISE) != 0);
    assert((capabilities & NK_AUDIO_DSP_CAPABILITY_ENVELOPE) != 0);
    assert((capabilities & NK_AUDIO_DSP_CAPABILITY_LFO) != 0);
    assert((capabilities & NK_AUDIO_DSP_CAPABILITY_FILTER) != 0);
    assert((capabilities & NK_AUDIO_DSP_CAPABILITY_MODULATION) != 0);

    nk_audio_dsp_patch_options patch_options{};
    patch_options.struct_size = sizeof(patch_options);
    patch_options.oscillator.struct_size = sizeof(patch_options.oscillator);
    patch_options.oscillator.waveform = NK_AUDIO_DSP_WAVEFORM_SINE;
    patch_options.oscillator.level = 0.75f;
    patch_options.noise.struct_size = sizeof(patch_options.noise);
    patch_options.noise.level = 0.25f;
    patch_options.envelope.struct_size = sizeof(patch_options.envelope);
    patch_options.envelope.attack_seconds = 0.0f;
    patch_options.envelope.decay_seconds = 0.0f;
    patch_options.envelope.sustain_level = 1.0f;
    patch_options.envelope.release_seconds = 0.0f;
    patch_options.filter.struct_size = sizeof(patch_options.filter);
    patch_options.filter.type = NK_AUDIO_DSP_FILTER_SVF_LOW_PASS;
    patch_options.filter.cutoff_hz = 1200.0f;
    patch_options.filter.resonance = 0.5f;
    patch_options.gain = 0.5f;
    patch_options.lfo.struct_size = sizeof(patch_options.lfo);
    patch_options.lfo.waveform = NK_AUDIO_DSP_WAVEFORM_SQUARE;
    patch_options.lfo.mode = NK_AUDIO_DSP_LFO_RETRIGGER;
    patch_options.lfo.rate_hz = 1000.0f;
    patch_options.lfo.phase = 0.0f;
    patch_options.route_count = 1;
    patch_options.routes[0].struct_size = sizeof(patch_options.routes[0]);
    patch_options.routes[0].source = NK_AUDIO_DSP_MODULATION_SOURCE_LFO;
    patch_options.routes[0].destination = NK_AUDIO_DSP_MODULATION_DESTINATION_AMPLITUDE;
    patch_options.routes[0].polarity = NK_AUDIO_DSP_MODULATION_UNIPOLAR;
    patch_options.routes[0].amount = 1.0f;
    patch_options.routes[1].struct_size = sizeof(patch_options.routes[1]);
    patch_options.routes[1].source = NK_AUDIO_DSP_MODULATION_SOURCE_ENVELOPE;
    patch_options.routes[1].destination = NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_HZ;
    patch_options.routes[1].polarity = NK_AUDIO_DSP_MODULATION_UNIPOLAR;
    patch_options.routes[1].amount = 300.0f;
    patch_options.routes[2].struct_size = sizeof(patch_options.routes[2]);
    patch_options.routes[2].source = NK_AUDIO_DSP_MODULATION_SOURCE_LFO;
    patch_options.routes[2].destination = NK_AUDIO_DSP_MODULATION_DESTINATION_PITCH_SEMITONES;
    patch_options.routes[2].polarity = NK_AUDIO_DSP_MODULATION_BIPOLAR;
    patch_options.routes[2].amount = 1.0f;
    patch_options.route_count = 3;

    auto invalid_patch_options = patch_options;
    invalid_patch_options.route_count = NK_AUDIO_DSP_MAX_MODULATION_ROUTES + 1;
    nk_audio_dsp_patch invalid_patch = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_patch_create(&invalid_patch_options, &invalid_patch) ==
           NK_ERROR_INVALID_ARGUMENT);

    nk_audio_dsp_patch patch = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_patch_create(&patch_options, &patch) == NK_OK);
    nk_audio_dsp_instrument instrument = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_instrument_create_from_patch(engine, patch, &instrument) == NK_OK);
    nk_audio_dsp_engine second_engine = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_engine_create(&engine_options, &second_engine) == NK_OK);
    nk_audio_dsp_instrument second_instrument = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_instrument_create_from_patch(second_engine, patch, &second_instrument) ==
           NK_OK);
    assert(nk_audio_dsp_patch_destroy(patch) == NK_OK);
    assert(nk_audio_dsp_patch_destroy(patch) == NK_ERROR_INVALID_HANDLE);

    nk_audio_dsp_instrument_options instrument_options{};
    instrument_options.struct_size = sizeof(instrument_options);
    instrument_options.waveform = NK_AUDIO_DSP_WAVEFORM_SINE;
    instrument_options.gain = 1.0f;
    instrument_options.attack_seconds = 0.0f;
    instrument_options.decay_seconds = 0.0f;
    instrument_options.sustain_level = 1.0f;
    instrument_options.release_seconds = 0.0f;

    nk_audio_dsp_instrument legacy_instrument = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_instrument_create(engine, &instrument_options, &legacy_instrument) ==
           NK_OK);
    float legacy_waveform = -1.0f;
    assert(nk_audio_dsp_instrument_get_parameter(legacy_instrument, NK_AUDIO_DSP_PARAMETER_WAVEFORM,
                                                 &legacy_waveform) == NK_OK);
    assert(legacy_waveform == static_cast<float>(NK_AUDIO_DSP_WAVEFORM_SINE));

    float samples[64]{};
    nk_audio_dsp_render_target target{};
    target.struct_size = sizeof(target);
    target.samples = samples;
    target.frame_count = 64;
    target.channels = 1;
    target.sample_count = 64;

    nk_audio_dsp_event note_on{};
    note_on.struct_size = sizeof(note_on);
    note_on.kind = NK_AUDIO_DSP_EVENT_NOTE_ON;
    note_on.frame_offset = 0;
    note_on.instrument = instrument;
    note_on.voice_id = 1;
    note_on.note = 69;
    note_on.velocity = 1.0f;
    assert(nk_audio_dsp_engine_render(engine, &target, &note_on, 1) == NK_OK);
    assert(contains_signal(samples, 64));

    nk_audio_dsp_event gain_off{};
    gain_off.struct_size = sizeof(gain_off);
    gain_off.kind = NK_AUDIO_DSP_EVENT_PARAMETER;
    gain_off.frame_offset = 32;
    gain_off.instrument = instrument;
    gain_off.parameter = NK_AUDIO_DSP_PARAMETER_GAIN;
    gain_off.value = 0.0f;
    assert(nk_audio_dsp_engine_render(engine, &target, &gain_off, 1) == NK_OK);
    assert(contains_signal(samples, 32));
    assert(all_silent(samples + 32, 32));

    nk_audio_dsp_event note_off{};
    note_off.struct_size = sizeof(note_off);
    note_off.kind = NK_AUDIO_DSP_EVENT_NOTE_OFF;
    note_off.frame_offset = 0;
    note_off.voice_id = 1;
    assert(nk_audio_dsp_engine_render(engine, &target, &note_off, 1) == NK_OK);
    assert(all_silent(samples, 64));

    assert(nk_audio_dsp_instrument_set_parameter(instrument, NK_AUDIO_DSP_PARAMETER_GAIN, 0.5f) ==
           NK_OK);
    assert(nk_audio_dsp_instrument_set_parameter(instrument, NK_AUDIO_DSP_PARAMETER_NOISE_LEVEL,
                                                 0.25f) == NK_OK);
    assert(nk_audio_dsp_instrument_set_parameter(
               instrument, NK_AUDIO_DSP_PARAMETER_FILTER_CUTOFF_HZ, 1200.0f) == NK_OK);
    assert(nk_audio_dsp_instrument_set_parameter(
               instrument, NK_AUDIO_DSP_PARAMETER_FILTER_RESONANCE, 0.5f) == NK_OK);
    float gain = 0.0f;
    assert(nk_audio_dsp_instrument_get_parameter(instrument, NK_AUDIO_DSP_PARAMETER_GAIN, &gain) ==
           NK_OK);
    assert(gain == 0.5f);
    float noise_level = 0.0f;
    assert(nk_audio_dsp_instrument_get_parameter(instrument, NK_AUDIO_DSP_PARAMETER_NOISE_LEVEL,
                                                 &noise_level) == NK_OK);
    assert(noise_level == 0.25f);
    float filter_cutoff = 0.0f;
    assert(nk_audio_dsp_instrument_get_parameter(
               instrument, NK_AUDIO_DSP_PARAMETER_FILTER_CUTOFF_HZ, &filter_cutoff) == NK_OK);
    assert(filter_cutoff == 1200.0f);
    assert(nk_audio_dsp_instrument_set_parameter(instrument,
                                                 NK_AUDIO_DSP_PARAMETER_FILTER_CUTOFF_HZ,
                                                 20000.0f) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_dsp_instrument_set_parameter(instrument, NK_AUDIO_DSP_PARAMETER_GAIN, -1.0f) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_dsp_engine_render(engine, &target, nullptr, 0) == NK_OK);
    assert(all_silent(samples, 64));

    auto free_running_options = patch_options;
    free_running_options.oscillator.waveform = NK_AUDIO_DSP_WAVEFORM_SQUARE;
    free_running_options.filter.type = NK_AUDIO_DSP_FILTER_NONE;
    free_running_options.filter.cutoff_hz = 0.0f;
    free_running_options.gain = 1.0f;
    free_running_options.lfo.mode = NK_AUDIO_DSP_LFO_FREE_RUNNING;
    free_running_options.lfo.rate_hz = 12000.0f;
    free_running_options.route_count = 1;
    free_running_options.routes[0].destination = NK_AUDIO_DSP_MODULATION_DESTINATION_AMPLITUDE;
    free_running_options.routes[0].polarity = NK_AUDIO_DSP_MODULATION_UNIPOLAR;
    free_running_options.routes[0].amount = -1.0f;
    nk_audio_dsp_patch free_running_patch = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_patch_create(&free_running_options, &free_running_patch) == NK_OK);
    nk_audio_dsp_instrument free_running_instrument = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_instrument_create_from_patch(engine, free_running_patch,
                                                     &free_running_instrument) == NK_OK);
    assert(nk_audio_dsp_patch_destroy(free_running_patch) == NK_OK);

    target.frame_count = 1;
    target.sample_count = 1;
    nk_audio_dsp_event free_running_note_on = note_on;
    free_running_note_on.instrument = free_running_instrument;
    free_running_note_on.voice_id = 2;
    assert(nk_audio_dsp_engine_render(engine, &target, &free_running_note_on, 1) == NK_OK);
    assert(all_silent(samples, 1));
    assert(nk_audio_dsp_engine_render(engine, &target, nullptr, 0) == NK_OK);
    assert(all_silent(samples, 1));
    assert(nk_audio_dsp_engine_render(engine, &target, nullptr, 0) == NK_OK);
    assert(contains_signal(samples, 1));
    assert(nk_audio_dsp_engine_render(engine, &target, &free_running_note_on, 1) == NK_OK);
    assert(contains_signal(samples, 1));
    nk_audio_dsp_event free_running_note_off{};
    free_running_note_off.struct_size = sizeof(free_running_note_off);
    free_running_note_off.kind = NK_AUDIO_DSP_EVENT_NOTE_OFF;
    free_running_note_off.frame_offset = 0;
    free_running_note_off.voice_id = 2;
    assert(nk_audio_dsp_engine_render(engine, &target, &free_running_note_off, 1) == NK_OK);
    assert(nk_audio_dsp_engine_render(engine, &target, nullptr, 0) == NK_OK);
    assert(all_silent(samples, 1));

    auto retrigger_options = free_running_options;
    retrigger_options.lfo.mode = NK_AUDIO_DSP_LFO_RETRIGGER;
    nk_audio_dsp_patch retrigger_patch = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_patch_create(&retrigger_options, &retrigger_patch) == NK_OK);
    nk_audio_dsp_instrument retrigger_instrument = NK_INVALID_HANDLE;
    assert(nk_audio_dsp_instrument_create_from_patch(engine, retrigger_patch,
                                                     &retrigger_instrument) == NK_OK);
    assert(nk_audio_dsp_patch_destroy(retrigger_patch) == NK_OK);
    nk_audio_dsp_event retrigger_note_on = free_running_note_on;
    retrigger_note_on.instrument = retrigger_instrument;
    retrigger_note_on.voice_id = 3;
    assert(nk_audio_dsp_engine_render(engine, &target, &retrigger_note_on, 1) == NK_OK);
    assert(all_silent(samples, 1));
    assert(nk_audio_dsp_engine_render(engine, &target, nullptr, 0) == NK_OK);
    assert(all_silent(samples, 1));
    assert(nk_audio_dsp_engine_render(engine, &target, nullptr, 0) == NK_OK);
    assert(contains_signal(samples, 1));
    assert(nk_audio_dsp_engine_render(engine, &target, &retrigger_note_on, 1) == NK_OK);
    assert(all_silent(samples, 1));

    assert(nk_audio_dsp_engine_reset(engine) == NK_OK);
    assert(nk_audio_dsp_instrument_destroy(instrument) == NK_OK);
    assert(nk_audio_dsp_instrument_destroy(instrument) == NK_ERROR_INVALID_HANDLE);
    assert(nk_audio_dsp_instrument_destroy(legacy_instrument) == NK_OK);
    assert(nk_audio_dsp_instrument_destroy(second_instrument) == NK_OK);
    assert(nk_audio_dsp_instrument_destroy(free_running_instrument) == NK_OK);
    assert(nk_audio_dsp_instrument_destroy(retrigger_instrument) == NK_OK);
    assert(nk_audio_dsp_engine_destroy(second_engine) == NK_OK);
    assert(nk_audio_dsp_engine_destroy(engine) == NK_OK);
    assert(nk_audio_dsp_engine_destroy(engine) == NK_ERROR_INVALID_HANDLE);
    nk_shutdown();
    return 0;
}
