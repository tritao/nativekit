#include "nanovg_path.h"

#include "nanovg.h"

#include <algorithm>
#include <utility>

namespace nkui {

namespace {

NVGprepareParams native_params(const PathPreparationParams &params) {
    NVGprepareParams native{};
    native.devicePixelRatio = params.device_pixel_ratio;
    native.tessellationTolerance = params.tessellation_tolerance;
    native.distanceTolerance = params.distance_tolerance;
    native.fringeWidth = params.fringe_width;
    native.edgeAntiAlias = params.edge_antialias ? 1 : 0;
    native.fillRule = static_cast<int>(params.fill_rule);
    std::copy(params.transform.begin(), params.transform.end(), native.transform);
    native.strokeWidth = params.stroke_width;
    native.lineCap = static_cast<int>(params.line_cap);
    native.lineJoin = static_cast<int>(params.line_join);
    native.miterLimit = params.miter_limit;
    return native;
}

bool prepare(const NanoVGPath &path, const PathPreparationParams &params, bool stroke,
             PreparedGeometry &output) {
    output = {};
    if (!path.valid() || path.empty())
        return false;
    const NVGprepareParams native = native_params(params);
    NVGprepareOutput query{};
    const int query_result = stroke ? nvgPrepareStroke(path.builder(), &native, &query)
                                    : nvgPrepareFill(path.builder(), &native, &query);
    if (query_result == NVG_PREPARE_INVALID || query.pathCount <= 0 || query.vertexCount <= 0)
        return false;
    output.paths.resize(static_cast<size_t>(query.pathCount));
    output.vertices.resize(static_cast<size_t>(query.vertexCount));
    NVGprepareOutput native_output{};
    native_output.paths = new NVGpreparedPath[static_cast<size_t>(query.pathCount)];
    native_output.pathCapacity = query.pathCount;
    native_output.vertices = new NVGvertex[static_cast<size_t>(query.vertexCount)];
    native_output.vertexCapacity = query.vertexCount;
    const int result = stroke ? nvgPrepareStroke(path.builder(), &native, &native_output)
                              : nvgPrepareFill(path.builder(), &native, &native_output);
    if (result != NVG_PREPARE_OK) {
        delete[] native_output.paths;
        delete[] native_output.vertices;
        output = {};
        return false;
    }
    for (int index = 0; index < native_output.pathCount; ++index) {
        const auto &source = native_output.paths[index];
        output.paths[static_cast<size_t>(index)] = {
            static_cast<uint32_t>(source.fillOffset), static_cast<uint32_t>(source.fillCount),
            static_cast<uint32_t>(source.strokeOffset), static_cast<uint32_t>(source.strokeCount),
            source.closed != 0, source.convex != 0};
    }
    for (int index = 0; index < native_output.vertexCount; ++index) {
        const auto &source = native_output.vertices[index];
        output.vertices[static_cast<size_t>(index)] = {source.x, source.y, source.u, source.v};
    }
    std::copy(std::begin(native_output.bounds), std::end(native_output.bounds), output.bounds.begin());
    output.fringe_width = native_output.fringeWidth;
    output.stroke_width = native_output.strokeWidth;
    output.fill_rule = static_cast<PathFillRule>(native_output.fillRule);
    delete[] native_output.paths;
    delete[] native_output.vertices;
    return true;
}

} // namespace

bool PreparedPath::set(PreparedPathKind kind, const PreparedGeometry &geometry,
                       const PreparedPaint &paint) {
    data_ = {};
    if (geometry.paths.empty() || geometry.vertices.empty())
        return false;
    try {
        data_.path_data = geometry.paths;
        data_.vertex_data = geometry.vertices;
        PreparedPathOperation operation{};
        operation.kind = kind;
        operation.paint = paint;
        operation.fringe = geometry.fringe_width;
        operation.stroke_width = geometry.stroke_width;
        operation.fill_rule = geometry.fill_rule;
        std::copy(geometry.bounds.begin(), geometry.bounds.end(), operation.bounds);
        operation.path_count = static_cast<uint32_t>(data_.path_data.size());
        operation.vertex_count = static_cast<uint32_t>(data_.vertex_data.size());
        data_.operation_data.push_back(operation);
    } catch (...) {
        data_ = {};
        return false;
    }
    return true;
}

const PreparedPathData &PreparedPath::data() const {
    return data_;
}

const std::vector<PreparedPathOperation> &PreparedPath::operations() const {
    return data_.operations();
}

const std::vector<PreparedPathRange> &PreparedPath::paths() const {
    return data_.paths();
}

const std::vector<PreparedVertex> &PreparedPath::vertices() const {
    return data_.vertices();
}

const std::vector<PreparedTexture> &PreparedPath::textures() const {
    return data_.textures();
}

NanoVGPath::NanoVGPath() : builder_(nvgCreatePathBuilder()) {}

NanoVGPath::~NanoVGPath() {
    nvgDeletePathBuilder(builder_);
}

NanoVGPath::NanoVGPath(const NanoVGPath &other) : NanoVGPath() {
    if (builder_ && other.builder_ && !nvgPathBuilderAppend(builder_, other.builder_)) {
        nvgDeletePathBuilder(builder_);
        builder_ = nullptr;
    }
}

NanoVGPath &NanoVGPath::operator=(const NanoVGPath &other) {
    if (this == &other)
        return *this;
    NanoVGPath copy(other);
    *this = std::move(copy);
    return *this;
}

NanoVGPath::NanoVGPath(NanoVGPath &&other) noexcept : builder_(other.builder_) {
    other.builder_ = nullptr;
}

NanoVGPath &NanoVGPath::operator=(NanoVGPath &&other) noexcept {
    if (this == &other)
        return *this;
    nvgDeletePathBuilder(builder_);
    builder_ = other.builder_;
    other.builder_ = nullptr;
    return *this;
}

bool NanoVGPath::valid() const {
    return builder_ != nullptr;
}

bool NanoVGPath::empty() const {
    return nvgPathBuilderIsEmpty(builder_) != 0;
}

void NanoVGPath::reset() {
    nvgResetPathBuilder(builder_);
}

void NanoVGPath::move_to(float x, float y) {
    nvgPathMoveTo(builder_, x, y);
}

void NanoVGPath::line_to(float x, float y) {
    nvgPathLineTo(builder_, x, y);
}

void NanoVGPath::bezier_to(float c1x, float c1y, float c2x, float c2y, float x, float y) {
    nvgPathBezierTo(builder_, c1x, c1y, c2x, c2y, x, y);
}

void NanoVGPath::quad_to(float cx, float cy, float x, float y) {
    nvgPathQuadTo(builder_, cx, cy, x, y);
}

void NanoVGPath::arc_to(float x1, float y1, float x2, float y2, float radius) {
    nvgPathArcTo(builder_, x1, y1, x2, y2, radius);
}

void NanoVGPath::close() {
    nvgPathClose(builder_);
}

void NanoVGPath::set_winding(int direction) {
    nvgPathBuilderWinding(builder_, direction);
}

bool NanoVGPath::append(const NanoVGPath &other) {
    return valid() && other.valid() && nvgPathBuilderAppend(builder_, other.builder_) != 0;
}

bool NanoVGPath::append_transformed(const NanoVGPath &other,
                                    const std::array<float, 6> &transform) {
    return valid() && other.valid() &&
           nvgPathBuilderAppendTransformed(builder_, other.builder_, transform.data()) != 0;
}

bool NanoVGPath::bounds(std::array<float, 4> &out) const {
    return valid() && nvgPathBuilderBounds(builder_, out.data()) != 0;
}

NVGpathBuilder *NanoVGPath::builder() const {
    return builder_;
}

bool prepare_fill(const NanoVGPath &path, const PathPreparationParams &params,
                  PreparedGeometry &output) {
    return prepare(path, params, false, output);
}

bool prepare_stroke(const NanoVGPath &path, const PathPreparationParams &params,
                    PreparedGeometry &output) {
    return prepare(path, params, true, output);
}

} // namespace nkui
