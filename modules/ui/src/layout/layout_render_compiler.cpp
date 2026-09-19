#include "layout/layout_render_compiler.h"

#include "prepare/nanovg_path.h"
#include "prepare/skribidi_adapter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace nkui {
namespace {

constexpr uint16_t kTransientGeneration = 0x0FFE;
constexpr uint32_t kMaxTransientSlot = std::numeric_limits<uint16_t>::max();

bool fail(LayoutRenderCompileError *error, std::size_t primitive_index, const char *message) {
    if (error)
        *error = {primitive_index, message};
    return false;
}

bool finite_rect(const LayoutRect &rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
           std::isfinite(rect.height) && rect.width >= 0.0f && rect.height >= 0.0f;
}

bool valid_color(const LayoutColor &color) {
    const auto valid = [](float value) {
        return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
    };
    return valid(color.red) && valid(color.green) && valid(color.blue) && valid(color.alpha);
}

bool valid_radii(const LayoutPrimitive &primitive) {
    return std::isfinite(primitive.radius_top_left) && std::isfinite(primitive.radius_top_right) &&
           std::isfinite(primitive.radius_bottom_left) &&
           std::isfinite(primitive.radius_bottom_right) && primitive.radius_top_left >= 0.0f &&
           primitive.radius_top_right >= 0.0f && primitive.radius_bottom_left >= 0.0f &&
           primitive.radius_bottom_right >= 0.0f;
}

LayoutRect intersect(LayoutRect left, const LayoutRect &right) {
    const float x = std::max(left.x, right.x);
    const float y = std::max(left.y, right.y);
    const float right_edge = std::min(left.x + left.width, right.x + right.width);
    const float bottom_edge = std::min(left.y + left.height, right.y + right.height);
    left.x = x;
    left.y = y;
    left.width = std::max(0.0f, right_edge - x);
    left.height = std::max(0.0f, bottom_edge - y);
    return left;
}

void append_rounded_rect(NanoVGPath &path, const LayoutRect &rect,
                         const LayoutPrimitive &primitive) {
    const float half_width = rect.width * 0.5f;
    const float half_height = rect.height * 0.5f;
    const float top_left = std::min({primitive.radius_top_left, half_width, half_height});
    const float top_right = std::min({primitive.radius_top_right, half_width, half_height});
    const float bottom_right = std::min({primitive.radius_bottom_right, half_width, half_height});
    const float bottom_left = std::min({primitive.radius_bottom_left, half_width, half_height});
    const float left = rect.x;
    const float top = rect.y;
    const float right = rect.x + rect.width;
    const float bottom = rect.y + rect.height;

    path.move_to(left + top_left, top);
    path.line_to(right - top_right, top);
    if (top_right > 0.0f)
        path.arc_to(right, top, right, top + top_right, top_right);
    path.line_to(right, bottom - bottom_right);
    if (bottom_right > 0.0f)
        path.arc_to(right, bottom, right - bottom_right, bottom, bottom_right);
    path.line_to(left + bottom_left, bottom);
    if (bottom_left > 0.0f)
        path.arc_to(left, bottom, left, bottom - bottom_left, bottom_left);
    path.line_to(left, top + top_left);
    if (top_left > 0.0f)
        path.arc_to(left, top, left + top_left, top, top_left);
    path.close();
}

PreparedPaint solid_paint(const LayoutColor &color) {
    PreparedPaint paint{};
    paint.transform[0] = paint.transform[3] = 1.0f;
    paint.feather = 1.0f;
    paint.inner_color = {color.red, color.green, color.blue, color.alpha};
    paint.outer_color = paint.inner_color;
    return paint;
}

uint8_t color_byte(float value) {
    return static_cast<uint8_t>(std::lround(value * 255.0f));
}

void tint_glyphs(PreparedGlyphs &glyphs, const LayoutColor &color) {
    const uint8_t red = color_byte(color.red);
    const uint8_t green = color_byte(color.green);
    const uint8_t blue = color_byte(color.blue);
    const uint8_t alpha = color_byte(color.alpha);
    for (auto &vertex : glyphs.vertices) {
        vertex.red = red;
        vertex.green = green;
        vertex.blue = blue;
        vertex.alpha = alpha;
    }
}

void set_scissor(RenderCommand &command, const LayoutRect &clip, float pixel_scale) {
    command.has_scissor = true;
    command.scissor_x = clip.x * pixel_scale;
    command.scissor_y = clip.y * pixel_scale;
    command.scissor_width = clip.width * pixel_scale;
    command.scissor_height = clip.height * pixel_scale;
}

constexpr uint64_t kContentHashOffset = UINT64_C(1469598103934665603);
constexpr uint64_t kContentHashPrime = UINT64_C(1099511628211);

void hash_u32(uint64_t &hash, uint32_t value) {
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        hash ^= static_cast<uint8_t>(value >> shift);
        hash *= kContentHashPrime;
    }
}

void hash_u64(uint64_t &hash, uint64_t value) {
    hash_u32(hash, static_cast<uint32_t>(value));
    hash_u32(hash, static_cast<uint32_t>(value >> 32));
}

void hash_float(uint64_t &hash, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    hash_u32(hash, bits);
}

void hash_string(uint64_t &hash, const std::string &value) {
    hash_u64(hash, value.size());
    for (const uint8_t byte : value) {
        hash ^= byte;
        hash *= kContentHashPrime;
    }
}

uint64_t primitive_content_generation(const LayoutPrimitive &primitive,
                                      const SkribidiAdapter *text = nullptr,
                                      uint64_t glyph_generation = 0) {
    uint64_t hash = kContentHashOffset;
    hash_u32(hash, static_cast<uint32_t>(primitive.kind));
    hash_u32(hash, primitive.node_id);
    for (const float value :
         {primitive.bounds.x, primitive.bounds.y, primitive.bounds.width, primitive.bounds.height,
          primitive.transform.a, primitive.transform.b, primitive.transform.c,
          primitive.transform.d, primitive.transform.tx, primitive.transform.ty,
          primitive.color.red, primitive.color.green, primitive.color.blue, primitive.color.alpha,
          primitive.radius_top_left, primitive.radius_top_right, primitive.radius_bottom_left,
          primitive.radius_bottom_right})
        hash_float(hash, value);
    hash_u32(hash, primitive.visible ? 1u : 0u);
    hash_string(hash, primitive.text);
    hash_u32(hash, static_cast<uint32_t>(primitive.text_style.family));
    hash_float(hash, primitive.text_style.font_size);
    hash_float(hash, primitive.text_style.letter_spacing);
    hash_u32(hash, static_cast<uint32_t>(primitive.paragraph_style.wrap));
    hash_u32(hash, static_cast<uint32_t>(primitive.paragraph_style.alignment));
    hash_float(hash, primitive.paragraph_style.line_height);
    hash_u32(hash, static_cast<uint32_t>(primitive.paragraph_style.direction));
    hash_u64(hash, primitive.text_layout_id);
    hash_u32(hash, primitive.text_line_index);
    if (text) {
        hash_u64(hash, text->font_collection_generation());
        hash_u64(hash, text->layout_generation());
    }
    hash_u64(hash, glyph_generation);
    return hash ? hash : 1;
}

std::array<float, 6> device_transform(const LayoutTransform &transform, float pixel_scale) {
    return {transform.a * pixel_scale, transform.b * pixel_scale,  transform.c * pixel_scale,
            transform.d * pixel_scale, transform.tx * pixel_scale, transform.ty * pixel_scale};
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

std::array<float, 6> transform_array(const LayoutTransform &transform) {
    return {transform.a, transform.b, transform.c, transform.d, transform.tx, transform.ty};
}

LayoutTransform transform_layout(const std::array<float, 6> &transform) {
    return {transform[0], transform[1], transform[2], transform[3], transform[4], transform[5]};
}

bool inverse_transform(const LayoutTransform &transform, std::array<float, 6> &out) {
    const float determinant = transform.a * transform.d - transform.b * transform.c;
    if (!std::isfinite(determinant) || std::abs(determinant) < 0.000001f)
        return false;
    const float inverse = 1.0f / determinant;
    out = {transform.d * inverse,
           -transform.b * inverse,
           -transform.c * inverse,
           transform.a * inverse,
           (transform.c * transform.ty - transform.d * transform.tx) * inverse,
           (transform.b * transform.tx - transform.a * transform.ty) * inverse};
    return true;
}

LayoutRect transform_bounds(LayoutRect rect, const LayoutTransform &transform) {
    const auto x = [&](float px, float py) {
        return transform.a * px + transform.c * py + transform.tx;
    };
    const auto y = [&](float px, float py) {
        return transform.b * px + transform.d * py + transform.ty;
    };
    const float x0 = x(rect.x, rect.y);
    const float x1 = x(rect.x + rect.width, rect.y);
    const float x2 = x(rect.x, rect.y + rect.height);
    const float x3 = x(rect.x + rect.width, rect.y + rect.height);
    const float y0 = y(rect.x, rect.y);
    const float y1 = y(rect.x + rect.width, rect.y);
    const float y2 = y(rect.x, rect.y + rect.height);
    const float y3 = y(rect.x + rect.width, rect.y + rect.height);
    const float left = std::min({x0, x1, x2, x3});
    const float top = std::min({y0, y1, y2, y3});
    return {left, top, std::max({x0, x1, x2, x3}) - left, std::max({y0, y1, y2, y3}) - top};
}

LayoutRect union_bounds(LayoutRect left, const LayoutRect &right) {
    if (left.width <= 0.0f || left.height <= 0.0f)
        return right;
    if (right.width <= 0.0f || right.height <= 0.0f)
        return left;
    const float x = std::min(left.x, right.x);
    const float y = std::min(left.y, right.y);
    const float right_edge = std::max(left.x + left.width, right.x + right.width);
    const float bottom_edge = std::max(left.y + left.height, right.y + right.height);
    return {x, y, right_edge - x, bottom_edge - y};
}

} // namespace

void LayoutRenderFrame::reset() {
    resources_.reset();
    owned_resources_.reset();
    sealable_ = true;
    plan_ = {};
    paths_.clear();
    glyphs_.clear();
    text_source_ = nullptr;
}

LayoutRenderCompiler::LayoutRenderCompiler() : fonts_(std::make_shared<SkribidiFontCollection>()) {}

void LayoutRenderCompiler::set_font_collection(std::shared_ptr<SkribidiFontCollection> fonts) {
    fonts_ = std::move(fonts);
}

bool LayoutRenderCompiler::add_font(const char *path, FontFamily family) {
    return fonts_ && fonts_->add_font(path, family);
}

bool LayoutRenderCompiler::add_font_from_data(const char *name, const void *data, std::size_t bytes,
                                              FontFamily family) {
    if (!fonts_ || !name || !*name || !data || !bytes)
        return false;
    auto owned = std::make_shared<std::vector<uint8_t>>(static_cast<const uint8_t *>(data),
                                                        static_cast<const uint8_t *>(data) + bytes);
    return fonts_->add_font_from_shared_data(name, owned, family);
}

bool LayoutRenderCompiler::add_system_fallbacks() {
    return fonts_ && fonts_->add_system_fallbacks();
}

bool LayoutRenderCompiler::compile(const LayoutSnapshot &snapshot, ResourceId main_target,
                                   float pixel_scale, LayoutRenderFrame &out,
                                   LayoutRenderCompileError *error, bool load_existing,
                                   SkribidiAdapter *text_source,
                                   const CustomPaintPlans *custom_paints,
                                   const RasterPaintNodes *raster_paint_nodes) const {
    if (error)
        *error = {};
    if (!is_resource_id(main_target, ResourceKind::RenderTarget) || !std::isfinite(pixel_scale) ||
        pixel_scale <= 0.0f)
        return fail(error, 0, "invalid layout render input");

    out.reset();
    out.text_source_ = text_source;
    {
        std::size_t pass_capacity = 1;
        if (custom_paints)
            for (const auto &[node_id, custom_plan] : *custom_paints) {
                (void)node_id;
                if (custom_plan)
                    pass_capacity += custom_plan->passes.size();
            }
        out.plan_.passes.reserve(pass_capacity);
        out.plan_.passes.push_back({main_target, {}, load_existing, {}});
        bool has_text = false;
        for (const auto &primitive : snapshot.primitives)
            has_text = has_text || primitive.kind == LayoutPrimitiveKind::Text;

        if (has_text) {
            if (!text_source && !out.text_)
                out.text_ = std::make_unique<SkribidiAdapter>(fonts_);
            SkribidiAdapter *text = out.text_adapter();
            if (!text || !text->valid())
                return fail(error, 0, "text renderer is unavailable");
        }

        std::vector<LayoutRect> clips;
        uint32_t transient_slot = 1;
        uint32_t transient_target_slot =
            (static_cast<uint16_t>(main_target.value) == 0x8000u) ? 0x8001 : 0x8000;
        std::size_t current_main_pass = 0;

        struct RasterRoot {
            uint32_t node_id = 0;
            std::size_t first = 0;
            std::size_t last = 0;
            LayoutRect bounds{};
            std::array<float, 6> world_to_cache{};
            std::array<float, 6> cache_to_world{};
            LayoutRect outer_clip{};
        };
        std::unordered_map<uint32_t, uint32_t> parent_ids;
        for (const auto &item : snapshot.items)
            parent_ids[item.id] = item.parent_id;
        const auto belongs_to = [&parent_ids](uint32_t node_id, uint32_t root_id) {
            for (std::size_t depth = 0; node_id && depth <= parent_ids.size(); ++depth) {
                if (node_id == root_id)
                    return true;
                const auto found = parent_ids.find(node_id);
                if (found == parent_ids.end() || found->second == node_id)
                    return false;
                node_id = found->second;
            }
            return false;
        };
        std::vector<RasterRoot> raster_roots;
        if (raster_paint_nodes) {
            for (const uint32_t node_id : *raster_paint_nodes) {
                const auto *item = snapshot.find(node_id);
                if (!item || item->visual_kind == LayoutVisualKind::Custom)
                    continue;
                bool has_cached_parent = false;
                uint32_t parent = item->parent_id;
                for (std::size_t depth = 0; parent && depth <= parent_ids.size(); ++depth) {
                    if (raster_paint_nodes->contains(parent)) {
                        has_cached_parent = true;
                        break;
                    }
                    const auto found = parent_ids.find(parent);
                    if (found == parent_ids.end() || found->second == parent)
                        break;
                    parent = found->second;
                }
                if (has_cached_parent)
                    continue;

                std::array<float, 6> inverse_root{};
                if (!inverse_transform(item->transform, inverse_root))
                    return fail(error, 0, "raster subtree transform is not invertible");
                const std::array<float, 6> base = compose_transform(
                    {1.0f, 0.0f, 0.0f, 1.0f, -item->bounds.x, -item->bounds.y}, inverse_root);
                const std::array<float, 6> root_local =
                    compose_transform(base, transform_array(item->transform));
                LayoutRect local_bounds = transform_bounds(item->bounds, transform_layout(root_local));
                std::size_t first = snapshot.primitives.size();
                std::size_t last = 0;
                for (std::size_t index = 0; index < snapshot.primitives.size(); ++index) {
                    const auto &primitive = snapshot.primitives[index];
                    if (!belongs_to(primitive.node_id, node_id))
                        continue;
                    first = std::min(first, index);
                    last = std::max(last, index);
                    if (primitive.kind != LayoutPrimitiveKind::ClipBegin &&
                        primitive.kind != LayoutPrimitiveKind::ClipEnd &&
                        finite_rect(primitive.bounds))
                        local_bounds = union_bounds(
                            local_bounds,
                            transform_bounds(primitive.bounds,
                                             transform_layout(compose_transform(base,
                                                                               transform_array(primitive.transform)))));
                }
                if (first == snapshot.primitives.size() ||
                    local_bounds.width <= 0.0f || local_bounds.height <= 0.0f)
                    continue;
                RasterRoot root;
                root.node_id = node_id;
                root.first = first;
                root.last = last;
                root.bounds = local_bounds;
                root.world_to_cache = compose_transform(
                    {1.0f, 0.0f, 0.0f, 1.0f, -local_bounds.x, -local_bounds.y}, base);
                root.cache_to_world = compose_transform(
                    transform_array(item->transform),
                    {1.0f, 0.0f, 0.0f, 1.0f, item->bounds.x + local_bounds.x,
                     item->bounds.y + local_bounds.y});
                raster_roots.push_back(root);
            }
            std::sort(raster_roots.begin(), raster_roots.end(),
                      [](const RasterRoot &left, const RasterRoot &right) {
                          return left.first < right.first;
                      });
        }
        constexpr std::size_t no_raster_root = std::numeric_limits<std::size_t>::max();
        std::size_t active_raster_root = no_raster_root;
        ResourceId active_raster_target{};
        std::unordered_set<uint32_t> appended_custom_nodes;
        const auto append_custom_plan = [&](const RenderPlan &custom_plan,
                                            std::size_t primitive_index,
                                            ResourceId destination_target,
                                            std::size_t destination_pass,
                                            const std::array<float, 6> *command_transform = nullptr,
                                            const LayoutRect *clip_override = nullptr) -> bool {
            const auto &primitive = snapshot.primitives[primitive_index];
            std::unordered_map<uint32_t, ResourceId> remapped_targets;
            for (const auto &pass : custom_plan.passes) {
                if (pass.target.value == main_target.value ||
                    remapped_targets.contains(pass.target.value))
                    continue;
                if (transient_target_slot > std::numeric_limits<uint16_t>::max())
                    return fail(error, primitive_index, "custom render-target limit exceeded");
                remapped_targets.emplace(
                    pass.target.value,
                    make_resource_id(ResourceKind::RenderTarget, 1,
                                     static_cast<uint16_t>(transient_target_slot++)));
            }
            std::array<float, 6> placement{primitive.transform.a,  primitive.transform.b,
                                           primitive.transform.c,  primitive.transform.d,
                                           primitive.transform.tx, primitive.transform.ty};
            std::array<float, 4> clip{};
            const bool has_clip = clip_override || !clips.empty();
            if (clip_override) {
                clip = {clip_override->x, clip_override->y, clip_override->width,
                        clip_override->height};
            } else if (has_clip) {
                clip = {clips.back().x, clips.back().y, clips.back().width, clips.back().height};
            }
            RenderPlanEmbedOptions options;
            options.source_main_target = main_target;
            options.destination_main_target = destination_target;
            options.placement = placement;
            options.pixel_scale = pixel_scale;
            options.target_remap = &remapped_targets;
            options.destination_main_pass = destination_pass;
            options.has_clip = has_clip;
            options.clip = clip;
            options.has_command_transform = command_transform != nullptr;
            if (command_transform)
                options.command_transform = *command_transform;
            RenderPlanEmbedError embed_error;
            if (!append_embedded_render_plan(custom_plan, options, out.plan_, &embed_error))
                return fail(error, primitive_index,
                            embed_error.message ? embed_error.message
                                                : "custom render-plan embedding failed");
            return true;
        };
        const auto append_custom_for_primitive = [&](std::size_t primitive_index) -> bool {
            if (!custom_paints)
                return true;
            const auto &primitive = snapshot.primitives[primitive_index];
            if (!appended_custom_nodes.insert(primitive.node_id).second)
                return true;
            const auto found = custom_paints->find(primitive.node_id);
            if (found == custom_paints->end() || !found->second)
                return true;
            if (active_raster_root != no_raster_root) {
                const auto &root = raster_roots[active_raster_root];
                const LayoutRect local_clip = clips.empty()
                                                   ? LayoutRect{}
                                                   : transform_bounds(
                                                         clips.back(),
                                                         transform_layout(root.world_to_cache));
                return append_custom_plan(
                    *found->second, primitive_index, active_raster_target, current_main_pass,
                    &root.world_to_cache, clips.empty() ? nullptr : &local_clip);
            }
            const bool raster = raster_paint_nodes &&
                                raster_paint_nodes->contains(primitive.node_id);
            if (!raster)
                return append_custom_plan(*found->second, primitive_index, main_target,
                                          current_main_pass);
            if (transient_target_slot > std::numeric_limits<uint16_t>::max())
                return fail(error, primitive_index, "raster cache target limit exceeded");
            const ResourceId raster_target =
                make_resource_id(ResourceKind::RenderTarget, 1,
                                 static_cast<uint16_t>(transient_target_slot++));
            RenderPass raster_pass;
            raster_pass.target = raster_target;
            raster_pass.kind = RenderPassKind::Raster;
            out.plan_.passes.push_back(std::move(raster_pass));
            const std::size_t raster_pass_index = out.plan_.passes.size() - 1;
            if (!append_custom_plan(*found->second, primitive_index, raster_target,
                                    raster_pass_index))
                return false;
            out.plan_.dependencies.push_back({raster_target, main_target});
            RenderPass continuation;
            continuation.target = main_target;
            continuation.load_existing = true;
            RenderCommand composite{RenderCommandKind::CompositeTarget, raster_target};
            continuation.commands.push_back(std::move(composite));
            out.plan_.passes.push_back(std::move(continuation));
            current_main_pass = out.plan_.passes.size() - 1;
            return true;
        };
        const auto begin_raster_root = [&](std::size_t root_index) -> bool {
            auto &root = raster_roots[root_index];
            if (transient_target_slot > std::numeric_limits<uint16_t>::max())
                return fail(error, root.first, "raster cache target limit exceeded");
            active_raster_target = make_resource_id(
                ResourceKind::RenderTarget, 1, static_cast<uint16_t>(transient_target_slot++));
            RenderPass pass;
            pass.target = active_raster_target;
            pass.kind = RenderPassKind::Raster;
            pass.target_descriptor.logical_width = root.bounds.width;
            pass.target_descriptor.logical_height = root.bounds.height;
            out.plan_.passes.push_back(std::move(pass));
            root.outer_clip = clips.empty() ? LayoutRect{} : clips.back();
            active_raster_root = root_index;
            current_main_pass = out.plan_.passes.size() - 1;
            return true;
        };
        const auto end_raster_root = [&]() {
            const auto &root = raster_roots[active_raster_root];
            out.plan_.dependencies.push_back({active_raster_target, main_target});
            RenderPass continuation;
            continuation.target = main_target;
            continuation.load_existing = true;
            RenderCommand composite{RenderCommandKind::CompositeTarget, active_raster_target,
                                    0.0f, 0.0f, root.bounds.width, root.bounds.height};
            composite.transform = root.cache_to_world;
            if (root.outer_clip.width > 0.0f && root.outer_clip.height > 0.0f)
                set_scissor(composite, root.outer_clip, pixel_scale);
            continuation.commands.push_back(std::move(composite));
            out.plan_.passes.push_back(std::move(continuation));
            current_main_pass = out.plan_.passes.size() - 1;
            active_raster_root = no_raster_root;
            active_raster_target = {};
        };
        const auto close_raster_if_last = [&](std::size_t index) {
            if (active_raster_root != no_raster_root && index == raster_roots[active_raster_root].last)
                end_raster_root();
        };
        const auto draw_transform_for = [&](const LayoutPrimitive &primitive) {
            if (active_raster_root == no_raster_root)
                return primitive.transform;
            return transform_layout(compose_transform(
                raster_roots[active_raster_root].world_to_cache,
                transform_array(primitive.transform)));
        };
        const auto clip_for = [&](const LayoutRect &clip) {
            if (active_raster_root == no_raster_root)
                return clip;
            return transform_bounds(clip,
                                    transform_layout(raster_roots[active_raster_root].world_to_cache));
        };
        for (std::size_t index = 0; index < snapshot.primitives.size(); ++index) {
            const auto &primitive = snapshot.primitives[index];
            if (active_raster_root == no_raster_root) {
                for (std::size_t root_index = 0; root_index < raster_roots.size(); ++root_index) {
                    if (raster_roots[root_index].first == index &&
                        !begin_raster_root(root_index))
                        return false;
                }
            }
            if (primitive.kind == LayoutPrimitiveKind::ClipBegin) {
                if (!finite_rect(primitive.bounds))
                    return fail(error, index, "layout clip rectangle is invalid");
                LayoutRect clip = transform_bounds(primitive.bounds, primitive.transform);
                if (!clips.empty())
                    clip = intersect(clips.back(), clip);
                clips.push_back(clip);
                close_raster_if_last(index);
                continue;
            }
            if (primitive.kind == LayoutPrimitiveKind::ClipEnd) {
                if (clips.empty())
                    return fail(error, index, "layout clip stack underflow");
                clips.pop_back();
                close_raster_if_last(index);
                continue;
            }
            if (primitive.kind == LayoutPrimitiveKind::Border)
                return fail(error, index, "layout borders are not supported yet");
            if (!primitive.visible) {
                close_raster_if_last(index);
                continue;
            }
            if (!finite_rect(primitive.bounds) || !valid_color(primitive.color) ||
                (primitive.kind == LayoutPrimitiveKind::Rectangle && !valid_radii(primitive)))
                return fail(error, index, "layout primitive is invalid");
            if (primitive.kind == LayoutPrimitiveKind::Custom) {
                if (!append_custom_for_primitive(index))
                    return false;
                close_raster_if_last(index);
                continue;
            }
            if (primitive.kind == LayoutPrimitiveKind::Rectangle) {
                if (primitive.bounds.width <= 0.0f || primitive.bounds.height <= 0.0f) {
                    close_raster_if_last(index);
                    continue;
                }
                if (transient_slot > kMaxTransientSlot)
                    return fail(error, index, "layout render resource limit exceeded");
                auto prepared = std::make_shared<PreparedPath>();
                NanoVGPath path;
                append_rounded_rect(path, primitive.bounds, primitive);
                PathPreparationParams params;
                params.device_pixel_ratio = pixel_scale;
                const LayoutTransform draw_transform = draw_transform_for(primitive);
                params.transform = device_transform(draw_transform, pixel_scale);
                PreparedGeometry geometry;
                if (!path.valid() || !prepared || !prepare_fill(path, params, geometry) ||
                    !prepared->set(PreparedPathKind::Fill, geometry, solid_paint(primitive.color)))
                    return fail(error, index, "layout rectangle preparation failed");
                const ResourceId id = make_resource_id(ResourceKind::Path, kTransientGeneration,
                                                       static_cast<uint16_t>(transient_slot++));
                auto generation_primitive = primitive;
                generation_primitive.transform = draw_transform;
                const uint64_t content_generation =
                    primitive_content_generation(generation_primitive);
                if (!out.resources_.bind_path(id, *prepared, 0, content_generation))
                    return fail(error, index, "layout path resource binding failed");
                out.paths_.push_back(std::move(prepared));
                if (!out.owned_resources_.bind_path(id, out.paths_.back(), 0,
                                                    content_generation))
                    out.sealable_ = false;
                RenderCommand command{RenderCommandKind::Path, id};
                command.content_generation = content_generation;
                if (!clips.empty())
                    set_scissor(command, clip_for(clips.back()), pixel_scale);
                out.plan_.passes[current_main_pass].commands.push_back(std::move(command));
                out.paths_.push_back(std::move(prepared));
                if (!append_custom_for_primitive(index))
                    return false;
                close_raster_if_last(index);
                continue;
            }
            if (primitive.kind == LayoutPrimitiveKind::Text) {
                if (primitive.text.empty()) {
                    close_raster_if_last(index);
                    continue;
                }
                SkribidiAdapter *text = out.text_adapter();
                if (!text || primitive.text_style.font_size <= 0.0f ||
                    transient_slot > kMaxTransientSlot)
                    return fail(error, index, "layout text preparation input is invalid");
                const LayoutTextLayout *text_layout = nullptr;
                if (primitive.text_layout_id) {
                    const auto found =
                        std::find_if(snapshot.text_layouts.begin(), snapshot.text_layouts.end(),
                                     [&primitive](const LayoutTextLayout &candidate) {
                                         return candidate.id == primitive.text_layout_id;
                                     });
                    if (found == snapshot.text_layouts.end() ||
                        primitive.text_line_index >= found->lines.size())
                        return fail(error, index, "layout text layout ID is invalid");
                    text_layout = &*found;
                    if (!text->has_layout(text_layout->id))
                        return fail(error, index, "layout text resource is unavailable");
                } else {
                    const float width = std::max(primitive.bounds.width, 1.0f);
                    TextLayoutOptions options;
                    options.font_size = primitive.text_style.font_size;
                    options.letter_spacing = primitive.text_style.letter_spacing;
                    options.line_height = primitive.paragraph_style.line_height;
                    options.family = primitive.text_style.family;
                    options.wrap = TextWrapMode::None;
                    options.alignment = primitive.paragraph_style.alignment;
                    options.direction = primitive.paragraph_style.direction;
                    if (!text->layout_utf8(primitive.text.c_str(), width, options))
                        return fail(error, index, "layout text shaping failed");
                }
                auto glyphs = std::make_unique<PreparedGlyphs>();
                const bool prepared =
                    text_layout
                        ? text->prepare_glyphs_for_line(text_layout->id, primitive.text_line_index,
                                                        0.0f, 0.0f, pixel_scale, GlyphMode::Alpha,
                                                        *glyphs)
                        : text->prepare_glyphs(0.0f, 0.0f, pixel_scale, GlyphMode::Alpha, *glyphs);
                if (!prepared)
                    return fail(error, index, "layout glyph preparation failed");
                tint_glyphs(*glyphs, primitive.color);
                const ResourceId id =
                    make_resource_id(ResourceKind::TextLayout, kTransientGeneration,
                                     static_cast<uint16_t>(transient_slot++));
                const LayoutTransform draw_transform = draw_transform_for(primitive);
                auto generation_primitive = primitive;
                generation_primitive.transform = draw_transform;
                const uint64_t content_generation =
                    primitive_content_generation(generation_primitive, text,
                                                 glyphs->layout_generation);
                if (!out.resources_.bind_text(id, *glyphs, content_generation))
                    return fail(error, index, "layout text resource binding failed");
                const GlyphTint tint{
                    color_byte(primitive.color.red), color_byte(primitive.color.green),
                    color_byte(primitive.color.blue), color_byte(primitive.color.alpha)};
                auto snapshot = text_layout
                                    ? text->published_glyphs_for_line(
                                          text_layout->id, primitive.text_line_index, 0.0f, 0.0f,
                                          pixel_scale, GlyphMode::Alpha, tint)
                                    : text->published_glyphs(text->active_layout_id(), 0.0f, 0.0f,
                                                             pixel_scale, GlyphMode::Alpha, tint);
                if (!snapshot ||
                    !out.owned_resources_.bind_text(id, std::move(snapshot), content_generation))
                    out.sealable_ = false;
                RenderCommand command{RenderCommandKind::GlyphBatch,
                                      id,
                                      primitive.bounds.x,
                                      primitive.bounds.y,
                                      primitive.bounds.width,
                                      primitive.bounds.height};
                command.content_generation = content_generation;
                command.transform = device_transform(draw_transform, pixel_scale);
                if (!clips.empty())
                    set_scissor(command, clip_for(clips.back()), pixel_scale);
                out.plan_.passes[current_main_pass].commands.push_back(std::move(command));
                out.glyphs_.push_back(std::move(glyphs));
                if (!append_custom_for_primitive(index))
                    return false;
                close_raster_if_last(index);
            }
        }
        if (active_raster_root != no_raster_root)
            end_raster_root();
        if (!clips.empty())
            return fail(error, snapshot.primitives.size(), "layout clip stack is unbalanced");
    }
    return true;
}

} // namespace nkui
