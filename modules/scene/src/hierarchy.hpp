#pragma once

#include "occurrence_store.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace nkscene {

/** Slot-indexed parent and sibling links for the live occurrence set. */
class HierarchyIndex {
public:
    explicit HierarchyIndex(const OccurrenceStore &occurrences) : occurrences_(&occurrences) {}

    OccurrenceHandle parent_handle(OccurrenceHandle handle) const noexcept {
        const auto *entry = find(handle);
        return entry ? entry->parent : OccurrenceHandle{};
    }

    OccurrenceId parent(OccurrenceId id) const noexcept {
        return occurrences_->id(parent_handle(occurrences_->resolve(id)));
    }

    std::vector<OccurrenceId> children(OccurrenceId id) const {
        std::vector<OccurrenceId> result;
        for_each_child(occurrences_->resolve(id), [&](OccurrenceId child, OccurrenceHandle) {
            result.push_back(child);
        });
        return result;
    }

    template<class Fn>
    void for_each_child(OccurrenceHandle parent, Fn &&fn) const {
        const auto *entry = find(parent);
        if (!entry)
            return;
        for (auto child = entry->first_child; child.valid();) {
            const auto *child_entry = find(child);
            const auto next = child_entry ? child_entry->next_sibling : OccurrenceHandle{};
            const auto id = occurrences_->id(child);
            if (id.valid())
                fn(id, child);
            child = next;
        }
    }

    bool is_descendant(OccurrenceHandle id, OccurrenceHandle ancestor) const noexcept {
        for (auto current = parent_handle(id); current.valid(); current = parent_handle(current)) {
            if (current == ancestor)
                return true;
        }
        return false;
    }

    bool is_descendant(OccurrenceId id, OccurrenceId ancestor) const noexcept {
        return is_descendant(occurrences_->resolve(id), occurrences_->resolve(ancestor));
    }

    bool reparent(OccurrenceHandle id, OccurrenceHandle new_parent) {
        if (!find(id) || (new_parent.valid() && !find(new_parent)) || id == new_parent ||
            is_descendant(new_parent, id))
            return false;
        const auto old_parent = parent_handle(id);
        if (old_parent == new_parent)
            return true;
        if (old_parent.valid())
            remove_child(old_parent, id);
        auto &entry = require(id);
        entry.parent = new_parent;
        if (new_parent.valid()) {
            auto &parent_entry = require(new_parent);
            const auto previous = last_child(parent_entry);
            if (previous.valid()) {
                require(previous).next_sibling = id;
                entry.prev_sibling = previous;
            } else {
                parent_entry.first_child = id;
            }
        }
        return true;
    }

    bool reparent(OccurrenceId id, OccurrenceId new_parent) {
        return reparent(occurrences_->resolve(id), occurrences_->resolve(new_parent));
    }

    void add(OccurrenceHandle handle) {
        if (!handle.valid())
            throw std::invalid_argument("cannot add an invalid occurrence handle to hierarchy");
        if (handle.slot >= entries_.size())
            entries_.resize(static_cast<std::size_t>(handle.slot) + 1);
        auto &entry = entries_[handle.slot];
        if (entry.generation != handle.generation)
            entry = Entry{handle.generation};
    }

    void add(OccurrenceId id) { add(occurrences_->resolve(id)); }

    void remove(OccurrenceHandle handle) noexcept {
        auto *entry = find(handle);
        if (!entry)
            return;
        if (entry->parent.valid())
            remove_child(entry->parent, handle);

        auto child = entry->first_child;
        while (child.valid()) {
            auto *child_entry = find(child);
            const auto next = child_entry ? child_entry->next_sibling : OccurrenceHandle{};
            if (child_entry) {
                child_entry->parent = {};
                child_entry->prev_sibling = {};
                child_entry->next_sibling = {};
            }
            child = next;
        }
        *entry = {};
    }

    void remove(OccurrenceId id) noexcept { remove(occurrences_->resolve(id)); }

private:
    struct Entry {
        std::uint32_t generation = 0;
        OccurrenceHandle parent;
        OccurrenceHandle first_child;
        OccurrenceHandle next_sibling;
        OccurrenceHandle prev_sibling;
    };

    Entry *find(OccurrenceHandle handle) noexcept {
        if (!handle.valid() || handle.slot >= entries_.size())
            return nullptr;
        auto &entry = entries_[handle.slot];
        return entry.generation == handle.generation ? &entry : nullptr;
    }

    const Entry *find(OccurrenceHandle handle) const noexcept {
        if (!handle.valid() || handle.slot >= entries_.size())
            return nullptr;
        const auto &entry = entries_[handle.slot];
        return entry.generation == handle.generation ? &entry : nullptr;
    }

    Entry &require(OccurrenceHandle handle) {
        auto *entry = find(handle);
        if (!entry)
            throw std::invalid_argument("occurrence handle is not live in hierarchy");
        return *entry;
    }

    OccurrenceHandle last_child(const Entry &parent) const noexcept {
        auto child = parent.first_child;
        while (child.valid()) {
            const auto *entry = find(child);
            if (!entry || !entry->next_sibling.valid())
                return child;
            child = entry->next_sibling;
        }
        return {};
    }

    void remove_child(OccurrenceHandle parent, OccurrenceHandle child) noexcept {
        auto *parent_entry = find(parent);
        auto *child_entry = find(child);
        if (!parent_entry || !child_entry)
            return;
        if (child_entry->prev_sibling.valid())
            if (auto *previous = find(child_entry->prev_sibling))
                previous->next_sibling = child_entry->next_sibling;
        if (child_entry->next_sibling.valid())
            if (auto *next = find(child_entry->next_sibling))
                next->prev_sibling = child_entry->prev_sibling;
        if (parent_entry->first_child == child)
            parent_entry->first_child = child_entry->next_sibling;
        child_entry->prev_sibling = {};
        child_entry->next_sibling = {};
    }

    const OccurrenceStore *occurrences_;
    std::vector<Entry> entries_;
};

} // namespace nkscene
