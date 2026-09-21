#ifndef NATIVEKIT_UI_SHOWCASE_OFFSCREEN_SURFACE_H
#define NATIVEKIT_UI_SHOWCASE_OFFSCREEN_SURFACE_H

#include "nativekit_graphics.h"
#include "nativekit_gpu.h"
#include "nativekit_ui.h"

class ShowcaseOffscreenSurface {
  public:
    explicit ShowcaseOffscreenSurface(nk_surface surface);
    ~ShowcaseOffscreenSurface();

    bool create(int width, int height);
    bool update(float rotation, int width, int height);
    void destroy();
    nkui_resource resource() const { return resource_; }
    bool render_on_executor(const nk_surface_frame_target &frame_target, float rotation, int width,
                            int height);
    void destroy_on_executor();

  private:
    nk_surface surface_ = NK_INVALID_HANDLE;
    nkui_resource resource_{};
    nk_graphics_image image_{};
    nkgpu_renderer renderer_{};
    nkgpu_image target_image_{};
    nkgpu_image depth_image_{};
    nkgpu_shader shader_{};
    nkgpu_pipeline pipeline_{};
    nkgpu_buffer index_buffer_{};
    int width_ = 0;
    int height_ = 0;
};

#endif
