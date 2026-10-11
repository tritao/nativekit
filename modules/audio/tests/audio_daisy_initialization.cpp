// Initialization must erase prior object storage and prior playback history.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <type_traits>
#include <vector>
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
#include "../patches/daisysp/synthsnaredrum.h"
#include "Drums/hihat.h"
#include "Synthesis/formantosc.h"
#include "Synthesis/vosim.h"
#include "Synthesis/zoscillator.h"
#include "Synthesis/variablesawosc.h"
#include "Synthesis/variableshapeosc.h"
#include "Synthesis/oscillatorbank.h"
#include "../patches/daisysp/harmonic_osc.h"
#include "Noise/grainlet.h"
#include "Noise/particle.h"
#include "Noise/dust.h"
#include "Noise/clockednoise.h"
#include "Noise/fractal_noise.h"
#include "Sampling/granularplayer.h"
#undef rand

static int failures = 0;
template<class T, class Init, class Process>
static void check_model(const char *name, Init init, Process process) {
    std::vector<float> expected;
    for (int fill : {0x00, 0x3f, 0xff, -1}) {
        std::unique_ptr<std::aligned_storage_t<sizeof(T), alignof(T)>> storage(
            new std::aligned_storage_t<sizeof(T), alignof(T)>);
        if (fill >= 0) std::memset(storage.get(), fill, sizeof(T));
        T *model = new(storage.get()) T;
        for (int pass=0; pass<2; ++pass) {
            uint32_t state=0x12345678;
            DaisyRandomScope random(state);
            init(*model);
            std::vector<float> samples;
            // Exercise processing both before and after an explicit trigger.
            for (int i=0; i<4161; ++i) samples.push_back(process(*model,i==64));
            if (expected.empty()) expected=samples;
            for (size_t i=0; i<samples.size(); ++i) {
                if (!std::isfinite(samples[i]) || samples[i]!=expected[i]) {
                    std::fprintf(stderr,"%s fill=%02x pass=%d sample=%zu expected=%g actual=%g\n",
                        name,fill,pass,i,expected[i],samples[i]);
                    ++failures; break;
                }
            }
        }
        model->~T();
    }
}
static void snare_instances_have_independent_clocks() {
    daisysp::SyntheticSnareDrum reference, left, right;
    for (auto *m : {&reference,&left,&right}) {m->Init(48000); m->SetDecay(0); m->SetSnappy(0);}
    uint32_t reference_random=123, left_random=123, right_random=456;
    for (int i=0; i<48001; ++i) {
        float expected, actual;
        {DaisyRandomScope scope(reference_random); expected=reference.Process(i==0);}
        {DaisyRandomScope scope(left_random); actual=left.Process(i==0);}
        {DaisyRandomScope scope(right_random); right.Process(i%8000==0);}
        if (!std::isfinite(actual) || actual!=expected) {
            std::fprintf(stderr,"Snare instance interference at sample %d\n",i);
            ++failures; break;
        }
    }
}
int main() {
    snare_instances_have_independent_clocks();
    for (float rate : {44100.f,48000.f,96000.f}) {
        auto init=[rate](auto &m) {m.Init(rate);};
        auto process=[](auto &m,bool) {return m.Process();};
        auto triggered=[](auto &m,bool trigger) {return m.Process(trigger);};
        check_model<daisysp::Fm2>("FM2",init,process);
        check_model<daisysp::StringVoice>("StringVoice",init,triggered);
        check_model<daisysp::String>("KarplusString",init,[](auto &m,bool t) {return m.Process(t?1.f:0.f);});
        check_model<daisysp::ModalVoice>("ModalVoice",init,triggered);
        check_model<daisysp::Resonator>("Resonator",[rate](auto &m) {m.Init(.015f,24,rate);},[](auto &m,bool t) {return m.Process(t?1.f:0.f);});
        check_model<daisysp::Drip>("Drip",[rate](auto &m) {m.Init(rate,.05f);},triggered);
        check_model<daisysp::AnalogBassDrum>("AnalogBassDrum",init,triggered);
        check_model<daisysp::SyntheticBassDrum>("SyntheticBassDrum",init,triggered);
        check_model<daisysp::AnalogSnareDrum>("AnalogSnareDrum",init,triggered);
        check_model<daisysp::SyntheticSnareDrum>("SyntheticSnareDrum",init,triggered);
        check_model<daisysp::HiHat<>>("HiHat",init,triggered);
        check_model<daisysp::FormantOscillator>("Formant",init,process);
        check_model<daisysp::VosimOscillator>("Vosim",init,process);
        check_model<daisysp::ZOscillator>("Zosc",init,process);
        check_model<daisysp::VariableSawOscillator>("VariableSaw",init,process);
        check_model<daisysp::VariableShapeOscillator>("VariableShape",init,process);
        check_model<daisysp::OscillatorBank>("OscillatorBank",init,process);
        check_model<daisysp::HarmonicOscillator<>>("Harmonic",init,process);
        check_model<daisysp::GrainletOscillator>("Grainlet",init,process);
        check_model<daisysp::Particle>("Particle",init,process);
        check_model<daisysp::Dust>("Dust",[](auto &m) {m.Init();},process);
        check_model<daisysp::ClockedNoise>("ClockedNoise",init,process);
        check_model<daisysp::FractalRandomGenerator<daisysp::ClockedNoise,4>>("FractalNoise",init,process);
        float pcm[128]; for(int i=0;i<128;++i) pcm[i]=.25f*std::sin(i*.2f);
        check_model<daisysp::GranularPlayer>("Granular",[rate,&pcm](auto &m) {m.Init(pcm,128,rate);},[](auto &m,bool) {return m.Process(1,0,50);});
    }
    if (failures) return 1;
    std::puts("All 24 Daisy source models: initialized storage and playback reset passed");
}
