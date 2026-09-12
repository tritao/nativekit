#include "frame_resources.h"

namespace nkui {

bool FrameResources::bind_path(ResourceId id, const PreparedPathData &path,
                               uint32_t operation_index) {
    if (!is_resource_id(id, ResourceKind::Path) || operation_index >= path.operations().size())
        return false;
    paths_[id.value] = {&path, operation_index};
    return true;
}

bool FrameResources::bind_path(ResourceId id, const PreparedPath &path,
                               uint32_t operation_index) {
    return bind_path(id, path.data(), operation_index);
}

bool FrameResources::bind_image(ResourceId id, const PreparedTexture &image) {
    if (!is_resource_id(id, ResourceKind::Image) || image.width <= 0 || image.height <= 0 ||
        image.pixels.empty())
        return false;
    images_[id.value] = {&image};
    return true;
}

bool FrameResources::bind_text(ResourceId id, const PreparedGlyphs &glyphs) {
    if (!is_resource_id(id, ResourceKind::TextLayout))
        return false;
    texts_[id.value] = &glyphs;
    return true;
}

bool FrameResources::bind_surface(ResourceId id, SurfaceProducer &producer) {
    if (!is_resource_id(id, ResourceKind::RenderTarget))
        return false;
    surfaces_[id.value] = &producer;
    return true;
}

bool FrameResources::bind_graphics_image(ResourceId id, nk_graphics_image image) {
    if (!is_resource_id(id, ResourceKind::RenderTarget) || !image.id)
        return false;
    graphics_images_[id.value] = image;
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
    return found == texts_.end() ? nullptr : found->second;
}

SurfaceProducer *FrameResources::surface(ResourceId id) const {
    const auto found = surfaces_.find(id.value);
    return found == surfaces_.end() ? nullptr : found->second;
}

const nk_graphics_image *FrameResources::graphics_image(ResourceId id) const {
    const auto found = graphics_images_.find(id.value);
    return found == graphics_images_.end() ? nullptr : &found->second;
}

void FrameResources::reset() {
    paths_.clear();
    images_.clear();
    texts_.clear();
    surfaces_.clear();
    graphics_images_.clear();
}

} // namespace nkui
