#include "nativekit_file_watch.h"

#include "core/error.hpp"
#include "core/file_watch.hpp"
#include "core/runtime.hpp"
#include "core/thread_name.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

namespace {

struct FileWatchResource final : nk::core::Resource {
    struct Directory {
        std::string path;
        bool recursive = false;
    };
    struct PendingMove {
        std::string path;
        bool directory = false;
        std::chrono::steady_clock::time_point timestamp;
    };
    /* A recursive registration whose subtree the worker still has to walk. */
    struct Walk {
        std::string root;
        std::uint64_t registration = 0;
    };

    int descriptor = -1;
    /* Wakes the worker for a queued walk or to stop. */
    int wake = -1;
    uint64_t generation = 0;
    nk_file_watch handle = NK_INVALID_HANDLE;
    std::mutex mutex;
    std::unordered_map<int, Directory> directories;
    /* Watch descriptor by path, so adding a directory does not scan every watched one. */
    std::unordered_map<std::string, int> by_path;
    /* Recursive roots and the registration that walks them; a walk stops once its root is removed
     * or added again. */
    std::unordered_map<std::string, std::uint64_t> roots;
    std::deque<Walk> walks;
    std::uint64_t next_registration = 0;
    /* Subtrees left out of recursive registrations. */
    std::vector<std::string> excluded;
    std::unordered_map<uint32_t, PendingMove> pending_moves;
    std::atomic<bool> stopping{false};
    std::thread worker;

    ~FileWatchResource() override { stop(); }

    void stop() noexcept {
        if (stopping.exchange(true, std::memory_order_acq_rel))
            return;
        wake_worker();
        if (worker.joinable())
            worker.join();
        if (descriptor >= 0) {
            close(descriptor);
            descriptor = -1;
        }
        if (wake >= 0) {
            close(wake);
            wake = -1;
        }
    }

    void wake_worker() noexcept {
        if (wake >= 0) {
            const std::uint64_t one = 1;
            [[maybe_unused]] const auto written = write(wake, &one, sizeof(one));
        }
    }

    static std::string normalize(const char *path) {
        if (!path || !*path)
            return {};
        std::filesystem::path value(path);
        if (!value.is_absolute())
            return {};
        return value.lexically_normal().string();
    }

    static bool within(const std::string &path, const std::string &directory) {
        return path == directory ||
               (path.size() > directory.size() &&
                path.compare(0, directory.size(), directory) == 0 && path[directory.size()] == '/');
    }

    bool excluded_locked(const std::string &path) const {
        for (const auto &directory : excluded)
            if (within(path, directory))
                return true;
        return false;
    }

    void forget_locked(int watch_descriptor) {
        const auto found = directories.find(watch_descriptor);
        if (found == directories.end())
            return;
        const auto indexed = by_path.find(found->second.path);
        if (indexed != by_path.end() && indexed->second == watch_descriptor)
            by_path.erase(indexed);
        directories.erase(found);
    }

    nk_result add_one_locked(const std::string &path, bool recursive) {
        if (excluded_locked(path))
            return NK_OK;
        if (const auto found = by_path.find(path); found != by_path.end()) {
            auto &directory = directories[found->second];
            directory.recursive = directory.recursive || recursive;
            return NK_OK;
        }
        const auto watch_descriptor =
            inotify_add_watch(descriptor, path.c_str(),
                              IN_CREATE | IN_DELETE | IN_MODIFY | IN_ATTRIB | IN_MOVED_FROM |
                                  IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_CLOSE_WRITE);
        if (watch_descriptor < 0) {
            if (errno == ENOENT || errno == ENOTDIR || errno == EACCES)
                return NK_ERROR_INVALID_ARGUMENT;
            return NK_ERROR_UNKNOWN;
        }
        // The kernel returns an existing descriptor for an inode already watched under another
        // path.
        if (const auto existing = directories.find(watch_descriptor);
            existing != directories.end()) {
            existing->second.recursive = existing->second.recursive || recursive;
            return NK_OK;
        }
        directories.emplace(watch_descriptor, Directory{path, recursive});
        by_path.emplace(path, watch_descriptor);
        return NK_OK;
    }

    /* Watches every directory under `root`, skipping excluded subtrees, while `wanted` holds. Takes
     * the lock for each directory only, so changes keep being reported while a large tree is
     * registered. */
    template <typename Wanted> void walk(const std::string &root, Wanted wanted) {
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, error),
            end;
        while (!error && iterator != end) {
            if (stopping.load(std::memory_order_acquire))
                return;
            std::error_code status_error;
            if (iterator->is_directory(status_error) && !status_error) {
                const auto path = iterator->path().lexically_normal().string();
                std::lock_guard lock(mutex);
                if (!wanted())
                    return;
                if (excluded_locked(path))
                    iterator.disable_recursion_pending();
                else
                    add_one_locked(path, true);
            }
            iterator.increment(error);
        }
    }

    nk_result add_directory(const char *raw_path, bool recursive) {
        const auto path = normalize(raw_path);
        std::error_code error;
        if (path.empty() || !std::filesystem::is_directory(path, error) || error) {
            nk::core::set_error("file-watch path must be an accessible absolute directory");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        std::uint64_t registration = 0;
        {
            std::lock_guard lock(mutex);
            const auto result = add_one_locked(path, recursive);
            if (result != NK_OK)
                return result;
            registration = ++next_registration;
            if (recursive) {
                roots[path] = registration;
                walks.push_back(Walk{path, registration});
            }
        }
        if (recursive)
            wake_worker();
        else
            emit_ready(path, registration);
        return NK_OK;
    }

    nk_result exclude_directory(const char *raw_path) {
        const auto path = normalize(raw_path);
        if (path.empty()) {
            nk::core::set_error("file-watch path must be an absolute UTF-8 directory");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        std::lock_guard lock(mutex);
        if (!excluded_locked(path))
            excluded.push_back(path);
        std::vector<int> removals;
        for (const auto &[watch_descriptor, directory] : directories)
            if (within(directory.path, path))
                removals.push_back(watch_descriptor);
        for (const auto watch_descriptor : removals) {
            inotify_rm_watch(descriptor, watch_descriptor);
            forget_locked(watch_descriptor);
        }
        return NK_OK;
    }

    nk_result remove_directory(const char *raw_path) {
        const auto path = normalize(raw_path);
        if (path.empty()) {
            nk::core::set_error("file-watch path must be an absolute UTF-8 directory");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        std::lock_guard lock(mutex);
        std::vector<int> removals;
        for (const auto &[watch_descriptor, directory] : directories) {
            if (within(directory.path, path))
                removals.push_back(watch_descriptor);
        }
        if (removals.empty()) {
            nk::core::set_error("file-watch directory is not registered");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        for (auto root = roots.begin(); root != roots.end();)
            root = within(root->first, path) ? roots.erase(root) : std::next(root);
        for (const auto watch_descriptor : removals) {
            inotify_rm_watch(descriptor, watch_descriptor);
            forget_locked(watch_descriptor);
        }
        return NK_OK;
    }

    /* Walks the subtrees queued by recursive adds and reports each one ready. */
    void run_walks() {
        for (;;) {
            Walk next;
            {
                std::lock_guard lock(mutex);
                if (walks.empty())
                    return;
                next = std::move(walks.front());
                walks.pop_front();
            }
            const auto current = [this, &next] {
                const auto found = roots.find(next.root);
                return found != roots.end() && found->second == next.registration;
            };
            walk(next.root, current);
            if (stopping.load(std::memory_order_acquire))
                return;
            bool finished = false;
            {
                std::lock_guard lock(mutex);
                finished = current();
            }
            if (finished)
                emit_ready(next.root, next.registration);
        }
    }

    void emit_ready(const std::string &path, std::uint64_t registration) {
        if (stopping.load(std::memory_order_acquire) ||
            !nk::core::is_runtime_generation(generation))
            return;
        nk::core::push_event(nk::core::file_watch_ready_event(handle, path, registration));
    }

    void emit(nk_event_kind event_kind, nk_file_change_type change, nk_file_item_kind item_kind,
              nk_file_event_flags flags, const std::string &path,
              const std::string &old_path = {}) {
        if (stopping.load(std::memory_order_acquire) ||
            !nk::core::is_runtime_generation(generation))
            return;
        nk::core::QueuedEvent event;
        event.kind = event_kind;
        event.source = handle;
        event.data = nk::core::file_changed_payload(change, item_kind, flags, path, old_path);
        /* A change rejected by a full queue becomes an overflow, which the
         * queue always accepts, so callers rescan instead of missing it. */
        if (nk::core::push_event(std::move(event)) == NK_ERROR_QUEUE_FULL)
            nk::core::push_event(nk::core::file_watch_overflow_event(handle));
    }

    void emit_overflow() {
        if (stopping.load(std::memory_order_acquire) ||
            !nk::core::is_runtime_generation(generation))
            return;
        nk::core::push_event(nk::core::file_watch_overflow_event(handle));
    }

    std::string directory_path(int watch_descriptor) {
        std::lock_guard lock(mutex);
        const auto found = directories.find(watch_descriptor);
        return found == directories.end() ? std::string{} : found->second.path;
    }

    bool recursive(int watch_descriptor) {
        std::lock_guard lock(mutex);
        const auto found = directories.find(watch_descriptor);
        return found != directories.end() && found->second.recursive;
    }

    void add_created_directory(const std::string &path) {
        {
            std::lock_guard lock(mutex);
            if (excluded_locked(path) || add_one_locked(path, true) != NK_OK)
                return;
        }
        walk(path, [] { return true; });
    }

    void update_renamed_directory(const std::string &old_path, const std::string &new_path) {
        std::lock_guard lock(mutex);
        for (auto &[watch_descriptor, directory] : directories) {
            if (!within(directory.path, old_path))
                continue;
            const auto renamed = new_path + directory.path.substr(old_path.size());
            const auto indexed = by_path.find(directory.path);
            if (indexed != by_path.end() && indexed->second == watch_descriptor)
                by_path.erase(indexed);
            directory.path = renamed;
            by_path[renamed] = watch_descriptor;
        }
    }

    void flush_moves() {
        const auto now = std::chrono::steady_clock::now();
        std::vector<PendingMove> expired;
        {
            std::lock_guard lock(mutex);
            for (auto iterator = pending_moves.begin(); iterator != pending_moves.end();) {
                if (now - iterator->second.timestamp > std::chrono::milliseconds(500)) {
                    expired.push_back(std::move(iterator->second));
                    iterator = pending_moves.erase(iterator);
                } else {
                    ++iterator;
                }
            }
        }
        for (const auto &move : expired)
            emit(NK_EVENT_FILE_CHANGED, NK_FILE_CHANGE_REMOVED,
                 move.directory ? NK_FILE_ITEM_DIRECTORY : NK_FILE_ITEM_FILE, 0, move.path);
    }

    void process(inotify_event *record, const char *name) {
        if (record->mask & IN_Q_OVERFLOW) {
            emit_overflow();
            return;
        }
        if (record->mask & IN_IGNORED) {
            // The kernel dropped the watch (its directory was deleted, or it was removed).
            std::lock_guard lock(mutex);
            forget_locked(record->wd);
            return;
        }
        const auto base = directory_path(record->wd);
        if (base.empty())
            return;
        std::string path = base;
        if (name && *name)
            path += "/" + std::string(name);
        const bool is_directory = (record->mask & IN_ISDIR) != 0;
        const auto item_kind = is_directory ? NK_FILE_ITEM_DIRECTORY : NK_FILE_ITEM_FILE;
        if (record->mask & IN_MOVED_FROM) {
            std::lock_guard lock(mutex);
            pending_moves[record->cookie] =
                PendingMove{path, is_directory, std::chrono::steady_clock::now()};
            return;
        }
        if (record->mask & IN_MOVED_TO) {
            PendingMove moved;
            bool paired = false;
            {
                std::lock_guard lock(mutex);
                const auto found = pending_moves.find(record->cookie);
                if (found != pending_moves.end()) {
                    moved = std::move(found->second);
                    pending_moves.erase(found);
                    paired = true;
                }
            }
            if (is_directory && recursive(record->wd))
                add_created_directory(path);
            if (paired) {
                if (moved.directory)
                    update_renamed_directory(moved.path, path);
                emit(NK_EVENT_FILE_CHANGED, NK_FILE_CHANGE_MOVED, item_kind, 0, path, moved.path);
            } else {
                emit(NK_EVENT_FILE_CHANGED, NK_FILE_CHANGE_ADDED, item_kind, 0, path);
            }
            return;
        }
        if (record->mask & IN_CREATE) {
            if (is_directory && recursive(record->wd))
                add_created_directory(path);
            emit(NK_EVENT_FILE_CHANGED, NK_FILE_CHANGE_ADDED, item_kind, 0, path);
        } else if (record->mask & (IN_DELETE | IN_DELETE_SELF)) {
            emit(NK_EVENT_FILE_CHANGED, NK_FILE_CHANGE_REMOVED, item_kind, 0, path);
        } else if (record->mask & (IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE)) {
            emit(NK_EVENT_FILE_CHANGED, NK_FILE_CHANGE_MODIFIED, item_kind, 0, path);
        }
    }

    void run_impl() {
        std::vector<char> buffer(64 * 1024);
        while (!stopping.load(std::memory_order_acquire)) {
            run_walks();
            pollfd polls[2] = {{descriptor, POLLIN, 0}, {wake, POLLIN, 0}};
            // The wake descriptor ends the wait for a queued walk or a stop; the timeout completes
            // unpaired moves.
            const auto ready = poll(polls, 2, 100);
            if (ready < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (ready > 0 && (polls[1].revents & POLLIN)) {
                std::uint64_t count = 0;
                [[maybe_unused]] const auto drained = read(wake, &count, sizeof(count));
            }
            auto &descriptor_poll = polls[0];
            if (ready > 0 && (descriptor_poll.revents & POLLIN)) {
                const auto size = read(descriptor, buffer.data(), buffer.size());
                if (size > 0) {
                    std::size_t offset = 0;
                    while (offset + sizeof(inotify_event) <= static_cast<std::size_t>(size)) {
                        auto *record = reinterpret_cast<inotify_event *>(buffer.data() + offset);
                        const auto record_size = sizeof(inotify_event) + record->len;
                        if (offset + record_size > static_cast<std::size_t>(size))
                            break;
                        process(record, record->len ? buffer.data() + offset + sizeof(inotify_event)
                                                    : nullptr);
                        offset += record_size;
                    }
                } else if (size < 0 && errno != EAGAIN && errno != EINTR) {
                    break;
                }
            }
            flush_moves();
        }
        flush_moves();
    }

    void run() noexcept { run_impl(); }
};

} // namespace

namespace nk::backend {

nk_result file_watch_create(const nk_file_watch_options *, nk_file_watch *out_watch) noexcept {
    const auto descriptor = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (descriptor < 0) {
        nk::core::set_error("inotify is unavailable");
        return NK_ERROR_UNSUPPORTED;
    }
    auto resource = std::make_shared<FileWatchResource>();
    resource->descriptor = descriptor;
    resource->wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (resource->wake < 0) {
        nk::core::set_error("eventfd is unavailable");
        return NK_ERROR_UNSUPPORTED;
    }
    resource->generation = nk::core::runtime_generation();
    const auto handle = nk::core::handles().insert(nk::core::ResourceType::file_watch, resource);
    if (handle == NK_INVALID_HANDLE)
        return NK_ERROR_OUT_OF_MEMORY;
    resource->handle = handle;
    resource->worker = std::thread([resource] {
        nk::core::name_current_thread("nk.file-watch");
        resource->run();
    });
    *out_watch = handle;
    return NK_OK;
}

nk_result file_watch_add_directory(nk_file_watch watch, const char *path,
                                   nk_bool recursive) noexcept {
    const auto resource = nk::core::handles().get(watch, nk::core::ResourceType::file_watch);
    if (!resource) {
        nk::core::set_error("invalid file-watch handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    return std::static_pointer_cast<FileWatchResource>(resource)->add_directory(path,
                                                                                recursive != 0);
}

nk_result file_watch_exclude_directory(nk_file_watch watch, const char *path) noexcept {
    const auto resource = nk::core::handles().get(watch, nk::core::ResourceType::file_watch);
    if (!resource) {
        nk::core::set_error("invalid file-watch handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    return std::static_pointer_cast<FileWatchResource>(resource)->exclude_directory(path);
}

nk_result file_watch_remove_directory(nk_file_watch watch, const char *path) noexcept {
    const auto resource = nk::core::handles().get(watch, nk::core::ResourceType::file_watch);
    if (!resource) {
        nk::core::set_error("invalid file-watch handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    return std::static_pointer_cast<FileWatchResource>(resource)->remove_directory(path);
}

nk_result file_watch_destroy(nk_file_watch watch) noexcept {
    const auto resource = nk::core::handles().get(watch, nk::core::ResourceType::file_watch);
    if (!resource) {
        nk::core::set_error("invalid file-watch handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    std::static_pointer_cast<FileWatchResource>(resource)->stop();
    if (!nk::core::handles().erase(watch, nk::core::ResourceType::file_watch))
        return NK_ERROR_INVALID_HANDLE;
    return NK_OK;
}

} // namespace nk::backend
