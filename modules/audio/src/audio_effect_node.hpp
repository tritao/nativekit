#pragma once
#include "audio_effect_processor.hpp"
#include "miniaudio.h"

namespace nk::audio {
struct ProcessorNode {
    ma_node_base base{};
    std::unique_ptr<EffectProcessor> processor;
};
inline void processor_process(ma_node *node, const float **input, ma_uint32 *input_count,
                              float **output, ma_uint32 *output_count) noexcept {
    auto &effect = *reinterpret_cast<ProcessorNode *>(node);
    effect.processor->process(input ? input[0] : nullptr, output[0], *output_count);
    if (input_count) *input_count = *output_count;
}
inline const ma_node_vtable processor_vtable{processor_process, nullptr, 1, 1,
    MA_NODE_FLAG_CONTINUOUS_PROCESSING | MA_NODE_FLAG_ALLOW_NULL_INPUT};
} // namespace nk::audio
