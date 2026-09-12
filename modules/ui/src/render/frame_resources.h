#ifndef NATIVEKIT_UI_FRAME_RESOURCES_H
#define NATIVEKIT_UI_FRAME_RESOURCES_H

#include "display_list/display_list.h"
#include "nativekit_graphics.h"
#include "prepare/nanovg_path.h"
#include "prepare/skribidi_adapter.h"

#include <cstdint>
#include <unordered_map>

namespace nkui {

class RenderBackend;

enum class SurfacePixelFormat : uint8_t {
    Rgba8 = 1,
};

enum class SurfaceAlphaMode : uint8_t {
    Opaque = 1,
    Premultiplied,
};

enum class SurfaceFilter : uint8_t {
    Nearest = 1,
    Linear,
};

enum class SurfaceColorSpace : uint8_t {
    Linear = 1,
    // Reserved for the sRGB pipeline family; the current backend rejects it explicitly.
    Srgb,
};

struct SurfaceDescriptor {
    int width = 0;
    int height = 0;
    SurfacePixelFormat format = SurfacePixelFormat::Rgba8;
    SurfaceAlphaMode alpha = SurfaceAlphaMode::Premultiplied;
    SurfaceFilter filter = SurfaceFilter::Nearest;
    SurfaceColorSpace color_space = SurfaceColorSpace::Linear;
};

enum class SurfaceRenderResult : uint8_t {
    Rendered = 1,
    Unavailable,
    Failed,
};

class SurfaceProducer {
  public:
    virtual ~SurfaceProducer() = default;
    // Return false when no new frame can be acquired. The executor may keep using the last valid
    // target in that case.
    virtual bool ready() const = 0;
    // Resolve the producer's output for the requested consumer size. The returned dimensions are
    // the dimensions used for the producer target and may differ from the consumer size.
    virtual bool describe(int requested_width, int requested_height,
                          SurfaceDescriptor &description) const = 0;
    // Return a non-zero revision for the pixels produced by this surface. The backend may reuse
    // the target while this revision and the resolved descriptor remain unchanged.
    virtual uint32_t generation() const = 0;
    // Return Unavailable for a transient synchronization miss; Failed aborts the frame.
    virtual SurfaceRenderResult render(RenderBackend &backend, ResourceId target,
                                       const SurfaceDescriptor &description) = 0;
};

struct PreparedPathRef {
    const PreparedPathData *path = nullptr;
    uint32_t operation_index = 0;
};

struct PreparedImageRef {
    const PreparedTexture *image = nullptr;
};

class FrameResources {
  public:
    bool bind_path(ResourceId id, const PreparedPathData &path, uint32_t operation_index);
    bool bind_path(ResourceId id, const PreparedPath &path, uint32_t operation_index);
    bool bind_image(ResourceId id, const PreparedTexture &image);
    bool bind_text(ResourceId id, const PreparedGlyphs &glyphs);
    bool bind_surface(ResourceId id, SurfaceProducer &producer);
    bool bind_graphics_image(ResourceId id, nk_graphics_image image);
    const PreparedPathRef *path(ResourceId id) const;
    const PreparedImageRef *image(ResourceId id) const;
    const PreparedGlyphs *text(ResourceId id) const;
    SurfaceProducer *surface(ResourceId id) const;
    const nk_graphics_image *graphics_image(ResourceId id) const;
    void reset();

  private:
    std::unordered_map<uint32_t, PreparedPathRef> paths_;
    std::unordered_map<uint32_t, PreparedImageRef> images_;
    std::unordered_map<uint32_t, const PreparedGlyphs *> texts_;
    std::unordered_map<uint32_t, SurfaceProducer *> surfaces_;
    std::unordered_map<uint32_t, nk_graphics_image> graphics_images_;
};

} // namespace nkui

#endif
