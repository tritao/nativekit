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
#include "PhysicalModeling/stringvoice.h"
#include <algorithm>
#include "dsp.h"

using namespace daisysp;

void StringVoice::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    trig_ = false;
    aux_ = 0.0f;

    excitation_filter_.Init(sample_rate);
    string_.Init(sample_rate_);
    dust_.Init();
    remaining_noise_samples_ = 0;

    SetSustain(false);
    SetFreq(440.f);
    SetAccent(.8f);
    SetStructure(.7f);
    SetBrightness(.2f);
    SetDamping(.7f);
}

void StringVoice::Reset()
{
    string_.Reset();
}

void StringVoice::SetSustain(bool sustain)
{
    sustain_ = sustain;
}

void StringVoice::Trig()
{
    trig_ = true;
}

void StringVoice::SetFreq(float freq)
{
    string_.SetFreq(freq);
    f0_ = freq / sample_rate_;
    f0_ = fclamp(f0_, 0.f, .25f);
}

void StringVoice::SetAccent(float accent)
{
    accent_ = fclamp(accent, 0.f, 1.f);
}

void StringVoice::SetStructure(float structure)
{
    structure = fclamp(structure, 0.f, 1.f);
    const float non_linearity
        = structure < 0.24f
              ? (structure - 0.24f) * 4.166f
              : (structure > 0.26f ? (structure - 0.26f) * 1.35135f : 0.0f);
    string_.SetNonLinearity(non_linearity);
}

void StringVoice::SetBrightness(float brightness)
{
    brightness_ = fclamp(brightness, 0.f, 1.f);
    density_    = brightness_ * brightness_;
}

void StringVoice::SetDamping(float damping)
{
    damping_ = fclamp(damping, 0.f, 1.f);
}

float StringVoice::GetAux()
{
    return aux_;
}

float StringVoice::Process(bool trigger)
{
    const float brightness = brightness_ + .25 * accent_ * (1.f - brightness_);
    const float damping    = damping_ + .25 * accent_ * (1.f - damping_);

    // Synthesize excitation signal.
    if(trigger || trig_ || sustain_)
    {
        trig_              = false;
        const float range  = 72.0f;
        const float f      = 4.0f * f0_;
        const float cutoff = fmin(
            f
                * powf(2.f,
                       kOneTwelfth * (brightness * (2.0f - brightness) - 0.5f)
                           * range),
            0.499f);
        const float q            = sustain_ ? 1.0f : 0.5f;
        remaining_noise_samples_ = static_cast<size_t>(1.0f / f0_);
        excitation_filter_.SetFreq(cutoff * sample_rate_);
        excitation_filter_.SetRes(q);
    }

    float temp = 0.f;

    if(sustain_)
    {
        const float dust_f = 0.00005f + 0.99995f * density_ * density_;
        dust_.SetDensity(dust_f);
        temp = dust_.Process() * (8.0f - dust_f * 6.0f) * accent_;
    }
    else if(remaining_noise_samples_)
    {
        temp = 2.0f * rand() * kRandFrac - 1.0f;
        remaining_noise_samples_--;
        remaining_noise_samples_ = DSY_MAX(remaining_noise_samples_, 0.f);
    }

    excitation_filter_.Process(temp);
    temp = excitation_filter_.Low();

    aux_ = temp;

    string_.SetBrightness(brightness);
    string_.SetDamping(damping);

    return string_.Process(temp);
}
