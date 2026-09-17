#include "render_plan.h"

#include <algorithm>
#include <cmath>
#include <limits>
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
    if (mask.kind == MaskKind::LinearGradient && (mask.values[4] < 0.0f || mask.values[4] > 1.0f ||
                                                  mask.values[5] < 0.0f || mask.values[5] > 1.0f))
        return false;
    return true;
}

bool valid_box_shadow_descriptor(const RenderCommand &command) {
    if (command.width <= 0.0f || command.height <= 0.0f ||
        !std::isfinite(command.x) || !std::isfinite(command.y) ||
        !std::isfinite(command.width) || !std::isfinite(command.height) ||
        !std::isfinite(command.opacity) || command.opacity < 0.0f || command.opacity > 1.0f)
        return false;
    const auto &shadow = command.box_shadow;
    if (!std::isfinite(shadow.offset_x) || !std::isfinite(shadow.offset_y) ||
        !std::isfinite(shadow.blur_sigma) || shadow.blur_sigma < 0.0f ||
        !std::isfinite(shadow.spread))
        return false;
    for (const float value : shadow.radii)
        if (!std::isfinite(value) || value < 0.0f)
            return false;
    for (const float value : shadow.color)
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
            return false;
    return true;
}

bool valid_input_rect(const RenderPass &pass) {
    if (!pass.has_input_rect)
        return true;
    for (const float value : pass.input_rect)
        if (!std::isfinite(value))
            return false;
    return pass.input_rect[0] >= 0.0f && pass.input_rect[1] >= 0.0f && pass.input_rect[2] > 0.0f &&
           pass.input_rect[3] > 0.0f;
}

bool finite_transform(const std::array<float, 6> &transform) {
    for (const float value : transform)
        if (!std::isfinite(value))
            return false;
    return true;
}

std::array<float, 6> compose_transform(const std::array<float, 6> &outer,
                                       const std::array<float, 6> &inner) {
    return {outer[0] * inner[0] + outer[2] * inner[1],
            outer[1] * inner[0] + outer[3] * inner[1],
            outer[0] * inner[2] + outer[2] * inner[3],
            outer[1] * inner[2] + outer[3] * inner[3],
            outer[0] * inner[4] + outer[2] * inner[5] + outer[4],
            outer[1] * inner[4] + outer[3] * inner[5] + outer[5]};
}

std::array<float, 6> scale_transform(const std::array<float, 6> &transform, float pixel_scale) {
    auto result = transform;
    for (float &value : result)
        value *= pixel_scale;
    return result;
}

struct EmbedBounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

EmbedBounds transform_bounds(float x, float y, float width, float height,
                             const std::array<float, 6> &transform) {
    const auto point_x = [&transform](float px, float py) {
        return transform[0] * px + transform[2] * py + transform[4];
    };
    const auto point_y = [&transform](float px, float py) {
        return transform[1] * px + transform[3] * py + transform[5];
    };
    const float x0 = point_x(x, y);
    const float x1 = point_x(x + width, y);
    const float x2 = point_x(x, y + height);
    const float x3 = point_x(x + width, y + height);
    const float y0 = point_y(x, y);
    const float y1 = point_y(x + width, y);
    const float y2 = point_y(x, y + height);
    const float y3 = point_y(x + width, y + height);
    const float left = std::min({x0, x1, x2, x3});
    const float top = std::min({y0, y1, y2, y3});
    return {left, top, std::max({x0, x1, x2, x3}) - left, std::max({y0, y1, y2, y3}) - top};
}

bool finite_bounds(const EmbedBounds &bounds) {
    return std::isfinite(bounds.x) && std::isfinite(bounds.y) && std::isfinite(bounds.width) &&
           std::isfinite(bounds.height) && bounds.width >= 0.0f && bounds.height >= 0.0f;
}

EmbedBounds intersect_bounds(EmbedBounds left, const EmbedBounds &right) {
    const float x = std::max(left.x, right.x);
    const float y = std::max(left.y, right.y);
    const float right_edge = std::min(left.x + left.width, right.x + right.width);
    const float bottom_edge = std::min(left.y + left.height, right.y + right.height);
    return {x, y, std::max(0.0f, right_edge - x), std::max(0.0f, bottom_edge - y)};
}

bool scale_effect_parameters(EffectDescriptor &effect, float pixel_scale) {
    if (effect.kind != EffectKind::Blur && effect.kind != EffectKind::DropShadow)
        return true;
    const double sigma = static_cast<double>(effect.color_matrix[0]) * pixel_scale;
    if (!std::isfinite(sigma) || sigma > std::numeric_limits<float>::max())
        return false;
    effect.color_matrix[0] = static_cast<float>(sigma);
    if (effect.kind == EffectKind::DropShadow) {
        const double offset_x = static_cast<double>(effect.color_matrix[2]) * pixel_scale;
        const double offset_y = static_cast<double>(effect.color_matrix[3]) * pixel_scale;
        if (!std::isfinite(offset_x) || !std::isfinite(offset_y) ||
            std::abs(offset_x) > std::numeric_limits<float>::max() ||
            std::abs(offset_y) > std::numeric_limits<float>::max())
            return false;
        effect.color_matrix[2] = static_cast<float>(offset_x);
        effect.color_matrix[3] = static_cast<float>(offset_y);
    }
    return true;
}

bool scale_mask_parameters(MaskDescriptor &mask, float pixel_scale) {
    if (mask.kind != MaskKind::RoundedRect && mask.kind != MaskKind::Circle)
        return true;
    const double radius = static_cast<double>(mask.values[0]) * pixel_scale;
    if (!std::isfinite(radius) || radius > std::numeric_limits<float>::max())
        return false;
    mask.values[0] = static_cast<float>(radius);
    return true;
}

bool scale_input_region(RenderPass &pass, float pixel_scale) {
    if (!pass.has_input_rect)
        return true;
    for (float &value : pass.input_rect) {
        const double scaled = static_cast<double>(value) * pixel_scale;
        if (!std::isfinite(scaled) || scaled > std::numeric_limits<float>::max())
            return false;
        value = static_cast<float>(scaled);
    }
    return true;
}

ResourceId remap_embedding_resource(ResourceId resource, const RenderPlanEmbedOptions &options) {
    if (resource.value == options.source_main_target.value)
        return options.destination_main_target;
    if (options.target_remap) {
        const auto found = options.target_remap->find(resource.value);
        if (found != options.target_remap->end())
            return found->second;
    }
    return resource;
}

bool valid_embed_clip(const RenderPlanEmbedOptions &options) {
    if (!options.has_clip)
        return true;
    return std::isfinite(options.clip[0]) && std::isfinite(options.clip[1]) &&
           std::isfinite(options.clip[2]) && std::isfinite(options.clip[3]) &&
           options.clip[2] >= 0.0f && options.clip[3] >= 0.0f;
}

void place_main_command(RenderCommand &command, const RenderPlanEmbedOptions &options) {
    const bool implicit_extent = command.kind == RenderCommandKind::CompositeTarget &&
                                 (command.width <= 0.0f || command.height <= 0.0f);
    if (!implicit_extent)
        command.transform = compose_transform(options.placement, command.transform);

    if (command.has_scissor) {
        const EmbedBounds transformed =
            transform_bounds(command.scissor_x, command.scissor_y, command.scissor_width,
                             command.scissor_height, options.placement);
        command.scissor_x = transformed.x;
        command.scissor_y = transformed.y;
        command.scissor_width = transformed.width;
        command.scissor_height = transformed.height;
    }
    if (options.has_clip) {
        const EmbedBounds clip{options.clip[0], options.clip[1], options.clip[2], options.clip[3]};
        const EmbedBounds current = command.has_scissor
                                        ? EmbedBounds{command.scissor_x, command.scissor_y,
                                                      command.scissor_width, command.scissor_height}
                                        : clip;
        const EmbedBounds combined = command.has_scissor ? intersect_bounds(current, clip) : clip;
        command.has_scissor = true;
        command.scissor_x = combined.x;
        command.scissor_y = combined.y;
        command.scissor_width = combined.width;
        command.scissor_height = combined.height;
    }

    if (implicit_extent) {
        command.x *= options.pixel_scale;
        command.y *= options.pixel_scale;
        command.width *= options.pixel_scale;
        command.height *= options.pixel_scale;
        // An unbounded target supplies its physical extent at execution time.
        // Its placement is defined by the target itself, not by a local quad.
        command.transform = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    } else {
        command.transform = scale_transform(command.transform, options.pixel_scale);
    }
    command.scissor_x *= options.pixel_scale;
    command.scissor_y *= options.pixel_scale;
    command.scissor_width *= options.pixel_scale;
    command.scissor_height *= options.pixel_scale;
}

void add_edge(std::vector<std::vector<uint32_t>> &edges, std::vector<uint32_t> &indegree,
              uint32_t from, uint32_t to) {
    if (from == to || std::find(edges[from].begin(), edges[from].end(), to) != edges[from].end())
        return;
    edges[from].push_back(to);
    ++indegree[to];
}

} // namespace

bool scale_render_plan_parameters(RenderPass &pass, float pixel_scale) {
    if (!std::isfinite(pixel_scale) || pixel_scale <= 0.0f)
        return false;
    return (pass.kind != RenderPassKind::Effect ||
            (scale_effect_parameters(pass.effect, pixel_scale) &&
             scale_input_region(pass, pixel_scale))) &&
           (pass.kind != RenderPassKind::Mask || scale_mask_parameters(pass.mask, pixel_scale));
}

bool append_embedded_render_plan(const RenderPlan &source, const RenderPlanEmbedOptions &options,
                                 RenderPlan &destination, RenderPlanEmbedError *error) {
    if (error)
        *error = {};
    const auto fail_embed = [error](const char *message) {
        if (error)
            error->message = message;
        return false;
    };
    if (!is_resource_id(options.source_main_target, ResourceKind::RenderTarget) ||
        !is_resource_id(options.destination_main_target, ResourceKind::RenderTarget) ||
        !std::isfinite(options.pixel_scale) || options.pixel_scale <= 0.0f ||
        !finite_transform(options.placement) || !valid_embed_clip(options))
        return fail_embed("invalid render-plan embedding options");

    const auto destination_main = std::find_if(
        destination.passes.begin(), destination.passes.end(), [&options](const RenderPass &pass) {
            return pass.target.value == options.destination_main_target.value;
        });
    if (destination_main == destination.passes.end())
        return fail_embed("destination render target is unavailable");
    const std::size_t destination_main_index =
        static_cast<std::size_t>(destination_main - destination.passes.begin());

    destination.isolated_layers += source.isolated_layers;
    destination.bounded_layers += source.bounded_layers;
    for (const auto &source_pass : source.passes) {
        if (!is_resource_id(source_pass.target, ResourceKind::RenderTarget))
            return fail_embed("embedded render target is invalid");
        if (source_pass.target.value == options.source_main_target.value) {
            for (auto command : source_pass.commands) {
                command.resource = remap_embedding_resource(command.resource, options);
                place_main_command(command, options);
                command.custom_payload = true;
                destination.passes[destination_main_index].commands.push_back(std::move(command));
            }
            continue;
        }

        RenderPass pass = source_pass;
        pass.target = remap_embedding_resource(pass.target, options);
        pass.input_target = remap_embedding_resource(pass.input_target, options);
        if (pass.target_descriptor.logical_width > 0.0f ||
            pass.target_descriptor.logical_height > 0.0f) {
            if (!std::isfinite(pass.target_descriptor.logical_width) ||
                !std::isfinite(pass.target_descriptor.logical_height) ||
                pass.target_descriptor.logical_width <= 0.0f ||
                pass.target_descriptor.logical_height <= 0.0f)
                return fail_embed("embedded render-target bounds are invalid");
            const EmbedBounds transformed =
                transform_bounds(pass.target_descriptor.origin_x, pass.target_descriptor.origin_y,
                                 pass.target_descriptor.logical_width,
                                 pass.target_descriptor.logical_height, options.placement);
            if (!finite_bounds(transformed))
                return fail_embed("embedded render-target transform is invalid");
            pass.target_descriptor.origin_x = transformed.x;
            pass.target_descriptor.origin_y = transformed.y;
            const double width =
                static_cast<double>(pass.target_descriptor.logical_width) * options.pixel_scale;
            const double height =
                static_cast<double>(pass.target_descriptor.logical_height) * options.pixel_scale;
            if (!std::isfinite(width) || !std::isfinite(height) ||
                width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max())
                return fail_embed("embedded render-target bounds are too large");
            pass.target_descriptor.width = std::max(1, static_cast<int>(std::ceil(width)));
            pass.target_descriptor.height = std::max(1, static_cast<int>(std::ceil(height)));
        }
        if (!scale_render_plan_parameters(pass, options.pixel_scale))
            return fail_embed("embedded render-pass parameters are too large");
        for (auto &command : pass.commands) {
            command.resource = remap_embedding_resource(command.resource, options);
            command.transform = scale_transform(command.transform, options.pixel_scale);
            command.scissor_x *= options.pixel_scale;
            command.scissor_y *= options.pixel_scale;
            command.scissor_width *= options.pixel_scale;
            command.scissor_height *= options.pixel_scale;
            command.custom_payload = true;
        }
        destination.passes.push_back(std::move(pass));
    }
    for (const auto &dependency : source.dependencies)
        destination.dependencies.push_back(
            {remap_embedding_resource(dependency.producer, options),
             remap_embedding_resource(dependency.consumer, options)});
    return true;
}

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
        for (const auto &command : pass.commands) {
            if (command.kind == RenderCommandKind::BoxShadow &&
                !valid_box_shadow_descriptor(command)) {
                if (error)
                    *error = {index, "invalid box-shadow command"};
                return false;
            }
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
