#include "render_plan.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>

namespace nkui {
namespace {

bool same_resource(ResourceId left, ResourceId right) {
    return left.value == right.value;
}

bool valid_descriptor(const RenderTargetDescriptor &descriptor) {
    if (descriptor.width < 0 || descriptor.height < 0 ||
        ((descriptor.width == 0) != (descriptor.height == 0)) ||
        !std::isfinite(descriptor.logical_width) || !std::isfinite(descriptor.logical_height) ||
        !std::isfinite(descriptor.origin_x) || !std::isfinite(descriptor.origin_y) ||
        descriptor.logical_width < 0.0f || descriptor.logical_height < 0.0f ||
        ((descriptor.logical_width == 0.0f) != (descriptor.logical_height == 0.0f)) ||
        descriptor.sample_count != 1 || descriptor.format != RenderTargetFormat::Rgba8 ||
        (descriptor.usage & RenderTargetColorAttachment) == 0)
        return false;
    return true;
}

bool valid_effect_descriptor(const EffectDescriptor &effect) {
    if (effect.kind != EffectKind::ColorMatrix && effect.kind != EffectKind::Blur &&
        effect.kind != EffectKind::DropShadow)
        return false;
    for (float value : effect.color_matrix)
        if (!std::isfinite(value))
            return false;
    if ((effect.kind == EffectKind::Blur || effect.kind == EffectKind::DropShadow) &&
        (effect.color_matrix[0] < 0.0f ||
         (effect.color_matrix[1] != 0.0f && effect.color_matrix[1] != 1.0f)))
        return false;
    if (effect.kind == EffectKind::DropShadow)
        for (size_t index = 4; index < 8; ++index)
            if (effect.color_matrix[index] < 0.0f || effect.color_matrix[index] > 1.0f)
                return false;
    return true;
}

bool valid_mask_descriptor(const MaskDescriptor &mask) {
    if (mask.kind == MaskKind::None || mask.kind < MaskKind::Rectangle ||
        mask.kind > MaskKind::Image)
        return false;
    for (float value : mask.values)
        if (!std::isfinite(value))
            return false;
    if (mask.kind != MaskKind::Image && mask.image.value != 0)
        return false;
    if (mask.kind == MaskKind::Image)
        return is_resource_id(mask.image, ResourceKind::Image);
    if ((mask.kind == MaskKind::RoundedRect || mask.kind == MaskKind::Circle) &&
        mask.values[0] < 0.0f)
        return false;
    if (mask.kind == MaskKind::LinearGradient &&
        (mask.values[4] < 0.0f || mask.values[4] > 1.0f || mask.values[5] < 0.0f ||
         mask.values[5] > 1.0f))
        return false;
    return true;
}

bool valid_input_rect(const RenderPass &pass) {
    if (!pass.has_input_rect)
        return true;
    for (const float value : pass.input_rect)
        if (!std::isfinite(value))
            return false;
    return pass.input_rect[0] >= 0.0f && pass.input_rect[1] >= 0.0f &&
           pass.input_rect[2] > 0.0f && pass.input_rect[3] > 0.0f;
}

void add_edge(std::vector<std::vector<uint32_t>> &edges, std::vector<uint32_t> &indegree,
              uint32_t from, uint32_t to) {
    if (from == to || std::find(edges[from].begin(), edges[from].end(), to) != edges[from].end())
        return;
    edges[from].push_back(to);
    ++indegree[to];
}

} // namespace

bool schedule_render_plan(const RenderPlan &plan, std::vector<uint32_t> &order,
                          RenderPlanScheduleError *error) {
    order.clear();
    const uint32_t pass_count = static_cast<uint32_t>(plan.passes.size());
    std::vector<std::vector<uint32_t>> edges(pass_count);
    std::vector<uint32_t> indegree(pass_count, 0);

    for (uint32_t index = 0; index < pass_count; ++index) {
        const auto &pass = plan.passes[index];
        if (!is_resource_id(pass.target, ResourceKind::RenderTarget) ||
            !valid_descriptor(pass.target_descriptor) ||
            (pass.kind != RenderPassKind::Draw && pass.kind != RenderPassKind::Effect &&
             pass.kind != RenderPassKind::Mask) ||
            ((pass.kind == RenderPassKind::Effect || pass.kind == RenderPassKind::Mask) &&
             (!is_resource_id(pass.input_target, ResourceKind::RenderTarget) ||
              pass.input_target.value == pass.target.value || !pass.commands.empty() ||
              !valid_input_rect(pass) ||
              (pass.kind == RenderPassKind::Effect
                   ? (pass.effect.kind == EffectKind::Custom
                          ? !valid_custom_effect_descriptor(pass.custom_effect)
                          : !valid_effect_descriptor(pass.effect))
                                                   : !valid_mask_descriptor(pass.mask))))) {
            if (error)
                *error = {index, "invalid render-target descriptor"};
            return false;
        }
        // Passes writing the same target are semantic continuations. Preserve
        // their order even when an unrelated dependency temporarily blocks a
        // later pass.
        for (uint32_t previous = 0; previous < index; ++previous)
            if (same_resource(plan.passes[previous].target, pass.target))
                add_edge(edges, indegree, previous, index);
        if (pass.kind == RenderPassKind::Effect || pass.kind == RenderPassKind::Mask) {
            bool found_input = false;
            for (uint32_t producer = 0; producer < index; ++producer) {
                if (!same_resource(plan.passes[producer].target, pass.input_target))
                    continue;
                found_input = true;
                add_edge(edges, indegree, producer, index);
            }
            if (!found_input) {
                if (error)
                    *error = {index, "sampled pass input is unavailable"};
                return false;
            }
        }
    }

    for (const auto &dependency : plan.dependencies) {
        std::vector<uint32_t> producers;
        std::vector<uint32_t> consumers;
        for (uint32_t index = 0; index < pass_count; ++index) {
            if (same_resource(plan.passes[index].target, dependency.producer))
                producers.push_back(index);
            if (same_resource(plan.passes[index].target, dependency.consumer))
                consumers.push_back(index);
        }
        if (producers.empty())
            continue; // The executor resolves an external SurfaceProducer.
        bool has_consumer_command = false;
        for (const uint32_t consumer : consumers) {
            for (const auto &command : plan.passes[consumer].commands) {
                if (command.kind == RenderCommandKind::CompositeTarget &&
                    same_resource(command.resource, dependency.producer)) {
                    has_consumer_command = true;
                    for (const uint32_t producer : producers)
                        add_edge(edges, indegree, producer, consumer);
                }
            }
        }
        if (!has_consumer_command) {
            if (error)
                *error = {0, "render dependency has no consuming command"};
            return false;
        }
    }

    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> ready;
    for (uint32_t index = 0; index < pass_count; ++index)
        if (indegree[index] == 0)
            ready.push(index);
    while (!ready.empty()) {
        const uint32_t index = ready.top();
        ready.pop();
        order.push_back(index);
        for (const uint32_t next : edges[index])
            if (--indegree[next] == 0)
                ready.push(next);
    }
    if (order.size() != pass_count) {
        for (uint32_t index = 0; index < pass_count; ++index) {
            if (indegree[index] != 0) {
                if (error)
                    *error = {index, "render-plan dependency cycle"};
                break;
            }
        }
        order.clear();
        return false;
    }
    if (error)
        *error = {};
    return true;
}

} // namespace nkui
