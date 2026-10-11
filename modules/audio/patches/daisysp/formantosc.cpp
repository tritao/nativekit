/*
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
THE SOFTWARE.
*/
// NativeKit safety adaptation.
#include "dsp.h"
#include "Synthesis/formantosc.h"
#include <math.h>

using namespace daisysp;

void FormantOscillator::Init(float sample_rate)
{
    carrier_phase_ = 0.0f;
    formant_phase_ = 0.0f;
    next_sample_   = 0.0f;

    carrier_frequency_ = 0.0f;
    formant_frequency_ = 100.f;
    phase_shift_       = 0.0f;
    ps_inc_            = 0.0f;

    sample_rate_ = sample_rate;
}

float FormantOscillator::Process()
{
    float this_sample = next_sample_;
    float next_sample = 0.0f;
    carrier_phase_ += carrier_frequency_;

    if(carrier_phase_ >= 1.0f)
    {
        carrier_phase_ -= 1.0f;
        float reset_time = carrier_phase_ / carrier_frequency_;

        float formant_phase_at_reset
            = formant_phase_ + (1.0f - reset_time) * formant_frequency_;
        float before        = Sine(formant_phase_at_reset + phase_shift_
                            + (ps_inc_ * (1.0f - reset_time)));
        float after         = Sine(0.0f + phase_shift_ + ps_inc_);
        float discontinuity = after - before;
        this_sample += discontinuity * ThisBlepSample(reset_time);
        next_sample += discontinuity * NextBlepSample(reset_time);
        formant_phase_ = reset_time * formant_frequency_;
    }
    else
    {
        formant_phase_ += formant_frequency_;
        if(formant_phase_ >= 1.0f)
        {
            formant_phase_ -= 1.0f;
        }
    }

    phase_shift_ += ps_inc_;
    ps_inc_ = 0.f;

    next_sample += Sine(formant_phase_ + phase_shift_);

    next_sample_ = next_sample;
    return this_sample;
}

void FormantOscillator::SetFormantFreq(float freq)
{
    //convert from Hz to phase_inc / sample
    formant_frequency_ = freq / sample_rate_;
    formant_frequency_ = formant_frequency_ >= .25f ? .25f : formant_frequency_;
    formant_frequency_
        = formant_frequency_ <= -.25f ? -.25f : formant_frequency_;
}

void FormantOscillator::SetCarrierFreq(float freq)
{
    //convert from Hz to phase_inc / sample
    carrier_frequency_ = freq / sample_rate_;
    carrier_frequency_ = carrier_frequency_ >= .25f ? .25f : carrier_frequency_;
    carrier_frequency_
        = carrier_frequency_ <= -.25f ? -.25f : carrier_frequency_;
}

void FormantOscillator::SetPhaseShift(float ps)
{
    ps_inc_ = ps - phase_shift_;
}

inline float FormantOscillator::Sine(float phase)
{
    return sinf(phase * TWOPI_F);
}

inline float FormantOscillator::ThisBlepSample(float t)
{
    return 0.5f * t * t;
}

inline float FormantOscillator::NextBlepSample(float t)
{
    t = 1.0f - t;
    return -0.5f * t * t;
}
