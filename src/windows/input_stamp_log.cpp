#include "windows/input_stamps.hpp"

namespace nk::windows_input_stamps {

void Log::record(const Stamp &stamp) noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    ring_[next_] = Entry{stamp, false};
    next_ = (next_ + 1) % capacity;
    if (count_ < capacity)
        ++count_;
}

std::uint64_t Log::take(Kind kind, std::uint32_t time_ms, std::int32_t x, std::int32_t y,
                        std::uint32_t code) noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto at = [&](std::size_t age) -> Entry & {
        return ring_[(next_ + capacity - 1 - age) % capacity];
    };
    const auto matches = [&](const Stamp &stamp) {
        if (stamp.kind != kind || stamp.time_ms != time_ms)
            return false;
        if (kind != Kind::key && (stamp.x != x || stamp.y != y))
            return false;
        return kind == Kind::move || stamp.code == code;
    };
    if (kind == Kind::move) {
        for (std::size_t age = 0; age < count_; ++age)
            if (matches(at(age).stamp))
                return at(age).stamp.time_ns;
        return 0;
    }
    for (std::size_t age = count_; age-- > 0;) {
        Entry &entry = at(age);
        if (!entry.taken && matches(entry.stamp)) {
            entry.taken = true;
            return entry.stamp.time_ns;
        }
    }
    return 0;
}

} // namespace nk::windows_input_stamps
