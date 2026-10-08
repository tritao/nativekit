#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "nativekit_filesystem.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace {

thread_local std::string last_error;

enum class HandleKind : uint8_t { none, root, directory, file };

struct Resource {
    virtual ~Resource() = default;
};

struct Root final : Resource {
    Root(HANDLE value, std::wstring final_path, const FILE_ID_INFO &id)
        : handle(value), path(std::move(final_path)), identity(id) {}
    ~Root() override { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }

    HANDLE handle = INVALID_HANDLE_VALUE;
    std::wstring path;
    FILE_ID_INFO identity{};
    std::shared_mutex mutex;
    bool closed = false;
};

struct Directory final : Resource {
    Directory(std::shared_ptr<Root> root_value, HANDLE handle_value)
        : root(std::move(root_value)), handle(handle_value), buffer(64 * 1024) {}
    ~Directory() override { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }

    std::shared_ptr<Root> root;
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::mutex mutex;
    std::vector<unsigned char> buffer;
    FILE_ID_BOTH_DIR_INFO *next = nullptr;
    bool first_query = true;
    bool done = false;
    bool pending = false;
    std::string pending_name;
    nk_filesystem_entry pending_entry{};
};

struct File final : Resource {
    File(std::shared_ptr<Root> root_value, HANDLE handle_value)
        : root(std::move(root_value)), handle(handle_value) {}
    ~File() override { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }

    std::shared_ptr<Root> root;
    HANDLE handle = INVALID_HANDLE_VALUE;
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
        if (slots_.size() >= index_mask) return 0;
        slots_.emplace_back();
        return use_slot(static_cast<uint32_t>(slots_.size() - 1), slots_.back(), kind,
                        std::move(resource));
    }

    std::shared_ptr<Resource> get(nk_filesystem_handle handle, HandleKind kind) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t index = 0;
        uint16_t generation = 0;
        if (!decode(handle, index, generation) || index >= slots_.size()) return {};
        const auto &slot = slots_[index];
        return slot.generation == generation && slot.kind == kind ? slot.resource : nullptr;
    }

    bool erase(nk_filesystem_handle handle, HandleKind kind) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t index = 0;
        uint16_t generation = 0;
        if (!decode(handle, index, generation) || index >= slots_.size()) return false;
        auto &slot = slots_[index];
        if (slot.generation != generation || slot.kind != kind || !slot.resource) return false;
        slot.resource.reset();
        slot.kind = HandleKind::none;
        if (slot.generation == max_generation) slot.retired = true;
        else ++slot.generation;
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
        if (raw_index == 0 || handle == 0) return false;
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
            if (*p == 0) return false;
            ++p;
            continue;
        } else if ((*p & 0xe0) == 0xc0) {
            cp = *p & 0x1f;
            trailing = 1;
            if (cp < 2) return false;
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
        if (static_cast<size_t>(end - p) < trailing) return false;
        for (uint32_t i = 0; i < trailing; ++i, ++p) {
            if ((*p & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (*p & 0x3f);
        }
        if ((trailing == 2 && cp < 0x800) || (trailing == 3 && cp < 0x10000) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}

bool valid_relative_path(const char *path) {
    if (!path) return false;
    const size_t length = std::strlen(path);
    if (length > NK_FILESYSTEM_MAX_PATH_BYTES || !valid_utf8(path, length)) return false;
    if (length == 0) return true;
    if (path[0] == '/' || path[length - 1] == '/' || std::strchr(path, '\\') ||
        std::strchr(path, ':')) return false;
    size_t start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i == length || path[i] == '/') {
            const size_t count = i - start;
            if (count == 0 || (count == 1 && path[start] == '.') ||
                (count == 2 && path[start] == '.' && path[start + 1] == '.') ||
                path[i - 1] == '.' || path[i - 1] == ' ')
                return false;
            start = i + 1;
        }
    }
    return true;
}

nk_result map_error(DWORD error, bool directory_operation = false) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return NK_ERROR_NOT_FOUND;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_CANT_ACCESS_FILE:
    case ERROR_CANNOT_MAKE:
    case ERROR_REPARSE_TAG_INVALID:
        return NK_ERROR_PERMISSION_DENIED;
    case ERROR_DIRECTORY:
        return directory_operation ? NK_ERROR_INVALID_ARGUMENT : NK_ERROR_NOT_FOUND;
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_PARAMETER:
        return NK_ERROR_INVALID_ARGUMENT;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return NK_ERROR_OUT_OF_MEMORY;
    default:
        return NK_ERROR_UNKNOWN;
    }
}

const char *error_message(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND: return "path does not exist";
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_CANT_ACCESS_FILE:
    case ERROR_CANNOT_MAKE: return "access to the requested path was denied";
    case ERROR_DIRECTORY: return "path is not a directory";
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_PARAMETER: return "path or request is invalid";
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY: return "filesystem operation ran out of memory";
    default: return "filesystem operation failed";
    }
}

nk_result fail_windows(DWORD error, bool directory_operation = false) {
    return fail(map_error(error, directory_operation), error_message(error));
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

bool utf8_to_wide(const char *value, std::wstring &result) {
    if (!value) return false;
    const size_t length = std::strlen(value);
    if (!valid_utf8(value, length) || length > NK_FILESYSTEM_MAX_PATH_BYTES) return false;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value,
                                          static_cast<int>(length), nullptr, 0);
    if (count < 0 || (count == 0 && length != 0)) return false;
    result.resize(static_cast<size_t>(count));
    return count == 0 || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value,
        static_cast<int>(length), result.data(), count) == count;
}

bool wide_to_utf8(const wchar_t *value, size_t length, std::string &result) {
    if (length > NK_FILESYSTEM_MAX_PATH_BYTES) return false;
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
        static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (count == 0 && length != 0) return false;
    if (count > NK_FILESYSTEM_MAX_NAME_BYTES) return false;
    result.resize(static_cast<size_t>(count));
    return count == 0 || WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
        static_cast<int>(length), result.data(), count, nullptr, nullptr) == count;
}

std::wstring extended_path(std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (path.rfind(L"\\\\?\\", 0) == 0 || path.rfind(L"\\\\.\\", 0) == 0)
        return path;
    if (path.rfind(L"\\\\", 0) == 0)
        return L"\\\\?\\UNC\\" + path.substr(2);
    if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\')
        return L"\\\\?\\" + path;
    return path;
}

bool final_path(HANDLE handle, std::wstring &path) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD count = GetFinalPathNameByHandleW(handle, buffer.data(),
            static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (count == 0) return false;
        if (count < buffer.size()) {
            path.assign(buffer.data(), count);
            return true;
        }
        if (count > NK_FILESYSTEM_MAX_PATH_BYTES) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return false;
        }
        buffer.resize(static_cast<size_t>(count) + 1);
    }
}

bool path_is_within(const std::wstring &path, const std::wstring &root) {
    if (path.size() < root.size() ||
        CompareStringOrdinal(path.data(), static_cast<int>(root.size()), root.data(),
                             static_cast<int>(root.size()), TRUE) != CSTR_EQUAL)
        return false;
    return path.size() == root.size() || root.back() == L'\\' || path[root.size()] == L'\\';
}

bool same_identity(const FILE_ID_INFO &left, const FILE_ID_INFO &right) {
    return left.VolumeSerialNumber == right.VolumeSerialNumber &&
        std::memcmp(left.FileId.Identifier, right.FileId.Identifier,
                    sizeof(left.FileId.Identifier)) == 0;
}

bool query_identity(HANDLE handle, FILE_ID_INFO &identity) {
    return GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity)) != 0;
}

bool root_still_resolves(const Root &root) {
    HANDLE current = CreateFileW(root.path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (current == INVALID_HANDLE_VALUE) return false;
    FILE_ID_INFO identity{};
    const bool matches = query_identity(current, identity) && same_identity(identity, root.identity);
    CloseHandle(current);
    return matches;
}

std::wstring joined_path(const Root &root, const std::wstring &relative) {
    if (relative.empty()) return root.path;
    std::wstring result = root.path;
    if (result.empty() || result.back() != L'\\') result.push_back(L'\\');
    for (wchar_t c : relative) result.push_back(c == L'/' ? L'\\' : c);
    return result;
}

HANDLE open_relative(const Root &root, const std::wstring &relative, DWORD access,
                     DWORD flags, bool directory_operation = false) {
    if (!root_still_resolves(root)) {
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    const std::wstring path = joined_path(root, relative);
    HANDLE handle = CreateFileW(path.c_str(), access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | flags, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return handle;
    if (relative.empty()) {
        FILE_ID_INFO identity{};
        if (!query_identity(handle, identity) || !same_identity(identity, root.identity)) {
            CloseHandle(handle);
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
        }
    }
    std::wstring resolved;
    if (!final_path(handle, resolved) || !path_is_within(resolved, root.path) ||
        !root_still_resolves(root)) {
        CloseHandle(handle);
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag))) {
        const DWORD error = GetLastError();
        CloseHandle(handle);
        SetLastError(error);
        return INVALID_HANDLE_VALUE;
    }
    if (directory_operation && !(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(handle);
        SetLastError(ERROR_DIRECTORY);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

int64_t filetime_ns(LARGE_INTEGER time) {
    if (time.QuadPart == 0) return 0;
    return (time.QuadPart - 116444736000000000LL) * 100;
}

uint64_t load_u64(const unsigned char *value) {
    uint64_t result = 0;
    std::memcpy(&result, value, sizeof(result));
    return result;
}

bool fill_entry(HANDLE handle, nk_filesystem_entry &entry) {
    FILE_BASIC_INFO basic{};
    FILE_STANDARD_INFO standard{};
    FILE_ATTRIBUTE_TAG_INFO tag{};
    FILE_ID_INFO identity{};
    if (!GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) ||
        !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) ||
        !GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) ||
        !query_identity(handle, identity))
        return false;

    entry.struct_size = sizeof(nk_filesystem_entry);
    if (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        entry.kind = NK_FILESYSTEM_ENTRY_SYMLINK;
    else if (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        entry.kind = NK_FILESYSTEM_ENTRY_DIRECTORY;
    else if (tag.FileAttributes & FILE_ATTRIBUTE_DEVICE)
        entry.kind = NK_FILESYSTEM_ENTRY_OTHER;
    else
        entry.kind = NK_FILESYSTEM_ENTRY_REGULAR;
    entry.size = standard.EndOfFile.QuadPart < 0 ? 0 : static_cast<uint64_t>(standard.EndOfFile.QuadPart);
    entry.modified_unix_ns = filetime_ns(basic.LastWriteTime);
    entry.changed_unix_ns = filetime_ns(basic.ChangeTime);
    entry.file_id_high = identity.VolumeSerialNumber;
    entry.file_id_low = load_u64(identity.FileId.Identifier);
    return true;
}

void fill_directory_entry(const FILE_ID_BOTH_DIR_INFO &info, uint64_t volume,
                          nk_filesystem_entry &entry) {
    entry.struct_size = sizeof(nk_filesystem_entry);
    if (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        entry.kind = NK_FILESYSTEM_ENTRY_SYMLINK;
    else if (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        entry.kind = NK_FILESYSTEM_ENTRY_DIRECTORY;
    else if (info.FileAttributes & FILE_ATTRIBUTE_DEVICE)
        entry.kind = NK_FILESYSTEM_ENTRY_OTHER;
    else
        entry.kind = NK_FILESYSTEM_ENTRY_REGULAR;
    entry.size = info.EndOfFile.QuadPart < 0 ? 0 : static_cast<uint64_t>(info.EndOfFile.QuadPart);
    entry.modified_unix_ns = filetime_ns(info.LastWriteTime);
    entry.changed_unix_ns = filetime_ns(info.ChangeTime);
    entry.file_id_high = volume;
    entry.file_id_low = static_cast<uint64_t>(info.FileId.QuadPart);
}

std::wstring relative_wide(const char *path) {
    std::wstring result;
    if (!utf8_to_wide(path, result)) return {};
    return result;
}

std::shared_ptr<Root> get_root(nk_filesystem_handle handle) {
    return std::static_pointer_cast<Root>(registry.get(handle, HandleKind::root));
}

std::shared_ptr<Directory> get_directory(nk_filesystem_handle handle) {
    return std::static_pointer_cast<Directory>(registry.get(handle, HandleKind::directory));
}

std::shared_ptr<File> get_file(nk_filesystem_handle handle) {
    return std::static_pointer_cast<File>(registry.get(handle, HandleKind::file));
}

} // namespace

extern "C" {

nk_result NK_CALL nk_filesystem_root_open(const char *absolute_path,
                                          nk_filesystem_handle *out_root) {
    clear_error();
    if (!out_root) return fail(NK_ERROR_INVALID_ARGUMENT, "root output handle is required");
    *out_root = 0;
    std::wstring path;
    if (!utf8_to_wide(absolute_path, path) || path.empty())
        return fail(NK_ERROR_INVALID_ARGUMENT, "root must be an absolute UTF-8 path within the configured limit");
    path = extended_path(std::move(path));
    const bool drive_absolute = path.size() >= 7 && path.rfind(L"\\\\?\\", 0) == 0 &&
        path[5] == L':' && path[6] == L'\\';
    const bool unc_absolute = path.rfind(L"\\\\?\\UNC\\", 0) == 0;
    if (!drive_absolute && !unc_absolute)
        return fail(NK_ERROR_INVALID_ARGUMENT, "root path must be absolute");

    HANDLE handle = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return fail_windows(GetLastError(), true);
    FILE_ATTRIBUTE_TAG_INFO tag{};
    FILE_ID_INFO identity{};
    std::wstring resolved;
    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag))) {
        const DWORD error = GetLastError();
        CloseHandle(handle);
        return fail_windows(error, true);
    }
    if (!(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(handle);
        return fail(NK_ERROR_INVALID_ARGUMENT, "workspace root is not a directory");
    }
    if (!query_identity(handle, identity) || !final_path(handle, resolved)) {
        const DWORD error = GetLastError();
        CloseHandle(handle);
        return fail_windows(error, true);
    }
    auto root = std::make_shared<Root>(handle, std::move(resolved), identity);
    const auto root_handle = registry.insert(HandleKind::root, root);
    if (root_handle == 0) return fail(NK_ERROR_OUT_OF_MEMORY, "filesystem root handle registry is full");
    *out_root = root_handle;
    return NK_OK;
}

nk_result NK_CALL nk_filesystem_root_close(nk_filesystem_handle handle) {
    clear_error();
    auto root = get_root(handle);
    if (!root) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem root handle");
    {
        std::unique_lock<std::shared_mutex> lock(root->mutex);
        if (root->closed) return fail(NK_ERROR_INVALID_HANDLE, "filesystem root is already closed");
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
    if (!root) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem root handle");
    std::shared_lock<std::shared_mutex> lock(root->mutex);
    if (root->closed) return fail(NK_ERROR_INVALID_REQUEST, "filesystem root was closed");
    HANDLE object = INVALID_HANDLE_VALUE;
    if (relative_path[0] == '\0') {
        if (!root_still_resolves(*root))
            return fail(NK_ERROR_PERMISSION_DENIED, "authorized workspace root path was replaced");
        object = root->handle;
    } else {
        object = open_relative(*root, relative_wide(relative_path), FILE_READ_ATTRIBUTES,
                               FILE_FLAG_OPEN_REPARSE_POINT);
        if (object == INVALID_HANDLE_VALUE) return fail_windows(GetLastError());
    }
    const bool ok = fill_entry(object, *out_entry);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (object != root->handle) CloseHandle(object);
    return ok ? NK_OK : fail_windows(error);
}

nk_result NK_CALL nk_filesystem_directory_open(nk_filesystem_handle handle,
                                               const char *relative_path,
                                               nk_filesystem_handle *out_cursor) {
    clear_error();
    if (!out_cursor || !valid_relative_path(relative_path)) {
        if (out_cursor) *out_cursor = 0;
        return fail(NK_ERROR_INVALID_ARGUMENT, "directory open requires a canonical UTF-8 relative path and output handle");
    }
    *out_cursor = 0;
    auto root = get_root(handle);
    if (!root) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem root handle");
    std::shared_lock<std::shared_mutex> lock(root->mutex);
    if (root->closed) return fail(NK_ERROR_INVALID_REQUEST, "filesystem root was closed");
    HANDLE directory = open_relative(*root, relative_wide(relative_path), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                     0, true);
    if (directory == INVALID_HANDLE_VALUE) return fail_windows(GetLastError(), true);
    auto cursor = std::make_shared<Directory>(root, directory);
    const auto cursor_handle = registry.insert(HandleKind::directory, cursor);
    if (cursor_handle == 0) return fail(NK_ERROR_OUT_OF_MEMORY, "filesystem directory handle registry is full");
    *out_cursor = cursor_handle;
    return NK_OK;
}

nk_result NK_CALL nk_filesystem_directory_next(nk_filesystem_handle handle,
                                               nk_filesystem_entry *out_entry, char *name,
                                               uint32_t *inout_name_size, nk_bool *out_end) {
    clear_error();
    if (!inout_name_size || !out_end || !output_entry(out_entry) || (!name && *inout_name_size != 0))
        return fail(NK_ERROR_INVALID_ARGUMENT, "directory next requires valid output buffers");
    const uint32_t capacity = *inout_name_size;
    *inout_name_size = 0;
    *out_end = 0;
    auto directory = get_directory(handle);
    if (!directory) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem directory cursor");

    std::lock_guard<std::mutex> cursor_lock(directory->mutex);
    std::shared_lock<std::shared_mutex> root_lock(directory->root->mutex);
    if (directory->root->closed) return fail(NK_ERROR_INVALID_REQUEST, "filesystem root was closed");
    FILE_ID_INFO root_id{};
    if (!query_identity(directory->root->handle, root_id)) return fail_windows(GetLastError());

    while (!directory->pending) {
        if (!directory->next) {
            if (directory->done) {
                *out_end = 1;
                out_entry->struct_size = sizeof(nk_filesystem_entry);
                return NK_OK;
            }
            const FILE_INFO_BY_HANDLE_CLASS kind = directory->first_query
                ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo;
            directory->first_query = false;
            if (!GetFileInformationByHandleEx(directory->handle, kind, directory->buffer.data(),
                                               static_cast<DWORD>(directory->buffer.size()))) {
                const DWORD error = GetLastError();
                if (error == ERROR_NO_MORE_FILES) {
                    directory->done = true;
                    continue;
                }
                return fail_windows(error, true);
            }
            directory->next = reinterpret_cast<FILE_ID_BOTH_DIR_INFO *>(directory->buffer.data());
        }

        auto *item = directory->next;
        if (item->NextEntryOffset == 0) directory->next = nullptr;
        else directory->next = reinterpret_cast<FILE_ID_BOTH_DIR_INFO *>(
            reinterpret_cast<unsigned char *>(item) + item->NextEntryOffset);
        const size_t name_length = item->FileNameLength / sizeof(wchar_t);
        if ((name_length == 1 && item->FileName[0] == L'.') ||
            (name_length == 2 && item->FileName[0] == L'.' && item->FileName[1] == L'.'))
            continue;

        fill_directory_entry(*item, root_id.VolumeSerialNumber, directory->pending_entry);
        directory->pending = true;
        if (!wide_to_utf8(item->FileName, name_length, directory->pending_name)) {
            directory->pending_name.clear();
            directory->pending_entry.name_unsupported = 1;
        }
    }

    const uint32_t required = static_cast<uint32_t>(directory->pending_name.size());
    *inout_name_size = required;
    if (capacity < required)
        return fail(NK_ERROR_BUFFER_TOO_SMALL, "directory entry name buffer is too small");
    if (required != 0) std::memcpy(name, directory->pending_name.data(), required);
    out_entry->struct_size = sizeof(nk_filesystem_entry);
    std::memcpy(reinterpret_cast<char *>(out_entry) + sizeof(out_entry->struct_size),
        reinterpret_cast<const char *>(&directory->pending_entry) + sizeof(directory->pending_entry.struct_size),
        sizeof(*out_entry) - sizeof(out_entry->struct_size));
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

nk_result NK_CALL nk_filesystem_file_open(nk_filesystem_handle handle, const char *relative_path,
                                          nk_filesystem_handle *out_file) {
    clear_error();
    if (!out_file || !valid_relative_path(relative_path) || relative_path[0] == '\0') {
        if (out_file) *out_file = 0;
        return fail(NK_ERROR_INVALID_ARGUMENT, "file open requires a non-empty canonical UTF-8 path and output handle");
    }
    *out_file = 0;
    auto root = get_root(handle);
    if (!root) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem root handle");
    std::shared_lock<std::shared_mutex> lock(root->mutex);
    if (root->closed) return fail(NK_ERROR_INVALID_REQUEST, "filesystem root was closed");
    HANDLE object = open_relative(*root, relative_wide(relative_path), GENERIC_READ | FILE_READ_ATTRIBUTES, 0);
    if (object == INVALID_HANDLE_VALUE) return fail_windows(GetLastError());
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(object, FileAttributeTagInfo, &tag, sizeof(tag))) {
        const DWORD error = GetLastError();
        CloseHandle(object);
        return fail_windows(error);
    }
    if ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (tag.FileAttributes & FILE_ATTRIBUTE_DEVICE)) {
        CloseHandle(object);
        return fail(NK_ERROR_INVALID_ARGUMENT, "content reads require a regular file");
    }
    auto file = std::make_shared<File>(root, object);
    const auto file_handle = registry.insert(HandleKind::file, file);
    if (file_handle == 0) return fail(NK_ERROR_OUT_OF_MEMORY, "filesystem file handle registry is full");
    *out_file = file_handle;
    return NK_OK;
}

nk_result NK_CALL nk_filesystem_file_info(nk_filesystem_handle handle,
                                          nk_filesystem_entry *out_entry) {
    clear_error();
    if (!output_entry(out_entry)) return NK_ERROR_INVALID_ARGUMENT;
    auto file = get_file(handle);
    if (!file) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem file handle");
    std::lock_guard<std::mutex> file_lock(file->mutex);
    std::shared_lock<std::shared_mutex> root_lock(file->root->mutex);
    if (file->root->closed) return fail(NK_ERROR_INVALID_REQUEST, "filesystem root was closed");
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(file->handle, FileAttributeTagInfo, &tag, sizeof(tag)))
        return fail_windows(GetLastError());
    if ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (tag.FileAttributes & FILE_ATTRIBUTE_DEVICE))
        return fail(NK_ERROR_INVALID_REQUEST, "opened filesystem object is no longer a regular file");
    return fill_entry(file->handle, *out_entry) ? NK_OK : fail_windows(GetLastError());
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
    if (!file) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale filesystem file handle");
    std::lock_guard<std::mutex> file_lock(file->mutex);
    std::shared_lock<std::shared_mutex> root_lock(file->root->mutex);
    if (file->root->closed) return fail(NK_ERROR_INVALID_REQUEST, "filesystem root was closed");
    if (offset > static_cast<uint64_t>(std::numeric_limits<LONGLONG>::max()))
        return fail(NK_ERROR_INVALID_ARGUMENT, "file read offset is outside the platform range");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file->handle, &size)) return fail_windows(GetLastError());
    if (size.QuadPart < 0) return fail(NK_ERROR_INVALID_REQUEST, "opened filesystem object has an invalid size");
    const uint64_t file_size = static_cast<uint64_t>(size.QuadPart);
    const uint32_t required = offset >= file_size ? 0 : static_cast<uint32_t>(
        std::min<uint64_t>(requested_bytes, file_size - offset));
    if (required == 0) return NK_OK;
    if (!out_bytes || capacity < required) {
        *inout_bytes = required;
        return fail(NK_ERROR_BUFFER_TOO_SMALL, "file read output buffer is too small");
    }
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file->handle, position, nullptr, FILE_BEGIN)) return fail_windows(GetLastError());
    DWORD count = 0;
    if (!ReadFile(file->handle, out_bytes, required, &count, nullptr)) return fail_windows(GetLastError());
    *inout_bytes = count;
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
