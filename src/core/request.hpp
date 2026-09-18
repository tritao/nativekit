#pragma once

#include "nativekit.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace nk::core {

/**
 * Runtime-owned metadata for one asynchronous operation.
 *
 * `kind`, `operation`, and `auxiliary` are deliberately opaque to the core;
 * each service uses them to identify its typed operation while the registry
 * owns generation checks and the exactly-once terminal claim.
 */
struct PendingRequest {
    nk_request_id id = NK_INVALID_REQUEST_ID;
    std::uint64_t generation = 0;
    nk_handle source = NK_INVALID_HANDLE;
    std::uint32_t kind = 0;
    std::uint64_t operation = 0;
    std::uint64_t auxiliary = 0;
};

/** Common request lifetime core for plugin and native async services. */
class RequestRegistry final {
  public:
    bool begin(PendingRequest request) noexcept;
    bool get(nk_request_id id, PendingRequest &out) const noexcept;
    bool take(nk_request_id id, nk_handle source, std::uint32_t kind,
              std::uint64_t generation, PendingRequest &out) noexcept;
    bool take_any(nk_request_id id, PendingRequest &out) noexcept;
    std::vector<PendingRequest> cancel_source(nk_handle source,
                                              std::uint32_t kind) noexcept;
    std::vector<PendingRequest> cancel_all(std::uint32_t kind) noexcept;
    void clear() noexcept;

  private:
    mutable std::mutex mutex_;
    std::unordered_map<nk_request_id, PendingRequest> pending_;
};

RequestRegistry &requests() noexcept;

} // namespace nk::core
