#include "prepare/nanovg_path.h"
#include "prepare/nanovg_recorder.h"

#include "nanovg.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

struct AllocationStats {
    int allocations = 0;
    int reallocations = 0;
    int frees = 0;
};

void *allocate(void *user_ptr, size_t size) {
    ++static_cast<AllocationStats *>(user_ptr)->allocations;
    return std::malloc(size);
}

void *reallocate(void *user_ptr, void *ptr, size_t size) {
    ++static_cast<AllocationStats *>(user_ptr)->reallocations;
    return std::realloc(ptr, size);
}

void release(void *user_ptr, void *ptr) {
    ++static_cast<AllocationStats *>(user_ptr)->frees;
    std::free(ptr);
}

} // namespace

using namespace nkui;

void append_complex_path(NanoVGPath &path) {
    path.move_to(10.0f, 10.0f);
    path.line_to(90.0f, 10.0f);
    path.line_to(90.0f, 90.0f);
    path.quad_to(90.0f, 100.0f, 80.0f, 90.0f);
    path.arc_to(50.0f, 50.0f, 10.0f, 90.0f, 8.0f);
    path.close();
}

void append_complex_path(NVGcontext *context) {
    nvgMoveTo(context, 10.0f, 10.0f);
    nvgLineTo(context, 90.0f, 10.0f);
    nvgLineTo(context, 90.0f, 90.0f);
    nvgQuadTo(context, 90.0f, 100.0f, 80.0f, 90.0f);
    nvgArcTo(context, 50.0f, 50.0f, 10.0f, 90.0f, 8.0f);
    nvgClosePath(context);
}

void append_hole_path(NanoVGPath &path) {
    path.move_to(10.0f, 10.0f);
    path.line_to(110.0f, 10.0f);
    path.line_to(110.0f, 110.0f);
    path.line_to(10.0f, 110.0f);
    path.close();
    path.set_winding(NVG_CCW);
    path.move_to(35.0f, 35.0f);
    path.line_to(35.0f, 85.0f);
    path.line_to(85.0f, 85.0f);
    path.line_to(85.0f, 35.0f);
    path.close();
    path.set_winding(NVG_CW);
}

void append_hole_path(NVGcontext *context) {
    nvgMoveTo(context, 10.0f, 10.0f);
    nvgLineTo(context, 110.0f, 10.0f);
    nvgLineTo(context, 110.0f, 110.0f);
    nvgLineTo(context, 10.0f, 110.0f);
    nvgClosePath(context);
    nvgPathWinding(context, NVG_CCW);
    nvgMoveTo(context, 35.0f, 35.0f);
    nvgLineTo(context, 35.0f, 85.0f);
    nvgLineTo(context, 85.0f, 85.0f);
    nvgLineTo(context, 85.0f, 35.0f);
    nvgClosePath(context);
    nvgPathWinding(context, NVG_CW);
}

void append_open_stroke_path(NanoVGPath &path) {
    path.move_to(12.0f, 18.0f);
    path.line_to(58.0f, 72.0f);
    path.line_to(106.0f, 28.0f);
}

void append_open_stroke_path(NVGcontext *context) {
    nvgMoveTo(context, 12.0f, 18.0f);
    nvgLineTo(context, 58.0f, 72.0f);
    nvgLineTo(context, 106.0f, 28.0f);
}

bool near(float left, float right) {
    return std::abs(left - right) <= 0.00001f;
}

bool matches_recorded(const PreparedGeometry &direct, const PreparedPathData &recorded,
                     uint32_t operation_index, bool compare_fill_rule = true,
                     bool compare_fringe = true) {
    if (operation_index >= recorded.operations().size())
        return false;
    const auto &operation = recorded.operations()[operation_index];
    if (direct.paths.size() != operation.path_count ||
        (compare_fringe && !near(direct.fringe_width, operation.fringe)) ||
        !near(direct.stroke_width, operation.stroke_width) ||
        (compare_fill_rule && direct.fill_rule != operation.fill_rule))
        return false;
    if (operation.kind == PreparedPathKind::Fill)
        for (size_t index = 0; index < direct.bounds.size(); ++index)
            if (!near(direct.bounds[index], operation.bounds[index]))
            return false;
    uint32_t vertex_offset = UINT32_MAX;
    uint32_t vertex_end = 0;
    for (uint32_t index = 0; index < operation.path_count; ++index) {
        const auto &expected = recorded.paths()[operation.path_offset + index];
        if (expected.fill_count) {
            const uint32_t offset = static_cast<uint32_t>(expected.fill_offset);
            const uint32_t count = static_cast<uint32_t>(expected.fill_count);
            vertex_offset = std::min(vertex_offset, offset);
            vertex_end = std::max(vertex_end, offset + count);
        }
        if (expected.stroke_count) {
            const uint32_t offset = static_cast<uint32_t>(expected.stroke_offset);
            const uint32_t count = static_cast<uint32_t>(expected.stroke_count);
            vertex_offset = std::min(vertex_offset, offset);
            vertex_end = std::max(vertex_end, offset + count);
        }
    }
    if (vertex_offset == UINT32_MAX || direct.vertices.size() != vertex_end - vertex_offset)
        return false;
    for (uint32_t index = 0; index < operation.path_count; ++index) {
        const auto &expected = recorded.paths()[operation.path_offset + index];
        const auto &actual = direct.paths[index];
        if (actual.fill_offset != expected.fill_offset - vertex_offset ||
            actual.fill_count != expected.fill_count ||
            actual.stroke_offset != expected.stroke_offset - vertex_offset ||
            actual.stroke_count != expected.stroke_count || actual.closed != expected.closed ||
            actual.convex != expected.convex || actual.winding != expected.winding)
            return false;
    }
    for (size_t index = 0; index < direct.vertices.size(); ++index) {
        const auto &expected = recorded.vertices()[vertex_offset + index];
        const auto &actual = direct.vertices[index];
        if (!near(actual.x, expected.x) || !near(actual.y, expected.y) ||
            !near(actual.u, expected.u) || !near(actual.v, expected.v))
            return false;
    }
    return true;
}

bool direct_matches_compatibility() {
    NanoVGPath path;
    append_complex_path(path);
    NanoVGPath hole_path;
    append_hole_path(hole_path);
    NanoVGPath open_stroke_path;
    append_open_stroke_path(open_stroke_path);
    NanoVGRecorder recorder;
    NVGcontext *context = recorder.context();
    if (!path.valid() || !hole_path.valid() || !open_stroke_path.valid() || !recorder.valid() ||
        !context)
        return false;

    const unsigned char image_pixels[4] = {20, 80, 140, 220};
    const int image = nvgCreateImageRGBA(context, 1, 1, NVG_IMAGE_NEAREST, image_pixels);
    if (!image)
        return false;
    nvgBeginFrame(context, 120.0f, 120.0f, 1.0f);
    nvgBeginPath(context);
    append_complex_path(context);
    nvgFillPaint(context, nvgLinearGradient(context, 0.0f, 0.0f, 120.0f, 120.0f,
                                            nvgRGBA(10, 20, 30, 255), nvgRGBA(220, 230, 240, 255)));
    nvgFill(context);
    nvgBeginPath(context);
    append_complex_path(context);
    nvgStrokeWidth(context, 4.0f);
    nvgLineCap(context, NVG_ROUND);
    nvgLineJoin(context, NVG_ROUND);
    nvgStrokeColor(context, nvgRGBA(240, 240, 240, 255));
    nvgStroke(context);
    nvgBeginPath(context);
    append_complex_path(context);
    nvgFillPaint(context, nvgImagePattern(context, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, image, 1.0f));
    nvgFill(context);
    nvgBeginPath(context);
    append_hole_path(context);
    nvgFillColor(context, nvgRGBA(120, 140, 180, 255));
    nvgFill(context);
    const float transformed[6] = {1.35f, 0.25f, -0.15f, 0.8f, 17.0f, 23.0f};
    nvgResetTransform(context);
    nvgTransform(context, transformed[0], transformed[1], transformed[2], transformed[3],
                 transformed[4], transformed[5]);
    nvgBeginPath(context);
    append_complex_path(context);
    nvgShapeAntiAlias(context, 0);
    nvgStrokeWidth(context, 5.5f);
    nvgLineCap(context, NVG_BUTT);
    nvgLineJoin(context, NVG_MITER);
    nvgMiterLimit(context, 2.5f);
    nvgStrokeColor(context, nvgRGBA(240, 240, 240, 255));
    nvgStroke(context);
    nvgResetTransform(context);
    nvgShapeAntiAlias(context, 1);
    nvgBeginPath(context);
    append_open_stroke_path(context);
    nvgStrokeWidth(context, 3.25f);
    nvgLineCap(context, NVG_SQUARE);
    nvgLineJoin(context, NVG_BEVEL);
    nvgMiterLimit(context, 7.0f);
    nvgStroke(context);
    nvgEndFrame(context);
    if (recorder.operations().size() != 6 || recorder.textures().size() != 1)
        return false;

    PathPreparationParams fill_params;
    PreparedGeometry fill;
    PathPreparationParams stroke_params;
    stroke_params.stroke_width = 4.0f;
    stroke_params.line_cap = PathLineCap::Round;
    stroke_params.line_join = PathLineJoin::Round;
    PreparedGeometry stroke;
    if (!prepare_fill(path, fill_params, fill) || !prepare_stroke(path, stroke_params, stroke) ||
        !matches_recorded(fill, recorder.data(), 0) ||
        !matches_recorded(stroke, recorder.data(), 1))
        return false;

    PathPreparationParams image_fill_params;
    PreparedGeometry image_fill;
    const auto &image_operation = recorder.operations()[2];
    if (!prepare_fill(path, image_fill_params, image_fill) ||
        !matches_recorded(image_fill, recorder.data(), 2) || !image_operation.paint.image ||
        image_operation.paint.image != static_cast<PreparedImageToken>(image) ||
        recorder.textures().size() != 1 || recorder.textures()[0].token != image_operation.paint.image)
        return false;

    PathPreparationParams hole_params;
    PreparedGeometry hole;
    if (!prepare_fill(hole_path, hole_params, hole) ||
        !matches_recorded(hole, recorder.data(), 3))
        return false;
    hole_params.fill_rule = PathFillRule::EvenOdd;
    PreparedGeometry even_odd_hole;
    if (!prepare_fill(hole_path, hole_params, even_odd_hole) ||
        even_odd_hole.fill_rule != PathFillRule::EvenOdd ||
        !matches_recorded(even_odd_hole, recorder.data(), 3, false))
        return false;

    PathPreparationParams transformed_stroke_params;
    transformed_stroke_params.transform = {transformed[0], transformed[1], transformed[2],
                                           transformed[3], transformed[4], transformed[5]};
    transformed_stroke_params.edge_antialias = false;
    transformed_stroke_params.fringe_width = 0.0f;
    transformed_stroke_params.stroke_width =
        5.5f * (std::sqrt(transformed[0] * transformed[0] + transformed[2] * transformed[2]) +
                std::sqrt(transformed[1] * transformed[1] + transformed[3] * transformed[3])) *
        0.5f;
    transformed_stroke_params.line_cap = PathLineCap::Butt;
    transformed_stroke_params.line_join = PathLineJoin::Miter;
    transformed_stroke_params.miter_limit = 2.5f;
    PreparedGeometry transformed_stroke;
    if (!prepare_stroke(path, transformed_stroke_params, transformed_stroke) ||
        !matches_recorded(transformed_stroke, recorder.data(), 4, true, false))
        return false;

    PathPreparationParams open_stroke_params;
    open_stroke_params.stroke_width = 3.25f;
    open_stroke_params.line_cap = PathLineCap::Square;
    open_stroke_params.line_join = PathLineJoin::Bevel;
    open_stroke_params.miter_limit = 7.0f;
    PreparedGeometry open_stroke;
    if (!prepare_stroke(open_stroke_path, open_stroke_params, open_stroke) ||
        !matches_recorded(open_stroke, recorder.data(), 5))
        return false;
    return true;
}

int main() {
    NanoVGPath path;
    if (!path.valid() || !path.empty())
        return 1;
    append_complex_path(path);
    std::array<float, 4> bounds{};
    if (!path.bounds(bounds) || bounds[0] > 10.0f || bounds[1] > 10.0f || bounds[2] < 90.0f ||
        bounds[3] < 90.0f)
        return 2;

    PathPreparationParams fill_params;
    fill_params.transform = {2.0f, 0.0f, 0.0f, 2.0f, 5.0f, 7.0f};
    fill_params.fill_rule = PathFillRule::EvenOdd;
    PreparedGeometry fill;
    if (!prepare_fill(path, fill_params, fill) || fill.paths.empty() || fill.vertices.empty() ||
        fill.fill_rule != PathFillRule::EvenOdd || fill.bounds[0] < 25.0f ||
        fill.bounds[1] < 27.0f)
        return 3;
    const auto fill_path_capacity = fill.paths.capacity();
    const auto fill_vertex_capacity = fill.vertices.capacity();
    if (!prepare_fill(path, fill_params, fill) || fill.paths.capacity() != fill_path_capacity ||
        fill.vertices.capacity() != fill_vertex_capacity)
        return 11;

    PathPreparationParams stroke_params;
    stroke_params.edge_antialias = false;
    stroke_params.fringe_width = 0.0f;
    stroke_params.stroke_width = 4.0f;
    stroke_params.line_cap = PathLineCap::Round;
    stroke_params.line_join = PathLineJoin::Round;
    PreparedGeometry stroke;
    if (!prepare_stroke(path, stroke_params, stroke) || stroke.paths.empty() ||
        stroke.vertices.empty() || stroke.fringe_width != 0.0f || stroke.stroke_width != 4.0f)
        return 4;

    AllocationStats allocation_stats;
    NVGprepareAllocator allocator{&allocation_stats, allocate, reallocate, release};
    NVGprepareParams native_params;
    nvgInitPrepareParams(&native_params);
    native_params.edgeAntiAlias = 1;
    native_params.fillRule = NVG_FILL_NON_ZERO;
    native_params.allocator = &allocator;
    NVGpathBuilder *native_path = nvgCreatePathBuilder();
    if (native_path == nullptr)
        return 5;
    nvgPathMoveTo(native_path, 10.0f, 10.0f);
    nvgPathLineTo(native_path, 90.0f, 10.0f);
    nvgPathLineTo(native_path, 90.0f, 90.0f);
    nvgPathLineTo(native_path, 10.0f, 90.0f);
    nvgPathClose(native_path);
    NVGprepareOutput query{};
    if (nvgPrepareFill(native_path, &native_params, &query) != NVG_PREPARE_OUTPUT_TOO_SMALL ||
        query.pathCount <= 0 || query.vertexCount <= 0) {
        nvgDeletePathBuilder(native_path);
        return 6;
    }
    std::vector<NVGpreparedPath> native_paths(static_cast<size_t>(query.pathCount));
    std::vector<NVGvertex> native_vertices(static_cast<size_t>(query.vertexCount));
    NVGprepareOutput prepared{};
    prepared.paths = native_paths.data();
    prepared.pathCapacity = static_cast<int>(native_paths.size());
    prepared.vertices = native_vertices.data();
    prepared.vertexCapacity = static_cast<int>(native_vertices.size());
    if (nvgPrepareFill(native_path, &native_params, &prepared) != NVG_PREPARE_OK ||
        allocation_stats.allocations == 0 || allocation_stats.frees == 0) {
        nvgDeletePathBuilder(native_path);
        return 7;
    }
    nvgDeletePathBuilder(native_path);

    NanoVGPath copy(path);
    NanoVGPath appended;
    if (!copy.valid() || copy.empty() || !appended.append_transformed(copy, {1, 0, 0, 1, 3, 4}) ||
        appended.empty())
        return 8;
    appended.reset();
    if (!appended.empty())
        return 9;
    return direct_matches_compatibility() ? 0 : 10;
}
