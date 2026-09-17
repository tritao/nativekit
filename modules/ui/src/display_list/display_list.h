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
/** Maximum number of semantic operations carried by one layer side. */
constexpr size_t kEffectProgramMaxOps = 8;

/** Concrete shader descriptor used by a render-plan effect pass. */
struct EffectDescriptor {
    EffectKind kind = EffectKind::None;
    // The render backend uses color_matrix for matrix values and the compact
    // parameters required by its separable effect shaders.
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

/** Fixed-width wire operation used by the versioned layer command. */
struct EffectOpCommand {
    EffectKind kind = EffectKind::None;
    std::array<float, kColorMatrixComponents> color_matrix{};
    CustomEffectDescriptor custom{};
};

/** Named native representation of one semantic effect operation. */
struct EffectOp {
    struct DropShadowParameters {
        float sigma = 0.0f;
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        std::array<float, 4> color{};
    };

    EffectKind kind = EffectKind::None;
    std::array<float, kColorMatrixComponents> color_matrix{};
    float blur_sigma = 0.0f;
    DropShadowParameters drop_shadow{};
    CustomEffectDescriptor custom{};
};

/** Decode one validated wire operation into native semantic parameters. */
bool decode_effect_op(const EffectOpCommand &command, EffectOp &operation);

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
    uint32_t foreground_count;
    uint32_t backdrop_count;
    MaskDescriptor mask;
    EffectOpCommand foreground[kEffectProgramMaxOps];
    EffectOpCommand backdrop[kEffectProgramMaxOps];
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
    bool begin_layer(float opacity, const LayerBounds &bounds,
                     const std::vector<EffectOpCommand> &foreground,
                     const MaskDescriptor &mask = {},
                     const std::vector<EffectOpCommand> &backdrop = {},
                     CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const EffectDescriptor &effect, const MaskDescriptor &mask,
                     const EffectDescriptor &backdrop_effect,
                     CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const CustomEffectDescriptor &effect,
                     CompositeMode mode = CompositeMode::SourceOver);
    bool begin_layer(float opacity, const LayerBounds &bounds, const CustomEffectDescriptor &effect,
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
