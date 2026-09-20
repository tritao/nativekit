#ifndef NATIVEKIT_UI_FRAME_RESOURCES_H
#define NATIVEKIT_UI_FRAME_RESOURCES_H

#include "display_list/display_list.h"
#include "nativekit_graphics.h"
#include "prepare/nanovg_path.h"
#include "prepare/text_engine.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace nkui {

class UiRenderer;

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
    virtual SurfaceRenderResult render(UiRenderer &renderer, ResourceId target,
                                       const SurfaceDescriptor &description) = 0;
};

struct PreparedPathRef {
    const PreparedPathData *path = nullptr;
    uint32_t operation_index = 0;
};

struct PreparedImageRef {
    const PreparedTexture *image = nullptr;
};

/**
 * Bindings from resource ids to prepared frame data for immediate execution.
 *
 * This set borrows everything it references, so it is only valid while the
 * frame, session, or producer that built it is alive. Data that has to outlive
 * its builder belongs in an OwnedFrameResources, which sealing requires.
 */
class FrameResources {
  public:
    bool bind_path(ResourceId id, const PreparedPathData &path, uint32_t operation_index,
                   uint64_t content_generation = 0);
    bool bind_path(ResourceId id, const PreparedPath &path, uint32_t operation_index,
                   uint64_t content_generation = 0);
    bool bind_image(ResourceId id, const PreparedTexture &image, uint64_t content_generation = 0);
    bool bind_text(ResourceId id, const PreparedGlyphs &glyphs, uint64_t content_generation = 0);
    bool bind_surface(ResourceId id, SurfaceProducer &producer, uint64_t content_generation = 0);
    bool bind_graphics_image(ResourceId id, nk_graphics_image image,
                             uint64_t content_generation = 0);
    const PreparedPathRef *path(ResourceId id) const;
    const PreparedImageRef *image(ResourceId id) const;
    const PreparedGlyphs *text(ResourceId id) const;
    SurfaceProducer *surface(ResourceId id) const;
    const nk_graphics_image *graphics_image(ResourceId id) const;
    uint64_t content_generation(ResourceId id) const;
    void reset();

  private:
    std::unordered_map<uint32_t, PreparedPathRef> paths_;
    std::unordered_map<uint32_t, PreparedImageRef> images_;
    std::unordered_map<uint32_t, const PreparedGlyphs *> texts_;
    std::unordered_map<uint32_t, SurfaceProducer *> surfaces_;
    std::unordered_map<uint32_t, nk_graphics_image> graphics_images_;
    std::unordered_map<uint32_t, uint64_t> content_generations_;
};

/**
 * Resource bindings that keep everything they reference alive.
 *
 * The type is the contract: borrowed bindings and live surface producers do not
 * exist here, so an owned set stays valid after the frame, session, or display
 * list that produced it is gone. Prepared data is shared as immutable objects,
 * and graphics images are retained until the set is destroyed.
 */
class OwnedFrameResources final : public FrameResources {
  public:
    bool bind_path(ResourceId id, std::shared_ptr<const PreparedPath> path,
                   uint32_t operation_index, uint64_t content_generation = 0);
    bool bind_image(ResourceId id, std::shared_ptr<const PreparedTexture> image,
                    uint64_t content_generation = 0);
    bool bind_text(ResourceId id, std::shared_ptr<const PreparedGlyphs> glyphs,
                   uint64_t content_generation = 0);
    /** Binds a graphics image and retains it until this set is destroyed. */
    bool bind_graphics_image(ResourceId id, nk_graphics_image image,
                             uint64_t content_generation = 0);

    /* Borrowed bindings and live producers cannot belong to an owned set. */
    bool bind_path(ResourceId, const PreparedPathData &, uint32_t, uint64_t) = delete;
    bool bind_path(ResourceId, const PreparedPath &, uint32_t, uint64_t) = delete;
    bool bind_image(ResourceId, const PreparedTexture &, uint64_t) = delete;
    bool bind_text(ResourceId, const PreparedGlyphs &, uint64_t) = delete;
    bool bind_surface(ResourceId, SurfaceProducer &, uint64_t) = delete;

    void reset();

  private:
    struct GraphicsImageLease {
        nk_graphics_image image{};
        ~GraphicsImageLease();
    };

    std::vector<std::shared_ptr<const void>> owners_;
    std::unordered_map<uint32_t, std::shared_ptr<const GraphicsImageLease>> graphics_image_leases_;
};

} // namespace nkui

#endif
