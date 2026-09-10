#include "frame_resources.h"

namespace nkui {

bool FrameResources::bind_path(ResourceId id, const NanoVGRecorder &recorder,
                               uint32_t operation_index) {
    if (!is_resource_id(id, ResourceKind::Path) || operation_index >= recorder.operations().size())
        return false;
    paths_[id.value] = {&recorder, operation_index};
    return true;
}

bool FrameResources::bind_text(ResourceId id, const PreparedGlyphs &glyphs) {
    if (!is_resource_id(id, ResourceKind::TextLayout))
        return false;
    texts_[id.value] = &glyphs;
    return true;
}

const PreparedPathRef *FrameResources::path(ResourceId id) const {
    const auto found = paths_.find(id.value);
    return found == paths_.end() ? nullptr : &found->second;
}

const PreparedGlyphs *FrameResources::text(ResourceId id) const {
    const auto found = texts_.find(id.value);
    return found == texts_.end() ? nullptr : found->second;
}

void FrameResources::reset() {
    paths_.clear();
    texts_.clear();
}

} // namespace nkui
