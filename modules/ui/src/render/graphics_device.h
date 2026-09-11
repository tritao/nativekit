#ifndef NATIVEKIT_UI_GRAPHICS_DEVICE_H
#define NATIVEKIT_UI_GRAPHICS_DEVICE_H

#include <memory>
#include <string>

namespace nkui {

/**
 * Shared owner of the UI Sokol runtime.
 *
 * Sokol's graphics runtime is process-global for the active graphics context,
 * so renderer instances must retain the same device instead of independently
 * calling sg_setup() and sg_shutdown(). The device is acquired after the
 * caller has made the intended NativeKit surface current.
 */
class GraphicsDevice {
  public:
    static std::shared_ptr<GraphicsDevice> acquire(std::string *error = nullptr);

    ~GraphicsDevice();
    GraphicsDevice(const GraphicsDevice &) = delete;
    GraphicsDevice &operator=(const GraphicsDevice &) = delete;

    bool valid() const { return valid_; }

  private:
    GraphicsDevice();

    bool valid_ = false;
};

} // namespace nkui

#endif
