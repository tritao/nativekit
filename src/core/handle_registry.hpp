#pragma once

#include "nativekit.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace nk::core {

enum class ResourceType : std::uint8_t { none, window, webview, mobile_host, surface };

struct Resource {
    virtual ~Resource() = default;
};

class HandleRegistry {
  public:
    nk_handle insert(ResourceType type, std::shared_ptr<Resource> resource);
    std::shared_ptr<Resource> get(nk_handle handle, ResourceType type) const;
    bool erase(nk_handle handle, ResourceType type);
    void clear();

  private:
    struct Slot {
        std::uint16_t generation = 1;
        ResourceType type = ResourceType::none;
        std::shared_ptr<Resource> resource;
    };

    static constexpr std::uint32_t index_bits = 20;
    static constexpr std::uint32_t index_mask = (1u << index_bits) - 1u;
    static constexpr std::uint32_t max_generation = (1u << (32 - index_bits)) - 1u;

    static nk_handle encode(std::uint32_t index, std::uint16_t generation);
    static std::uint32_t decode_index(nk_handle handle);
    static std::uint16_t decode_generation(nk_handle handle);

    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
};

} // namespace nk::core
