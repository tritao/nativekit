#include "core/request.hpp"

namespace nk::core {

bool RequestRegistry::begin(PendingRequest request) noexcept {
    if (request.id == NK_INVALID_REQUEST_ID || request.generation == 0 || request.kind == 0)
        return false;
    try {
        std::lock_guard lock(mutex_);
        return pending_.emplace(request.id, request).second;
    } catch (...) {
        return false;
    }
}

bool RequestRegistry::get(nk_request_id id, PendingRequest &out) const noexcept {
    std::lock_guard lock(mutex_);
    const auto found = pending_.find(id);
    if (found == pending_.end())
        return false;
    out = found->second;
    return true;
}

bool RequestRegistry::take(nk_request_id id, nk_handle source, std::uint32_t kind,
                           std::uint64_t generation, PendingRequest &out) noexcept {
    std::lock_guard lock(mutex_);
    const auto found = pending_.find(id);
    if (found == pending_.end() || found->second.source != source || found->second.kind != kind ||
        found->second.generation != generation)
        return false;
    out = found->second;
    pending_.erase(found);
    return true;
}

bool RequestRegistry::take_any(nk_request_id id, PendingRequest &out) noexcept {
    std::lock_guard lock(mutex_);
    const auto found = pending_.find(id);
    if (found == pending_.end())
        return false;
    out = found->second;
    pending_.erase(found);
    return true;
}

std::vector<PendingRequest> RequestRegistry::cancel_source(nk_handle source,
                                                           std::uint32_t kind) noexcept {
    std::vector<PendingRequest> canceled;
    std::lock_guard lock(mutex_);
    try {
        std::size_t count = 0;
        for (const auto &entry : pending_)
            if (entry.second.source == source && (kind == 0 || entry.second.kind == kind))
                ++count;
        canceled.reserve(count);
    } catch (...) {
        return {};
    }
    for (auto found = pending_.begin(); found != pending_.end();) {
        if (found->second.source != source || (kind != 0 && found->second.kind != kind)) {
            ++found;
            continue;
        }
        canceled.push_back(found->second);
        found = pending_.erase(found);
    }
    return canceled;
}

std::vector<PendingRequest> RequestRegistry::cancel_all(std::uint32_t kind) noexcept {
    std::vector<PendingRequest> canceled;
    std::lock_guard lock(mutex_);
    try {
        canceled.reserve(pending_.size());
    } catch (...) {
        return {};
    }
    for (auto found = pending_.begin(); found != pending_.end();) {
        if (kind != 0 && found->second.kind != kind) {
            ++found;
            continue;
        }
        canceled.push_back(found->second);
        found = pending_.erase(found);
    }
    return canceled;
}

void RequestRegistry::clear() noexcept {
    std::lock_guard lock(mutex_);
    pending_.clear();
}

RequestRegistry &requests() noexcept {
    static RequestRegistry registry;
    return registry;
}

} // namespace nk::core
