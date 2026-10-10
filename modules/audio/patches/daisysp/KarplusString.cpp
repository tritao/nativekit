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

#include <cmath>
#include "dsp.h"
#include "PhysicalModeling/KarplusString.h"
#include <stdlib.h>

using namespace daisysp;

void String::Init(float sample_rate)
{
    sample_rate_ = sample_rate;

    SetFreq(440.f);
    non_linearity_amount_ = .5f;
    brightness_           = .5f;
    damping_              = .5f;

    string_.Init();
    stretch_.Init();
    Reset();

    SetFreq(440.f);
    SetDamping(.8f);
    SetNonLinearity(.1f);
    SetBrightness(.5f);

    crossfade_.Init();
}

void String::Reset()
{
    string_.Reset();
    stretch_.Reset();
    iir_damping_filter_.Init();

    dc_blocker_.Init(sample_rate_);

    dispersion_noise_ = 0.0f;
    curved_bridge_    = 0.0f;
    out_sample_[0] = out_sample_[1] = 0.0f;
    // Consume the initial excitation even when the low-pitch resampler skips frames.
    src_phase_                      = 1.0f;
}

float String::Process(const float in)
{
    if(non_linearity_amount_ <= 0.0f)
    {
        non_linearity_amount_ *= -1;
        float ret = ProcessInternal<NON_LINEARITY_CURVED_BRIDGE>(in);
        non_linearity_amount_ *= -1;
        return ret;
    }
    else
    {
        return ProcessInternal<NON_LINEARITY_DISPERSION>(in);
    }
}

void String::SetFreq(float freq)
{
    freq /= sample_rate_;
    frequency_ = fclamp(freq, 0.f, .25f);
}

void String::SetNonLinearity(float non_linearity_amount)
{
    // Negative values select the curved-bridge branch documented in the header.
    non_linearity_amount_ = fclamp(non_linearity_amount, -1.f, 1.f);
}

void String::SetBrightness(float brightness)
{
    brightness_ = fclamp(brightness, 0.f, 1.f);
}

void String::SetDamping(float damping)
{
    damping_ = fclamp(damping, 0.f, 1.f);
}

template <String::StringNonLinearity non_linearity>
float String::ProcessInternal(const float in)
{
    float brightness = brightness_;

    const float dc_gain = 1.f - 10.f / sample_rate_;
    const float dc_ratio = (1.f - dc_gain) / (1.f + dc_gain);
    const float delay_limit = kDelayLineSize - 4.0f;
    // Reserve room for the DC blocker's phase lead even at infinite sustain.
    const float phase_reserve = atanf(dc_ratio / tanf(PI_F / delay_limit)) / TWOPI_F;
    float delay = 1.0f / frequency_;
    delay = fclamp(delay, 4.f, delay_limit / (1.f + phase_reserve));

    // Below the delay-line range, advance the model at a reduced internal rate
    // and interpolate its output back to the engine rate.
    float src_ratio = delay * frequency_;
    if(src_ratio >= 0.9999f)
    {
        // Within the delay-line range, process every sample without resampling.
        src_phase_ = 1.0f;
        src_ratio  = 1.0f;
    }

    // The filter runs only when the delay loop advances. Below the delay-line
    // range its normalized frequency must use that internal rate.
    const float loop_frequency = 1.0f / delay;

    float damping_cutoff
        = fmin(12.0f + damping_ * damping_ * 60.0f + brightness * 24.0f, 84.0f);
    float damping_f
        = fmin(loop_frequency * powf(2.f, damping_cutoff * kOneTwelfth), 0.499f);

    // Crossfade to infinite decay.
    if(damping_ >= 0.95f)
    {
        float to_infinite = 20.0f * (damping_ - 0.95f);
        brightness += to_infinite * (1.0f - brightness);
        damping_f += to_infinite * (0.4999f - damping_f);
        damping_cutoff += to_infinite * (128.0f - damping_cutoff);
    }

    float temp_f = damping_f;
    iir_damping_filter_.SetFrequency(temp_f);

    // Compensate the actual one-pole phase lag and DC blocker's phase lead.
    // The upstream port used twice the low-pass lag, making the string sharp.
    const float tangent = tanf(PI_F * loop_frequency);
    const float filter_g = tanf(PI_F * fmin(damping_f, 0.497f));
    const float lowpass_lag = atanf(tangent / filter_g);
    const float dc_lead = atanf(dc_ratio / tangent);
    const float damping_compensation = 1.f - (lowpass_lag - dc_lead) / TWOPI_F;


    float stretch_point
        = non_linearity_amount_ * (2.0f - non_linearity_amount_) * 0.225f;
    float stretch_correction = (160.0f / sample_rate_) * delay;
    stretch_correction       = fclamp(stretch_correction, 1.f, 2.1f);

    float noise_amount_sqrt = non_linearity_amount_ > 0.75f
                                  ? 4.0f * (non_linearity_amount_ - 0.75f)
                                  : 0.0f;
    float noise_amount = noise_amount_sqrt * noise_amount_sqrt * 0.1f;
    float noise_filter = 0.06f + 0.94f * brightness * brightness;

    float bridge_curving_sqrt = non_linearity_amount_;
    float bridge_curving = bridge_curving_sqrt * bridge_curving_sqrt * 0.01f;

    float ap_gain = -0.618f * non_linearity_amount_
                    / (0.15f + fabsf(non_linearity_amount_));

    src_phase_ += src_ratio;
    if(src_phase_ > 1.0f)
    {
        src_phase_ -= 1.0f;

        delay   = delay * damping_compensation;
        float s = 0.0f;

        if(non_linearity == NON_LINEARITY_DISPERSION)
        {
            float noise = rand() * kRandFrac - 0.5f;
            fonepole(dispersion_noise_, noise, noise_filter);
            delay *= 1.0f + dispersion_noise_ * noise_amount;
        }
        else
        {
            delay *= 1.0f - curved_bridge_ * bridge_curving;
        }

        // Nonlinear bridge/noise offsets must not wrap around the circular delay.
        delay = fclamp(delay, 4.f, delay_limit);

        if(non_linearity == NON_LINEARITY_DISPERSION)
        {
            float ap_delay   = delay * stretch_point;
            float main_delay = delay
                               - ap_delay * (0.408f - stretch_point * 0.308f)
                                     * stretch_correction;
            if(ap_delay >= 4.0f && main_delay >= 4.0f)
            {
                s = string_.Read(main_delay);
                s = stretch_.Allpass(s, ap_delay, ap_gain);
            }
            else
            {
                s = string_.ReadHermite(delay);
            }
        }
        else
        {
            s = string_.ReadHermite(delay);
        }

        if(non_linearity == NON_LINEARITY_CURVED_BRIDGE)
        {
            float value    = fabsf(s) - 0.025f;
            float sign     = s > 0.0f ? 1.0f : -1.5f;
            curved_bridge_ = (fabsf(value) + value) * sign;
        }

        s += in;
        s = fclamp(s, -20.f, +20.f);

        s = dc_blocker_.Process(s);

        s = iir_damping_filter_.Process(s);
        string_.Write(s);

        out_sample_[1] = out_sample_[0];
        out_sample_[0] = s;
    }

    crossfade_.SetPos(src_phase_);
    return crossfade_.Process(out_sample_[1], out_sample_[0]);
}
