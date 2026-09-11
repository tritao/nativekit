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
        descriptor.logical_width < 0.0f || descriptor.logical_height < 0.0f ||
        descriptor.sample_count != 1 || descriptor.format != RenderTargetFormat::Rgba8 ||
        (descriptor.usage & RenderTargetColorAttachment) == 0)
        return false;
    return true;
}

void add_edge(std::vector<std::vector<uint32_t>> &edges, std::vector<uint32_t> &indegree,
              uint32_t from, uint32_t to) {
    if (from == to ||
        std::find(edges[from].begin(), edges[from].end(), to) != edges[from].end())
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
            !valid_descriptor(pass.target_descriptor)) {
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
