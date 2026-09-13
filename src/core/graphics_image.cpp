#include "core/graphics_image_registry.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>

namespace {
struct Slot {
    uint16_t generation = 1;
    uint32_t references = 0;
    bool releasing = false;
    nk_graphics_image_info info{};
    const void *runtime = nullptr;
    uint64_t backend_image = 0;
    nk_core_graphics_image_release_fn release = nullptr;
};

std::mutex registry_mutex;
std::vector<Slot> registry;
std::unordered_map<uint32_t, uint32_t> device_references;

bool retain_device_locked(nk_graphics_device device) {
    if (!device.id)
        return false;
    auto &references = device_references[device.id];
    if (references == std::numeric_limits<uint32_t>::max())
        return false;
    ++references;
    return true;
}

void release_device_locked(nk_graphics_device device) {
    const auto found = device_references.find(device.id);
    if (found == device_references.end())
        return;
    if (--found->second == 0)
        device_references.erase(found);
}

nk_graphics_image handle_for(size_t index, uint16_t generation) {
    return nk_graphics_image{(static_cast<uint32_t>(generation) << 16) |
                             static_cast<uint32_t>(index + 1)};
}

Slot *resolve_locked(nk_graphics_image image) {
    const uint32_t encoded_index = image.id & 0xFFFFu;
    const uint16_t generation = static_cast<uint16_t>(image.id >> 16);
    if (!encoded_index || !generation || encoded_index > registry.size())
        return nullptr;
    Slot &slot = registry[encoded_index - 1];
    return slot.references && !slot.releasing && slot.generation == generation ? &slot : nullptr;
}

void advance_generation(Slot &slot) {
    slot.generation = static_cast<uint16_t>(slot.generation + 1);
    if (!slot.generation)
        slot.generation = 1;
}
} // namespace

extern "C" nk_result NK_CALL nk_core_graphics_image_register(
    nk_graphics_api api, nk_graphics_device device, int32_t width, int32_t height,
    const void *runtime, uint64_t backend_image, nk_core_graphics_image_release_fn release,
    nk_graphics_image *out_image) {
    if (!out_image || !device.id || width <= 0 || height <= 0 || !runtime || !backend_image ||
        !release ||
        (api != NK_GRAPHICS_OPENGL && api != NK_GRAPHICS_OPENGL_ES && api != NK_GRAPHICS_VULKAN))
        return NK_ERROR_INVALID_ARGUMENT;
    out_image->id = 0;
    try {
        std::lock_guard<std::mutex> lock(registry_mutex);
        size_t index = 0;
        for (; index < registry.size(); ++index)
            if (!registry[index].references)
                break;
        if (index == 0xFFFFu)
            return NK_ERROR_OUT_OF_MEMORY;
        if (index == registry.size())
            registry.emplace_back();
        if (!retain_device_locked(device))
            return NK_ERROR_OUT_OF_MEMORY;
        Slot &slot = registry[index];
        slot.references = 1;
        slot.releasing = false;
        slot.info = {sizeof(nk_graphics_image_info), api, device, width, height, {0, 0}};
        slot.runtime = runtime;
        slot.backend_image = backend_image;
        slot.release = release;
        *out_image = handle_for(index, slot.generation);
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return NK_ERROR_UNKNOWN;
    }
}

extern "C" nk_result NK_CALL nk_graphics_image_retain(nk_graphics_image image) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    Slot *slot = resolve_locked(image);
    if (!slot)
        return NK_ERROR_INVALID_HANDLE;
    if (slot->references == std::numeric_limits<uint32_t>::max())
        return NK_ERROR_INVALID_REQUEST;
    ++slot->references;
    return NK_OK;
}

extern "C" nk_result NK_CALL nk_graphics_image_release(nk_graphics_image image) {
    if (const nk_result thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    nk_core_graphics_image_release_fn release = nullptr;
    const void *runtime = nullptr;
    uint64_t backend_image = 0;
    nk_graphics_device device{};
    {
        std::lock_guard<std::mutex> lock(registry_mutex);
        Slot *slot = resolve_locked(image);
        if (!slot)
            return NK_ERROR_INVALID_HANDLE;
        if (slot->references > 1) {
            --slot->references;
            return NK_OK;
        }
        slot->references = 0;
        slot->releasing = true;
        release = slot->release;
        device = slot->info.device;
        runtime = slot->runtime;
        backend_image = slot->backend_image;
    }
    if (!release(runtime, device, backend_image)) {
        std::lock_guard<std::mutex> lock(registry_mutex);
        const uint32_t encoded_index = image.id & 0xFFFFu;
        const uint16_t generation = static_cast<uint16_t>(image.id >> 16);
        if (encoded_index && encoded_index <= registry.size()) {
            Slot &slot = registry[encoded_index - 1];
            if (slot.generation == generation && slot.releasing) {
                slot.references = 1;
                slot.releasing = false;
            }
        }
        return NK_ERROR_INVALID_REQUEST;
    }
    {
        std::lock_guard<std::mutex> lock(registry_mutex);
        const uint32_t encoded_index = image.id & 0xFFFFu;
        const uint16_t generation = static_cast<uint16_t>(image.id >> 16);
        if (!encoded_index || encoded_index > registry.size())
            return NK_ERROR_INVALID_HANDLE;
        Slot &slot = registry[encoded_index - 1];
        if (slot.generation != generation || !slot.releasing)
            return NK_ERROR_INVALID_HANDLE;
        slot.info = {};
        slot.runtime = nullptr;
        slot.backend_image = 0;
        slot.release = nullptr;
        slot.releasing = false;
        release_device_locked(device);
        advance_generation(slot);
    }
    return NK_OK;
}

extern "C" nk_result NK_CALL nk_graphics_image_get_info(nk_graphics_image image,
                                                        nk_graphics_image_info *out_info) {
    if (!out_info || out_info->struct_size < sizeof(nk_graphics_image_info))
        return NK_ERROR_INVALID_ARGUMENT;
    const uint32_t size = out_info->struct_size;
    std::lock_guard<std::mutex> lock(registry_mutex);
    const Slot *slot = resolve_locked(image);
    if (!slot)
        return NK_ERROR_INVALID_HANDLE;
    *out_info = slot->info;
    out_info->struct_size = size;
    return NK_OK;
}

extern "C" nk_result NK_CALL nk_graphics_device_retain(nk_graphics_device device) {
    if (!device.id)
        return NK_ERROR_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(registry_mutex);
        return retain_device_locked(device) ? NK_OK : NK_ERROR_OUT_OF_MEMORY;
    } catch (const std::bad_alloc &) {
        return NK_ERROR_OUT_OF_MEMORY;
    }
}

extern "C" nk_result NK_CALL nk_graphics_device_release(nk_graphics_device device) {
    if (const nk_result thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!device.id)
        return NK_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(registry_mutex);
    const auto found = device_references.find(device.id);
    if (found == device_references.end())
        return NK_ERROR_INVALID_REQUEST;
    release_device_locked(device);
    return NK_OK;
}

extern "C" nk_result NK_CALL nk_core_graphics_image_get_backend(nk_graphics_image image,
                                                                nk_graphics_image_info *out_info,
                                                                const void **out_runtime,
                                                                uint64_t *out_backend_image) {
    if (!out_info || out_info->struct_size < sizeof(nk_graphics_image_info) || !out_runtime ||
        !out_backend_image)
        return NK_ERROR_INVALID_ARGUMENT;
    const uint32_t size = out_info->struct_size;
    std::lock_guard<std::mutex> lock(registry_mutex);
    const Slot *slot = resolve_locked(image);
    if (!slot)
        return NK_ERROR_INVALID_HANDLE;
    *out_info = slot->info;
    out_info->struct_size = size;
    *out_runtime = slot->runtime;
    *out_backend_image = slot->backend_image;
    return NK_OK;
}

extern "C" int NK_CALL nk_core_graphics_device_has_references(nk_graphics_device device) {
    if (!device.id)
        return 0;
    std::lock_guard<std::mutex> lock(registry_mutex);
    return device_references.find(device.id) != device_references.end();
}
