#ifndef NATIVEKIT_UI_COMPOSITOR_H
#define NATIVEKIT_UI_COMPOSITOR_H

#include "compositor/render_plan.h"

#include <cstdint>

namespace nkui {

struct CompositorError {
    uint32_t command_index = 0;
    const char *message = nullptr;
};

class Compositor {
  public:
    bool compile(const DisplayList &display_list, ResourceId main_target, RenderPlan &plan,
                 CompositorError *error = nullptr);

  private:
    ResourceId allocate_transient_target();
    uint16_t next_target_slot_ = 0x8000;
};

} // namespace nkui

#endif
