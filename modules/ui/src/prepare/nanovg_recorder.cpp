#include "nanovg_recorder.h"

#include "nanovg.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace nkui {

struct NanoVGRecorder::State : PreparedPathData {
    uint32_t flushes = 0;
    int next_texture = 1;
};

namespace {

using State = NanoVGRecorder::State;

int render_create(void *) {
    return 1;
}

int render_create_texture(void *context, int type, int width, int height, int flags,
                          const uint8_t *data) {
    auto &state = *static_cast<State *>(context);
    if (width <= 0 || height <= 0)
        return 0;
    const int id = state.next_texture++;
    const size_t bytes_per_pixel = type == NVG_TEXTURE_RGBA ? 4 : 1;
    const PreparedTextureType prepared_type =
        type == NVG_TEXTURE_RGBA ? PreparedTextureType::Rgba : PreparedTextureType::Alpha;
    PreparedImageFlags prepared_flags = PreparedImageFlags::None;
    if (flags & NVG_IMAGE_GENERATE_MIPMAPS)
        prepared_flags |= PreparedImageFlags::GenerateMipmaps;
    if (flags & NVG_IMAGE_REPEATX)
        prepared_flags |= PreparedImageFlags::RepeatX;
    if (flags & NVG_IMAGE_REPEATY)
        prepared_flags |= PreparedImageFlags::RepeatY;
    if (flags & NVG_IMAGE_FLIPY)
        prepared_flags |= PreparedImageFlags::FlipY;
    if (flags & NVG_IMAGE_PREMULTIPLIED)
        prepared_flags |= PreparedImageFlags::Premultiplied;
    if (flags & NVG_IMAGE_NEAREST)
        prepared_flags |= PreparedImageFlags::Nearest;
    PreparedTexture texture{static_cast<PreparedImageToken>(id), prepared_type, width, height,
                            prepared_flags, 1, true, {}};
    texture.pixels.resize(static_cast<size_t>(width) * height * bytes_per_pixel);
    if (data)
        std::memcpy(texture.pixels.data(), data, texture.pixels.size());
    state.texture_data.push_back(std::move(texture));
    return id;
}

int render_delete_texture(void *context, int image) {
    auto &textures = static_cast<State *>(context)->texture_data;
    const auto found = std::find_if(textures.begin(), textures.end(), [image](const auto &texture) {
        return texture.token == static_cast<PreparedImageToken>(image);
    });
    if (found == textures.end())
        return 0;
    textures.erase(found);
    return 1;
}

int render_update_texture(void *context, int image, int x, int y, int width, int height,
                          const uint8_t *data) {
    auto &textures = static_cast<State *>(context)->texture_data;
    const auto found = std::find_if(textures.begin(), textures.end(), [image](const auto &texture) {
        return texture.token == static_cast<PreparedImageToken>(image);
    });
    if (found == textures.end() || !data || x < 0 || y < 0 || width < 0 || height < 0 ||
        x + width > found->width || y + height > found->height)
        return 0;
    const size_t bytes_per_pixel = found->type == PreparedTextureType::Rgba ? 4 : 1;
    const size_t source_pitch = static_cast<size_t>(found->width) * bytes_per_pixel;
    for (int row = y; row < y + height; ++row)
        std::memcpy(found->pixels.data() +
                        (static_cast<size_t>(row) * found->width + x) * bytes_per_pixel,
                    data + static_cast<size_t>(row) * source_pitch +
                        static_cast<size_t>(x) * bytes_per_pixel,
                    static_cast<size_t>(width) * bytes_per_pixel);
    ++found->generation;
    found->dirty = true;
    return 1;
}

int render_get_texture_size(void *context, int image, int *width, int *height) {
    const auto &textures = static_cast<State *>(context)->texture_data;
    const auto found = std::find_if(textures.begin(), textures.end(), [image](const auto &texture) {
        return texture.token == static_cast<PreparedImageToken>(image);
    });
    if (found == textures.end())
        return 0;
    *width = found->width;
    *height = found->height;
    return 1;
}

void render_viewport(void *, float, float, float) {}

void render_cancel(void *context) {
    auto &state = *static_cast<State *>(context);
    state.operation_data.clear();
    state.path_data.clear();
    state.vertex_data.clear();
}

void render_flush(void *context) {
    ++static_cast<State *>(context)->flushes;
}

PreparedColor prepare_color(const NVGcolor &color) {
    return {color.r, color.g, color.b, color.a};
}

PreparedPaint prepare_paint(const NVGpaint &paint) {
    PreparedPaint result{};
    std::memcpy(result.transform, paint.xform, sizeof(result.transform));
    std::memcpy(result.extent, paint.extent, sizeof(result.extent));
    result.radius = paint.radius;
    result.feather = paint.feather;
    result.inner_color = prepare_color(paint.innerColor);
    result.outer_color = prepare_color(paint.outerColor);
    result.image_token = static_cast<PreparedImageToken>(paint.image);
    return result;
}

uint32_t copy_vertices(State &state, const NVGvertex *vertices, int count) {
    const uint32_t offset = static_cast<uint32_t>(state.vertex_data.size());
    if (vertices && count > 0) {
        state.vertex_data.reserve(state.vertex_data.size() + static_cast<size_t>(count));
        for (int index = 0; index < count; ++index)
            state.vertex_data.push_back({vertices[index].x, vertices[index].y, vertices[index].u,
                                         vertices[index].v});
    }
    return offset;
}

PreparedPathOperation base_operation(PreparedPathKind kind, const NVGpaint &paint, float fringe) {
    PreparedPathOperation operation{};
    operation.kind = kind;
    operation.paint = prepare_paint(paint);
    operation.fringe = fringe;
    return operation;
}

void copy_paths(State &state, PreparedPathOperation &operation, const NVGpath *paths,
                int path_count) {
    operation.path_offset = static_cast<uint32_t>(state.path_data.size());
    operation.path_count = static_cast<uint32_t>(path_count);
    for (int index = 0; index < path_count; ++index) {
        PreparedPathRange path{};
        path.fill_offset = copy_vertices(state, paths[index].fill, paths[index].nfill);
        path.fill_count = static_cast<uint32_t>(paths[index].nfill);
        path.stroke_offset = copy_vertices(state, paths[index].stroke, paths[index].nstroke);
        path.stroke_count = static_cast<uint32_t>(paths[index].nstroke);
        path.closed = paths[index].closed != 0;
        path.convex = paths[index].convex != 0;
        path.winding = paths[index].winding;
        state.path_data.push_back(path);
    }
}

void render_fill(void *context, NVGpaint *paint, NVGcompositeOperationState composite,
                 NVGscissor *scissor, float fringe, const float *bounds, const NVGpath *paths,
                 int path_count) {
    auto &state = *static_cast<State *>(context);
    (void)composite;
    (void)scissor;
    auto operation = base_operation(PreparedPathKind::Fill, *paint, fringe);
    std::memcpy(operation.bounds, bounds, sizeof(operation.bounds));
    copy_paths(state, operation, paths, path_count);
    state.operation_data.push_back(operation);
}

void render_stroke(void *context, NVGpaint *paint, NVGcompositeOperationState composite,
                   NVGscissor *scissor, float fringe, float stroke_width, const NVGpath *paths,
                   int path_count) {
    auto &state = *static_cast<State *>(context);
    (void)composite;
    (void)scissor;
    auto operation = base_operation(PreparedPathKind::Stroke, *paint, fringe);
    operation.stroke_width = stroke_width;
    copy_paths(state, operation, paths, path_count);
    state.operation_data.push_back(operation);
}

void render_triangles(void *context, NVGpaint *paint, NVGcompositeOperationState composite,
                      NVGscissor *scissor, const NVGvertex *vertices, int vertex_count,
                      float fringe) {
    auto &state = *static_cast<State *>(context);
    (void)composite;
    (void)scissor;
    auto operation =
        base_operation(PreparedPathKind::Triangles, *paint, fringe);
    operation.vertex_offset = copy_vertices(state, vertices, vertex_count);
    operation.vertex_count = static_cast<uint32_t>(vertex_count);
    state.operation_data.push_back(operation);
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
    state_->operation_data.clear();
    state_->path_data.clear();
    state_->vertex_data.clear();
    state_->flushes = 0;
}

const std::vector<PreparedPathOperation> &NanoVGRecorder::operations() const {
    return state_->operation_data;
}

const std::vector<PreparedPathRange> &NanoVGRecorder::paths() const {
    return state_->path_data;
}

const std::vector<PreparedVertex> &NanoVGRecorder::vertices() const {
    return state_->vertex_data;
}

const std::vector<PreparedTexture> &NanoVGRecorder::textures() const {
    return state_->texture_data;
}

const PreparedPathData &NanoVGRecorder::data() const {
    return *state_;
}

NanoVGRecorderStats NanoVGRecorder::stats() const {
    return {static_cast<uint32_t>(state_->operation_data.size()),
            static_cast<uint32_t>(state_->path_data.size()),
            static_cast<uint32_t>(state_->vertex_data.size()), state_->flushes};
}

} // namespace nkui
