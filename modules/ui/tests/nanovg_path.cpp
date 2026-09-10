#include "prepare/nanovg_path.h"

#include "nanovg.h"

#include <array>
#include <cstdlib>

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

int main() {
    NanoVGPath path;
    if (!path.valid() || !path.empty())
        return 1;
    path.move_to(10.0f, 10.0f);
    path.line_to(90.0f, 10.0f);
    path.line_to(90.0f, 90.0f);
    path.quad_to(90.0f, 100.0f, 80.0f, 90.0f);
    path.arc_to(50.0f, 50.0f, 10.0f, 90.0f, 8.0f);
    path.close();
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
    NVGprepareParams native_params{};
    native_params.edgeAntiAlias = 1;
    native_params.fillRule = NVG_FILL_NON_ZERO;
    native_params.transform[0] = 1.0f;
    native_params.transform[3] = 1.0f;
    native_params.lineJoin = NVG_MITER;
    native_params.allocator = &allocator;
    NVGprepareOutput query{};
    if (nvgPrepareFill(path.builder(), &native_params, &query) != NVG_PREPARE_OUTPUT_TOO_SMALL ||
        query.pathCount <= 0 || query.vertexCount <= 0)
        return 5;
    std::vector<NVGpreparedPath> native_paths(static_cast<size_t>(query.pathCount));
    std::vector<NVGvertex> native_vertices(static_cast<size_t>(query.vertexCount));
    NVGprepareOutput prepared{};
    prepared.paths = native_paths.data();
    prepared.pathCapacity = static_cast<int>(native_paths.size());
    prepared.vertices = native_vertices.data();
    prepared.vertexCapacity = static_cast<int>(native_vertices.size());
    if (nvgPrepareFill(path.builder(), &native_params, &prepared) != NVG_PREPARE_OK ||
        allocation_stats.allocations == 0 || allocation_stats.frees == 0)
        return 6;

    NanoVGPath copy(path);
    NanoVGPath appended;
    if (!copy.valid() || copy.empty() || !appended.append_transformed(copy, {1, 0, 0, 1, 3, 4}) ||
        appended.empty())
        return 7;
    appended.reset();
    return appended.empty() ? 0 : 8;
}
