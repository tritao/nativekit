#pragma once

#include "ids.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace nkscene {

class HierarchyIndex {
public:
    OccurrenceId parent(OccurrenceId id) const noexcept {
        const auto found = parents.find(id);
        return found == parents.end() ? invalid_occurrence : found->second;
    }

    const std::vector<OccurrenceId> &children(OccurrenceId id) const noexcept {
        const auto found = child_lists.find(id);
        return found == child_lists.end() ? empty_children : found->second;
    }

    bool is_descendant(OccurrenceId id, OccurrenceId ancestor) const noexcept {
        for (auto current = parent(id); current.valid(); current = parent(current)) {
            if (current == ancestor)
                return true;
        }
        return false;
    }

    bool reparent(OccurrenceId id, OccurrenceId new_parent) {
        if (id == new_parent || is_descendant(new_parent, id))
            return false;
        const auto old_parent = parent(id);
        if (old_parent.valid())
            remove_child(old_parent, id);
        parents[id] = new_parent;
        if (new_parent.valid())
            child_lists[new_parent].push_back(id);
        return true;
    }

    void add(OccurrenceId id) { parents.emplace(id, invalid_occurrence); }

    void remove(OccurrenceId id) {
        const auto old_parent = parent(id);
        if (old_parent.valid())
            remove_child(old_parent, id);
        parents.erase(id);
        child_lists.erase(id);
    }

private:
    void remove_child(OccurrenceId parent_id, OccurrenceId id) {
        auto found = child_lists.find(parent_id);
        if (found == child_lists.end())
            return;
        auto &children_for_parent = found->second;
        children_for_parent.erase(
            std::remove(children_for_parent.begin(), children_for_parent.end(), id),
            children_for_parent.end());
        if (children_for_parent.empty())
            child_lists.erase(found);
    }

    std::unordered_map<OccurrenceId, OccurrenceId> parents;
    std::unordered_map<OccurrenceId, std::vector<OccurrenceId>> child_lists;
    inline static const std::vector<OccurrenceId> empty_children;
};

} // namespace nkscene
