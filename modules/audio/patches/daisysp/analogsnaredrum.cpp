/*
Copyright (c) 2020 Electrosmith, Corp, Emilie Gillet

DaisySP, copyright (c) 2020 Electrosmith, Corp. 

Published under the MIT license:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

-------------------------------------------------------------------------------

Plaits, copyright 2016 Emilie Gillet (emilie.o.gillet@gmail.com)

Published under the MIT license:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

-------------------------------------------------------------------------------

Soundpipe, copyright 2015 Paul Batchelor

Published under the MIT license:

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.*/

// DaisySP MIT implementation; see installed DaisySP-LICENSE.
#include "dsp.h"
#include "Drums/analogsnaredrum.h"
#include <math.h>
#include <stdlib.h>

using namespace daisysp;

static const int kNumModes = 5;

void AnalogSnareDrum::Init(float sample_rate)
{
    sample_rate_ = sample_rate;

    trig_ = false;

    pulse_remaining_samples_ = 0;
    pulse_                   = 0.0f;
    pulse_height_            = 0.0f;
    pulse_lp_                = 0.0f;
    noise_envelope_          = 0.0f;
    sustain_gain_            = 0.0f;

    SetSustain(false);
    SetAccent(.6f);
    SetFreq(200.f);
    SetDecay(.3f);
    SetSnappy(.7f);
    SetTone(.5f);

    for(int i = 0; i < kNumModes; ++i)
    {
        resonator_[i].Init(sample_rate_);
        // The physical resonators are linear. Daisy SVF drive can diverge on the trigger pulse.
        resonator_[i].SetDrive(0.0f);
        phase_[i] = 0.f;
    }
    noise_filter_.Init(sample_rate_);
    noise_filter_.SetDrive(0.0f);
}

/** Trigger the drum */
void AnalogSnareDrum::Trig()
{
    trig_ = true;
}

void AnalogSnareDrum::SetSustain(bool sustain)
{
    sustain_ = sustain;
}

void AnalogSnareDrum::SetAccent(float accent)
{
    accent_ = fclamp(accent, 0.f, 1.f);
}

void AnalogSnareDrum::SetFreq(float f0)
{
    f0  = f0 / sample_rate_;
    f0_ = fclamp(f0, 0.f, .4f);
}

void AnalogSnareDrum::SetTone(float tone)
{
    tone_ = fclamp(tone, 0.f, 1.f);
    tone_ *= 2.f;
}

void AnalogSnareDrum::SetDecay(float decay)
{
    decay_ = decay;
    return;
    decay_ = fmax(decay, 0.f);
}

void AnalogSnareDrum::SetSnappy(float snappy)
{
    snappy_ = fclamp(snappy, 0.f, 1.f);
}

float AnalogSnareDrum::Process(bool trigger)
{
    const float decay_xt = decay_ * (1.0f + decay_ * (decay_ - 1.0f));
    const int   kTriggerPulseDuration = 1.0e-3 * sample_rate_;
    const float kPulseDecayTime       = 0.1e-3 * sample_rate_;
    const float q = 2000.0f * powf(2.f, kOneTwelfth * decay_xt * 84.0f);
    const float noise_envelope_decay
        = 1.0f
          - 0.0017f
                * powf(2.f,
                       kOneTwelfth * (-decay_ * (50.0f + snappy_ * 10.0f)));
    const float exciter_leak = snappy_ * (2.0f - snappy_) * 0.1f;

    float snappy = snappy_ * 1.1f - 0.05f;
    snappy       = fclamp(snappy, 0.0f, 1.0f);

    float tone = tone_;

    if(trigger || trig_)
    {
        trig_                    = false;
        pulse_remaining_samples_ = kTriggerPulseDuration;
        pulse_height_            = 3.0f + 7.0f * accent_;
        noise_envelope_          = 2.0f;
    }

    static const float kModeFrequencies[kNumModes]
        = {1.00f, 2.00f, 3.18f, 4.16f, 5.62f};

    float f[kNumModes];
    float gain[kNumModes];

    for(int i = 0; i < kNumModes; ++i)
    {
        f[i] = fmin(f0_ * kModeFrequencies[i], 0.499f);
        resonator_[i].SetFreq(f[i] * sample_rate_);
        // The physical model specifies Q, while Daisy SVF expects normalized
        // resonance with damping = 2 * (1 - resonance^(1/4)). Match damping=1/Q.
        const float mode_q = 1.0f + f[i] * (i == 0 ? q : q * 0.25f);
        const float root_resonance = 1.0f - 0.5f / mode_q;
        const float squared_resonance = root_resonance * root_resonance;
        resonator_[i].SetRes(squared_resonance * squared_resonance);
    }

    if(tone < 0.666667f)
    {
        // 808-style (2 modes)
        tone *= 1.5f;
        gain[0] = 1.5f + (1.0f - tone) * (1.0f - tone) * 4.5f;
        gain[1] = 2.0f * tone + 0.15f;
        for(int i = 2; i < kNumModes; i++)
        {
            gain[i] = 0.f;
        }
    }
    else
    {
        // What the 808 could have been if there were extra modes!
        tone    = (tone - 0.666667f) * 3.0f;
        gain[0] = 1.5f - tone * 0.5f;
        gain[1] = 2.15f - tone * 0.7f;
        for(int i = 2; i < kNumModes; ++i)
        {
            gain[i] = tone;
            tone *= tone;
        }
    }

    float f_noise = f0_ * 16.0f;
    f_noise = fclamp(f_noise, 0.0f, 0.499f);
    noise_filter_.SetFreq(f_noise * sample_rate_);
    const float noise_q = 1.0f + f_noise * 1.5f;
    const float noise_root_resonance = 1.0f - 0.5f / noise_q;
    const float noise_squared_resonance = noise_root_resonance * noise_root_resonance;
    noise_filter_.SetRes(noise_squared_resonance * noise_squared_resonance);

    // Q45 / Q46
    float pulse = 0.0f;
    if(pulse_remaining_samples_)
    {
        --pulse_remaining_samples_;
        pulse = pulse_remaining_samples_ ? pulse_height_ : pulse_height_ - 1.0f;
        pulse_ = pulse;
    }
    else
    {
        pulse_ *= 1.0f - 1.0f / kPulseDecayTime;
        pulse = pulse_;
    }

    float sustain_gain_value = sustain_gain_ = accent_ * decay_;

    // R189 / C57 / R190 + C58 / C59 / R197 / R196 / IC14
    fonepole(pulse_lp_, pulse, 0.75f);

    float shell = 0.0f;
    for(int i = 0; i < kNumModes; ++i)
    {
        float excitation
            = i == 0 ? (pulse - pulse_lp_) + 0.006f * pulse : 0.026f * pulse;

        phase_[i] += f[i];
        phase_[i] = phase_[i] >= 1.f ? phase_[i] - 1.f : phase_[i];

        resonator_[i].Process(excitation);

        shell += gain[i]
                 * (sustain_
                        ? sin(phase_[i] * TWOPI_F) * sustain_gain_value * 0.25f
                        : resonator_[i].Band() + excitation * exciter_leak);
    }
    shell = SoftClip(shell);

    // C56 / R194 / Q48 / C54 / R188 / D54
    float noise = 2.0f * rand() * kRandFrac - 1.0f;
    if(noise < 0.0f)
        noise = 0.0f;
    noise_envelope_ *= noise_envelope_decay;
    noise *= (sustain_ ? sustain_gain_value : noise_envelope_) * snappy * 2.0f;

    // C66 / R201 / C67 / R202 / R203 / Q49
    noise_filter_.Process(noise);
    noise = noise_filter_.Band();

    // IC13
    return noise + shell * (1.0f - snappy);
}

inline float AnalogSnareDrum::SoftLimit(float x)
{
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float AnalogSnareDrum::SoftClip(float x)
{
    if(x < -3.0f)
    {
        return -1.0f;
    }
    else if(x > 3.0f)
    {
        return 1.0f;
    }
    else
    {
        return SoftLimit(x);
    }
}