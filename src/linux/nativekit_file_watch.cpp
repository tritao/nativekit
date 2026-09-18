#include "nativekit_file_watch.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
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

    int descriptor = -1;
    uint64_t generation = 0;
    nk_file_watch handle = NK_INVALID_HANDLE;
    std::mutex mutex;
    std::unordered_map<int, Directory> directories;
    std::unordered_map<uint32_t, PendingMove> pending_moves;
    std::atomic<bool> stopping{false};
    std::thread worker;

    ~FileWatchResource() override { stop(); }

    void stop() noexcept {
        if (stopping.exchange(true, std::memory_order_acq_rel))
            return;
        if (worker.joinable())
            worker.join();
        if (descriptor >= 0) {
            close(descriptor);
            descriptor = -1;
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

    nk_result add_one_locked(const std::string &path, bool recursive) {
        for (auto &[watch_descriptor, directory] : directories) {
            (void)watch_descriptor;
            if (directory.path == path) {
                directory.recursive = directory.recursive || recursive;
                return NK_OK;
            }
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
        directories.emplace(watch_descriptor, Directory{path, recursive});
        return NK_OK;
    }

    nk_result add_directory(const char *raw_path, bool recursive) {
        const auto path = normalize(raw_path);
        std::error_code error;
        if (path.empty() || !std::filesystem::is_directory(path, error) || error) {
            nk::core::set_error("file-watch path must be an accessible absolute directory");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        std::lock_guard lock(mutex);
        auto result = add_one_locked(path, recursive);
        if (result != NK_OK || !recursive)
            return result;
        for (std::filesystem::recursive_directory_iterator
                 iterator(path, std::filesystem::directory_options::skip_permission_denied, error),
             end;
             iterator != end && !error; iterator.increment(error)) {
            if (iterator->is_directory(error) && !error) {
                result = add_one_locked(iterator->path().lexically_normal().string(), true);
                if (result != NK_OK)
                    return result;
            }
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
            if (directory.path == path || (directory.path.size() > path.size() &&
                                           directory.path.compare(0, path.size(), path) == 0 &&
                                           directory.path[path.size()] == '/'))
                removals.push_back(watch_descriptor);
        }
        if (removals.empty()) {
            nk::core::set_error("file-watch directory is not registered");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        for (const auto watch_descriptor : removals) {
            inotify_rm_watch(descriptor, watch_descriptor);
            directories.erase(watch_descriptor);
        }
        return NK_OK;
    }

    static std::vector<std::byte> make_payload(nk_file_change_type change, nk_file_item_kind kind,
                                               nk_file_event_flags flags, const std::string &path,
                                               const std::string &old_path) {
        nk_file_changed_event header{};
        header.change_type = change;
        header.item_kind = kind;
        header.flags = flags;
        header.path_offset = path.empty() ? 0u : static_cast<uint32_t>(sizeof(header));
        header.path_length = static_cast<uint32_t>(path.size());
        header.old_path_offset =
            old_path.empty() ? 0u : header.path_offset + header.path_length + 1u;
        header.old_path_length = static_cast<uint32_t>(old_path.size());
        std::size_t size = sizeof(header);
        if (!path.empty())
            size = header.path_offset + path.size() + 1u;
        if (!old_path.empty())
            size = header.old_path_offset + old_path.size() + 1u;
        std::vector<std::byte> result(size);
        std::memcpy(result.data(), &header, sizeof(header));
        if (!path.empty())
            std::memcpy(result.data() + header.path_offset, path.c_str(), path.size() + 1u);
        if (!old_path.empty())
            std::memcpy(result.data() + header.old_path_offset, old_path.c_str(),
                        old_path.size() + 1u);
        return result;
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
        event.data = make_payload(change, item_kind, flags, path, old_path);
        nk::core::push_event(std::move(event));
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
        std::lock_guard lock(mutex);
        if (add_one_locked(path, true) != NK_OK)
            return;
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator
                 iterator(path, std::filesystem::directory_options::skip_permission_denied, error),
             end;
             iterator != end && !error; iterator.increment(error))
            if (iterator->is_directory(error) && !error)
                add_one_locked(iterator->path().lexically_normal().string(), true);
    }

    void update_renamed_directory(const std::string &old_path, const std::string &new_path) {
        std::lock_guard lock(mutex);
        for (auto &[watch_descriptor, directory] : directories) {
            (void)watch_descriptor;
            if (directory.path == old_path)
                directory.path = new_path;
            else if (directory.path.size() > old_path.size() &&
                     directory.path.compare(0, old_path.size(), old_path) == 0 &&
                     directory.path[old_path.size()] == '/')
                directory.path = new_path + directory.path.substr(old_path.size());
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
            emit(NK_EVENT_FILE_WATCH_OVERFLOW, NK_FILE_CHANGE_MODIFIED, NK_FILE_ITEM_UNKNOWN,
                 NK_FILE_EVENT_FLAG_OVERFLOW | NK_FILE_EVENT_FLAG_RESCAN_REQUIRED, {});
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
            pollfd descriptor_poll{descriptor, POLLIN, 0};
            const auto ready = poll(&descriptor_poll, 1, 100);
            if (ready < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
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

    void run() noexcept {
        try {
            run_impl();
        } catch (...) {
            /* A backend worker must not unwind through std::thread. */
        }
    }
};

} // namespace

namespace nk::backend {

nk_result file_watch_create(const nk_file_watch_options *, nk_file_watch *out_watch) noexcept {
    try {
        const auto descriptor = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (descriptor < 0) {
            nk::core::set_error("inotify is unavailable");
            return NK_ERROR_UNSUPPORTED;
        }
        auto resource = std::make_shared<FileWatchResource>();
        resource->descriptor = descriptor;
        resource->generation = nk::core::runtime_generation();
        const auto handle =
            nk::core::handles().insert(nk::core::ResourceType::file_watch, resource);
        if (handle == NK_INVALID_HANDLE)
            return NK_ERROR_OUT_OF_MEMORY;
        resource->handle = handle;
        try {
            resource->worker = std::thread([resource] { resource->run(); });
        } catch (...) {
            nk::core::handles().erase(handle, nk::core::ResourceType::file_watch);
            nk::core::set_error("could not start file-watch worker");
            return NK_ERROR_OUT_OF_MEMORY;
        }
        *out_watch = handle;
        return NK_OK;
    } catch (const std::bad_alloc &) {
        nk::core::set_error("out of memory while creating file watcher");
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        nk::core::set_error("unexpected error while creating file watcher");
        return NK_ERROR_UNKNOWN;
    }
}

nk_result file_watch_add_directory(nk_file_watch watch, const char *path,
                                   nk_bool recursive) noexcept {
    try {
        const auto resource = nk::core::handles().get(watch, nk::core::ResourceType::file_watch);
        if (!resource) {
            nk::core::set_error("invalid file-watch handle");
            return NK_ERROR_INVALID_HANDLE;
        }
        return std::static_pointer_cast<FileWatchResource>(resource)->add_directory(path,
                                                                                    recursive != 0);
    } catch (const std::bad_alloc &) {
        nk::core::set_error("out of memory while adding file-watch directory");
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        nk::core::set_error("unexpected error while adding file-watch directory");
        return NK_ERROR_UNKNOWN;
    }
}

nk_result file_watch_remove_directory(nk_file_watch watch, const char *path) noexcept {
    try {
        const auto resource = nk::core::handles().get(watch, nk::core::ResourceType::file_watch);
        if (!resource) {
            nk::core::set_error("invalid file-watch handle");
            return NK_ERROR_INVALID_HANDLE;
        }
        return std::static_pointer_cast<FileWatchResource>(resource)->remove_directory(path);
    } catch (const std::bad_alloc &) {
        nk::core::set_error("out of memory while removing file-watch directory");
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        nk::core::set_error("unexpected error while removing file-watch directory");
        return NK_ERROR_UNKNOWN;
    }
}

nk_result file_watch_destroy(nk_file_watch watch) noexcept {
    try {
        const auto resource = nk::core::handles().get(watch, nk::core::ResourceType::file_watch);
        if (!resource) {
            nk::core::set_error("invalid file-watch handle");
            return NK_ERROR_INVALID_HANDLE;
        }
        std::static_pointer_cast<FileWatchResource>(resource)->stop();
        if (!nk::core::handles().erase(watch, nk::core::ResourceType::file_watch))
            return NK_ERROR_INVALID_HANDLE;
        return NK_OK;
    } catch (...) {
        nk::core::set_error("unexpected error while destroying file watcher");
        return NK_ERROR_UNKNOWN;
    }
}

} // namespace nk::backend
