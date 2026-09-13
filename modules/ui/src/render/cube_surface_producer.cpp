#include "cube_surface_producer.h"

#include "render_backend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace nkui {
namespace {

constexpr uint8_t opaque = 255;

constexpr std::array<SurfaceMeshVertex, 24> cube_vertices = {{
    {-1, -1, 1, 52, 190, 238, opaque},   {1, -1, 1, 52, 190, 238, opaque},
    {1, 1, 1, 52, 190, 238, opaque},     {-1, 1, 1, 52, 190, 238, opaque},
    {1, -1, -1, 242, 104, 143, opaque},  {-1, -1, -1, 242, 104, 143, opaque},
    {-1, 1, -1, 242, 104, 143, opaque},  {1, 1, -1, 242, 104, 143, opaque},
    {1, -1, 1, 50, 214, 143, opaque},    {1, -1, -1, 50, 214, 143, opaque},
    {1, 1, -1, 50, 214, 143, opaque},    {1, 1, 1, 50, 214, 143, opaque},
    {-1, -1, -1, 247, 171, 76, opaque},  {-1, -1, 1, 247, 171, 76, opaque},
    {-1, 1, 1, 247, 171, 76, opaque},    {-1, 1, -1, 247, 171, 76, opaque},
    {-1, 1, 1, 143, 105, 245, opaque},   {1, 1, 1, 143, 105, 245, opaque},
    {1, 1, -1, 143, 105, 245, opaque},   {-1, 1, -1, 143, 105, 245, opaque},
    {-1, -1, -1, 255, 207, 112, opaque}, {1, -1, -1, 255, 207, 112, opaque},
    {1, -1, 1, 255, 207, 112, opaque},   {-1, -1, 1, 255, 207, 112, opaque},
}};

constexpr std::array<uint32_t, 36> make_cube_indices() {
    std::array<uint32_t, 36> indices{};
    for (uint32_t face = 0; face < 6; ++face) {
        const uint32_t vertex = face * 4;
        const uint32_t index = face * 6;
        indices[index] = vertex;
        indices[index + 1] = vertex + 1;
        indices[index + 2] = vertex + 2;
        indices[index + 3] = vertex;
        indices[index + 4] = vertex + 2;
        indices[index + 5] = vertex + 3;
    }
    return indices;
}

constexpr auto cube_indices = make_cube_indices();

using Matrix = std::array<float, 16>;

Matrix multiply(const Matrix &left, const Matrix &right) {
    Matrix output{};
    for (size_t column = 0; column < 4; ++column)
        for (size_t row = 0; row < 4; ++row)
            for (size_t inner = 0; inner < 4; ++inner)
                output[column * 4 + row] += left[inner * 4 + row] * right[column * 4 + inner];
    return output;
}

Matrix model_view_projection(float rotation, float aspect) {
    constexpr float near_plane = 0.1f;
    constexpr float far_plane = 20.0f;
    constexpr float field_of_view = 0.7853981633974483f;
    const float focal = 1.0f / std::tan(field_of_view * 0.5f);
    Matrix projection{};
    projection[0] = focal / aspect;
    projection[5] = focal;
    projection[10] = (far_plane + near_plane) / (near_plane - far_plane);
    projection[11] = -1.0f;
    projection[14] = (2.0f * far_plane * near_plane) / (near_plane - far_plane);

    Matrix translation{};
    translation[0] = translation[5] = translation[10] = translation[15] = 1.0f;
    translation[14] = -4.0f;

    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    Matrix rotate_x{};
    rotate_x[0] = rotate_x[15] = 1.0f;
    rotate_x[5] = rotate_x[10] = cosine;
    rotate_x[6] = sine;
    rotate_x[9] = -sine;

    Matrix rotate_y{};
    rotate_y[5] = rotate_y[15] = 1.0f;
    rotate_y[0] = rotate_y[10] = cosine;
    rotate_y[2] = -sine;
    rotate_y[8] = sine;

    return multiply(projection, multiply(translation, multiply(rotate_y, rotate_x)));
}

} // namespace

bool CubeSurfaceProducer::describe(int requested_width, int requested_height,
                                   SurfaceDescriptor &description) const {
    if (requested_width <= 0 || requested_height <= 0)
        return false;
    constexpr int maximum_extent = 768;
    const float reduction =
        std::min(1.0f, static_cast<float>(maximum_extent) /
                           static_cast<float>(std::max(requested_width, requested_height)));
    description.width = std::max(1, static_cast<int>(std::lround(requested_width * reduction)));
    description.height = std::max(1, static_cast<int>(std::lround(requested_height * reduction)));
    description.format = SurfacePixelFormat::Rgba8;
    description.alpha = SurfaceAlphaMode::Premultiplied;
    description.filter = SurfaceFilter::Linear;
    description.color_space = SurfaceColorSpace::Linear;
    return true;
}

void CubeSurfaceProducer::set_rotation(float radians) {
    if (!std::isfinite(radians) || radians == rotation_)
        return;
    constexpr float full_turn = 6.2831853071795865f;
    rotation_ = std::fmod(radians, full_turn);
    if (rotation_ < 0.0f)
        rotation_ += full_turn;
    if (++generation_ == 0)
        generation_ = 1;
}

SurfaceRenderResult CubeSurfaceProducer::render(RenderBackend &backend, ResourceId target,
                                                const SurfaceDescriptor &description) {
    if (description.width <= 0 || description.height <= 0)
        return SurfaceRenderResult::Failed;
    if (!backend.begin_surface_pass(target, description, false))
        return SurfaceRenderResult::Failed;
    SurfaceMeshView mesh{
        cube_vertices, cube_indices,
        model_view_projection(rotation_, static_cast<float>(description.width) /
                                             static_cast<float>(description.height))};
    if (!backend.draw_surface_mesh(mesh)) {
        backend.end_pass();
        return SurfaceRenderResult::Failed;
    }
    return backend.end_pass() ? SurfaceRenderResult::Rendered : SurfaceRenderResult::Failed;
}

} // namespace nkui
