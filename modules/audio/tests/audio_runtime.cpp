#include "nativekit_audio_dsp.h"
#include "nativekit_audio.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_use_offline_device();
extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_render(float *, uint32_t);
extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_dsp_process(nk_audio_dsp_engine, float *, uint32_t, uint64_t);

static nk_audio_dsp_engine engine(uint32_t voices = 8) {
    nk_audio_dsp_engine_options options{}; options.struct_size = sizeof(options);
    options.sample_rate = 48000; options.channels = 2; options.block_size = 256;
    options.max_voices = voices;
    nk_audio_dsp_engine result = 0;
    assert(nk_audio_dsp_engine_create(&options, &result) == NK_OK);
    return result;
}
static nk_audio_dsp_instrument instrument(nk_audio_dsp_engine owner) {
    nk_audio_dsp_patch_options p{}; p.struct_size = sizeof(p);
    p.oscillator_count = 1; p.oscillators[0].struct_size = sizeof(p.oscillators[0]);
    p.oscillators[0].waveform = NK_AUDIO_DSP_WAVEFORM_SINE; p.oscillators[0].level = 1;
    p.noise.struct_size = sizeof(p.noise); p.filter.struct_size = sizeof(p.filter);
    p.envelope.struct_size = sizeof(p.envelope); p.envelope.attack_seconds = 0.001f;
    p.envelope.sustain_level = 1; p.envelope.release_seconds = 0.002f;
    p.lfo.struct_size = sizeof(p.lfo); p.gain = 0.2f;
    nk_audio_dsp_patch patch = 0; nk_audio_dsp_instrument result = 0;
    assert(nk_audio_dsp_patch_create(&p, &patch) == NK_OK);
    assert(nk_audio_dsp_instrument_create_from_patch(owner, patch, &result) == NK_OK);
    assert(nk_audio_dsp_patch_destroy(patch) == NK_OK);
    return result;
}
static std::vector<float> render(nk_audio_dsp_engine e, uint32_t count) {
    if (count > 256) {
        std::vector<float> output;
        for (uint32_t start = 0; start < count; start += 256) {
            auto block = render(e, std::min(256u, count-start));
            output.insert(output.end(), block.begin(), block.end());
        }
        return output;
    }
    std::vector<float> samples(count * 2);
    nk_audio_dsp_render_target target{}; target.struct_size = sizeof(target);
    target.samples = samples.data(); target.sample_count = samples.size();
    target.channels = 2; target.frame_count = count;
    assert(nk_audio_dsp_engine_render(e, &target, nullptr, 0) == NK_OK);
    for (float value : samples) assert(std::isfinite(value));
    return samples;
}
static nk_audio_dsp_runtime_status status(nk_audio_dsp_engine e) {
    nk_audio_dsp_runtime_status s{}; s.struct_size = sizeof(s);
    assert(nk_audio_dsp_engine_get_runtime_status(e, &s) == NK_OK); return s;
}
static uint64_t note(nk_audio_dsp_engine e, nk_audio_dsp_instrument i, uint64_t at,
                     uint64_t hold = 0, int32_t priority = 0) {
    nk_audio_dsp_note_options options{}; options.struct_size = sizeof(options);
    options.note = 60; options.velocity = 1; options.start_frame = at;
    options.hold_frames = hold; options.priority = priority;
    uint64_t token = 0;
    assert(nk_audio_dsp_engine_note_on(e, i, &options, &token) == NK_OK);
    assert(token); return token;
}
static void tokens_and_stealing() {
    auto e = engine(1); auto i = instrument(e);
    nk_audio_dsp_voice_policy_options policy{sizeof(policy), 1, 1, 240};
    assert(nk_audio_dsp_engine_set_voice_policy(e, &policy) == NK_OK);
    const auto old = note(e, i, 0, 0, 10); render(e, 1000);
    uint32_t voices=99, tails=99;
    assert(nk_audio_dsp_engine_get_voice_counts(e,&voices,&tails)==NK_OK && voices==1 && tails==0);
    assert(nk_audio_dsp_engine_get_voice_counts(e,nullptr,&tails)==NK_ERROR_INVALID_ARGUMENT);
    note(e, i, 1000, 0, 5); render(e, 1); // Lower priority cannot replace the held voice.
    assert(status(e).dropped_notes == 1);
    const auto replacement = note(e, i, 1001, 0, 20);
    assert(replacement != old);
    auto bridge = render(e, 1);
    assert(nk_audio_dsp_engine_get_voice_counts(e,&voices,&tails)==NK_OK && voices==1 && tails==1);
    auto remainder = render(e, 239);
    bridge.insert(bridge.end(),remainder.begin(),remainder.end());
    assert(nk_audio_dsp_engine_get_voice_counts(e,&voices,&tails)==NK_OK && voices==1 && tails==0);
    assert(status(e).stolen_voices == 1);
    for (uint32_t frame = 1; frame < 240; ++frame)
        assert(std::abs(bridge[frame*2] - bridge[(frame-1)*2]) < 0.02f);
    assert(nk_audio_dsp_engine_note_off(e, old, 1241) == NK_OK);
    const auto held = render(e, 2048);
    assert(std::any_of(held.begin(), held.end(), [](float x) { return std::abs(x) > 0.1f; }));
    assert(nk_audio_dsp_engine_note_off(e, replacement, 3289) == NK_OK);
    auto ended = render(e, 2048);
    assert(std::all_of(ended.end()-256, ended.end(), [](float x) { return x == 0; }));
    assert(nk_audio_dsp_engine_get_voice_counts(e,&voices,&tails)==NK_OK && voices==0 && tails==0);
    // Future commands may arrive out of time order; the bounded heap orders them.
    const auto future = note(e, i, 10000, 500);
    assert(nk_audio_dsp_engine_note_off(e, future, 10300) == NK_OK);
    note(e, i, 7000, 200);
    render(e, 10000);
    assert(nk_audio_dsp_instrument_destroy(i) == NK_OK);
    assert(nk_audio_dsp_engine_collect(e) == NK_OK);
    assert(nk_audio_dsp_engine_destroy(e) == NK_OK);
}
static uint64_t program(nk_audio_dsp_engine e, nk_audio_dsp_instrument i, double length_delta=0) {
    nk_audio_dsp_transport_event events[] = {
        {0,2,0,0,0,0,88}, {0,0,1,i,60,1,0}, {900.2,1,1,0,0,0,0},
        {0,2,0,0,0,0,120}, {0,0,1,i,67,1,0}, {1900.2,1,1,0,0,0,0}
    };
    nk_audio_dsp_transport_section sections[] = {
        {1000.3,0,3,0,2}, {2000.3,3,3,2,2}
    };
    sections[0].length_frames += length_delta;
    double bars[] = {0,500.15,0,1000.15}; uint64_t id = 0;
    assert(nk_audio_dsp_engine_prepare_transport(e, sections, 2, events, 6, bars, 4, &id) == NK_OK);
    assert(id); return id;
}
static void transport_position() {
    auto e=engine();auto i=instrument(e);auto id=program(e,i);
    uint32_t section=99;double frame=-1;
    assert(nk_audio_dsp_engine_get_transport_position(e,nullptr,&frame)==NK_ERROR_INVALID_ARGUMENT);
    assert(nk_audio_dsp_engine_play_transport(e,id,0,400)==NK_OK);
    render(e,200);
    assert(nk_audio_dsp_engine_get_transport_position(e,&section,&frame)==NK_OK);
    assert(section==0 && frame==0);
    render(e,700);
    assert(nk_audio_dsp_engine_get_transport_position(e,&section,&frame)==NK_OK);
    assert(section==0 && std::abs(frame-500)<0.001);
    render(e,1200);
    assert(nk_audio_dsp_engine_get_transport_position(e,&section,&frame)==NK_OK);
    assert(section==0 && std::abs(frame-699.7)<0.001);
    assert(nk_audio_dsp_engine_request_section(e,1)==NK_OK);
    render(e,1000);
    assert(nk_audio_dsp_engine_get_transport_position(e,&section,&frame)==NK_OK);
    assert(section==1 && frame>=0 && frame<2000.3);
    assert(nk_audio_dsp_engine_destroy(e)==NK_OK);
}
static std::vector<float> sequence(uint32_t block, bool attached) {
    auto e = engine(); auto i = instrument(e); auto id = program(e, i);
    uint64_t start = 0;
    if (attached) {
        assert(nk_audio_get_time_pcm_frames(&start) == NK_OK);
        start += 1024;
        assert(nk_audio_dsp_engine_attach_device(e) == NK_OK);
    }
    assert(nk_audio_dsp_engine_play_transport(e, id, 0, start) == NK_OK);
    if (attached) {
        std::vector<float> warmup(2048);
        assert(nk_audio_test_render(warmup.data(), 1024) == NK_OK);
    }
    std::vector<float> output;
    for (uint32_t frame = 0; frame < 40000;) {
        const auto count = std::min(block, 40000-frame);
        std::vector<float> samples;
        if (attached) {
            samples.resize(count*2);
            assert(nk_audio_test_render(samples.data(), count) == NK_OK);
        } else samples = render(e, count);
        output.insert(output.end(), samples.begin(), samples.end()); frame += count;
    }
    assert(status(e).dropped_notes == 0 && status(e).bpm == 88);
    if (attached) assert(nk_audio_dsp_engine_detach_device(e) == NK_OK);
    assert(nk_audio_dsp_instrument_destroy(i) == NK_OK);
    assert(nk_audio_dsp_engine_destroy(e) == NK_OK);
    return output;
}
static void transitions_and_retirement() {
    auto e = engine(); auto i = instrument(e); auto first = program(e, i);
    assert(nk_audio_dsp_engine_play_transport(e, first, 0, 0) == NK_OK);
    render(e, 100);
    assert(nk_audio_dsp_engine_request_section(e, 1) == NK_OK);
    render(e, 401);
    assert(status(e).section == 1 && status(e).bpm == 120);
    assert(nk_audio_dsp_engine_retire_transport(e, first) == NK_OK);
    assert(status(e).retired_resources == 1); // Playing resources remain pinned.
    const auto next = program(e, i);
    assert(nk_audio_dsp_engine_play_transport(e, next, 0, 501) == NK_OK);
    render(e, 256);
    assert(nk_audio_dsp_engine_collect(e) == NK_OK);
    assert(status(e).retired_resources == 0);
    assert(nk_audio_dsp_engine_play_transport(e, first, 0, 800) != NK_OK);
    assert(nk_audio_dsp_engine_stop_transport(e) == NK_OK);
    assert(nk_audio_dsp_engine_retire_transport(e, next) == NK_OK);
    render(e, 2048);
    assert(status(e).playing == 0 && status(e).retired_resources == 0);
    assert(nk_audio_dsp_instrument_destroy(i) == NK_OK);
    assert(nk_audio_dsp_engine_destroy(e) == NK_OK);
}
static void group_publication() {
    auto a=engine(); auto b=engine(); auto ia=instrument(a); auto ib=instrument(b);
    auto pa=program(a,ia); auto pb=program(b,ib);
    auto mismatch=program(b,ib,1);
    nk_audio_dsp_transport_track tracks[]={{a,pa},{b,mismatch}};
    assert(nk_audio_dsp_group_play_transport(tracks,2,0,0)!=NK_OK);
    assert(!status(a).playing && !status(b).playing);
    assert(nk_audio_dsp_engine_retire_transport(b,mismatch)==NK_OK);
    tracks[1].program_id=pb;
    assert(nk_audio_dsp_engine_attach_device(b)==NK_OK);
    assert(nk_audio_dsp_group_play_transport(tracks,2,0,0)!=NK_OK); // Different clock domains.
    assert(nk_audio_dsp_engine_detach_device(b)==NK_OK);
    assert(nk_audio_dsp_group_play_transport(tracks,2,0,0)==NK_OK);
    assert(render(a,100)==render(b,100));
    assert(status(a).loops==0 && status(b).loops==0);
    tracks[1].program_id=mismatch;
    assert(nk_audio_dsp_group_request_section(tracks,2,1)!=NK_OK); // A stale program cannot request a new one.
    tracks[1].program_id=pb;
    assert(nk_audio_dsp_group_request_section(tracks,2,1)==NK_OK);
    // A group uses one admission cutoff after publication, even if rendering
    // of the independently routed engines is interleaved differently.
    auto first=render(a,1000); auto second=render(b,1000);
    assert(first==second && status(a).section==1 && status(b).section==1);
    for (uint32_t i=0;i<64;++i) note(b,ib,100000+i,100);
    assert(nk_audio_dsp_group_request_section(tracks,2,0)==NK_ERROR_QUEUE_FULL);
    render(a,2000); render(b,2000);
    assert(status(a).section==1 && status(b).section==1); // Failed batch changed neither track.
    assert(nk_audio_dsp_engine_retire_transport(a,pa)==NK_OK);
    assert(nk_audio_dsp_engine_retire_transport(b,pb)==NK_OK);
    assert(nk_audio_dsp_engine_destroy(a)==NK_OK);
    assert(nk_audio_dsp_engine_destroy(b)==NK_OK);
    assert(nk_audio_dsp_instrument_destroy(ia)==NK_OK);
    assert(nk_audio_dsp_instrument_destroy(ib)==NK_OK);
}

static void cancellation_reset_and_clock_gaps() {
    auto e=engine(); auto i=instrument(e);
    auto token=note(e,i,1000);
    assert(nk_audio_dsp_engine_note_off(e,token,0)==NK_OK);
    auto cancelled=render(e,2000);
    assert(std::all_of(cancelled.begin(),cancelled.end(),[](float x){return x==0;}));
    note(e,i,5000);
    assert(nk_audio_dsp_engine_reset(e)==NK_OK);
    auto reset=render(e,6000);
    assert(std::all_of(reset.begin(),reset.end(),[](float x){return x==0;}));
    assert(nk_audio_dsp_engine_attach_device(e)==NK_OK);
    note(e,i,1000,100);
    std::vector<float> samples(512);
    assert(nk_audio_test_dsp_process(e,samples.data(),256,0)==NK_OK);
    std::fill(samples.begin(),samples.end(),0);
    assert(nk_audio_test_dsp_process(e,samples.data(),256,1500)==NK_OK);
    assert(std::all_of(samples.begin(),samples.end(),[](float x){return x==0;}));
    auto id=program(e,i);
    assert(nk_audio_dsp_engine_play_transport(e,id,0,2000)==NK_OK);
    assert(nk_audio_test_dsp_process(e,samples.data(),256,2000)==NK_OK);
    std::fill(samples.begin(),samples.end(),0);
    // A gap spanning hundreds of millions of loops must seek analytically.
    assert(nk_audio_test_dsp_process(e,samples.data(),256,1000000000000ULL)==NK_OK);
    auto state=status(e);
    assert(state.playing && state.loops>1000000 && state.bpm==88);
    assert(state.clock_discontinuities>=2 && state.dropped_notes==0);
    assert(nk_audio_test_dsp_process(e,samples.data(),256,0)==NK_OK);
    assert(!status(e).playing); // A reversed source clock stops the old program.
    assert(nk_audio_dsp_engine_detach_device(e)==NK_OK);
    assert(nk_audio_dsp_engine_reset(e)==NK_OK);
    assert(nk_audio_dsp_engine_retire_transport(e,id)==NK_OK);
    note(e,i,1000);
    assert(nk_audio_dsp_instrument_destroy(i)==NK_OK);
    assert(status(e).retired_resources>0);
    assert(nk_audio_dsp_engine_reset(e)==NK_OK);
    assert(nk_audio_dsp_engine_collect(e)==NK_OK && status(e).retired_resources==0);
    assert(nk_audio_dsp_engine_destroy(e)==NK_OK);
}

static std::vector<float> sample_playback(uint32_t block) {
    auto e=engine(2);
    nk_audio_dsp_sample_options options{}; options.struct_size=sizeof(options);
    options.sample_rate=48000; options.channels=2; options.root_note=60;
    options.loop_start_frame=0; options.loop_end_frame=100; options.gain=0.5f;
    options.envelope.struct_size=sizeof(options.envelope);
    options.envelope.attack_seconds=0.001f; options.envelope.sustain_level=1;
    options.envelope.release_seconds=0.001f;
    std::vector<float> pcm(200);
    for (uint32_t i=0;i<100;++i) { pcm[i*2]=0.5f*std::sin(i*6.2831853f/100); pcm[i*2+1]=-pcm[i*2]; }
    nk_audio_dsp_instrument sample=0;
    assert(nk_audio_dsp_instrument_create_from_sample(e,&options,pcm.data(),pcm.size(),&sample)==NK_OK);
    std::fill(pcm.begin(),pcm.end(),0); // Preparation owns immutable sample data.
    note(e,sample,0,1000); note(e,sample,300,500);
    assert(nk_audio_dsp_instrument_destroy(sample)==NK_OK);
    std::vector<float> output;
    for (uint32_t at=0;at<4000;) {
        auto count=std::min(block,4000-at); auto part=render(e,count);
        for (uint32_t i=0;i<count;++i) assert(part[i*2]==-part[i*2+1]);
        output.insert(output.end(),part.begin(),part.end()); at+=count;
    }
    assert(*std::max_element(output.begin(),output.end())>0.2f);
    assert(std::all_of(output.end()-256,output.end(),[](float x){return x==0;}));
    assert(status(e).retired_resources==0);
    assert(nk_audio_dsp_engine_destroy(e)==NK_OK);
    return output;
}

static void late_play_keeps_requested_origin() {
    auto e=engine(); auto i=instrument(e); auto id=program(e,i);
    render(e,600);
    assert(nk_audio_dsp_engine_play_transport(e,id,0,0)==NK_OK);
    auto output=render(e,1400);
    // Onset at zero is overdue; the next loop remains anchored at frame 1000.
    assert(std::all_of(output.begin(),output.begin()+800,[](float x){return x==0;}));
    assert(std::any_of(output.begin()+800,output.end(),[](float x){return std::abs(x)>0.1f;}));
    assert(status(e).loops==1 && status(e).bpm==88);
    assert(nk_audio_dsp_engine_destroy(e)==NK_OK);
    assert(nk_audio_dsp_instrument_destroy(i)==NK_OK);
}

static void choke_groups() {
    auto e = engine(); auto open = instrument(e); auto closed = instrument(e); auto kick = instrument(e);
    assert(nk_audio_dsp_instrument_set_choke_group(open, 1) == NK_OK);
    assert(nk_audio_dsp_instrument_set_choke_group(closed, 1) == NK_OK);
    assert(nk_audio_dsp_instrument_set_choke_group(open, 256) == NK_ERROR_INVALID_ARGUMENT);
    note(e, open, 0); note(e, kick, 0); render(e, 256);
    note(e, closed, 256); render(e, 4096);
    uint32_t voices = 0, tails = 0;
    assert(nk_audio_dsp_engine_get_voice_counts(e, &voices, &tails) == NK_OK && voices == 2);
    assert(status(e).dropped_notes == 0 && status(e).stolen_voices == 0);
    assert(nk_audio_dsp_engine_destroy(e) == NK_OK);
    assert(nk_audio_dsp_instrument_destroy(open) == NK_OK);
    assert(nk_audio_dsp_instrument_destroy(closed) == NK_OK);
    assert(nk_audio_dsp_instrument_destroy(kick) == NK_OK);
}

static void same_frame_choke_cancels_unheard_note() {
    auto e = engine(); auto first = instrument(e); auto second = instrument(e);
    assert(nk_audio_dsp_instrument_set_choke_group(first, 1) == NK_OK);
    assert(nk_audio_dsp_instrument_set_choke_group(second, 1) == NK_OK);
    note(e, first, 0);
    const auto token = note(e, second, 0);
    const auto output = render(e, 4096);
    uint32_t voices=0, tails=0;
    assert(nk_audio_dsp_engine_get_voice_counts(e,&voices,&tails)==NK_OK);
    assert(voices==1 && tails==0 && status(e).dropped_notes==0);
    assert(std::any_of(output.begin(),output.end(),[](float x) {return std::abs(x)>.01f;}));
    assert(nk_audio_dsp_engine_note_off(e,token,4096)==NK_OK);
    render(e,4096);
    assert(nk_audio_dsp_engine_get_voice_counts(e,&voices,&tails)==NK_OK && voices==0);
    assert(nk_audio_dsp_instrument_destroy(first)==NK_OK);
    assert(nk_audio_dsp_instrument_destroy(second)==NK_OK);
    assert(nk_audio_dsp_engine_destroy(e)==NK_OK);
}
int main() {
    nk_init_options init{}; init.struct_size = sizeof(init); init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    assert(nk_audio_test_use_offline_device() == NK_OK);
    transport_position();
    tokens_and_stealing(); choke_groups(); same_frame_choke_cancels_unheard_note();
    auto fixed = sequence(256,false);
    assert(fixed == sequence(73,false));
    assert(fixed == sequence(256,true));
    transitions_and_retirement();
    group_publication();
    cancellation_reset_and_clock_gaps();
    late_play_keeps_requested_origin();
    assert(sample_playback(256)==sample_playback(73));
    nk_shutdown();
}
