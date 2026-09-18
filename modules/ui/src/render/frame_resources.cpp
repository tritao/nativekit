#include "frame_resources.h"

namespace nkui {

FrameResources::GraphicsImageLease::~GraphicsImageLease() {
    if (image.id)
        nk_graphics_image_release(image);
}

bool FrameResources::bind_path(ResourceId id, const PreparedPathData &path,
                               uint32_t operation_index, uint64_t content_generation) {
    if (!is_resource_id(id, ResourceKind::Path) || operation_index >= path.operations().size())
        return false;
    paths_[id.value] = {&path, operation_index, {}};
    content_generations_[id.value] = content_generation;
    return true;
}

bool FrameResources::bind_path(ResourceId id, const PreparedPath &path, uint32_t operation_index,
                               uint64_t content_generation) {
    return bind_path(id, path.data(), operation_index, content_generation);
}

bool FrameResources::bind_path(ResourceId id, std::shared_ptr<const PreparedPath> path,
                               uint32_t operation_index, uint64_t content_generation) {
    if (!path || !bind_path(id, path->data(), operation_index, content_generation))
        return false;
    paths_[id.value].owner = std::move(path);
    return true;
}

bool FrameResources::bind_image(ResourceId id, const PreparedTexture &image,
                                uint64_t content_generation) {
    if (!is_resource_id(id, ResourceKind::Image) || image.width <= 0 || image.height <= 0 ||
        image.pixels.empty())
        return false;
    images_[id.value] = {&image, {}};
    content_generations_[id.value] = content_generation;
    return true;
}

bool FrameResources::bind_image(ResourceId id, std::shared_ptr<const PreparedTexture> image,
                                uint64_t content_generation) {
    if (!image || !bind_image(id, *image, content_generation))
        return false;
    images_[id.value].owner = std::move(image);
    return true;
}

bool FrameResources::bind_text(ResourceId id, const PreparedGlyphs &glyphs,
                               uint64_t content_generation) {
    if (!is_resource_id(id, ResourceKind::TextLayout))
        return false;
    texts_[id.value] = {&glyphs, {}};
    content_generations_[id.value] = content_generation;
    return true;
}

bool FrameResources::bind_text(ResourceId id, std::shared_ptr<const PreparedGlyphs> glyphs,
                               uint64_t content_generation) {
    if (!glyphs || !bind_text(id, *glyphs, content_generation))
        return false;
    texts_[id.value].owner = std::move(glyphs);
    return true;
}

bool FrameResources::bind_surface(ResourceId id, SurfaceProducer &producer,
                                  uint64_t content_generation) {
    if (!is_resource_id(id, ResourceKind::RenderTarget))
        return false;
    surfaces_[id.value] = &producer;
    content_generations_[id.value] = content_generation;
    return true;
}

bool FrameResources::bind_graphics_image(ResourceId id, nk_graphics_image image,
                                         uint64_t content_generation) {
    if (!is_resource_id(id, ResourceKind::RenderTarget) || !image.id)
        return false;
    graphics_images_[id.value] = image;
    graphics_image_leases_.erase(id.value);
    content_generations_[id.value] = content_generation;
    return true;
}

bool FrameResources::bind_retained_graphics_image(ResourceId id, nk_graphics_image image,
                                                  uint64_t content_generation) {
    if (!is_resource_id(id, ResourceKind::RenderTarget) || !image.id)
        return false;
    if (nk_graphics_image_retain(image) != NK_OK)
        return false;
    auto lease = std::make_shared<GraphicsImageLease>();
    lease->image = image;
    graphics_images_[id.value] = image;
    graphics_image_leases_[id.value] = std::move(lease);
    content_generations_[id.value] = content_generation;
    return true;
}

const PreparedPathRef *FrameResources::path(ResourceId id) const {
    const auto found = paths_.find(id.value);
    return found == paths_.end() ? nullptr : &found->second;
}

const PreparedImageRef *FrameResources::image(ResourceId id) const {
    const auto found = images_.find(id.value);
    return found == images_.end() ? nullptr : &found->second;
}

const PreparedGlyphs *FrameResources::text(ResourceId id) const {
    const auto found = texts_.find(id.value);
    return found == texts_.end() ? nullptr : found->second.glyphs;
}

SurfaceProducer *FrameResources::surface(ResourceId id) const {
    const auto found = surfaces_.find(id.value);
    return found == surfaces_.end() ? nullptr : found->second;
}

const nk_graphics_image *FrameResources::graphics_image(ResourceId id) const {
    const auto found = graphics_images_.find(id.value);
    return found == graphics_images_.end() ? nullptr : &found->second;
}

uint64_t FrameResources::content_generation(ResourceId id) const {
    const auto found = content_generations_.find(id.value);
    return found == content_generations_.end() ? 0 : found->second;
}

bool FrameResources::has_borrowed_resources() const noexcept {
    for (const auto &[id, entry] : paths_) {
        (void)id;
        if (!entry.owner)
            return true;
    }
    for (const auto &[id, entry] : images_) {
        (void)id;
        if (!entry.owner)
            return true;
    }
    for (const auto &[id, entry] : texts_) {
        (void)id;
        if (!entry.owner)
            return true;
    }
    for (const auto &[id, image] : graphics_images_) {
        (void)image;
        if (graphics_image_leases_.find(id) == graphics_image_leases_.end())
            return true;
    }
    return false;
}

bool FrameResources::has_surface_producers() const noexcept {
    return !surfaces_.empty();
}

void FrameResources::reset() {
    paths_.clear();
    images_.clear();
    texts_.clear();
    surfaces_.clear();
    graphics_images_.clear();
    graphics_image_leases_.clear();
    content_generations_.clear();
}

} // namespace nkui
