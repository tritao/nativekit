#include "nativekit_audio_dsp.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <vector>

struct Fixture {
    nk_audio_dsp_patch patch = 0;
    nk_audio_dsp_engine engine = 0;
    nk_audio_dsp_instrument instrument = 0;
    Fixture(nk_audio_dsp_patch_options p, nk_audio_dsp_voice_options v) {
        assert(nk_audio_dsp_patch_create_extended(&p, &v, &patch) == NK_OK);
        nk_audio_dsp_engine_options e{}; e.struct_size=sizeof(e); e.sample_rate=48000;
        e.channels=2; e.block_size=256; e.max_voices=2;
        assert(nk_audio_dsp_engine_create(&e,&engine)==NK_OK);
        assert(nk_audio_dsp_instrument_create_from_patch(engine,patch,&instrument)==NK_OK);
    }
    ~Fixture() {
        nk_audio_dsp_instrument_destroy(instrument); nk_audio_dsp_engine_destroy(engine);
        nk_audio_dsp_patch_destroy(patch);
    }
    nk_audio_dsp_event note(uint32_t id,uint32_t pitch,uint32_t frame,bool on=true,float velocity=1) {
        nk_audio_dsp_event e{}; e.struct_size=sizeof(e); e.kind=on?NK_AUDIO_DSP_EVENT_NOTE_ON:NK_AUDIO_DSP_EVENT_NOTE_OFF;
        e.voice_id=id; e.note=pitch; e.instrument=instrument; e.frame_offset=frame; e.velocity=velocity;
        return e;
    }
    std::vector<float> render(const std::vector<nk_audio_dsp_event> &events,uint32_t block=256,uint32_t frames=30000) {
        std::vector<float> result;
        for (uint32_t at=0;at<frames;) {
            uint32_t count=std::min(block,frames-at);
            std::vector<nk_audio_dsp_event> current;
            for (auto e:events) if (e.frame_offset>=at && e.frame_offset<at+count) {
                e.frame_offset-=at; current.push_back(e);
            }
            std::vector<float> samples(count*2);
            nk_audio_dsp_render_target t{}; t.struct_size=sizeof(t); t.frame_count=count;
            t.channels=2; t.samples=samples.data(); t.sample_count=samples.size();
            assert(nk_audio_dsp_engine_render(engine,&t,current.data(),current.size())==NK_OK);
            uint32_t voices=0,tails=0;
            assert(nk_audio_dsp_engine_get_voice_counts(engine,&voices,&tails)==NK_OK);
            assert(voices<=1 && tails==0);
            for (uint32_t i=0;i<count;++i) { assert(std::isfinite(samples[i*2]) && std::abs(samples[i*2])<1); result.push_back(samples[i*2]); }
            at+=count;
        }
        return result;
    }
};
nk_audio_dsp_patch_options patch() {
    nk_audio_dsp_patch_options p{}; p.struct_size=sizeof(p); p.gain=0.2f;
    p.oscillator_count=1; p.oscillators[0].struct_size=sizeof(p.oscillators[0]);
    p.oscillators[0].waveform=NK_AUDIO_DSP_WAVEFORM_SINE; p.oscillators[0].level=1;
    p.envelope.struct_size=sizeof(p.envelope); p.envelope.attack_seconds=0.001f;
    p.envelope.sustain_level=1; p.envelope.release_seconds=0.02f;
    p.noise.struct_size=sizeof(p.noise); p.filter.struct_size=sizeof(p.filter);
    p.lfo.struct_size=sizeof(p.lfo);
    return p;
}
nk_audio_dsp_voice_options voice() {
    nk_audio_dsp_voice_options v{}; v.struct_size=sizeof(v);
    v.filter_envelope.struct_size=sizeof(v.filter_envelope);
    v.monophonic=1; v.legato=1; v.accent_velocity_threshold=0.8f;
    return v;
}
float frequency(const std::vector<float> &s,uint32_t begin,uint32_t end) {
    uint32_t crossings=0;
    for (uint32_t i=begin+1;i<end;++i) if(s[i-1]<=0 && s[i]>0) ++crossings;
    return crossings*48000.f/(end-begin);
}
int main() {
    nk_init_options init{}; init.struct_size=sizeof(init); init.api_version=NK_API_VERSION;
    assert(nk_init(&init)==NK_OK);
    {
        auto p=patch(); auto v=voice();
        // Older callers retain polyphonic defaults; incomplete/invalid extensions fail.
        auto legacy=v; legacy.struct_size=offsetof(nk_audio_dsp_voice_options,monophonic);
        nk_audio_dsp_patch checked=0;
        assert(nk_audio_dsp_patch_create_extended(&p,&legacy,&checked)==NK_OK);
        nk_audio_dsp_patch_destroy(checked);
        auto invalid=v; invalid.monophonic=0;
        assert(nk_audio_dsp_patch_create_extended(&p,&invalid,&checked)!=NK_OK);
        invalid=v; invalid.glide_seconds=2.1f;
        assert(nk_audio_dsp_patch_create_extended(&p,&invalid,&checked)!=NK_OK);
        invalid=v; invalid.struct_size=legacy.struct_size+4;
        assert(nk_audio_dsp_patch_create_extended(&p,&invalid,&checked)!=NK_OK);

        // Fixed held-note capacity rejects the seventeenth identity without stranding notes.
        {
            Fixture bounded(p,v);
            std::vector<nk_audio_dsp_event> starts;
            for(uint32_t id=1;id<=17;++id) starts.push_back(bounded.note(id,57,0));
            float samples[2]{}; nk_audio_dsp_render_target target{};
            target.struct_size=sizeof(target); target.frame_count=1; target.channels=2;
            target.samples=samples; target.sample_count=2;
            assert(nk_audio_dsp_engine_render(bounded.engine,&target,starts.data(),starts.size())==NK_ERROR_QUEUE_FULL);
            std::vector<nk_audio_dsp_event> stops;
            for(uint32_t id=1;id<=16;++id) stops.push_back(bounded.note(id,57,0,false));
            auto silence=bounded.render(stops);
            assert(std::all_of(silence.begin()+5000,silence.end(),[](float value){return value==0;}));
        }
        Fixture connected(p,v), baseline(p,v);
        auto a=connected.render({connected.note(1,57,0),connected.note(2,57,4800),connected.note(2,57,12000,false),connected.note(1,57,22000,false)});
        auto b=baseline.render({baseline.note(1,57,0),baseline.note(1,57,22000,false)},73);
        assert(a==b); // No phase/filter/ADSR restart, and releasing latest returns to held root.
        v.legato=0; Fixture retrigger(p,v);
        auto c=retrigger.render({retrigger.note(1,57,0),retrigger.note(2,57,4800),retrigger.note(2,57,12000,false),retrigger.note(1,57,22000,false)});
        assert(a!=c && std::abs(c[4800]-c[4799])<0.00001f);
        v.legato=1; v.glide_seconds=0.1f;
        Fixture glide(p,v), partitions(p,v);
        auto notes=std::vector<nk_audio_dsp_event>{glide.note(1,57,0),glide.note(2,69,4800),glide.note(2,69,12000,false),glide.note(1,57,22000,false)};
        auto g=glide.render(notes);
        for(auto &e:notes) e.instrument=partitions.instrument;
        assert(g==partitions.render(notes,73));
        assert(frequency(g,5000,7000)>220 && frequency(g,5000,7000)<350);
        assert(std::abs(frequency(g,10000,12000)-440)<30);
        assert(std::abs(frequency(g,18000,21000)-220)<20);
        assert(std::all_of(g.begin()+28000,g.end(),[](float value){return value==0;}));
        // A lower note released while another is held must never return.
        Fixture cancelled(p,v);
        auto d=cancelled.render({cancelled.note(1,57,0),cancelled.note(2,69,4800),cancelled.note(1,57,6000,false),cancelled.note(2,69,12000,false)});
        assert(std::all_of(d.begin()+18000,d.end(),[](float value){return value==0;}));
        v.glide_seconds=0; v.accent_gain=0.5f;
        Fixture boosted(p,v); auto loud=boosted.render({boosted.note(1,57,0),boosted.note(1,57,22000,false)});
        for(size_t i=0;i<b.size();++i) assert(std::abs(loud[i]-b[i]*1.5f)<0.000001f);
        Fixture quiet(p,v); v.accent_gain=0; Fixture normal(p,v);
        assert(quiet.render({quiet.note(1,57,0,true,0.7f),quiet.note(1,57,22000,false)})==
               normal.render({normal.note(1,57,0,true,0.7f),normal.note(1,57,22000,false)}));
    }
    nk_shutdown();
    std::puts("Articulation passed: mono priority, legato continuity, logarithmic glide, partitions, stale releases and velocity accent");
}
