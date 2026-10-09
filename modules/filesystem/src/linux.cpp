#include "nativekit_filesystem.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <limits>
#include <shared_mutex>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <vector>
#include <unistd.h>
#include <linux/openat2.h>

namespace {

thread_local std::string last_error;

enum class HandleKind : uint8_t { none, root, directory, file };

struct Resource {
    virtual ~Resource() = default;
};

struct Root final : Resource {
    explicit Root(int value) : fd(value) {}
    ~Root() override;
    int fd = -1;
    std::shared_mutex mutex;
    bool closed = false;
};

struct Directory final : Resource {
    Directory(std::shared_ptr<Root> root_value, void *stream_value)
        : root(std::move(root_value)), stream(stream_value) {}
    ~Directory() override;
    std::shared_ptr<Root> root;
    void *stream = nullptr;
    std::mutex mutex;
    std::string pending_name;
    nk_filesystem_entry pending_entry{};
    bool pending = false;
};

struct File final : Resource {
    File(std::shared_ptr<Root> root_value, int fd_value)
        : root(std::move(root_value)), fd(fd_value) {}
    ~File() override;
    std::shared_ptr<Root> root;
    int fd = -1;
    std::mutex mutex;
};

struct Slot {
    uint16_t generation = 1;
    HandleKind kind = HandleKind::none;
    std::shared_ptr<Resource> resource;
    bool retired = false;
};

class Registry {
  public:
    nk_filesystem_handle insert(HandleKind kind, std::shared_ptr<Resource> resource) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32_t i = 0; i < slots_.size(); ++i) {
            auto &slot = slots_[i];
            if (!slot.retired && !slot.resource)
                return use_slot(i, slot, kind, std::move(resource));
        }
        if (slots_.size() >= index_mask)
            return 0;
        slots_.emplace_back();
        return use_slot(static_cast<uint32_t>(slots_.size() - 1), slots_.back(), kind,
                        std::move(resource));
    }

    std::shared_ptr<Resource> get(nk_filesystem_handle handle, HandleKind kind) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t index = 0;
        uint16_t generation = 0;
        if (!decode(handle, index, generation) || index >= slots_.size())
            return {};
        const auto &slot = slots_[index];
        return slot.generation == generation && slot.kind == kind ? slot.resource : nullptr;
    }

    bool erase(nk_filesystem_handle handle, HandleKind kind) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t index = 0;
        uint16_t generation = 0;
        if (!decode(handle, index, generation) || index >= slots_.size())
            return false;
        auto &slot = slots_[index];
        if (slot.generation != generation || slot.kind != kind || !slot.resource)
            return false;
        slot.resource.reset();
        slot.kind = HandleKind::none;
        if (slot.generation == max_generation)
            slot.retired = true;
        else
            ++slot.generation;
        return true;
    }

  private:
    static constexpr uint32_t index_bits = 20;
    static constexpr uint32_t index_mask = (1u << index_bits) - 1u;
    static constexpr uint16_t max_generation = (1u << (32 - index_bits)) - 1u;

    static nk_filesystem_handle use_slot(uint32_t index, Slot &slot, HandleKind kind,
                                         std::shared_ptr<Resource> resource) {
        slot.kind = kind;
        slot.resource = std::move(resource);
        return (static_cast<uint32_t>(slot.generation) << index_bits) | (index + 1u);
    }

    static bool decode(nk_filesystem_handle handle, uint32_t &index, uint16_t &generation) {
        const uint32_t raw_index = handle & index_mask;
        if (raw_index == 0 || handle == 0)
            return false;
        index = raw_index - 1u;
        generation = static_cast<uint16_t>(handle >> index_bits);
        return generation != 0;
    }

    std::mutex mutex_;
    std::vector<Slot> slots_;
};

Registry registry;

nk_result fail(nk_result result, const char *message) {
    last_error = message;
    return result;
}

void clear_error() { last_error.clear(); }

bool valid_utf8(const char *text, size_t length) {
    const auto *p = reinterpret_cast<const unsigned char *>(text);
    const auto *end = p + length;
    while (p < end) {
        uint32_t cp = 0;
        uint32_t trailing = 0;
        if (*p < 0x80) {
            if (*p == 0)
                return false;
            ++p;
            continue;
        } else if ((*p & 0xe0) == 0xc0) {
            cp = *p & 0x1f;
            trailing = 1;
            if (cp < 2)
                return false;
        } else if ((*p & 0xf0) == 0xe0) {
            cp = *p & 0x0f;
            trailing = 2;
        } else if ((*p & 0xf8) == 0xf0) {
            cp = *p & 0x07;
            trailing = 3;
        } else {
            return false;
        }
        ++p;
        if (static_cast<size_t>(end - p) < trailing)
            return false;
        for (uint32_t i = 0; i < trailing; ++i, ++p) {
            if ((*p & 0xc0) != 0x80)
                return false;
            cp = (cp << 6) | (*p & 0x3f);
        }
        if ((trailing == 2 && cp < 0x800) || (trailing == 3 && cp < 0x10000) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return false;
    }
    return true;
}

bool valid_relative_path(const char *path) {
    if (!path)
        return false;
    const size_t length = std::strlen(path);
    if (length > NK_FILESYSTEM_MAX_PATH_BYTES || !valid_utf8(path, length))
        return false;
    if (length == 0)
        return true;
    if (path[0] == '/' || path[length - 1] == '/' || std::strchr(path, '\\'))
        return false;
    size_t start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i == length || path[i] == '/') {
            const size_t count = i - start;
            if (count == 0 || (count == 1 && path[start] == '.') ||
                (count == 2 && path[start] == '.' && path[start + 1] == '.'))
                return false;
            start = i + 1;
        }
    }
    return true;
}

nk_result map_errno(int error, bool directory_operation = false) {
    switch (error) {
    case ENOENT:
        return NK_ERROR_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EXDEV:
    case ELOOP:
        return NK_ERROR_PERMISSION_DENIED;
    case ENOTDIR:
        return directory_operation ? NK_ERROR_INVALID_ARGUMENT : NK_ERROR_NOT_FOUND;
    case ENOSYS:
    case EINVAL:
    case E2BIG:
        return NK_ERROR_UNSUPPORTED;
    default:
        return NK_ERROR_UNKNOWN;
    }
}

const char *errno_message(int error) {
    switch (error) {
    case ENOENT: return "path does not exist";
    case EACCES:
    case EPERM: return "access to the requested path was denied";
    case EXDEV:
    case ELOOP: return "path resolution would leave the authorized root";
    case ENOTDIR: return "path is not a directory";
    case ENOSYS:
    case EINVAL:
    case E2BIG: return "secure root-relative resolution is unavailable on this system";
    default: return "filesystem operation failed";
    }
}

bool output_entry(nk_filesystem_entry *entry) {
    if (!entry || entry->struct_size != sizeof(nk_filesystem_entry)) {
        fail(NK_ERROR_INVALID_ARGUMENT, "entry output must use the current NativeKit filesystem ABI size");
        return false;
    }
    std::memset(reinterpret_cast<char *>(entry) + sizeof(entry->struct_size), 0,
                sizeof(*entry) - sizeof(entry->struct_size));
    return true;
}

void fill_entry(const struct stat &info, nk_filesystem_entry &entry) {
    if (S_ISREG(info.st_mode))
        entry.kind = NK_FILESYSTEM_ENTRY_REGULAR;
    else if (S_ISDIR(info.st_mode))
        entry.kind = NK_FILESYSTEM_ENTRY_DIRECTORY;
    else if (S_ISLNK(info.st_mode))
        entry.kind = NK_FILESYSTEM_ENTRY_SYMLINK;
    else
        entry.kind = NK_FILESYSTEM_ENTRY_OTHER;
    entry.size = info.st_size < 0 ? 0 : static_cast<uint64_t>(info.st_size);
    entry.modified_unix_ns = static_cast<int64_t>(info.st_mtim.tv_sec) * 1000000000LL +
                             static_cast<int64_t>(info.st_mtim.tv_nsec);
    entry.file_id_high = static_cast<uint64_t>(info.st_dev);
    entry.file_id_low = static_cast<uint64_t>(info.st_ino);
    entry.changed_unix_ns = static_cast<int64_t>(info.st_ctim.tv_sec) * 1000000000LL +
                            static_cast<int64_t>(info.st_ctim.tv_nsec);
}

int open_beneath(int root_fd, const char *path, uint64_t flags) {
    struct open_how how {};
    how.flags = flags;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS;
    return static_cast<int>(syscall(SYS_openat2, root_fd, path, &how, sizeof(how)));
}

std::shared_ptr<Root> get_root(nk_filesystem_handle handle) {
    auto resource = registry.get(handle, HandleKind::root);
    return std::static_pointer_cast<Root>(resource);
}

std::shared_ptr<Directory> get_directory(nk_filesystem_handle handle) {
    auto resource = registry.get(handle, HandleKind::directory);
    return std::static_pointer_cast<Directory>(resource);
}

std::shared_ptr<File> get_file(nk_filesystem_handle handle) {
    auto resource = registry.get(handle, HandleKind::file);
    return std::static_pointer_cast<File>(resource);
}

} // namespace

Root::~Root() {
    if (fd >= 0)
        close(fd);
}

Directory::~Directory() {
    if (stream)
        closedir(static_cast<DIR *>(stream));
}

File::~File() {
    if (fd >= 0)
        close(fd);
}

extern "C" {

nk_result NK_CALL nk_filesystem_root_open(const char *absolute_path,
                                          nk_filesystem_handle *out_root) {
    clear_error();
    if (!out_root)
        return fail(NK_ERROR_INVALID_ARGUMENT, "root output handle is required");
    if (!absolute_path) {
        *out_root = 0;
        return fail(NK_ERROR_INVALID_ARGUMENT, "absolute root path and output handle are required");
    }
    *out_root = 0;
    const size_t length = std::strlen(absolute_path);
    if (length == 0 || length > NK_FILESYSTEM_MAX_PATH_BYTES || absolute_path[0] != '/' ||
        !valid_utf8(absolute_path, length))
        return fail(NK_ERROR_INVALID_ARGUMENT, "root must be an absolute UTF-8 path within the configured limit");

    const int fd = open(absolute_path, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return fail(map_errno(errno, true), errno_message(errno));
    auto root = std::make_shared<Root>(fd);
    const auto handle = registry.insert(HandleKind::root, root);
    if (handle == 0)
        return fail(NK_ERROR_OUT_OF_MEMORY, "filesystem root handle registry is full");
    *out_root = handle;
    return NK_OK;
}

nk_result NK_CALL nk_filesystem_root_close(nk_filesystem_handle handle) {
    clear_error();
    auto root = get_root(handle);
    if (!root)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem root handle");
    {
        std::unique_lock<std::shared_mutex> lock(root->mutex);
        if (root->closed)
            return fail(NK_ERROR_INVALID_HANDLE, "filesystem root is already closed");
        root->closed = true;
    }
    registry.erase(handle, HandleKind::root);
    return NK_OK;
}

nk_result NK_CALL nk_filesystem_stat(nk_filesystem_handle handle, const char *relative_path,
                                     nk_filesystem_entry *out_entry) {
    clear_error();
    if (!output_entry(out_entry) || !valid_relative_path(relative_path))
        return fail(NK_ERROR_INVALID_ARGUMENT, "stat requires a canonical UTF-8 path relative to the root");
    auto root = get_root(handle);
    if (!root)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem root handle");

    std::shared_lock<std::shared_mutex> lock(root->mutex);
    if (root->closed)
        return fail(NK_ERROR_INVALID_REQUEST, "filesystem root was closed");
    struct stat info {};
    if (relative_path[0] == '\0') {
        if (fstat(root->fd, &info) != 0)
            return fail(map_errno(errno), errno_message(errno));
    } else {
        const int fd = open_beneath(root->fd, relative_path, O_PATH | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0)
            return fail(map_errno(errno), errno_message(errno));
        const int status = fstat(fd, &info);
        const int saved_errno = errno;
        close(fd);
        if (status != 0)
            return fail(map_errno(saved_errno), errno_message(saved_errno));
    }
    out_entry->struct_size = sizeof(nk_filesystem_entry);
    fill_entry(info, *out_entry);
    return NK_OK;
}

nk_result NK_CALL nk_filesystem_directory_open(nk_filesystem_handle handle,
                                               const char *relative_path,
                                               nk_filesystem_handle *out_cursor) {
    clear_error();
    if (!out_cursor || !valid_relative_path(relative_path)) {
        if (out_cursor)
            *out_cursor = 0;
        return fail(NK_ERROR_INVALID_ARGUMENT, "directory open requires a canonical UTF-8 relative path and output handle");
    }
    *out_cursor = 0;
    auto root = get_root(handle);
    if (!root)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem root handle");

    int fd = -1;
    {
        std::shared_lock<std::shared_mutex> lock(root->mutex);
        if (root->closed)
            return fail(NK_ERROR_INVALID_REQUEST, "filesystem root was closed");
        if (relative_path[0] == '\0')
            fd = openat(root->fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        else
            fd = open_beneath(root->fd, relative_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    }
    if (fd < 0)
        return fail(map_errno(errno, true), errno_message(errno));
    DIR *stream = fdopendir(fd);
    if (!stream) {
        const int saved_errno = errno;
        close(fd);
        return fail(map_errno(saved_errno, true), errno_message(saved_errno));
    }
    auto directory = std::make_shared<Directory>(root, stream);
    const auto cursor = registry.insert(HandleKind::directory, directory);
    if (cursor == 0)
        return fail(NK_ERROR_OUT_OF_MEMORY, "filesystem directory handle registry is full");
    *out_cursor = cursor;
    return NK_OK;
}

nk_result NK_CALL nk_filesystem_directory_next(nk_filesystem_handle handle,
                                               nk_filesystem_entry *out_entry, char *name,
                                               uint32_t *inout_name_size, nk_bool *out_end) {
    clear_error();
    if (!inout_name_size || !out_end || !output_entry(out_entry) ||
        (!name && *inout_name_size != 0))
        return fail(NK_ERROR_INVALID_ARGUMENT, "directory next requires valid output buffers");
    const uint32_t capacity = *inout_name_size;
    *inout_name_size = 0;
    *out_end = 0;
    auto directory = get_directory(handle);
    if (!directory)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem directory cursor");

    std::lock_guard<std::mutex> cursor_lock(directory->mutex);
    std::shared_lock<std::shared_mutex> root_lock(directory->root->mutex);
    if (directory->root->closed)
        return fail(NK_ERROR_INVALID_REQUEST, "filesystem root was closed");
    auto *stream = static_cast<DIR *>(directory->stream);
    while (!directory->pending) {
        errno = 0;
        auto *native_entry = readdir(stream);
        if (!native_entry) {
            if (errno != 0)
                return fail(map_errno(errno), errno_message(errno));
            *out_end = 1;
            out_entry->struct_size = sizeof(nk_filesystem_entry);
            return NK_OK;
        }
        if (std::strcmp(native_entry->d_name, ".") == 0 ||
            std::strcmp(native_entry->d_name, "..") == 0)
            continue;
        struct stat info {};
        if (fstatat(dirfd(stream), native_entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno == ENOENT)
                continue;
            return fail(map_errno(errno), errno_message(errno));
        }
        directory->pending_name.assign(native_entry->d_name);
        directory->pending_entry.struct_size = sizeof(nk_filesystem_entry);
        fill_entry(info, directory->pending_entry);
        directory->pending_entry.name_unsupported =
            directory->pending_name.size() > NK_FILESYSTEM_MAX_NAME_BYTES ||
            !valid_utf8(directory->pending_name.data(), directory->pending_name.size());
        if (directory->pending_entry.name_unsupported)
            directory->pending_name.clear();
        directory->pending = true;
    }

    const uint32_t required = static_cast<uint32_t>(directory->pending_name.size());
    *inout_name_size = required;
    if (capacity < required)
        return fail(NK_ERROR_BUFFER_TOO_SMALL, "directory entry name buffer is too small");
    if (required != 0)
        std::memcpy(name, directory->pending_name.data(), required);
    out_entry->struct_size = sizeof(nk_filesystem_entry);
    const uint32_t struct_size = out_entry->struct_size;
    std::memcpy(reinterpret_cast<char *>(out_entry) + sizeof(struct_size),
                reinterpret_cast<const char *>(&directory->pending_entry) + sizeof(struct_size),
                sizeof(*out_entry) - sizeof(struct_size));
    directory->pending = false;
    directory->pending_name.clear();
    return NK_OK;
}

nk_result NK_CALL nk_filesystem_directory_close(nk_filesystem_handle handle) {
    clear_error();
    if (!registry.erase(handle, HandleKind::directory))
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem directory cursor");
    return NK_OK;
}

nk_result NK_CALL nk_filesystem_file_open(nk_filesystem_handle handle,
                                          const char *relative_path,
                                          nk_filesystem_handle *out_file) {
    clear_error();
    if (!out_file || !valid_relative_path(relative_path) || relative_path[0] == '\0') {
        if (out_file)
            *out_file = 0;
        return fail(NK_ERROR_INVALID_ARGUMENT, "file open requires a non-empty canonical UTF-8 path and output handle");
    }
    *out_file = 0;
    auto root = get_root(handle);
    if (!root)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem root handle");

    int fd = -1;
    {
        std::shared_lock<std::shared_mutex> lock(root->mutex);
        if (root->closed)
            return fail(NK_ERROR_INVALID_REQUEST, "filesystem root was closed");
        fd = open_beneath(root->fd, relative_path, O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    }
    if (fd < 0)
        return fail(map_errno(errno), errno_message(errno));
    struct stat info {};
    if (fstat(fd, &info) != 0) {
        const int saved_errno = errno;
        close(fd);
        return fail(map_errno(saved_errno), errno_message(saved_errno));
    }
    if (!S_ISREG(info.st_mode)) {
        close(fd);
        return fail(NK_ERROR_INVALID_ARGUMENT, "content reads require a regular file");
    }
    auto file = std::make_shared<File>(root, fd);
    const auto file_handle = registry.insert(HandleKind::file, file);
    if (file_handle == 0)
        return fail(NK_ERROR_OUT_OF_MEMORY, "filesystem file handle registry is full");
    *out_file = file_handle;
    return NK_OK;
}

nk_result NK_CALL nk_filesystem_file_info(nk_filesystem_handle handle,
                                          nk_filesystem_entry *out_entry) {
    clear_error();
    if (!output_entry(out_entry))
        return NK_ERROR_INVALID_ARGUMENT;
    auto file = get_file(handle);
    if (!file)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem file handle");
    std::lock_guard<std::mutex> file_lock(file->mutex);
    std::shared_lock<std::shared_mutex> root_lock(file->root->mutex);
    if (file->root->closed)
        return fail(NK_ERROR_INVALID_REQUEST, "filesystem root was closed");
    struct stat info {};
    if (fstat(file->fd, &info) != 0)
        return fail(map_errno(errno), errno_message(errno));
    if (!S_ISREG(info.st_mode))
        return fail(NK_ERROR_INVALID_REQUEST, "opened filesystem object is no longer a regular file");
    out_entry->struct_size = sizeof(nk_filesystem_entry);
    fill_entry(info, *out_entry);
    return NK_OK;
}

nk_result NK_CALL nk_filesystem_file_read(nk_filesystem_handle handle, uint64_t offset,
                                          uint32_t requested_bytes, uint8_t *out_bytes,
                                          uint32_t *inout_bytes) {
    clear_error();
    if (!inout_bytes || requested_bytes > NK_FILESYSTEM_MAX_READ_BYTES ||
        (!out_bytes && *inout_bytes != 0))
        return fail(NK_ERROR_INVALID_ARGUMENT, "file read requires a valid bounded output buffer");
    const uint32_t capacity = *inout_bytes;
    *inout_bytes = 0;
    auto file = get_file(handle);
    if (!file)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem file handle");
    std::lock_guard<std::mutex> file_lock(file->mutex);
    std::shared_lock<std::shared_mutex> root_lock(file->root->mutex);
    if (file->root->closed)
        return fail(NK_ERROR_INVALID_REQUEST, "filesystem root was closed");
    if (offset > static_cast<uint64_t>(std::numeric_limits<off_t>::max()))
        return fail(NK_ERROR_INVALID_ARGUMENT, "file read offset is outside the platform range");
    struct stat info {};
    if (fstat(file->fd, &info) != 0)
        return fail(map_errno(errno), errno_message(errno));
    if (!S_ISREG(info.st_mode) || info.st_size < 0)
        return fail(NK_ERROR_INVALID_REQUEST, "opened filesystem object is no longer a regular file");
    const uint64_t size = static_cast<uint64_t>(info.st_size);
    const uint32_t required = offset >= size ? 0 : static_cast<uint32_t>(
        std::min<uint64_t>(requested_bytes, size - offset));
    if (required == 0)
        return NK_OK;
    if (!out_bytes || capacity < required) {
        *inout_bytes = required;
        return fail(NK_ERROR_BUFFER_TOO_SMALL, "file read output buffer is too small");
    }
    ssize_t read_count;
    do {
        read_count = pread(file->fd, out_bytes, required, static_cast<off_t>(offset));
    } while (read_count < 0 && errno == EINTR);
    if (read_count < 0)
        return fail(map_errno(errno), errno_message(errno));
    *inout_bytes = static_cast<uint32_t>(read_count);
    return NK_OK;
}

nk_result NK_CALL nk_filesystem_file_close(nk_filesystem_handle handle) {
    clear_error();
    if (!registry.erase(handle, HandleKind::file))
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem file handle");
    return NK_OK;
}

const char *NK_CALL nk_filesystem_last_error(void) { return last_error.c_str(); }

} // extern "C"
