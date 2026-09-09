#include "core/handle_registry.hpp"

#include <limits>

namespace nk::core {

nk_handle HandleRegistry::encode(std::uint32_t index, std::uint16_t generation) {
    return (static_cast<std::uint32_t>(generation) << index_bits) | (index + 1u);
}

std::uint32_t HandleRegistry::decode_index(nk_handle handle) {
    return (handle & index_mask) - 1u;
}

std::uint16_t HandleRegistry::decode_generation(nk_handle handle) {
    return static_cast<std::uint16_t>(handle >> index_bits);
}

nk_handle HandleRegistry::insert(ResourceType type, std::shared_ptr<Resource> resource) {
    if (type == ResourceType::none || !resource)
        return NK_INVALID_HANDLE;
    std::lock_guard lock(mutex_);
    for (std::uint32_t index = 0; index < slots_.size(); ++index) {
        auto &slot = slots_[index];
        if (!slot.resource) {
            slot.type = type;
            slot.resource = std::move(resource);
            return encode(index, slot.generation);
        }
    }
    if (slots_.size() >= index_mask)
        return NK_INVALID_HANDLE;
    slots_.push_back(Slot{});
    auto &slot = slots_.back();
    slot.type = type;
    slot.resource = std::move(resource);
    return encode(static_cast<std::uint32_t>(slots_.size() - 1), slot.generation);
}

std::shared_ptr<Resource> HandleRegistry::get(nk_handle handle, ResourceType type) const {
    if (handle == NK_INVALID_HANDLE || (handle & index_mask) == 0)
        return {};
    std::lock_guard lock(mutex_);
    const auto index = decode_index(handle);
    if (index >= slots_.size())
        return {};
    const auto &slot = slots_[index];
    if (slot.generation != decode_generation(handle) || slot.type != type)
        return {};
    return slot.resource;
}

bool HandleRegistry::erase(nk_handle handle, ResourceType type) {
    if (handle == NK_INVALID_HANDLE || (handle & index_mask) == 0)
        return false;
    std::lock_guard lock(mutex_);
    const auto index = decode_index(handle);
    if (index >= slots_.size())
        return false;
    auto &slot = slots_[index];
    if (!slot.resource || slot.generation != decode_generation(handle) || slot.type != type)
        return false;
    slot.resource.reset();
    slot.type = ResourceType::none;
    slot.generation = static_cast<std::uint16_t>((slot.generation % max_generation) + 1u);
    return true;
}

void HandleRegistry::clear() {
    std::lock_guard lock(mutex_);
    for (auto &slot : slots_) {
        slot.resource.reset();
        slot.type = ResourceType::none;
        slot.generation = static_cast<std::uint16_t>((slot.generation % max_generation) + 1u);
    }
}

} // namespace nk::core
