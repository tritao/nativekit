#pragma once

#include "ids.hpp"

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nkscene {

template<class T>
class ComponentStore {
public:
    bool contains(OccurrenceId id) const noexcept { return sparse.contains(id); }

    T *find(OccurrenceId id) noexcept {
        const auto found = sparse.find(id);
        return found == sparse.end() ? nullptr : &dense_values[found->second];
    }

    const T *find(OccurrenceId id) const noexcept {
        const auto found = sparse.find(id);
        return found == sparse.end() ? nullptr : &dense_values[found->second];
    }

    T &insert_or_assign(OccurrenceId id, T value) {
        const auto found = sparse.find(id);
        if (found != sparse.end()) {
            dense_values[found->second] = std::move(value);
            return dense_values[found->second];
        }
        const auto index = static_cast<std::uint32_t>(dense_values.size());
        sparse.emplace(id, index);
        dense_ids.push_back(id);
        dense_values.push_back(std::move(value));
        return dense_values.back();
    }

    bool erase(OccurrenceId id) noexcept {
        const auto found = sparse.find(id);
        if (found == sparse.end())
            return false;
        const auto index = found->second;
        const auto last = dense_values.size() - 1;
        if (index != last) {
            dense_ids[index] = dense_ids[last];
            dense_values[index] = std::move(dense_values[last]);
            sparse[dense_ids[index]] = index;
        }
        dense_ids.pop_back();
        dense_values.pop_back();
        sparse.erase(found);
        return true;
    }

    void clear() noexcept {
        dense_ids.clear();
        dense_values.clear();
        sparse.clear();
    }

    std::size_t size() const noexcept { return dense_values.size(); }

    template<class Fn>
    void for_each(Fn &&fn) const {
        for (std::size_t index = 0; index < dense_values.size(); ++index)
            fn(dense_ids[index], dense_values[index]);
    }

private:
    std::vector<OccurrenceId> dense_ids;
    std::vector<T> dense_values;
    std::unordered_map<OccurrenceId, std::uint32_t> sparse;
};

} // namespace nkscene
