#include "nativekit_audio_dsp.h"
#include "nativekit_audio.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstring>
#include <chrono>
#include <cstdio>
extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_use_offline_device();
extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_render(float *, uint32_t);
extern "C" NKAUDIO_API nk_result NK_CALL nk_audio_test_dsp_process(nk_audio_dsp_engine, float *, uint32_t, uint64_t);
extern "C" NKAUDIO_API void NK_CALL nk_audio_test_soundfont_callback_counts(uint64_t *, uint64_t *);
static std::vector<uint8_t> fixture() {
    std::ifstream input(NK_SF2_FIXTURE, std::ios::binary);
    assert(input); return {std::istreambuf_iterator<char>(input),{}};
}
static nk_audio_soundfont load(const std::vector<uint8_t> &bytes) {
    nk_audio_soundfont font=0;
    assert(nk_audio_soundfont_load_memory(bytes.data(),bytes.size(),&font)==NK_OK); return font;
}
static std::vector<float> playback(uint32_t block, bool attached, bool overlap) {
    const auto bytes=fixture(); auto font=load(bytes);
    uint32_t count=0; assert(nk_audio_soundfont_get_preset_count(font,&count)==NK_OK && count==1);
    nk_audio_soundfont_preset_info info{}; info.struct_size=sizeof(info);
    assert(nk_audio_soundfont_get_preset_info(font,0,&info)==NK_OK);
    assert(info.bank==0 && info.program==0 && !std::strcmp(info.name,"Test sine"));
    nk_audio_dsp_engine_options opts{}; opts.struct_size=sizeof(opts);
    opts.sample_rate=48000; opts.channels=2; opts.block_size=256; opts.max_voices=2;
    nk_audio_dsp_engine e=0; assert(nk_audio_dsp_engine_create(&opts,&e)==NK_OK);
    nk_audio_dsp_voice_policy_options policy{sizeof(policy),1,2,240};
    assert(nk_audio_dsp_engine_set_voice_policy(e,&policy)==NK_OK);
    nk_audio_dsp_instrument i=0;
    assert(nk_audio_dsp_instrument_create_from_soundfont(e,font,0,0.5f,&i)==NK_OK);
    float gain=0;
    assert(nk_audio_dsp_instrument_get_parameter(i,NK_AUDIO_DSP_PARAMETER_GAIN,&gain)==NK_OK && gain==0.5f);
    assert(nk_audio_dsp_instrument_get_parameter(i,NK_AUDIO_DSP_PARAMETER_FILTER_CUTOFF_HZ,&gain)==NK_ERROR_INVALID_ARGUMENT);
    // Bank handles may be closed as soon as prepared instruments retain samples.
    assert(nk_audio_soundfont_destroy(font)==NK_OK);
    uint64_t start=0;
    if (attached) {
        assert(nk_audio_get_time_pcm_frames(&start)==NK_OK); start+=1024;
        assert(nk_audio_dsp_engine_attach_device(e)==NK_OK);
    }
    nk_audio_dsp_note_options note{}; note.struct_size=sizeof(note);
    note.note=57; note.velocity=1; note.start_frame=start; note.hold_frames=1000;
    uint64_t token=0; assert(nk_audio_dsp_engine_note_on(e,i,&note,&token)==NK_OK);
    if (overlap) {
        note.start_frame=start+500; note.hold_frames=3500;
        uint64_t other=0; assert(nk_audio_dsp_engine_note_on(e,i,&note,&other)==NK_OK);
        assert(other!=token);
        // Release of the first same-pitch token must leave the second playing.
        assert(nk_audio_dsp_engine_note_off(e,token,start+1000)==NK_OK);
    }
    // Callback continues using the retired instrument until all release tails end.
    assert(nk_audio_dsp_instrument_destroy(i)==NK_OK);
    if (attached) { std::vector<float> warmup(2048); assert(nk_audio_test_render(warmup.data(),1024)==NK_OK); }
    std::vector<float> result;
    for (uint32_t at=0;at<50000;) {
        const auto n=std::min(block,50000-at); std::vector<float> samples(n*2);
        if (attached) assert(nk_audio_test_render(samples.data(),n)==NK_OK);
        else {
            nk_audio_dsp_render_target target{}; target.struct_size=sizeof(target);
            target.samples=samples.data(); target.sample_count=samples.size(); target.channels=2; target.frame_count=n;
            assert(nk_audio_dsp_engine_render(e,&target,nullptr,0)==NK_OK);
        }
        for (auto value:samples) assert(std::isfinite(value));
        result.insert(result.end(),samples.begin(),samples.end()); at+=n;
    }
    assert(*std::max_element(result.begin(),result.end())>0.1f);
    assert(std::all_of(result.end()-512,result.end(),[](float x){return x==0;}));
    if (overlap) assert(std::any_of(result.begin()+4000,result.begin()+6000,[](float x){return std::abs(x)>0.1f;}));
    nk_audio_dsp_runtime_status status{}; status.struct_size=sizeof(status);
    assert(nk_audio_dsp_engine_get_runtime_status(e,&status)==NK_OK);
    assert(status.retired_resources==0 && status.dropped_notes==0);
    if (attached) assert(nk_audio_dsp_engine_detach_device(e)==NK_OK);
    assert(nk_audio_dsp_engine_destroy(e)==NK_OK); return result;
}
static void polyphony_and_stealing() {
    const auto bytes=fixture(); auto font=load(bytes);
    nk_audio_dsp_engine_options opts{sizeof(opts),48000,2,256,32};
    nk_audio_dsp_engine e=0; assert(nk_audio_dsp_engine_create(&opts,&e)==NK_OK);
    nk_audio_dsp_voice_policy_options policy{sizeof(policy),1,8,240};
    assert(nk_audio_dsp_engine_set_voice_policy(e,&policy)==NK_OK);
    nk_audio_dsp_instrument i=0;
    assert(nk_audio_dsp_instrument_create_from_soundfont(e,font,0,0.01f,&i)==NK_OK);
    assert(nk_audio_soundfont_destroy(font)==NK_OK);
    assert(nk_audio_dsp_engine_attach_device(e)==NK_OK);
    nk_audio_dsp_note_options note{}; note.struct_size=sizeof(note); note.velocity=0.8;
    for (uint32_t n=0;n<32;++n) {
        note.note=45+n; uint64_t token=0;
        assert(nk_audio_dsp_engine_note_on(e,i,&note,&token)==NK_OK);
    }
    std::vector<float> output(1024); double maximum_ms=0;
    for (uint32_t block=0;block<40;++block) {
        if (block==10) {
            for (uint32_t n=0;n<8;++n) {
                note.note=60+n; note.start_frame=block*512; uint64_t token=0;
                assert(nk_audio_dsp_engine_note_on(e,i,&note,&token)==NK_OK);
            }
        }
        std::fill(output.begin(),output.end(),0);
        const auto before=std::chrono::steady_clock::now();
        assert(nk_audio_test_dsp_process(e,output.data(),512,block*512)==NK_OK);
        maximum_ms=std::max(maximum_ms,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count());
        for (auto value:output) assert(std::isfinite(value) && std::abs(value)<1);
    }
    nk_audio_dsp_runtime_status state{}; state.struct_size=sizeof(state);
    assert(nk_audio_dsp_engine_get_runtime_status(e,&state)==NK_OK);
    assert(state.stolen_voices==8 && state.dropped_notes==0);
    std::printf("SF2 fixture: 32 voices plus 8 stealing tails, max 512-frame source callback %.3f ms (Debug build)\n",maximum_ms);
    assert(nk_audio_dsp_engine_detach_device(e)==NK_OK);
    assert(nk_audio_dsp_instrument_destroy(i)==NK_OK);
    assert(nk_audio_dsp_engine_destroy(e)==NK_OK);
}

int main() {
    nk_init_options init{}; init.struct_size=sizeof(init); init.api_version=NK_API_VERSION;
    assert(nk_init(&init)==NK_OK); assert(nk_audio_test_use_offline_device()==NK_OK);
    const auto bytes=fixture();
    for (uint32_t length=0;length<bytes.size();++length) {
        nk_audio_soundfont invalid=0;
        assert(nk_audio_soundfont_load_memory(bytes.data(),length,&invalid)!=NK_OK && invalid==0);
    }
    auto invalid=bytes;
    // Corrupt the instrument generator index without disturbing RIFF bounds.
    for (uint32_t p=0;p+12<invalid.size();++p) if (!std::memcmp(invalid.data()+p,"pgen",4)) {
        invalid[p+10]=255; invalid[p+11]=255; break;
    }
    nk_audio_soundfont bad=0; assert(nk_audio_soundfont_load_memory(invalid.data(),invalid.size(),&bad)!=NK_OK);
    // One-shot sample generators can leave unused loop addresses reversed.
    // Those addresses must remain invalid when looping is enabled.
    invalid=bytes;
    uint32_t mode=0;
    for (uint32_t p=0;p+28<invalid.size();++p) if (!std::memcmp(invalid.data()+p,"igen",4)) {
        invalid[p+8]=2; invalid[p+9]=0; // startloopAddrsOffset
        invalid[p+10]=144; invalid[p+11]=1; // +400, past inclusive loop end 399
        mode=p+18; invalid[mode]=0; invalid[mode+1]=0;
        break;
    }
    assert(mode);
    auto one_shot=load(invalid);
    nk_audio_dsp_engine_options one_shot_options{sizeof(one_shot_options),48000,2,256,2};
    nk_audio_dsp_engine one_shot_engine=0;
    assert(nk_audio_dsp_engine_create(&one_shot_options,&one_shot_engine)==NK_OK);
    nk_audio_dsp_instrument one_shot_instrument=0;
    assert(nk_audio_dsp_instrument_create_from_soundfont(one_shot_engine,one_shot,0,1,&one_shot_instrument)==NK_OK);
    nk_audio_dsp_note_options one_shot_note{};
    one_shot_note.struct_size=sizeof(one_shot_note); one_shot_note.note=57; one_shot_note.velocity=1;
    uint64_t one_shot_token=0;
    assert(nk_audio_dsp_engine_note_on(one_shot_engine,one_shot_instrument,&one_shot_note,&one_shot_token)==NK_OK);
    std::vector<float> one_shot_output(512);
    nk_audio_dsp_render_target one_shot_target{sizeof(one_shot_target),one_shot_output.data(),256,2,512};
    assert(nk_audio_dsp_engine_render(one_shot_engine,&one_shot_target,nullptr,0)==NK_OK);
    assert(std::all_of(one_shot_output.begin(),one_shot_output.end(),[](float x){return std::isfinite(x);}));
    assert(*std::max_element(one_shot_output.begin(),one_shot_output.end())>0.1f);
    assert(nk_audio_dsp_engine_destroy(one_shot_engine)==NK_OK);
    assert(nk_audio_soundfont_destroy(one_shot)==NK_OK);
    invalid[mode]=1;
    assert(nk_audio_soundfont_load_memory(invalid.data(),invalid.size(),&bad)!=NK_OK);
    // Extreme key-scaled hold time would overflow the upstream signed counter.
    invalid=bytes;
    for (uint32_t p=0;p+12<invalid.size();++p) if (!std::memcmp(invalid.data()+p,"igen",4)) {
        invalid[p+8]=39; invalid[p+9]=0; invalid[p+10]=255; invalid[p+11]=127; break;
    }
    assert(nk_audio_soundfont_load_memory(invalid.data(),invalid.size(),&bad)!=NK_OK);
    // Generator-adjusted sample offsets must stay within decoded PCM.
    invalid=bytes;
    for (uint32_t p=0;p+12<invalid.size();++p) if (!std::memcmp(invalid.data()+p,"igen",4)) {
        invalid[p+8]=0; invalid[p+9]=0; invalid[p+10]=255; invalid[p+11]=127; break;
    }
    assert(nk_audio_soundfont_load_memory(invalid.data(),invalid.size(),&bad)!=NK_OK);
    // A compact hydra can expand into many regions: reject before TSF allocates.
    invalid=bytes;
    auto put32=[&](uint32_t pos,uint32_t value) {
        for (uint32_t j=0;j<4;++j) invalid[pos+j]=uint8_t(value>>(j*8));
    };
    uint32_t pdta=0,pgen=0;
    for (uint32_t p=0;p+16<invalid.size();++p) {
        if (!std::memcmp(invalid.data()+p,"LIST",4) && !std::memcmp(invalid.data()+p+8,"pdta",4)) pdta=p;
        if (!std::memcmp(invalid.data()+p,"pgen",4)) pgen=p;
        if (!std::memcmp(invalid.data()+p,"pbag",4)) { invalid[p+12]=1; invalid[p+13]=16; }
    }
    assert(pdta && pgen);
    const uint32_t old_list=uint32_t(invalid[pdta+4]) | uint32_t(invalid[pdta+5])<<8 |
        uint32_t(invalid[pdta+6])<<16 | uint32_t(invalid[pdta+7])<<24;
    std::vector<uint8_t> generators(4098*4);
    for (uint32_t g=0;g<4097;++g) generators[g*4]=41;
    invalid.erase(invalid.begin()+pgen+8,invalid.begin()+pgen+16);
    invalid.insert(invalid.begin()+pgen+8,generators.begin(),generators.end());
    put32(pgen+4,generators.size()); put32(pdta+4,old_list+generators.size()-8);
    put32(4,invalid.size()-8);
    assert(nk_audio_soundfont_load_memory(invalid.data(),invalid.size(),&bad)!=NK_OK);
    auto offline=playback(256,false,true);
    assert(offline==playback(73,false,true)); assert(offline==playback(73,true,true));
    playback(256,false,false);
    polyphony_and_stealing();
    uint64_t allocations=0,frees=0;
    nk_audio_test_soundfont_callback_counts(&allocations,&frees);
    assert(allocations==0 && frees==0);
    nk_shutdown();
}
