#include "graphics_device.h"

#define SOKOL_GLCORE
#include "sokol_gfx.h"

#include <mutex>

namespace nkui {
namespace {

std::mutex device_mutex;
std::weak_ptr<GraphicsDevice> shared_device;

} // namespace

GraphicsDevice::GraphicsDevice() {
    // A valid Sokol runtime not represented by shared_device belongs to
    // another NativeKit graphics owner. Do not shut that runtime down when
    // this device is released.
    if (sg_isvalid())
        return;
    sg_desc desc{};
    desc.environment.defaults = {SG_PIXELFORMAT_RGBA8, SG_PIXELFORMAT_DEPTH_STENCIL, 1};
    sg_setup(&desc);
    valid_ = sg_isvalid();
}

GraphicsDevice::~GraphicsDevice() {
    if (!valid_)
        return;
    std::lock_guard<std::mutex> lock(device_mutex);
    sg_shutdown();
}

std::shared_ptr<GraphicsDevice> GraphicsDevice::acquire(std::string *error) {
    std::lock_guard<std::mutex> lock(device_mutex);
    if (auto device = shared_device.lock())
        return device;
    auto device = std::shared_ptr<GraphicsDevice>(new GraphicsDevice);
    if (!device->valid()) {
        if (error)
            *error = sg_isvalid() ? "Sokol runtime is already owned"
                                  : "sg_setup failed";
        return nullptr;
    }
    shared_device = device;
    return device;
}

} // namespace nkui
