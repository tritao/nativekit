#include "prepare/nanovg_recorder.h"

using namespace nkui;

int main() {
    NanoVGRecorder recorder;
    if (!recorder.valid())
        return 1;
    NVGcontext *vg = recorder.context();
    nvgBeginFrame(vg, 640.0f, 480.0f, 1.0f);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 10.0f, 20.0f, 100.0f, 60.0f, 8.0f);
    nvgFillColor(vg, nvgRGBA(20, 40, 80, 255));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, 5.0f, 5.0f);
    nvgLineTo(vg, 50.0f, 70.0f);
    nvgStrokeWidth(vg, 3.0f);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgStroke(vg);
    nvgEndFrame(vg);

    const auto stats = recorder.stats();
    if (stats.operations != 2 || stats.paths != 2 || !stats.vertices || stats.flushes != 1)
        return 2;
    if (recorder.operations()[0].kind != PreparedPathKind::Fill ||
        recorder.operations()[1].kind != PreparedPathKind::Stroke ||
        recorder.paths()[0].fill_count == 0 || recorder.paths()[1].stroke_count == 0)
        return 3;
    recorder.reset();
    const auto reset = recorder.stats();
    return reset.operations == 0 && reset.paths == 0 && reset.vertices == 0 && reset.flushes == 0
               ? 0
               : 4;
}
