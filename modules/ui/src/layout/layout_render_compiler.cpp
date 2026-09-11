#include "layout/layout_render_compiler.h"

#include "prepare/nanovg_path.h"
#include "prepare/skribidi_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

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
    return std::isfinite(primitive.radius_top_left) &&
           std::isfinite(primitive.radius_top_right) &&
           std::isfinite(primitive.radius_bottom_left) &&
           std::isfinite(primitive.radius_bottom_right) &&
           primitive.radius_top_left >= 0.0f && primitive.radius_top_right >= 0.0f &&
           primitive.radius_bottom_left >= 0.0f && primitive.radius_bottom_right >= 0.0f;
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
    const float bottom_right =
        std::min({primitive.radius_bottom_right, half_width, half_height});
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

} // namespace

void LayoutRenderFrame::reset() {
    resources_.reset();
    plan_ = {};
    paths_.clear();
    glyphs_.clear();
    text_source_ = nullptr;
}

bool LayoutRenderCompiler::add_font(const char *path, FontFamily family) {
    if (!path || !*path)
        return false;
    try {
        fonts_.push_back({path, family, {}});
    } catch (...) {
        return false;
    }
    return true;
}

bool LayoutRenderCompiler::add_font_from_data(const char *name, const void *data, std::size_t bytes,
                                              FontFamily family) {
    if (!name || !*name || !data || !bytes)
        return false;
    try {
        auto owned = std::make_shared<std::vector<uint8_t>>(
            static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + bytes);
        fonts_.push_back({name, family, std::move(owned)});
    } catch (...) {
        return false;
    }
    return true;
}

bool LayoutRenderCompiler::add_system_fallbacks() {
    system_fallbacks_ = true;
    return true;
}

bool LayoutRenderCompiler::compile(const LayoutSnapshot &snapshot, ResourceId main_target,
                                   float pixel_scale, LayoutRenderFrame &out,
                                   LayoutRenderCompileError *error, bool load_existing,
                                   SkribidiAdapter *text_source) const {
    if (error)
        *error = {};
    if (!is_resource_id(main_target, ResourceKind::RenderTarget) || !std::isfinite(pixel_scale) ||
        pixel_scale <= 0.0f)
        return fail(error, 0, "invalid layout render input");

    out.reset();
    out.text_source_ = text_source;
    try {
        out.plan_.passes.push_back({main_target, {}, load_existing, {}});
        bool has_text = false;
        for (const auto &primitive : snapshot.primitives)
            has_text = has_text || primitive.kind == LayoutPrimitiveKind::Text;

        if (has_text) {
            if (!text_source && !out.text_)
                out.text_ = std::make_unique<SkribidiAdapter>();
            SkribidiAdapter *text = out.text_adapter();
            if (!text || !text->valid())
                return fail(error, 0, "text renderer is unavailable");
            if (!text_source) {
                while (out.configured_font_count_ < fonts_.size()) {
                    const auto &font = fonts_[out.configured_font_count_];
                    const bool added = font.data
                                           ? text->add_font_from_data(
                                                 font.name.c_str(), font.data->data(),
                                                 font.data->size(), font.family)
                                           : text->add_font(font.name.c_str(), font.family);
                    if (!added)
                        return fail(error, 0, "layout font could not be loaded");
                    ++out.configured_font_count_;
                }
                if (system_fallbacks_ && !out.configured_system_fallbacks_) {
                    if (!text->add_system_fallbacks())
                        return fail(error, 0, "system font fallbacks are unavailable");
                    out.configured_system_fallbacks_ = true;
                }
                if (out.configured_font_count_ == 0 && !out.configured_system_fallbacks_)
                    return fail(error, 0, "text primitives require a configured font");
            }
        }

        std::vector<LayoutRect> clips;
        uint32_t transient_slot = 1;
        auto &commands = out.plan_.passes.front().commands;
        for (std::size_t index = 0; index < snapshot.primitives.size(); ++index) {
            const auto &primitive = snapshot.primitives[index];
            if (primitive.kind == LayoutPrimitiveKind::ClipBegin) {
                if (!finite_rect(primitive.bounds))
                    return fail(error, index, "layout clip rectangle is invalid");
                LayoutRect clip = primitive.bounds;
                if (!clips.empty())
                    clip = intersect(clips.back(), clip);
                clips.push_back(clip);
                continue;
            }
            if (primitive.kind == LayoutPrimitiveKind::ClipEnd) {
                if (clips.empty())
                    return fail(error, index, "layout clip stack underflow");
                clips.pop_back();
                continue;
            }
            if (primitive.kind == LayoutPrimitiveKind::Border)
                return fail(error, index, "layout borders are not supported yet");
            if (!finite_rect(primitive.bounds) || !valid_color(primitive.color) ||
                (primitive.kind == LayoutPrimitiveKind::Rectangle && !valid_radii(primitive)))
                return fail(error, index, "layout primitive is invalid");
            if (primitive.kind == LayoutPrimitiveKind::Rectangle) {
                if (primitive.bounds.width <= 0.0f || primitive.bounds.height <= 0.0f)
                    continue;
                if (transient_slot > kMaxTransientSlot)
                    return fail(error, index, "layout render resource limit exceeded");
                auto prepared = std::make_unique<PreparedPath>();
                NanoVGPath path;
                append_rounded_rect(path, primitive.bounds, primitive);
                PathPreparationParams params;
                params.device_pixel_ratio = pixel_scale;
                params.transform = {pixel_scale, 0.0f, 0.0f, pixel_scale, 0.0f, 0.0f};
                PreparedGeometry geometry;
                if (!path.valid() || !prepared || !prepare_fill(path, params, geometry) ||
                    !prepared->set(PreparedPathKind::Fill, geometry, solid_paint(primitive.color)))
                    return fail(error, index, "layout rectangle preparation failed");
                const ResourceId id =
                    make_resource_id(ResourceKind::Path, kTransientGeneration,
                                     static_cast<uint16_t>(transient_slot++));
                if (!out.resources_.bind_path(id, *prepared, 0))
                    return fail(error, index, "layout path resource binding failed");
                RenderCommand command{RenderCommandKind::Path, id};
                if (!clips.empty())
                    set_scissor(command, clips.back(), pixel_scale);
                commands.push_back(command);
                out.paths_.push_back(std::move(prepared));
                continue;
            }
            if (primitive.kind == LayoutPrimitiveKind::Text) {
                if (primitive.text.empty())
                    continue;
                SkribidiAdapter *text = out.text_adapter();
                if (!text || primitive.font_size == 0 || transient_slot > kMaxTransientSlot)
                    return fail(error, index, "layout text preparation input is invalid");
                const LayoutTextLayout *text_layout = nullptr;
                if (primitive.text_layout_id) {
                    const auto found = std::find_if(
                        snapshot.text_layouts.begin(), snapshot.text_layouts.end(),
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
                    options.font_size = static_cast<float>(primitive.font_size);
                    options.letter_spacing = static_cast<float>(primitive.letter_spacing);
                    options.line_height = static_cast<float>(primitive.line_height);
                    options.wrap = TextWrapMode::None;
                    if (!text->layout_utf8(primitive.text.c_str(), width, options))
                        return fail(error, index, "layout text shaping failed");
                }
                auto glyphs = std::make_unique<PreparedGlyphs>();
                const bool prepared = text_layout
                                          ? text->prepare_glyphs_for_line(
                                                text_layout->id, primitive.text_line_index, 0.0f,
                                                0.0f, pixel_scale, GlyphMode::Alpha, *glyphs)
                                          : text->prepare_glyphs(
                                                0.0f, 0.0f, pixel_scale, GlyphMode::Alpha, *glyphs);
                if (!prepared)
                    return fail(error, index, "layout glyph preparation failed");
                tint_glyphs(*glyphs, primitive.color);
                const ResourceId id =
                    make_resource_id(ResourceKind::TextLayout, kTransientGeneration,
                                     static_cast<uint16_t>(transient_slot++));
                if (!out.resources_.bind_text(id, *glyphs))
                    return fail(error, index, "layout text resource binding failed");
                RenderCommand command{RenderCommandKind::GlyphBatch, id, primitive.bounds.x,
                                      primitive.bounds.y, primitive.bounds.width,
                                      primitive.bounds.height};
                command.transform = {pixel_scale, 0.0f, 0.0f, pixel_scale, 0.0f, 0.0f};
                if (!clips.empty())
                    set_scissor(command, clips.back(), pixel_scale);
                commands.push_back(command);
                out.glyphs_.push_back(std::move(glyphs));
            }
        }
        if (!clips.empty())
            return fail(error, snapshot.primitives.size(), "layout clip stack is unbalanced");
    } catch (...) {
        return fail(error, 0, "layout render compilation ran out of memory");
    }
    return true;
}

} // namespace nkui
