#include "nanovg_recorder.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace nkui {

struct NanoVGRecorder::State {
    struct Texture {
        int id = 0;
        int type = 0;
        int width = 0;
        int height = 0;
    };

    std::vector<PreparedPathOperation> operations;
    std::vector<PreparedPathRange> paths;
    std::vector<NVGvertex> vertices;
    std::vector<Texture> textures;
    uint32_t flushes = 0;
    int next_texture = 1;
};

namespace {

using State = NanoVGRecorder::State;

int render_create(void *) {
    return 1;
}

int render_create_texture(void *context, int type, int width, int height, int, const uint8_t *) {
    auto &state = *static_cast<State *>(context);
    const int id = state.next_texture++;
    state.textures.push_back({id, type, width, height});
    return id;
}

int render_delete_texture(void *context, int image) {
    auto &textures = static_cast<State *>(context)->textures;
    const auto found = std::find_if(textures.begin(), textures.end(),
                                    [image](const auto &texture) { return texture.id == image; });
    if (found == textures.end())
        return 0;
    textures.erase(found);
    return 1;
}

int render_update_texture(void *context, int image, int, int, int, int, const uint8_t *) {
    const auto &textures = static_cast<State *>(context)->textures;
    return std::any_of(textures.begin(), textures.end(),
                       [image](const auto &texture) { return texture.id == image; });
}

int render_get_texture_size(void *context, int image, int *width, int *height) {
    const auto &textures = static_cast<State *>(context)->textures;
    const auto found = std::find_if(textures.begin(), textures.end(),
                                    [image](const auto &texture) { return texture.id == image; });
    if (found == textures.end())
        return 0;
    *width = found->width;
    *height = found->height;
    return 1;
}

void render_viewport(void *, float, float, float) {}

void render_cancel(void *context) {
    auto &state = *static_cast<State *>(context);
    state.operations.clear();
    state.paths.clear();
    state.vertices.clear();
}

void render_flush(void *context) {
    ++static_cast<State *>(context)->flushes;
}

uint32_t copy_vertices(State &state, const NVGvertex *vertices, int count) {
    const uint32_t offset = static_cast<uint32_t>(state.vertices.size());
    if (vertices && count > 0)
        state.vertices.insert(state.vertices.end(), vertices, vertices + count);
    return offset;
}

PreparedPathOperation base_operation(PreparedPathKind kind, const NVGpaint &paint,
                                     NVGcompositeOperationState composite,
                                     const NVGscissor &scissor, float fringe) {
    PreparedPathOperation operation{};
    operation.kind = kind;
    operation.paint = paint;
    operation.composite = composite;
    operation.scissor = scissor;
    operation.fringe = fringe;
    return operation;
}

void copy_paths(State &state, PreparedPathOperation &operation, const NVGpath *paths,
                int path_count) {
    operation.path_offset = static_cast<uint32_t>(state.paths.size());
    operation.path_count = static_cast<uint32_t>(path_count);
    for (int index = 0; index < path_count; ++index) {
        PreparedPathRange path{};
        path.fill_offset = copy_vertices(state, paths[index].fill, paths[index].nfill);
        path.fill_count = static_cast<uint32_t>(paths[index].nfill);
        path.stroke_offset = copy_vertices(state, paths[index].stroke, paths[index].nstroke);
        path.stroke_count = static_cast<uint32_t>(paths[index].nstroke);
        path.closed = paths[index].closed != 0;
        path.convex = paths[index].convex != 0;
        state.paths.push_back(path);
    }
}

void render_fill(void *context, NVGpaint *paint, NVGcompositeOperationState composite,
                 NVGscissor *scissor, float fringe, const float *bounds, const NVGpath *paths,
                 int path_count) {
    auto &state = *static_cast<State *>(context);
    auto operation = base_operation(PreparedPathKind::Fill, *paint, composite, *scissor, fringe);
    std::memcpy(operation.bounds, bounds, sizeof(operation.bounds));
    copy_paths(state, operation, paths, path_count);
    state.operations.push_back(operation);
}

void render_stroke(void *context, NVGpaint *paint, NVGcompositeOperationState composite,
                   NVGscissor *scissor, float fringe, float stroke_width, const NVGpath *paths,
                   int path_count) {
    auto &state = *static_cast<State *>(context);
    auto operation = base_operation(PreparedPathKind::Stroke, *paint, composite, *scissor, fringe);
    operation.stroke_width = stroke_width;
    copy_paths(state, operation, paths, path_count);
    state.operations.push_back(operation);
}

void render_triangles(void *context, NVGpaint *paint, NVGcompositeOperationState composite,
                      NVGscissor *scissor, const NVGvertex *vertices, int vertex_count,
                      float fringe) {
    auto &state = *static_cast<State *>(context);
    auto operation =
        base_operation(PreparedPathKind::Triangles, *paint, composite, *scissor, fringe);
    operation.vertex_offset = copy_vertices(state, vertices, vertex_count);
    operation.vertex_count = static_cast<uint32_t>(vertex_count);
    state.operations.push_back(operation);
}

void render_delete(void *) {}

} // namespace

NanoVGRecorder::NanoVGRecorder() : state_(new State) {
    NVGparams params{};
    params.userPtr = state_;
    params.edgeAntiAlias = 1;
    params.renderCreate = render_create;
    params.renderCreateTexture = render_create_texture;
    params.renderDeleteTexture = render_delete_texture;
    params.renderUpdateTexture = render_update_texture;
    params.renderGetTextureSize = render_get_texture_size;
    params.renderViewport = render_viewport;
    params.renderCancel = render_cancel;
    params.renderFlush = render_flush;
    params.renderFill = render_fill;
    params.renderStroke = render_stroke;
    params.renderTriangles = render_triangles;
    params.renderDelete = render_delete;
    context_ = nvgCreateInternal(&params);
}

NanoVGRecorder::~NanoVGRecorder() {
    if (context_)
        nvgDeleteInternal(context_);
    delete state_;
}

bool NanoVGRecorder::valid() const {
    return context_ != nullptr;
}

NVGcontext *NanoVGRecorder::context() const {
    return context_;
}

void NanoVGRecorder::reset() {
    state_->operations.clear();
    state_->paths.clear();
    state_->vertices.clear();
    state_->flushes = 0;
}

const std::vector<PreparedPathOperation> &NanoVGRecorder::operations() const {
    return state_->operations;
}

const std::vector<PreparedPathRange> &NanoVGRecorder::paths() const {
    return state_->paths;
}

const std::vector<NVGvertex> &NanoVGRecorder::vertices() const {
    return state_->vertices;
}

NanoVGRecorderStats NanoVGRecorder::stats() const {
    return {static_cast<uint32_t>(state_->operations.size()),
            static_cast<uint32_t>(state_->paths.size()),
            static_cast<uint32_t>(state_->vertices.size()), state_->flushes};
}

} // namespace nkui
