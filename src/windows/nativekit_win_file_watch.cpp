#include "nativekit_file_watch.h"

#include "core/error.hpp"
#include "core/file_watch.hpp"
#include "core/runtime.hpp"
#include "core/thread_name.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

/* Windows file watching on ReadDirectoryChangesW, with one completion port and
 * worker thread per watcher.
 *
 * A directory handle prevents its ancestors from being renamed or deleted, so
 * handles are only opened on "top" registrations: those with no registered
 * ancestor. A top watches its whole subtree when it is recursive or has other
 * registrations below it, and changes are filtered back down to what was
 * registered. A second handle on each top's parent reports the top itself
 * being renamed or deleted, which the top's own handle cannot observe.
 *
 * Re-adding a registered directory is a lookup without disk access. A
 * directory reported removed is marked stale, so its next add checks the disk
 * and fails if it is gone. */

namespace {

using ReadDirectoryChangesExFn = BOOL(WINAPI *)(HANDLE, LPVOID, DWORD, BOOL, DWORD, LPDWORD,
                                                LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE,
                                                READ_DIRECTORY_NOTIFY_INFORMATION_CLASS);

/* ReadDirectoryChangesExW reports each item's attributes, which saves a disk
 * query per change. It is available from Windows 10 1709. */
ReadDirectoryChangesExFn read_directory_changes_ex() {
    static const auto function = [] {
        HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
        return kernel ? reinterpret_cast<ReadDirectoryChangesExFn>(
                            ::GetProcAddress(kernel, "ReadDirectoryChangesExW"))
                      : nullptr;
    }();
    return function;
}

constexpr DWORD k_tree_filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
constexpr DWORD k_parent_filter = FILE_NOTIFY_CHANGE_DIR_NAME;
/* 64 KiB is the largest buffer ReadDirectoryChangesW accepts for network
 * shares. A parent only reports directory names, so a small buffer suffices;
 * when it overflows, the top is checked directly. */
constexpr DWORD k_tree_buffer_bytes = 64 * 1024;
constexpr DWORD k_parent_buffer_bytes = 4 * 1024;
constexpr ULONG_PTR k_stop_key = 1;
/* A rename's old and new names normally arrive in one batch. When a batch
 * ends between them, the old name waits this long for its partner. */
constexpr ULONGLONG k_rename_pairing_ms = 50;

bool is_separator(wchar_t character) {
    return character == L'\\' || character == L'/';
}

bool to_wide(const char *text, std::wstring &result) {
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (size <= 1)
        return false;
    result.resize(static_cast<std::size_t>(size));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), size);
    result.resize(static_cast<std::size_t>(size - 1));
    return true;
}

/* Appends UTF-16 text as UTF-8 with backslashes turned into forward slashes. */
void append_portable_utf8(std::string &result, std::wstring_view text) {
    if (text.empty())
        return;
    const int length = static_cast<int>(text.size());
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return;
    const auto start = result.size();
    result.resize(start + static_cast<std::size_t>(size));
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data() + start, size, nullptr,
                        nullptr);
    std::replace(result.begin() + static_cast<std::ptrdiff_t>(start), result.end(), '\\', '/');
}

/* Appends a path in the case-insensitive, backslash-separated form used as a
 * registration key. */
void append_folded(std::wstring &result, std::wstring_view text) {
    if (text.empty())
        return;
    const auto start = result.size();
    const int length = static_cast<int>(text.size());
    result.resize(start + text.size());
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, text.data(), length,
                      result.data() + start, length, nullptr, nullptr, 0) != length)
        std::copy(text.begin(), text.end(), result.begin() + static_cast<std::ptrdiff_t>(start));
    std::replace(result.begin() + static_cast<std::ptrdiff_t>(start), result.end(), L'/', L'\\');
}

std::size_t trimmed_length(std::wstring_view path) {
    auto length = path.size();
    while (length > 1 && is_separator(path[length - 1]))
        --length;
    return length;
}

std::wstring key_of(std::wstring_view path) {
    std::wstring key;
    key.reserve(path.size());
    append_folded(key, path.substr(0, trimmed_length(path)));
    return key;
}

/* The caller's spelling without trailing separators. */
std::string trimmed_original(std::string_view path) {
    const auto last = path.find_last_not_of("/\\");
    return std::string(path.substr(0, last == std::string_view::npos ? 1 : last + 1));
}

bool is_absolute(std::wstring_view path) {
    if (path.size() >= 2 && is_separator(path[0]) && is_separator(path[1]))
        return true;
    return path.size() >= 3 &&
           ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
           path[1] == L':' && is_separator(path[2]);
}

std::wstring full_path(const std::wstring &path) {
    const DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!size)
        return {};
    std::wstring result(size, L'\0');
    const DWORD length = GetFullPathNameW(path.c_str(), size, result.data(), nullptr);
    if (!length || length >= size)
        return {};
    result.resize(length);
    return result;
}

/* The directory holding a full path, or empty for a drive or share root. */
std::wstring parent_of(const std::wstring &path) {
    const auto length = trimmed_length(path);
    const auto slash = path.find_last_of(L"\\/", length - 1);
    if (slash == std::wstring::npos)
        return {};
    auto parent = path.substr(0, slash);
    if (parent.size() == 2 && parent[1] == L':')
        return parent + L'\\';
    if (parent.rfind(LR"(\\)", 0) == 0 && parent.find_first_of(L"\\/", 2) == std::wstring::npos)
        return {};
    return parent;
}

/* Paths at or beyond MAX_PATH need the \\?\ form. */
std::wstring openable(const std::wstring &path) {
    if (path.size() < MAX_PATH || path.rfind(LR"(\\?\)", 0) == 0)
        return path;
    if (path.rfind(LR"(\\)", 0) == 0)
        return LR"(\\?\UNC\)" + path.substr(2);
    return LR"(\\?\)" + path;
}

bool is_directory(const std::wstring &path) {
    const DWORD attributes = GetFileAttributesW(openable(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool is_within(const std::wstring &key, const std::wstring &ancestor) {
    return key.size() > ancestor.size() && key.compare(0, ancestor.size(), ancestor) == 0 &&
           key[ancestor.size()] == L'\\';
}

struct Registration {
    /* The caller's spelling, which prefixes the paths of changes below it
     * when it is a top. */
    std::string original;
    std::wstring native;
    bool recursive = false;
    /* The directory was reported removed, renamed or unopenable, so the next
     * add checks the disk again. A stale registration is not watched. */
    bool stale = false;
};

/* One open directory with a read always outstanding while it is open.
 * Deriving from OVERLAPPED lets completions find their watch. */
struct Watch : OVERLAPPED {
    enum class Role {
        /* Reports changes below a top registration. */
        tree,
        /* Watches a top's parent for the top itself being renamed or deleted. */
        parent
    };

    Role role = Role::tree;
    /* The top's registration key. */
    std::wstring key;
    std::string prefix;
    /* The directory this handle is open on. */
    std::wstring native;
    HANDLE directory = INVALID_HANDLE_VALUE;
    DWORD filter = 0;
    DWORD buffer_bytes = 0;
    bool subtree = false;
    bool recursive = false;
    bool recursive_below = false;
    bool extended = true;
    bool reading = false;
    bool closing = false;
    /* The read in flight targets buffers[active]; the other one holds the
     * batch being processed. */
    std::size_t active = 0;
    std::array<std::unique_ptr<DWORD[]>, 2> buffers;
    /* A tree's parent watch, or the tree a parent watch reports for. */
    Watch *partner = nullptr;
    /* For a parent watch, the folded name of its top. */
    std::wstring leaf;
    bool rename_pending = false;
    std::wstring rename_old;
    nk_file_item_kind rename_kind = NK_FILE_ITEM_UNKNOWN;
    ULONGLONG rename_deadline = 0;

    Watch() : OVERLAPPED{} {}
};

struct Change {
    DWORD action = 0;
    std::wstring_view name;
    nk_file_item_kind kind = NK_FILE_ITEM_UNKNOWN;
};

struct FileWatchResource final : nk::core::Resource {
    HANDLE port = nullptr;
    std::uint64_t generation = 0;
    nk_file_watch handle = NK_INVALID_HANDLE;
    std::mutex mutex;
    std::unordered_map<std::wstring, Registration> registrations;
    std::vector<std::unique_ptr<Watch>> watches;
    std::thread worker;
    bool stopped = false;
    /* Cleared when the worker exits, after which nothing can be watched. */
    bool running = true;
    /* Reused by the worker to build lookup keys without allocating. */
    std::wstring scratch;
    /* Folded keys of excluded subtrees. ReadDirectoryChangesW cannot skip a subtree, so their changes are dropped. */
    std::vector<std::wstring> excluded;
    std::wstring excluded_scratch;
    std::uint64_t next_registration = 0;

    ~FileWatchResource() override { stop(); }

    void stop() noexcept {
        {
            std::lock_guard lock(mutex);
            if (stopped)
                return;
            stopped = true;
        }
        if (worker.joinable()) {
            PostQueuedCompletionStatus(port, 0, k_stop_key, nullptr);
            worker.join();
        }
        if (port) {
            CloseHandle(port);
            port = nullptr;
        }
    }

    /* --- Registration (UI thread) --- */

    /* Registration is synchronous here, so a successful add is ready at once. */
    nk_result add_directory(const char *raw_path, bool recursive) {
        const auto result = register_directory(raw_path, recursive);
        if (result == NK_OK && nk::core::is_runtime_generation(generation)) {
            std::uint64_t registration = 0;
            {
                std::lock_guard lock(mutex);
                registration = ++next_registration;
            }
            (void)nk::core::push_event(
                nk::core::file_watch_ready_event(handle, trimmed_original(raw_path), registration));
            nk::core::wake_events();
        }
        return result;
    }

    nk_result exclude_directory(const char *raw_path) {
        std::wstring path;
        if (!raw_path || !to_wide(raw_path, path) || !is_absolute(path)) {
            nk::core::set_error("file-watch path must be an absolute UTF-8 directory");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        const auto key = key_of(full_path(path));
        std::lock_guard lock(mutex);
        if (std::find(excluded.begin(), excluded.end(), key) == excluded.end())
            excluded.push_back(key);
        return NK_OK;
    }

    nk_result register_directory(const char *raw_path, bool recursive) {
        std::wstring path;
        if (!raw_path || !to_wide(raw_path, path)) {
            nk::core::set_error("file-watch path must be an absolute UTF-8 directory");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        {
            std::lock_guard lock(mutex);
            if (!running)
                return worker_stopped();
            const auto found = registrations.find(key_of(path));
            if (found != registrations.end() && !found->second.stale) {
                if (!recursive || found->second.recursive)
                    return NK_OK;
                found->second.recursive = true;
                const auto result = reconcile_locked(&found->first);
                if (result != NK_OK) {
                    found->second.recursive = false;
                    (void)reconcile_locked();
                }
                return result;
            }
        }
        const auto native = is_absolute(path) ? full_path(path) : std::wstring{};
        const auto key = key_of(native);
        const bool accessible = !native.empty() && is_directory(native);
        std::lock_guard lock(mutex);
        if (!running)
            return worker_stopped();
        if (!accessible) {
            if (!key.empty() && registrations.count(key))
                forget_locked(key);
            nk::core::set_error("file-watch path must be an accessible absolute directory");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        const auto [entry, inserted] = registrations.try_emplace(key);
        const auto previous = entry->second;
        auto &registration = entry->second;
        if (inserted)
            registration.original = trimmed_original(raw_path);
        registration.native = native;
        registration.recursive = registration.recursive || recursive;
        registration.stale = false;
        const auto result = reconcile_locked(&key);
        if (result != NK_OK) {
            /* A stale registration that cannot be reopened is forgotten, not
             * restored, so it cannot shadow registrations below it. */
            if (inserted || previous.stale)
                registrations.erase(key);
            else
                registrations[key] = previous;
            (void)reconcile_locked();
        }
        return result;
    }

    nk_result remove_directory(const char *raw_path) {
        std::wstring path;
        if (!raw_path || !to_wide(raw_path, path) || !is_absolute(path)) {
            nk::core::set_error("file-watch path must be an absolute UTF-8 directory");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        std::lock_guard lock(mutex);
        auto key = key_of(path);
        if (!registrations.count(key))
            key = key_of(full_path(path));
        if (!registrations.count(key)) {
            nk::core::set_error("file-watch directory is not registered");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        forget_locked(key);
        return NK_OK;
    }

    static nk_result worker_stopped() {
        nk::core::set_error("file-watch worker has stopped");
        return NK_ERROR_UNKNOWN;
    }

    /* Drops a registration and everything below it. */
    void forget_locked(const std::wstring &key) {
        for (auto iterator = registrations.begin(); iterator != registrations.end();) {
            if (iterator->first == key || is_within(iterator->first, key))
                iterator = registrations.erase(iterator);
            else
                ++iterator;
        }
        (void)reconcile_locked();
    }

    void mark_stale_locked(const std::wstring &key) {
        for (auto &[registered, registration] : registrations)
            if (registered == key || is_within(registered, key))
                registration.stale = true;
    }

    /* The outermost live registration at or above a key. */
    std::wstring top_of_locked(const std::wstring &key) const {
        for (auto position = key.find(L'\\'); position != std::wstring::npos;
             position = key.find(L'\\', position + 1)) {
            if (position == 0)
                continue;
            const auto found = registrations.find(key.substr(0, position));
            if (found != registrations.end() && !found->second.stale)
                return found->first;
        }
        return key;
    }

    Watch *find_tree_locked(const std::wstring &key) const {
        for (const auto &watch : watches)
            if (watch->role == Watch::Role::tree && !watch->closing && watch->key == key)
                return watch.get();
        return nullptr;
    }

    Watch *open_watch_locked(const std::wstring &native, bool subtree, DWORD filter,
                             DWORD buffer_bytes) {
        auto watch = std::make_unique<Watch>();
        watch->native = native;
        watch->subtree = subtree;
        watch->filter = filter;
        watch->buffer_bytes = buffer_bytes;
        watch->directory =
            CreateFileW(openable(native).c_str(), FILE_LIST_DIRECTORY,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (watch->directory == INVALID_HANDLE_VALUE)
            return nullptr;
        for (auto &buffer : watch->buffers)
            buffer.reset(new (std::nothrow) DWORD[buffer_bytes / sizeof(DWORD)]);
        if (!watch->buffers[0] || !watch->buffers[1] ||
            !CreateIoCompletionPort(watch->directory, port, 0, 0) || !issue_read_locked(*watch)) {
            CloseHandle(watch->directory);
            return nullptr;
        }
        watches.push_back(std::move(watch));
        return watches.back().get();
    }

    /* Opens a top's tree watch, and a parent watch for it where the top has a
     * parent. The parent watch is best effort: without it, a renamed top is
     * still noticed when a change inside it arrives. */
    Watch *open_tree_locked(const std::wstring &key, const Registration &top, bool subtree) {
        auto *tree = open_watch_locked(top.native, subtree, k_tree_filter, k_tree_buffer_bytes);
        if (!tree)
            return nullptr;
        tree->key = key;
        tree->prefix = top.original;
        const auto parent = parent_of(top.native);
        if (parent.empty())
            return tree;
        if (auto *lookout =
                open_watch_locked(parent, false, k_parent_filter, k_parent_buffer_bytes)) {
            lookout->role = Watch::Role::parent;
            lookout->key = key;
            lookout->leaf = key.substr(key.rfind(L'\\') + 1);
            lookout->partner = tree;
            tree->partner = lookout;
        }
        return tree;
    }

    /* Brings the open handles in line with the live registrations.
     * Replacement handles are armed before the ones they replace are closed,
     * so no change goes unobserved. Only the top covering `required` must
     * open; another top that fails is marked stale until it is re-added. */
    nk_result reconcile_locked(const std::wstring *required = nullptr) {
        struct Desired {
            bool subtree = false;
            bool recursive_below = false;
        };
        std::unordered_map<std::wstring, Desired> desired;
        for (const auto &[key, registration] : registrations) {
            if (registration.stale)
                continue;
            const auto top = top_of_locked(key);
            auto &entry = desired[top];
            if (top != key) {
                entry.subtree = true;
                entry.recursive_below = entry.recursive_below || registration.recursive;
            } else if (registration.recursive) {
                entry.subtree = true;
            }
        }
        const auto required_top = required ? top_of_locked(*required) : std::wstring{};
        std::vector<Watch *> keep;
        std::vector<Watch *> opened;
        for (const auto &[key, want] : desired) {
            const auto &top = registrations.at(key);
            Watch *current = find_tree_locked(key);
            if (!current || current->subtree != want.subtree) {
                if (auto *replacement = open_tree_locked(key, top, want.subtree)) {
                    if (current)
                        adopt_rename_locked(*replacement, *current);
                    opened.push_back(replacement);
                    current = replacement;
                } else if (current && !want.subtree) {
                    /* Narrowing failed; the wider handle still filters correctly. */
                } else if (key == required_top) {
                    for (auto *watch : opened)
                        close_watch_locked(*watch);
                    erase_closed_locked();
                    nk::core::set_error("file-watch directory could not be opened");
                    return NK_ERROR_INVALID_ARGUMENT;
                } else {
                    mark_stale_locked(key);
                    current = nullptr;
                }
            }
            if (!current)
                continue;
            current->recursive = top.recursive;
            current->recursive_below = want.recursive_below;
            keep.push_back(current);
        }
        for (const auto &watch : watches)
            if (watch->role == Watch::Role::tree && !watch->closing &&
                std::find(keep.begin(), keep.end(), watch.get()) == keep.end())
                close_watch_locked(*watch);
        erase_closed_locked();
        return NK_OK;
    }

    /* A replacement takes over a rename whose new name has not arrived yet. */
    static void adopt_rename_locked(Watch &replacement, Watch &current) {
        replacement.rename_pending = current.rename_pending;
        replacement.rename_old = std::move(current.rename_old);
        replacement.rename_kind = current.rename_kind;
        replacement.rename_deadline = current.rename_deadline;
        current.rename_pending = false;
    }

    /* Closes a watch's handle, and its partner's, at once, so they stop
     * pinning their ancestors. A read in flight still completes on the port,
     * and the watch and its buffers are released only then. */
    void close_watch_locked(Watch &watch) {
        watch.closing = true;
        watch.rename_pending = false;
        if (watch.directory != INVALID_HANDLE_VALUE) {
            CancelIoEx(watch.directory, &watch);
            CloseHandle(watch.directory);
            watch.directory = INVALID_HANDLE_VALUE;
        }
        if (auto *partner = watch.partner) {
            watch.partner = nullptr;
            partner->partner = nullptr;
            close_watch_locked(*partner);
        }
    }

    void erase_closed_locked() {
        watches.erase(std::remove_if(watches.begin(), watches.end(),
                                     [](const std::unique_ptr<Watch> &watch) {
                                         return watch->closing && !watch->reading;
                                     }),
                      watches.end());
    }

    bool issue_read_locked(Watch &watch) {
        static_cast<OVERLAPPED &>(watch) = OVERLAPPED{};
        void *buffer = watch.buffers[watch.active].get();
        DWORD unused = 0;
        if (watch.extended) {
            if (const auto read_ex = read_directory_changes_ex()) {
                if (read_ex(watch.directory, buffer, watch.buffer_bytes, watch.subtree,
                            watch.filter, &unused, &watch, nullptr,
                            ReadDirectoryNotifyExtendedInformation))
                    return watch.reading = true;
                const DWORD error = GetLastError();
                if (error != ERROR_INVALID_PARAMETER && error != ERROR_INVALID_FUNCTION &&
                    error != ERROR_NOT_SUPPORTED)
                    return watch.reading = false;
            }
            watch.extended = false;
        }
        return watch.reading =
                   ReadDirectoryChangesW(watch.directory, buffer, watch.buffer_bytes, watch.subtree,
                                         watch.filter, &unused, &watch, nullptr) != FALSE;
    }

    /* --- Delivery (worker thread) --- */

    void run() noexcept {
        for (;;) {
            DWORD timeout = INFINITE;
            {
                std::lock_guard lock(mutex);
                timeout = rename_timeout_locked();
            }
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED *overlapped = nullptr;
            const BOOL ok = GetQueuedCompletionStatus(port, &bytes, &key, &overlapped, timeout);
            const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            /* Without a completion, only a timeout continues: anything else is
             * k_stop_key from stop() or a failed port. */
            if (!overlapped && error != WAIT_TIMEOUT)
                break;
            std::vector<nk::core::QueuedEvent> events;
            {
                std::lock_guard lock(mutex);
                if (overlapped)
                    complete_locked(*static_cast<Watch *>(overlapped), bytes, error, events);
                flush_expired_renames_locked(events);
            }
            deliver(events);
        }
        drain();
    }

    /* Cancels every read and waits for the aborted completions, so no
     * buffer is released while the kernel may still write to it. */
    void drain() noexcept {
        std::unique_lock lock(mutex);
        running = false;
        for (const auto &watch : watches)
            close_watch_locked(*watch);
        erase_closed_locked();
        while (!watches.empty()) {
            lock.unlock();
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED *overlapped = nullptr;
            GetQueuedCompletionStatus(port, &bytes, &key, &overlapped, INFINITE);
            lock.lock();
            if (overlapped) {
                static_cast<Watch *>(overlapped)->reading = false;
                erase_closed_locked();
            }
        }
    }

    DWORD rename_timeout_locked() const {
        const auto now = GetTickCount64();
        ULONGLONG timeout = INFINITE;
        for (const auto &watch : watches)
            if (watch->rename_pending)
                timeout =
                    std::min(timeout, watch->rename_deadline > now ? watch->rename_deadline - now
                                                                   : ULONGLONG{0});
        return static_cast<DWORD>(timeout);
    }

    void flush_expired_renames_locked(std::vector<nk::core::QueuedEvent> &events) {
        const auto now = GetTickCount64();
        for (const auto &watch : watches)
            if (watch->rename_pending && watch->rename_deadline <= now)
                flush_rename_locked(*watch, events);
    }

    void complete_locked(Watch &watch, DWORD bytes, DWORD error,
                         std::vector<nk::core::QueuedEvent> &events) {
        watch.reading = false;
        const auto batch = watch.active;
        const bool extended = watch.extended;
        const auto *buffer = reinterpret_cast<const std::byte *>(watch.buffers[batch].get());
        if (watch.closing) {
            /* A replaced tree may have completed before its replacement was
             * armed; its batch is delivered rather than lost. */
            if (watch.role == Watch::Role::tree && error == ERROR_SUCCESS && bytes) {
                process_locked(watch, buffer, bytes, extended, events);
                flush_rename_locked(watch, events);
            }
            erase_closed_locked();
            return;
        }
        if (watch.role == Watch::Role::parent) {
            complete_parent_locked(watch, buffer, bytes, error, extended, events);
            return;
        }
        const bool overflow = error == ERROR_NOTIFY_ENUM_DIR || (error == ERROR_SUCCESS && !bytes);
        if ((error != ERROR_SUCCESS && !overflow) || (overflow && !is_directory(watch.native))) {
            lose_locked(watch, events);
            return;
        }
        watch.active = 1 - batch;
        if (!issue_read_locked(watch)) {
            lose_locked(watch, events);
            return;
        }
        if (overflow) {
            watch.rename_pending = false;
            events.push_back(nk::core::file_watch_overflow_event(handle));
            return;
        }
        process_locked(watch, buffer, bytes, extended, events);
    }

    /* A parent watch only looks out for its top being renamed or deleted. If
     * it fails, the top stays watched without it. */
    void complete_parent_locked(Watch &watch, const std::byte *buffer, DWORD bytes, DWORD error,
                                bool extended, std::vector<nk::core::QueuedEvent> &events) {
        auto &tree = *watch.partner;
        watch.active = 1 - watch.active;
        if ((error != ERROR_SUCCESS && error != ERROR_NOTIFY_ENUM_DIR) ||
            !issue_read_locked(watch)) {
            tree.partner = nullptr;
            watch.partner = nullptr;
            close_watch_locked(watch);
            erase_closed_locked();
            return;
        }
        if (error != ERROR_SUCCESS || !bytes) {
            if (!is_directory(tree.native))
                lose_locked(tree, events);
            return;
        }
        for (DWORD offset = 0; offset < bytes;) {
            const auto change = read_change(watch, buffer + offset, extended);
            if (change.action == FILE_ACTION_REMOVED ||
                change.action == FILE_ACTION_RENAMED_OLD_NAME) {
                scratch.clear();
                append_folded(scratch, change.name);
                if (scratch == watch.leaf) {
                    lose_locked(tree, events);
                    return;
                }
            }
            const DWORD next = *reinterpret_cast<const DWORD *>(buffer + offset);
            if (!next)
                break;
            offset += next;
        }
    }

    /* The watched top directory was deleted, renamed or became unreadable. */
    void lose_locked(Watch &tree, std::vector<nk::core::QueuedEvent> &events) {
        flush_rename_locked(tree, events);
        events.push_back(make_event(NK_FILE_CHANGE_REMOVED, NK_FILE_ITEM_DIRECTORY,
                                    NK_FILE_EVENT_FLAG_RESCAN_REQUIRED, tree.prefix));
        mark_stale_locked(tree.key);
        close_watch_locked(tree);
        erase_closed_locked();
    }

    void process_locked(Watch &watch, const std::byte *buffer, DWORD bytes, bool extended,
                        std::vector<nk::core::QueuedEvent> &events) {
        std::wstring_view previous;
        for (DWORD offset = 0; offset < bytes;) {
            const auto change = read_change(watch, buffer + offset, extended);
            if (watch.rename_pending && change.action != FILE_ACTION_RENAMED_NEW_NAME)
                flush_rename_locked(watch, events);
            switch (change.action) {
            case FILE_ACTION_ADDED:
                emit_locked(watch, NK_FILE_CHANGE_ADDED, change.kind, change.name, {}, events);
                break;
            case FILE_ACTION_REMOVED:
                note_removed_locked(watch, change);
                emit_locked(watch, NK_FILE_CHANGE_REMOVED, change.kind, change.name, {}, events);
                break;
            case FILE_ACTION_MODIFIED:
                /* One save reports several modifications in a row, and adding
                 * or renaming an item can report one right after; consumers
                 * already treat those changes as content changes. */
                if (change.name != previous)
                    emit_locked(watch, NK_FILE_CHANGE_MODIFIED, change.kind, change.name, {},
                                events);
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
                note_removed_locked(watch, change);
                watch.rename_pending = true;
                watch.rename_old.assign(change.name);
                watch.rename_kind = change.kind;
                watch.rename_deadline = GetTickCount64() + k_rename_pairing_ms;
                break;
            case FILE_ACTION_RENAMED_NEW_NAME:
                if (watch.rename_pending) {
                    watch.rename_pending = false;
                    const auto kind =
                        change.kind != NK_FILE_ITEM_UNKNOWN ? change.kind : watch.rename_kind;
                    emit_locked(watch, NK_FILE_CHANGE_MOVED, kind, change.name, watch.rename_old,
                                events);
                } else {
                    emit_locked(watch, NK_FILE_CHANGE_ADDED, change.kind, change.name, {}, events);
                }
                break;
            default:
                break;
            }
            previous = change.name;
            const DWORD next = *reinterpret_cast<const DWORD *>(buffer + offset);
            if (!next)
                break;
            offset += next;
        }
    }

    /* Decodes one record in the format the batch was read with. */
    static Change read_change(const Watch &watch, const std::byte *record, bool extended) {
        Change change;
        if (extended) {
            const auto *info = reinterpret_cast<const FILE_NOTIFY_EXTENDED_INFORMATION *>(record);
            change.action = info->Action;
            change.name = {info->FileName, info->FileNameLength / sizeof(wchar_t)};
            if (info->FileAttributes)
                change.kind = (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                                  ? NK_FILE_ITEM_DIRECTORY
                                  : NK_FILE_ITEM_FILE;
            return change;
        }
        const auto *info = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(record);
        change.action = info->Action;
        change.name = {info->FileName, info->FileNameLength / sizeof(wchar_t)};
        if (change.action != FILE_ACTION_REMOVED && change.action != FILE_ACTION_RENAMED_OLD_NAME) {
            auto path = watch.native;
            if (!is_separator(path.back()))
                path += L'\\';
            path += change.name;
            const DWORD attributes = GetFileAttributesW(openable(path).c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES)
                change.kind = (attributes & FILE_ATTRIBUTE_DIRECTORY) ? NK_FILE_ITEM_DIRECTORY
                                                                      : NK_FILE_ITEM_FILE;
        }
        return change;
    }

    /* Registrations at or below a removed directory are gone, so re-adding
     * them must check the disk. */
    void note_removed_locked(const Watch &watch, const Change &change) {
        if (!watch.subtree || change.kind == NK_FILE_ITEM_FILE)
            return;
        scratch = watch.key;
        scratch += L'\\';
        append_folded(scratch, change.name);
        mark_stale_locked(scratch);
    }

    void flush_rename_locked(Watch &watch, std::vector<nk::core::QueuedEvent> &events) {
        if (!watch.rename_pending)
            return;
        watch.rename_pending = false;
        emit_locked(watch, NK_FILE_CHANGE_REMOVED, watch.rename_kind, watch.rename_old, {}, events);
    }

    /* Whether a change below the top lies in an excluded subtree. */
    bool excluded_locked(const Watch &watch, std::wstring_view name) {
        if (excluded.empty())
            return false;
        excluded_scratch = watch.key;
        excluded_scratch += L'\\';
        append_folded(excluded_scratch, name);
        for (const auto &directory : excluded)
            if (excluded_scratch.size() >= directory.size() &&
                excluded_scratch.compare(0, directory.size(), directory) == 0 &&
                (excluded_scratch.size() == directory.size() || excluded_scratch[directory.size()] == L'\\'))
                return true;
        return false;
    }

    /* Whether a change below the top falls inside a registration: anything
     * under a recursive one, a direct child of any registered directory, or a
     * registered directory itself. */
    bool wanted_locked(const Watch &watch, std::wstring_view name, nk_file_item_kind kind) {
        if (excluded_locked(watch, name))
            return false;
        if (watch.recursive)
            return true;
        const auto slash = name.rfind(L'\\');
        if (slash == std::wstring_view::npos)
            return true;
        scratch = watch.key;
        scratch += L'\\';
        append_folded(scratch, name.substr(0, slash));
        if (registrations.count(scratch))
            return true;
        if (kind != NK_FILE_ITEM_FILE) {
            const auto parent_length = scratch.size();
            scratch += L'\\';
            append_folded(scratch, name.substr(slash + 1));
            const bool registered = registrations.count(scratch) != 0;
            scratch.resize(parent_length);
            if (registered)
                return true;
        }
        if (!watch.recursive_below)
            return false;
        for (auto position = scratch.rfind(L'\\'); position > watch.key.size();
             position = scratch.rfind(L'\\', position - 1)) {
            const auto found = registrations.find(scratch.substr(0, position));
            if (found != registrations.end() && found->second.recursive)
                return true;
        }
        return false;
    }

    void emit_locked(const Watch &watch, nk_file_change_type change, nk_file_item_kind kind,
                     std::wstring_view name, std::wstring_view old_name,
                     std::vector<nk::core::QueuedEvent> &events) {
        const bool wanted = wanted_locked(watch, name, kind);
        const bool old_wanted = !old_name.empty() && wanted_locked(watch, old_name, kind);
        if (!wanted && !old_wanted)
            return;
        if (!old_name.empty() && !old_wanted) {
            change = NK_FILE_CHANGE_ADDED;
            old_name = {};
        } else if (!old_name.empty() && !wanted) {
            change = NK_FILE_CHANGE_REMOVED;
            name = old_name;
            old_name = {};
        }
        std::string path = watch.prefix + '/';
        append_portable_utf8(path, name);
        std::string old_path;
        if (!old_name.empty()) {
            old_path = watch.prefix + '/';
            append_portable_utf8(old_path, old_name);
        }
        events.push_back(make_event(change, kind, 0, path, old_path));
    }

    nk::core::QueuedEvent make_event(nk_file_change_type change, nk_file_item_kind kind,
                                     nk_file_event_flags flags, std::string_view path,
                                     std::string_view old_path = {}) const {
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_FILE_CHANGED;
        event.source = handle;
        event.data = nk::core::file_changed_payload(change, kind, flags, path, old_path);
        return event;
    }

    /* Pushes a batch and wakes the event loop once. A full queue turns the
     * rest of the batch into an overflow, which the queue always accepts, so
     * no change is dropped without a rescan. */
    void deliver(std::vector<nk::core::QueuedEvent> &events) {
        if (events.empty() || !nk::core::is_runtime_generation(generation))
            return;
        for (auto &event : events) {
            if (nk::core::push_event(std::move(event)) == NK_ERROR_QUEUE_FULL) {
                (void)nk::core::push_event(nk::core::file_watch_overflow_event(handle));
                break;
            }
        }
        nk::core::wake_events();
    }
};

std::shared_ptr<FileWatchResource> find(nk_file_watch watch) {
    const auto resource = nk::core::handles().get(watch, nk::core::ResourceType::file_watch);
    if (!resource) {
        nk::core::set_error("invalid file-watch handle");
        return nullptr;
    }
    return std::static_pointer_cast<FileWatchResource>(resource);
}

} // namespace

namespace nk::backend {

nk_result file_watch_create(const nk_file_watch_options *, nk_file_watch *out_watch) noexcept {
    auto resource = std::make_shared<FileWatchResource>();
    resource->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (!resource->port) {
        nk::core::set_error("file-watch completion port could not be created");
        return NK_ERROR_UNKNOWN;
    }
    resource->generation = nk::core::runtime_generation();
    const auto handle = nk::core::handles().insert(nk::core::ResourceType::file_watch, resource);
    if (handle == NK_INVALID_HANDLE)
        return NK_ERROR_OUT_OF_MEMORY;
    resource->handle = handle;
    resource->worker = std::thread([resource = resource.get()] {
        nk::core::name_current_thread("nk.file-watch");
        resource->run();
    });
    *out_watch = handle;
    return NK_OK;
}

nk_result file_watch_add_directory(nk_file_watch watch, const char *path,
                                   nk_bool recursive) noexcept {
    const auto resource = find(watch);
    return resource ? resource->add_directory(path, recursive != 0) : NK_ERROR_INVALID_HANDLE;
}

nk_result file_watch_exclude_directory(nk_file_watch watch, const char *path) noexcept {
    const auto resource = find(watch);
    return resource ? resource->exclude_directory(path) : NK_ERROR_INVALID_HANDLE;
}

nk_result file_watch_remove_directory(nk_file_watch watch, const char *path) noexcept {
    const auto resource = find(watch);
    return resource ? resource->remove_directory(path) : NK_ERROR_INVALID_HANDLE;
}

nk_result file_watch_destroy(nk_file_watch watch) noexcept {
    const auto resource = find(watch);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    resource->stop();
    if (!nk::core::handles().erase(watch, nk::core::ResourceType::file_watch))
        return NK_ERROR_INVALID_HANDLE;
    return NK_OK;
}

} // namespace nk::backend
