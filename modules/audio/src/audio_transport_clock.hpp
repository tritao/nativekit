#pragma once
#include <atomic>
namespace nk::audio_dsp {
/** Small shared clock snapshot; independent of engine/resource destruction. */
struct TransportClock {
    std::atomic<float> bpm{120};
};
}
