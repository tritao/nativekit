#include "nativekit_audio_dsp.h"
#include "nativekit_audio.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <vector>

extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_use_offline_device();
extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_render(float *, uint32_t);
namespace {
nk_audio_dsp_patch_options options() {
    nk_audio_dsp_patch_options p{};
    p.struct_size = sizeof(p);
    p.oscillator_count = 1;
    p.oscillators[0].struct_size = sizeof(p.oscillators[0]);
    p.oscillators[0].waveform = NK_AUDIO_DSP_WAVEFORM_SAW;
    p.oscillators[0].level = 1;
    p.noise.struct_size = sizeof(p.noise);
    p.envelope.struct_size = sizeof(p.envelope);
    p.envelope.attack_seconds = 0.005f;
    p.envelope.sustain_level = 1;
    p.envelope.release_seconds = 0.05f;
    p.filter.struct_size = sizeof(p.filter);
    p.filter.type = NK_AUDIO_DSP_FILTER_SVF_LOW_PASS;
    p.filter.cutoff_hz = 500;
    p.filter.resonance = 0.1f;
    p.lfo.struct_size = sizeof(p.lfo);
    p.gain = 0.25f;
    return p;
}
nk_audio_dsp_voice_options advanced() {
    nk_audio_dsp_voice_options v{};
    v.struct_size = sizeof(v);
    v.filter_drive = 0.5f;
    v.unison_count = 5;
    v.unison_detune_cents = 12;
    v.unison_stereo_spread = 0.9f;
    v.filter_envelope.struct_size = sizeof(v.filter_envelope);
    v.filter_envelope.attack_seconds = 0.1f;
    v.filter_envelope.decay_seconds = 0.1f;
    v.filter_envelope.sustain_level = 0.2f;
    v.filter_envelope.release_seconds = 0.05f;
    return v;
}
std::vector<float> render(nk_audio_dsp_patch patch, uint32_t channels = 2, uint32_t block = 256,
                          bool attached = false, bool retrigger = false, uint32_t repeat_interval = 0,
                          nk_audio_dsp_patch replacement_patch = NK_INVALID_HANDLE) {
    nk_audio_dsp_engine_options e{};
    e.struct_size = sizeof(e); e.sample_rate = 48000; e.channels = channels;
    e.block_size = 256; e.max_voices = 4;
    nk_audio_dsp_engine engine = 0;
    assert(nk_audio_dsp_engine_create(&e, &engine) == NK_OK);
    nk_audio_dsp_instrument instrument = 0;
    assert(nk_audio_dsp_instrument_create_from_patch(engine, patch, &instrument) == NK_OK);
    nk_audio_dsp_event note{};
    note.struct_size = sizeof(note); note.kind = NK_AUDIO_DSP_EVENT_NOTE_ON;
    note.instrument = instrument; note.voice_id = 1; note.note = 57; note.velocity = 1;
    nk_audio_dsp_event off = note; off.kind = retrigger ? NK_AUDIO_DSP_EVENT_NOTE_ON : NK_AUDIO_DSP_EVENT_NOTE_OFF;
    nk_audio_dsp_instrument replacement = NK_INVALID_HANDLE;
    if (replacement_patch != NK_INVALID_HANDLE) {
        assert(retrigger);
        assert(nk_audio_dsp_instrument_create_from_patch(engine, replacement_patch, &replacement) == NK_OK);
        off.instrument = replacement;
    }
    constexpr uint32_t frames = 40000, release = 16384;
    if (attached) {
        assert(channels == 2);
        uint64_t now = 0;
        assert(nk_audio_get_time_pcm_frames(&now) == NK_OK);
        assert(nk_audio_dsp_engine_attach_device(engine) == NK_OK);
        off.frame_offset = release;
        std::vector<nk_audio_dsp_event> events{note};
        if (repeat_interval > 0) {
            for (uint32_t frame = repeat_interval; frame < release; frame += repeat_interval) {
                auto repeated = note; repeated.frame_offset = frame; events.push_back(repeated);
            }
        }
        events.push_back(off);
        // Schedule ahead of any suffix already cached by the mixer graph. This also
        // checks a newly attached source after another source has been detached.
        assert(nk_audio_dsp_engine_schedule(engine, now + 1024, events.data(), static_cast<uint32_t>(events.size())) == NK_OK);
        std::vector<float> warmup(1024 * channels);
        assert(nk_audio_test_render(warmup.data(), 1024) == NK_OK);
    }
    std::vector<float> output(frames * channels);
    for (uint32_t start = 0; start < frames;) {
        // Stop a block at release to vary partitioning without changing event time.
        auto count = std::min(block, frames - start);
        if (start < release) {
            count = std::min(count, release - start);
            if (repeat_interval > 0)
                count = std::min(count, repeat_interval - start % repeat_interval);
        }
        if (attached) {
            assert(nk_audio_test_render(output.data() + start * channels, count) == NK_OK);
        } else {
            nk_audio_dsp_render_target t{};
            t.struct_size = sizeof(t); t.samples = output.data() + start * channels;
            t.sample_count = count * channels; t.frame_count = count; t.channels = channels;
            auto *event = start == 0 ? &note : start == release ? &off :
                repeat_interval > 0 && start < release && start % repeat_interval == 0 ? &note : nullptr;
            assert(nk_audio_dsp_engine_render(engine, &t, event, event ? 1 : 0) == NK_OK);
        }
        start += count;
    }
    if (attached) assert(nk_audio_dsp_engine_detach_device(engine) == NK_OK);
    assert(nk_audio_dsp_instrument_destroy(instrument) == NK_OK);
    if (replacement != NK_INVALID_HANDLE)
        assert(nk_audio_dsp_instrument_destroy(replacement) == NK_OK);
    assert(nk_audio_dsp_engine_destroy(engine) == NK_OK);
    return output;
}
nk_audio_dsp_patch create(const nk_audio_dsp_patch_options &p, const nk_audio_dsp_voice_options *v) {
    nk_audio_dsp_patch patch = 0;
    assert(nk_audio_dsp_patch_create_extended(&p, v, &patch) == NK_OK);
    return patch;
}
double energy(const std::vector<float> &samples, size_t begin, size_t end) {
    double sum = 0;
    for (size_t i = begin; i < end; ++i) sum += samples[i] * samples[i];
    return sum;
}
}
int main() {
    nk_init_options init{}; init.struct_size = sizeof(init); init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    assert(nk_audio_test_use_offline_device() == NK_OK);
    auto p = options(); auto v = advanced();
    nk_audio_dsp_patch legacy = 0;
    assert(nk_audio_dsp_patch_create(&p, &legacy) == NK_OK);
    auto defaults = v; defaults.unison_count = 1;
    auto single = create(p, &defaults);
    const auto old_signal = render(legacy);
    assert(old_signal == render(single)); // Advanced defaults retain the old sound exactly.
    defaults.unison_count = 0;
    auto zero = create(p, &defaults);
    assert(old_signal == render(zero));
    auto stereo_patch = create(p, &v);
    auto stereo = render(stereo_patch);
    assert(stereo == render(stereo_patch, 2, 73)); // Sample-exact across block boundaries.
    auto mono = render(stereo_patch, 1);
    auto surround = render(stereo_patch, 4);
    double width = 0;
    for (size_t i = 0; i < mono.size(); ++i) {
        assert(std::abs(mono[i] - (stereo[i*2] + stereo[i*2+1])*0.5f) < 1e-7f);
        assert(surround[i*4] == stereo[i*2] && surround[i*4+1] == stereo[i*2+1]);
        assert(surround[i*4+2] == mono[i] && surround[i*4+3] == mono[i]);
        width += std::abs(stereo[i*2] - stereo[i*2+1]);
    }
    assert(width > 1);
    assert(energy(stereo, stereo.size()-2000, stereo.size()) == 0); // Release ends the voice.
    auto clean_options = defaults; clean_options.unison_count = 1; clean_options.filter_drive = 0;
    auto clean_patch = create(p, &clean_options);
    auto clean_signal = render(clean_patch);
    assert(clean_signal != old_signal); // Drive reaches the filter after note-on resets.
    assert(clean_signal == render(clean_patch, 2, 73)); // Retriggers retain the configured drive.
    nk_audio_dsp_patch_destroy(clean_patch);
    // The device callback must preserve stereo, just like offline rendering.
    const auto live = render(stereo_patch, 2, 256, true);
    assert(live == stereo);
    for (uint32_t count = 1; count <= 8; ++count) {
        auto u = v; u.unison_count = count; u.unison_stereo_spread = 0;
        auto patch = create(p, &u); const auto signal = render(patch);
        for (size_t i = 0; i < signal.size(); i += 2) {
            assert(signal[i] == signal[i+1]);
            assert(std::isfinite(signal[i]) && std::abs(signal[i]) < 0.5f);
        }
        nk_audio_dsp_patch_destroy(patch);
    }
    // A slow brightness rise occurs while amplitude is already at full sustain.
    defaults.unison_count = 1; defaults.filter_envelope_amount_semitones = 36;
    auto sweep = create(p, &defaults); const auto swept = render(sweep);
    assert(energy(swept, 8000, 12000) > energy(old_signal, 8000, 12000)*1.1);
    assert(swept != old_signal);
    // Zero sweep bypasses its ADSR regardless of its timing.
    defaults.filter_envelope_amount_semitones = 0;
    defaults.filter_envelope.attack_seconds = 10;
    auto disabled = create(p, &defaults); assert(render(disabled) == old_signal);
    nk_audio_dsp_source_options source{};
    assert(nk_audio_dsp_source_defaults(NK_AUDIO_DSP_SOURCE_HARMONIC, &source) == NK_OK);
    auto source_options = p; source_options.oscillators[0].waveform = NK_AUDIO_DSP_WAVEFORM_SINE;
    nk_audio_dsp_patch invalid = 123;
    assert(nk_audio_dsp_patch_create_source_extended(&source_options, &v, &source, nullptr, 0, &invalid)
           == NK_ERROR_INVALID_ARGUMENT && invalid == NK_INVALID_HANDLE);
    defaults.filter_envelope_amount_semitones = 12;
    assert(nk_audio_dsp_patch_create_source_extended(&source_options, &defaults, &source, nullptr, 0,
                                                     &invalid) == NK_OK);
    assert(energy(render(invalid), 0, 12000) > 0);
    nk_audio_dsp_patch_destroy(invalid);
    auto reject = [&](const nk_audio_dsp_voice_options &value) {
        invalid = 123;
        assert(nk_audio_dsp_patch_create_extended(&p, &value, &invalid) == NK_ERROR_INVALID_ARGUMENT);
        assert(invalid == NK_INVALID_HANDLE);
    };
    auto bad = v; bad.unison_count = 9; reject(bad);
    bad = v; bad.unison_detune_cents = -1; reject(bad);
    bad = v; bad.unison_detune_cents = 101; reject(bad);
    bad = v; bad.unison_stereo_spread = 1.1f; reject(bad);
    bad = v; bad.filter_envelope_amount_semitones = 97; reject(bad);
    bad = v; bad.filter_envelope.sustain_level = 2; reject(bad);
    bad = v; bad.filter_envelope.struct_size = 0; reject(bad);
    bad = v; bad.struct_size = 0; reject(bad);
    bad = v; bad.unison_detune_cents = std::numeric_limits<float>::quiet_NaN(); reject(bad);
    bad = v; bad.filter_envelope.attack_seconds = std::numeric_limits<float>::infinity(); reject(bad);
    bad = v; bad.filter_drive = -1; reject(bad);
    bad = v; bad.filter_drive = 1.1f; reject(bad);
    bad = v; bad.filter_drive = std::numeric_limits<float>::quiet_NaN(); reject(bad);
    // The v2 prefix must ignore the newer drive field, including invalid contents.
    auto v2 = advanced(); v2.unison_count = 1;
    v2.struct_size = offsetof(nk_audio_dsp_voice_options, filter_drive);
    v2.filter_drive = std::numeric_limits<float>::quiet_NaN();
    auto v2_voice = create(p, &v2); assert(render(v2_voice) == old_signal);
    nk_audio_dsp_patch_destroy(v2_voice);
    auto partial_v3 = v2; partial_v3.struct_size++; reject(partial_v3);
    // The committed v1 prefix remains accepted and ignores appended fields entirely.
    auto v1 = advanced(); v1.unison_count = 1;
    v1.struct_size = offsetof(nk_audio_dsp_voice_options, additional_lfos);
    v1.additional_lfo_count = 99; // Must not be inspected with the old size.
    auto old_voice = create(p, &v1); assert(render(old_voice) == old_signal);
    nk_audio_dsp_patch_destroy(old_voice);
    auto truncated = v1; truncated.struct_size++; reject(truncated);

    auto moving = advanced(); moving.unison_count = 1;
    moving.additional_lfo_count = 2;
    for (uint32_t i = 0; i < 2; ++i) {
        auto &lfo = moving.additional_lfos[i]; lfo.struct_size = sizeof(lfo);
        lfo.rate_hz = i == 0 ? 7 : 11; lfo.phase = 0.2f;
    }
    auto motion = p; motion.lfo.rate_hz = 5; motion.lfo.phase = 0.1f;
    motion.route_count = 3;
    for (uint32_t i = 0; i < 3; ++i) {
        auto &r = motion.routes[i]; r.struct_size = sizeof(r);
        r.source = i == 0 ? NK_AUDIO_DSP_MODULATION_SOURCE_LFO :
                   i == 1 ? NK_AUDIO_DSP_MODULATION_SOURCE_LFO2 : NK_AUDIO_DSP_MODULATION_SOURCE_LFO3;
        r.destination = i == 0 ? NK_AUDIO_DSP_MODULATION_DESTINATION_PITCH_SEMITONES :
                        i == 1 ? NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_SEMITONES :
                                 NK_AUDIO_DSP_MODULATION_DESTINATION_PAN;
        r.amount = i == 0 ? 0.3f : i == 1 ? 12.0f : 0.7f;
    }
    auto motion_patch = create(motion, &moving); const auto moving_signal = render(motion_patch);
    assert(moving_signal == render(motion_patch, 2, 71));
    assert(moving_signal == render(motion_patch, 2, 256, true));
    for (uint32_t i = 0; i < 3; ++i) {
        auto isolated = motion; isolated.routes[i].amount = 0;
        auto patch = create(isolated, &moving);
        assert(render(patch) != moving_signal); nk_audio_dsp_patch_destroy(patch);
    }
    auto restarted = render(motion_patch, 2, 256, false, true);
    // Reuse is continuous in both channels, then converges to the restarted
    // oscillator/envelope/LFO phase once the five-millisecond correction ends.
    for (uint32_t channel = 0; channel < 2; ++channel)
        assert(std::abs(restarted[16384*2 + channel] - restarted[16383*2 + channel]) < 1e-7f);
    assert(std::equal(restarted.begin()+480, restarted.begin()+4096,
                      restarted.begin()+16384*2+480));
    assert(restarted == render(motion_patch, 2, 71, false, true));
    assert(restarted == render(motion_patch, 2, 256, true, true));
    // Switching instruments on a reused voice retains stereo continuity too.
    const auto switched = render(motion_patch, 2, 256, false, true, 0, legacy);
    for (uint32_t channel = 0; channel < 2; ++channel)
        assert(std::abs(switched[16384*2+channel] - switched[16383*2+channel]) < 1e-7f);
    assert(switched == render(motion_patch, 2, 71, false, true, 0, legacy));
    assert(switched == render(motion_patch, 2, 256, true, true, 0, legacy));
    for (const auto value : switched) assert(std::isfinite(value) && std::abs(value) < 1);
    // Include reuses faster than the continuity bridge, and immediate releases.
    auto rapid_options = motion; rapid_options.envelope.release_seconds = 0;
    auto rapid_patch = create(rapid_options, &moving);
    for (uint32_t interval : {64u, 1024u}) {
        const auto repeated = render(rapid_patch, 2, 256, false, false, interval);
        assert(repeated == render(rapid_patch, 2, 71, false, false, interval));
        assert(repeated == render(rapid_patch, 2, 256, true, false, interval));
        for (uint32_t frame = interval; frame < 16384; frame += interval)
            for (uint32_t channel = 0; channel < 2; ++channel)
                assert(std::abs(repeated[frame*2+channel] - repeated[(frame-1)*2+channel]) < 1e-7f);
        for (const auto value : repeated) assert(std::isfinite(value) && std::abs(value) < 1);
        assert(energy(repeated, repeated.size()-2000, repeated.size()) == 0);
    }
    assert(nk_audio_dsp_patch_destroy(rapid_patch) == NK_OK);
    auto running = moving; running.additional_lfos[0].mode = NK_AUDIO_DSP_LFO_FREE_RUNNING;
    auto running_patch = create(motion, &running);
    auto continuous = render(running_patch, 2, 256, false, true);
    assert(!std::equal(continuous.begin(), continuous.begin()+4096, continuous.begin()+16384*2));
    // Multiple routes sharing a source advance that LFO only once per frame.
    auto shared = motion; shared.route_count = 1;
    shared.routes[0].source = NK_AUDIO_DSP_MODULATION_SOURCE_LFO2;
    shared.routes[0].destination = NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_SEMITONES;
    shared.routes[0].amount = 12;
    auto one_route = create(shared, &moving);
    shared.route_count = 2; shared.routes[1] = shared.routes[0];
    shared.routes[0].amount = shared.routes[1].amount = 6;
    auto two_routes = create(shared, &moving); assert(render(one_route) == render(two_routes));
    // FilterEnvelope works with zero direct sweep, and matches the explicit sweep path.
    auto direct_options = moving; direct_options.filter_envelope_amount_semitones = 24;
    auto direct = create(p, &direct_options);
    auto routed_options = p; routed_options.route_count = 1;
    auto &r = routed_options.routes[0]; r.struct_size = sizeof(r);
    r.source = NK_AUDIO_DSP_MODULATION_SOURCE_FILTER_ENVELOPE;
    r.destination = NK_AUDIO_DSP_MODULATION_DESTINATION_FILTER_CUTOFF_SEMITONES;
    r.polarity = NK_AUDIO_DSP_MODULATION_UNIPOLAR; r.amount = 24;
    auto routed = create(routed_options, &moving); assert(render(direct) == render(routed));
    r.destination = NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_LEVEL;
    r.oscillator_index = 1; r.amount = -0.75f;
    auto filter_level = create(routed_options, &moving);
    r.source = NK_AUDIO_DSP_MODULATION_SOURCE_ENVELOPE;
    auto amp_level = create(routed_options, &moving);
    assert(energy(render(filter_level), 1000, 3000) > energy(render(amp_level), 1000, 3000)*1.5);
    // Stereo balance clamps at its endpoints and never boosts either channel.
    auto pan_options = p; pan_options.route_count = 1;
    pan_options.routes[0] = motion.routes[2]; pan_options.routes[0].amount = 2;
    auto stationary = moving; stationary.additional_lfos[1].waveform = NK_AUDIO_DSP_WAVEFORM_SQUARE;
    stationary.additional_lfos[1].rate_hz = 0; stationary.additional_lfos[1].phase = 0;
    auto panned = create(pan_options, &stationary); const auto right_only = render(panned);
    for (size_t i = 0; i < right_only.size(); i += 2) {
        assert(right_only[i] == 0); assert(right_only[i+1] == old_signal[i+1]);
    }
    auto missing = moving; missing.additional_lfo_count = 1;
    invalid = 123;
    assert(nk_audio_dsp_patch_create_extended(&motion, &missing, &invalid) == NK_ERROR_INVALID_ARGUMENT);
    assert(invalid == NK_INVALID_HANDLE);
    assert(nk_audio_dsp_patch_create(&motion, &invalid) == NK_ERROR_INVALID_ARGUMENT);
    missing = moving; missing.additional_lfo_count = 3; reject(missing);
    missing = moving; missing.additional_lfos[0].struct_size = 0; reject(missing);
    missing = moving; missing.additional_lfos[0].rate_hz = -1; reject(missing);
    missing = moving; missing.additional_lfos[0].phase = std::numeric_limits<float>::quiet_NaN(); reject(missing);
    for (auto patch : {motion_patch, running_patch, one_route, two_routes, direct, routed,
                       filter_level, amp_level, panned}) assert(nk_audio_dsp_patch_destroy(patch) == NK_OK);

    // Wavetables and operator modulation also run independently in each copy.
    std::vector<float> cycle(128);
    for (size_t i = 0; i < cycle.size(); ++i) cycle[i] = std::sin(6.283185307*i/cycle.size());
    nk_audio_dsp_wavetable table = 0;
    assert(nk_audio_dsp_wavetable_create(cycle.data(), cycle.size(), &table) == NK_OK);
    p.oscillators[0].wavetable = table;
    p.oscillator_count = 2; p.oscillators[1] = p.oscillators[0];
    p.oscillators[1].wavetable = 0; p.oscillators[1].level = 0.25f;
    p.oscillators[1].detune_cents = 1200;
    p.route_count = 1; auto &route = p.routes[0]; route.struct_size = sizeof(route);
    route.source = NK_AUDIO_DSP_MODULATION_SOURCE_OSCILLATOR;
    route.source_oscillator_index = 2; route.oscillator_index = 1;
    route.destination = NK_AUDIO_DSP_MODULATION_DESTINATION_OSCILLATOR_PHASE;
    route.amount = 0.1f;
    auto operators = create(p, &v);
    assert(render(operators) == render(operators, 2, 37));
    assert(energy(render(operators), 0, 16000) > 0);
    nk_audio_dsp_wavetable_destroy(table);
    for (auto patch : {legacy, single, zero, stereo_patch, sweep, disabled, operators})
        assert(nk_audio_dsp_patch_destroy(patch) == NK_OK);
    nk_shutdown();
    std::puts("PASS: stereo unison, filter ADSR routing, three LFOs, stereo balance, callback parity and ABI compatibility");
}
