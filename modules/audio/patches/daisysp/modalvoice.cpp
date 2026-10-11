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
#include "PhysicalModeling/modalvoice.h"
#include <algorithm>

using namespace daisysp;

void ModalVoice::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    aux_         = 0.f;
    trig_        = false;

    excitation_filter_.Init();
    resonator_.Init(0.015f, 24, sample_rate_);
    excitation_filter_.Init();
    dust_.Init();

    SetSustain(false);
    SetFreq(440.f);
    SetAccent(.3f);
    SetStructure(.6f);
    SetBrightness(.8f);
    SetDamping(.6f);
}

void ModalVoice::SetSustain(bool sustain)
{
    sustain_ = sustain;
}

void ModalVoice::Trig()
{
    trig_ = true;
}

void ModalVoice::SetFreq(float freq)
{
    resonator_.SetFreq(freq);
    f0_ = freq / sample_rate_;
    f0_ = fclamp(f0_, 0.f, .25f);
}

void ModalVoice::SetAccent(float accent)
{
    accent_ = fclamp(accent, 0.f, 1.f);
}

void ModalVoice::SetStructure(float structure)
{
    resonator_.SetStructure(structure);
}

void ModalVoice::SetBrightness(float brightness)
{
    brightness_ = fclamp(brightness, 0.f, 1.f);
    density_    = brightness_ * brightness_;
}

void ModalVoice::SetDamping(float damping)
{
    damping_ = fclamp(damping, 0.f, 1.f);
}

float ModalVoice::GetAux()
{
    return aux_;
}

float ModalVoice::Process(bool trigger)
{
    float brightness = brightness_ + 0.25f * accent_ * (1.0f - brightness_);
    float damping    = damping_ + 0.25f * accent_ * (1.0f - damping_);

    const float range  = sustain_ ? 36.0f : 60.0f;
    const float f      = sustain_ ? 4.0f * f0_ : 2.0f * f0_;
    const float cutoff = fmin(
        f
            * powf(2.f,
                   kOneTwelfth
                       * ((brightness * (2.0f - brightness) - 0.5f) * range)),
        0.499f);
    const float q = sustain_ ? 0.7f : 1.5f;

    float temp = 0.f;
    // Synthesize excitation signal.
    if(sustain_)
    {
        const float dust_f = 0.00005f + 0.99995f * density_ * density_;
        dust_.SetDensity(dust_f);
        temp = dust_.Process() * (4.0f - dust_f * 3.0f) * accent_;
    }
    else if(trigger || trig_)
    {
        const float attenuation = 1.0f - damping * 0.5f;
        const float amplitude   = (0.12f + 0.08f * accent_) * attenuation;
        temp = amplitude * powf(2.f, kOneTwelfth * (cutoff * cutoff * 24.0f))
               / cutoff;
        trig_ = false;
    }

    const float one = 1.0f;
    excitation_filter_.Process<ResonatorSvf<1>::LOW_PASS, false>(
        &cutoff, &q, &one, temp, &temp);

    aux_ = temp;

    resonator_.SetBrightness(brightness);
    resonator_.SetDamping(damping);

    return resonator_.Process(temp);
}
