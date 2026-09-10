#ifndef NATIVEKIT_UI_SOKOL_BACKEND_H
#define NATIVEKIT_UI_SOKOL_BACKEND_H

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
    bool draw_paths(const NanoVGRecorder &recorder);
    bool upload_atlases(SkribidiAdapter &adapter);
    bool draw_glyphs(const PreparedGlyphs &glyphs);
    bool end_frame();
    SokolBackendStats stats() const;
    const char *last_error() const;

    struct State;

  private:
    State *state_ = nullptr;
};

} // namespace nkui

#endif
