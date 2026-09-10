#ifndef NATIVEKIT_UI_DISPLAY_LIST_H
#define NATIVEKIT_UI_DISPLAY_LIST_H

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
    bool end_layer();
    bool draw_render_target(ResourceId target, float x, float y, float width, float height);

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
