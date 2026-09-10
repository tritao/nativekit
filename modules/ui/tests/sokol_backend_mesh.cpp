#include "prepare/nanovg_recorder.h"
#include "render/sokol_backend.h"

using namespace nkui;

int main() {
    NanoVGRecorder recorder;
    NVGcontext *vg = recorder.context();
    nvgBeginFrame(vg, 100.0f, 100.0f, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, 10.0f, 20.0f, 30.0f, 40.0f);
    nvgFill(vg);
    nvgEndFrame(vg);
    if (recorder.operations().size() != 1)
        return 1;
    SolidMesh mesh;
    if (!triangulate_prepared_path(recorder, recorder.operations()[0], mesh))
        return 2;
    return mesh.vertices.size() >= 4 && mesh.indices.size() >= 6 ? 0 : 3;
}
