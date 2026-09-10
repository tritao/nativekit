#include "render/frame_resources.h"

using namespace nkui;

int main() {
    NanoVGRecorder recorder;
    NVGcontext *vg = recorder.context();
    nvgBeginFrame(vg, 100.0f, 100.0f, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, 10.0f, 10.0f);
    nvgFill(vg);
    nvgEndFrame(vg);
    const auto path = make_resource_id(ResourceKind::Path, 2, 3);
    const auto text = make_resource_id(ResourceKind::TextLayout, 2, 4);
    PreparedGlyphs glyphs;
    FrameResources resources;
    if (!resources.bind_path(path, recorder, 0) || resources.bind_path(text, recorder, 0) ||
        resources.bind_path(path, recorder, 1) || !resources.bind_text(text, glyphs) ||
        resources.bind_text(path, glyphs))
        return 1;
    if (!resources.path(path) || resources.path(path)->operation_index != 0 ||
        resources.text(text) != &glyphs)
        return 2;
    resources.reset();
    return !resources.path(path) && !resources.text(text) ? 0 : 3;
}
