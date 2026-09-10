#ifndef NATIVEKIT_UI_NANOVG_PATH_H
#define NATIVEKIT_UI_NANOVG_PATH_H

#include "prepared_path.h"

#include <array>
#include <cstdint>
#include <vector>

struct NVGpathBuilder;

namespace nkui {

enum class PathLineCap : uint8_t {
    Butt = 0,
    Round,
    Square,
};

enum class PathLineJoin : uint8_t {
    Bevel = 3,
    Round = 1,
    Miter = 4,
};

/** Geometry-only inputs for NanoVG path preparation.
 *
 * Coordinates are transformed before flattening. Stroke width is specified
 * in the transformed coordinate space, which lets callers choose the exact
 * device-pixel geometry they want to retain. A zero tessellation or distance
 * tolerance, and a negative fringe width, select NanoVG's device-ratio
 * defaults. A zero fringe width disables antialias fringe geometry.
 */
struct PathPreparationParams {
    float device_pixel_ratio = 1.0f;
    float tessellation_tolerance = 0.0f;
    float distance_tolerance = 0.0f;
    float fringe_width = -1.0f;
    bool edge_antialias = true;
    PathFillRule fill_rule = PathFillRule::NonZero;
    std::array<float, 6> transform = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    float stroke_width = 1.0f;
    PathLineCap line_cap = PathLineCap::Butt;
    PathLineJoin line_join = PathLineJoin::Miter;
    float miter_limit = 10.0f;
};

/** Caller-owned NativeKit representation of one prepared geometry result. */
struct PreparedGeometry {
    std::vector<PreparedPathRange> paths;
    std::vector<PreparedVertex> vertices;
    std::array<float, 4> bounds{};
    float fringe_width = 0.0f;
    float stroke_width = 0.0f;
    PathFillRule fill_rule = PathFillRule::NonZero;
};

/** One NativeKit-owned prepared path operation and its geometry. */
class PreparedPath {
  public:
    bool set(PreparedPathKind kind, const PreparedGeometry &geometry,
             const PreparedPaint &paint);
    const PreparedPathData &data() const;
    const std::vector<PreparedPathOperation> &operations() const;
    const std::vector<PreparedPathRange> &paths() const;
    const std::vector<PreparedVertex> &vertices() const;
    const std::vector<PreparedTexture> &textures() const;

  private:
    PreparedPathData data_;
};

/** Reusable path commands, independent of an NVGcontext or frame. */
class NanoVGPath {
  public:
    NanoVGPath();
    ~NanoVGPath();
    NanoVGPath(const NanoVGPath &other);
    NanoVGPath &operator=(const NanoVGPath &other);
    NanoVGPath(NanoVGPath &&other) noexcept;
    NanoVGPath &operator=(NanoVGPath &&other) noexcept;

    bool valid() const;
    bool empty() const;
    void reset();
    void move_to(float x, float y);
    void line_to(float x, float y);
    void bezier_to(float c1x, float c1y, float c2x, float c2y, float x, float y);
    void quad_to(float cx, float cy, float x, float y);
    void arc_to(float x1, float y1, float x2, float y2, float radius);
    void close();
    void set_winding(int direction);
    bool append(const NanoVGPath &other);
    bool append_transformed(const NanoVGPath &other, const std::array<float, 6> &transform);
    bool bounds(std::array<float, 4> &out) const;
    NVGpathBuilder *builder() const;

  private:
    NVGpathBuilder *builder_ = nullptr;
};

/** Prepare fill geometry without beginning a frame or invoking a renderer. */
bool prepare_fill(const NanoVGPath &path, const PathPreparationParams &params,
                  PreparedGeometry &output);

/** Prepare stroke geometry without beginning a frame or invoking a renderer. */
bool prepare_stroke(const NanoVGPath &path, const PathPreparationParams &params,
                    PreparedGeometry &output);

} // namespace nkui

#endif
