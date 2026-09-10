#ifndef NATIVEKIT_UI_SOKOL_BACKEND_H
#define NATIVEKIT_UI_SOKOL_BACKEND_H

#include "display_list/display_list.h"
#include "prepare/nanovg_recorder.h"
#include "prepare/skribidi_adapter.h"

#include <cstdint>
#include <vector>

namespace nkui {

struct SolidVertex {
    float x;
    float y;
};

struct SolidMesh {
    std::vector<SolidVertex> vertices;
    std::vector<uint32_t> indices;
};

bool triangulate_prepared_path(const NanoVGRecorder &recorder,
                               const PreparedPathOperation &operation, SolidMesh &mesh);

struct SokolBackendStats {
    uint32_t passes = 0;
    uint32_t draws = 0;
    uint32_t pipeline_changes = 0;
    uint32_t binding_changes = 0;
    uint32_t image_uploads = 0;
    uint64_t uploaded_bytes = 0;
    uint64_t transient_bytes = 0;
    uint32_t gpu_resources = 0;
};

class SokolBackend {
  public:
    SokolBackend();
    ~SokolBackend();
    SokolBackend(const SokolBackend &) = delete;
    SokolBackend &operator=(const SokolBackend &) = delete;

    bool initialize();
    bool valid() const;
    bool begin_window_pass(int width, int height, uint32_t framebuffer, bool clear);
    bool begin_target_pass(ResourceId target, int width, int height, bool load_existing);
    bool set_scissor(bool enabled, float x = 0.0f, float y = 0.0f, float width = 0.0f,
                     float height = 0.0f);
    bool draw_path(const NanoVGRecorder &recorder, uint32_t operation_index, float opacity = 1.0f);
    bool draw_path_transformed(const NanoVGRecorder &recorder, uint32_t operation_index,
                               const float transform[6], float opacity = 1.0f);
    bool draw_paths(const NanoVGRecorder &recorder);
    bool upload_atlases(SkribidiAdapter &adapter, bool include_clean = false);
    bool draw_glyphs(const PreparedGlyphs &glyphs, float opacity = 1.0f);
    bool draw_glyphs_transformed(const PreparedGlyphs &glyphs, const float transform[6],
                                 float origin_x, float origin_y, float opacity = 1.0f);
    bool draw_target(ResourceId target, float x, float y, float width, float height, float opacity);
    bool end_pass();
    bool commit_frame();
    bool end_frame();
    SokolBackendStats stats() const;
    const char *last_error() const;

    struct State;

  private:
    State *state_ = nullptr;
};

} // namespace nkui

#endif
