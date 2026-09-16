#ifndef NATIVEKIT_UI_DISPLAY_LIST_H
#define NATIVEKIT_UI_DISPLAY_LIST_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nkui {

enum class ResourceKind : uint8_t {
    Path = 1,
    Image,
    TextLayout,
    RenderTarget,
    Paint,
    FontCollection,
};

struct ResourceId {
    uint32_t value = 0;
};

ResourceId make_resource_id(ResourceKind kind, uint16_t generation, uint16_t slot);
bool is_resource_id(ResourceId id, ResourceKind kind);

enum class CommandOpcode : uint16_t {
    SetTransform = 1,
    SetPaint,
    SetGlobalAlpha,
    SetCompositeMode,
    PushState,
    PopState,
    ClipRect,
    DrawPath,
    DrawImage,
    DrawTextLayout,
    BeginLayer,
    EndLayer,
    DrawRenderTarget,
    StrokePath,
};

enum class CompositeMode : uint32_t {
    SourceOver = 1,
};

/** Reasons a layer is isolated from its parent target. */
enum LayerFlags : uint32_t {
    LayerIsolated = 1u << 0,
    LayerHasBounds = 1u << 1,
};

/** Effect kinds that can be carried by a display-list layer. */
enum class EffectKind : uint32_t {
    None = 0,
    ColorMatrix = 1,
    Blur = 2,
    DropShadow = 3,
    Custom = 4,
};

constexpr size_t kColorMatrixComponents = 20;

/** Backend-neutral effect data used by the render-plan compiler. */
struct EffectDescriptor {
    EffectKind kind = EffectKind::None;
    // ColorMatrix uses all 20 values as a row-major 4x5 matrix. Blur uses
    // value 0 for sigma and value 1 internally for the pass axis. DropShadow
    // uses values 0..7 for sigma, axis, offset X/Y, and RGBA color.
    std::array<float, kColorMatrixComponents> color_matrix{};
};

constexpr size_t kCustomEffectParameterComponents = 20;

/** Backend-neutral contract for one renderer-owned custom effect registration. */
struct CustomEffectDescriptor {
    uint32_t registration_id = 0;
    uint32_t parameter_count = 0;
    uint32_t pass_count = 1;
    uint32_t sampling_inputs = 1;
    std::array<float, 4> ink_overflow{};
    std::array<float, kCustomEffectParameterComponents> parameters{};
};

bool valid_custom_effect_descriptor(const CustomEffectDescriptor &effect);

/** Separate source-alpha input used to mask an isolated layer. */
enum class MaskKind : uint32_t {
    None = 0,
    Rectangle = 1,
    RoundedRect = 2,
    Circle = 3,
    LinearGradient = 4,
    Image = 5,
};

struct MaskDescriptor {
    MaskKind kind = MaskKind::None;
    ResourceId image{};
    // RoundedRect/Circle use value 0 for radius. LinearGradient uses values
    // 0..3 for normalized start/end coordinates and 4..5 for endpoint alpha.
    std::array<float, 8> values{};
};

struct CommandHeader {
    CommandOpcode opcode{};
    uint16_t version = 1;
    uint32_t size = 0;
};

struct SetTransformCommand {
    CommandHeader header;
    float matrix[6];
};

struct SetPaintCommand {
    CommandHeader header;
    ResourceId paint;
};

struct SetGlobalAlphaCommand {
    CommandHeader header;
    float alpha;
};

struct SetCompositeModeCommand {
    CommandHeader header;
    CompositeMode mode;
};

struct ScopeCommand {
    CommandHeader header;
};

struct ClipRectCommand {
    CommandHeader header;
    float x;
    float y;
    float width;
    float height;
};

struct DrawResourceCommand {
    CommandHeader header;
    ResourceId resource;
};

struct StrokePathCommand {
    CommandHeader header;
    ResourceId path;
    float width;
    uint32_t line_cap;
    uint32_t line_join;
    float miter_limit;
};

struct DrawRectResourceCommand {
    CommandHeader header;
    ResourceId resource;
    float x;
    float y;
    float width;
    float height;
};

struct BeginLayerCommand {
    CommandHeader header;
    float opacity;
    CompositeMode mode;
    float x;
    float y;
    float width;
    float height;
    uint32_t flags;
};

/** Extended layer record carrying one sampled effect descriptor. */
struct BeginLayerEffectCommand {
    CommandHeader header;
    float opacity;
    CompositeMode mode;
    float x;
    float y;
    float width;
    float height;
    uint32_t flags;
    EffectDescriptor effect;
};

/** Extended layer record carrying an effect and a separate mask input. */
struct BeginLayerMaskCommand {
    CommandHeader header;
    float opacity;
    CompositeMode mode;
    float x;
    float y;
    float width;
    float height;
    uint32_t flags;
    EffectDescriptor effect;
    MaskDescriptor mask;
};

/** Extended layer record carrying an effect, mask, and backdrop effect. */
struct BeginLayerBackdropCommand {
    CommandHeader header;
    float opacity;
    CompositeMode mode;
    float x;
    float y;
    float width;
    float height;
    uint32_t flags;
    EffectDescriptor effect;
    MaskDescriptor mask;
    EffectDescriptor backdrop_effect;
};

/** Versioned layer record carrying one renderer-owned custom effect. */
struct BeginLayerCustomEffectCommand {
    CommandHeader header;
    float opacity;
    CompositeMode mode;
    float x;
    float y;
    float width;
    float height;
    uint32_t flags;
    CustomEffectDescriptor effect;
};

/** Legacy 16-byte layer record accepted for display-list compatibility. */
struct LegacyBeginLayerCommand {
    CommandHeader header;
    float opacity;
    CompositeMode mode;
};

struct LayerBounds {
    float x;
    float y;
    float width;
    float height;
};

struct ValidationError {
    size_t offset = 0;
    uint32_t command_index = 0;
    const char *message = nullptr;
};

class DisplayList {
  public:
    explicit DisplayList(size_t initial_capacity = 256);

    void reset();
    const uint8_t *data() const;
    size_t size() const;
    size_t capacity() const;
    uint32_t growth_count() const;
    uint32_t command_count() const;
    bool assign_validated(const uint8_t *data, size_t size);

    bool set_transform(const float matrix[6]);
    bool set_paint(ResourceId paint);
    bool set_global_alpha(float alpha);
    bool set_composite_mode(CompositeMode mode);
    bool push_state();
    bool pop_state();
    bool clip_rect(float x, float y, float width, float height);
    bool draw_path(ResourceId path);
    bool stroke_path(ResourceId path, float width, uint32_t line_cap, uint32_t line_join,
                     float miter_limit);
    bool draw_image(ResourceId image, float x, float y, float width, float height);
    bool draw_text_layout(ResourceId layout, float x, float y);
    bool begin_layer(float opacity, CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const LayerBounds &bounds,
                     CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const EffectDescriptor &effect,
                     CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const LayerBounds &bounds, const EffectDescriptor &effect,
                     CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const MaskDescriptor &mask,
                     CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const LayerBounds &bounds, const MaskDescriptor &mask,
                     CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const LayerBounds &bounds, const EffectDescriptor &effect,
                     const MaskDescriptor &mask, CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const LayerBounds &bounds, const EffectDescriptor &effect,
                     const MaskDescriptor &mask, const EffectDescriptor &backdrop_effect,
                     CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const EffectDescriptor &effect, const MaskDescriptor &mask,
                     const EffectDescriptor &backdrop_effect,
                     CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const CustomEffectDescriptor &effect,
                     CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const LayerBounds &bounds,
                     const CustomEffectDescriptor &effect,
                     CompositeMode mode = CompositeMode::SourceOver);
    bool end_layer();
    bool draw_render_target(ResourceId target, float x, float y, float width, float height);
    /** Returns whether the validated command stream contains a backdrop layer. */
    bool has_backdrop_effects() const;

  private:
    template <class T> bool append(const T &command);
    bool reserve_record(size_t size);

    std::vector<uint8_t> bytes_;
    size_t size_ = 0;
    uint32_t growth_count_ = 0;
    uint32_t command_count_ = 0;
};

bool validate_display_list(const uint8_t *data, size_t size, ValidationError *error = nullptr);

} // namespace nkui

#endif
