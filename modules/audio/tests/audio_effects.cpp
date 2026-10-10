#include "audio_effect_node.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace nk::audio;
namespace {
constexpr uint32_t rate = 48000, channels = 2, frames = 257; // exercise chunk boundaries
float energy(const float *samples, size_t count) {
    float sum = 0;
    for (size_t i = 0; i < count; ++i) {
        assert(std::isfinite(samples[i]));
        sum += samples[i] * samples[i];
    }
    return sum;
}
struct ImpulseSource {
    ma_node_base base{};
    bool emitted = false;
};
void impulse_process(ma_node *node, const float **, ma_uint32 *, float **output,
                     ma_uint32 *count) {
    auto &source = *reinterpret_cast<ImpulseSource *>(node);
    if (source.emitted) { *count = 0; return; }
    ma_silence_pcm_frames(output[0], *count, ma_format_f32, channels);
    output[0][0] = output[0][1] = 1;
    source.emitted = true;
}
const ma_node_vtable impulse_vtable{impulse_process, nullptr, 0, 1, 0};

void test_reverb() {
    assert(!create_effect_processor(NK_AUDIO_EFFECT_REVERB, rate, 1));
    auto effect = create_effect_processor(NK_AUDIO_EFFECT_REVERB, rate, channels);
    assert(effect);
    assert(effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_DRY, 0));
    assert(effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_WET, 1));
    assert(effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_DECAY_SECONDS, 0.6f));
    assert(!effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_ROOM_MS, 201));
    assert(!effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_HIGH_CUT_HZ, rate / 2));
    assert(!effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_WET, std::numeric_limits<float>::quiet_NaN()));
    assert(!effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_RATIO, 2));
    float value = 0;
    assert(effect->get_parameter(NK_AUDIO_EFFECT_PARAMETER_DECAY_SECONDS, value));
    assert(std::abs(value - 0.6f) < 0.00001f);
    assert(effect->tail_frames() >= 28800 && effect->tail_frames() <= 28801);
    assert(effect->latency_frames() == 0);
    effect->request_reset();
    std::array<float, frames * channels> input{}, output{};
    input[0] = input[1] = 1;
    effect->process(input.data(), output.data(), frames);
    float early = 0, late = 0;
    for (int i = 0; i < 400; ++i) {
        effect->process(nullptr, output.data(), frames);
        const auto e = energy(output.data(), output.size());
        if (i < 100) early += e;
        if (i >= 300) late += e;
    }
    assert(early > 0.001f && late < early * 0.01f);
    effect->request_reset();
    effect->process(nullptr, output.data(), frames);
    assert(energy(output.data(), output.size()) == 0);
    assert(effect->get_parameter(NK_AUDIO_EFFECT_PARAMETER_WET, value) && value == 1);
}

void test_dynamics() {
    auto effect = create_effect_processor(NK_AUDIO_EFFECT_DYNAMICS, rate, channels);
    assert(effect);
    assert(effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_GATE_THRESHOLD_DB, -80));
    assert(!effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_ATTACK_MS, 0));
    assert(!effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_WET, 1));
    std::array<float, frames * channels> input{}, output{};
    input.fill(0.5f);
    effect->request_reset();
    for (int i = 0; i < 100; ++i) effect->process(input.data(), output.data(), frames);
    assert(std::abs(output.back() - 0.5f) < 0.001f);
    assert(effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_THRESHOLD_DB, -20));
    assert(effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_RATIO, 4));
    for (int i = 0; i < 200; ++i) effect->process(input.data(), output.data(), frames);
    assert(energy(output.data(), output.size()) > 0);
    assert(output.back() > 0.1f && output.back() < 0.2f);
    assert(effect->set_parameter(NK_AUDIO_EFFECT_PARAMETER_MIX, 0));
    for (int i = 0; i < 100; ++i) effect->process(input.data(), output.data(), frames);
    assert(std::abs(output.back() - 0.5f) < 0.001f);
    assert(effect->latency_frames() == 0 && effect->tail_frames() == 0);
    // Mono dynamics is supported independently of the stereo-only reverb.
    assert(create_effect_processor(NK_AUDIO_EFFECT_DYNAMICS, rate, 1));
}

void test_graph_tail() {
    ma_node_graph graph{};
    auto graph_config = ma_node_graph_config_init(channels);
    assert(ma_node_graph_init(&graph_config, nullptr, &graph) == MA_SUCCESS);
    ProcessorNode reverb;
    reverb.processor = create_effect_processor(NK_AUDIO_EFFECT_REVERB, rate, channels);
    assert(reverb.processor->set_parameter(NK_AUDIO_EFFECT_PARAMETER_DRY, 0));
    assert(reverb.processor->set_parameter(NK_AUDIO_EFFECT_PARAMETER_WET, 1));
    reverb.processor->request_reset();
    auto config = ma_node_config_init();
    config.vtable = &processor_vtable;
    config.pInputChannels = config.pOutputChannels = &channels;
    assert(ma_node_init(&graph, &config, nullptr, &reverb.base) == MA_SUCCESS);
    ImpulseSource source;
    config = ma_node_config_init();
    config.vtable = &impulse_vtable;
    config.pOutputChannels = &channels;
    assert(ma_node_init(&graph, &config, nullptr, &source.base) == MA_SUCCESS);
    assert(ma_node_attach_output_bus(&source.base, 0, &reverb.base, 0) == MA_SUCCESS);
    assert(ma_node_attach_output_bus(&reverb.base, 0, ma_node_graph_get_endpoint(&graph), 0) == MA_SUCCESS);
    std::array<float, frames * channels> output{};
    float tail_energy = 0;
    for (int i = 0; i < 100; ++i) {
        ma_uint64 read = 0;
        assert(ma_node_graph_read_pcm_frames(&graph, output.data(), frames, &read) == MA_SUCCESS);
        assert(read == frames);
        if (i > 4) tail_energy += energy(output.data(), output.size());
    }
    assert(tail_energy > 0.001f); // graph continues processing after source stops delivering frames
    ma_node_uninit(&source.base, nullptr);
    ma_node_uninit(&reverb.base, nullptr);
    ma_node_graph_uninit(&graph, nullptr);
}
} // namespace
int main() {
    test_reverb();
    test_dynamics();
    test_graph_tail();
    std::puts("PASS: offline reverb decay/reset, dynamics gain, and graph tails");
}
