#include "nativekit_filesystem.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool require(bool condition, const char *message) {
    if (condition)
        return true;
    std::fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

bool write_file(const fs::path &path, const char *contents) {
    std::ofstream stream(path, std::ios::binary);
    stream << contents;
    return stream.good();
}

nk_result stat_path(nk_filesystem_handle root, const char *path, nk_filesystem_entry &entry) {
    entry = {};
    entry.struct_size = sizeof(entry);
    return nk_filesystem_stat(root, path, &entry);
}

} // namespace

int main() {
    std::array<char, 64> temp{};
    std::strcpy(temp.data(), "/tmp/nativekit-filesystem-XXXXXX");
    char *base_name = mkdtemp(temp.data());
    if (!require(base_name != nullptr, "could not create test directory"))
        return 1;
    const fs::path base(base_name);
    const fs::path root_path = base / "workspace";
    const fs::path outside_path = base / "outside";
    fs::create_directories(root_path / "nested");
    fs::create_directories(outside_path);
    if (!require(write_file(root_path / "nested" / "hello.txt", "hello") &&
                     write_file(root_path / "safe.txt", "ok") &&
                     write_file(outside_path / "secret.txt", "outside") &&
                     write_file(root_path / "nested" / "secret.txt", "inside"),
                 "could not create fixture files"))
        return 1;
    fs::create_directory_symlink("nested", root_path / "inside-link");
    fs::create_directory_symlink(outside_path, root_path / "outside-link");
    fs::create_symlink("loop", root_path / "loop");
    if (mkfifo((root_path / "pipe").c_str(), 0600) != 0)
        return 1;
    const char invalid_name[] = {static_cast<char>(0xff), 'x', '\0'};
    const int invalid_fd = open((root_path / invalid_name).c_str(), O_CREAT | O_WRONLY, 0600);
    if (invalid_fd < 0)
        return 1;
    close(invalid_fd);

    nk_filesystem_handle root = 0;
    if (!require(nk_filesystem_root_open(root_path.c_str(), &root) == NK_OK && root != 0,
                 "root open failed"))
        return 1;

    nk_filesystem_entry entry{};
    if (!require(stat_path(root, "nested/hello.txt", entry) == NK_OK &&
                     entry.kind == NK_FILESYSTEM_ENTRY_REGULAR && entry.size == 5,
                 "root-relative stat returned incorrect metadata") ||
        !require(stat_path(root, "", entry) == NK_OK &&
                     entry.kind == NK_FILESYSTEM_ENTRY_DIRECTORY,
                 "empty path did not address the root") ||
        !require(stat_path(root, "inside-link/hello.txt", entry) == NK_OK &&
                     entry.kind == NK_FILESYSTEM_ENTRY_REGULAR && entry.size == 5,
                 "contained intermediate symlink was not resolved") ||
        !require(stat_path(root, "outside-link", entry) == NK_OK &&
                     entry.kind == NK_FILESYSTEM_ENTRY_SYMLINK,
                 "stat did not expose the final symlink as a link") ||
        !require(stat_path(root, "outside-link/secret.txt", entry) == NK_ERROR_PERMISSION_DENIED,
                 "external symlink traversal was not denied") ||
        !require(stat_path(root, "loop/secret.txt", entry) == NK_ERROR_PERMISSION_DENIED,
                 "symlink loop did not fail safely") ||
        !require(stat_path(root, "pipe", entry) == NK_OK &&
                     entry.kind == NK_FILESYSTEM_ENTRY_OTHER,
                 "special file was not classified as other"))
        return 1;

    nk_filesystem_handle file = 0;
    if (!require(nk_filesystem_file_open(root, "nested/hello.txt", &file) == NK_OK && file != 0,
                 "regular file could not be opened for bounded reads"))
        return 1;
    entry = {};
    entry.struct_size = sizeof(entry);
    if (!require(nk_filesystem_file_info(file, &entry) == NK_OK &&
                     entry.kind == NK_FILESYSTEM_ENTRY_REGULAR && entry.size == 5,
                 "opened file identity returned incorrect metadata"))
        return 1;
    uint32_t read_size = 0;
    if (!require(nk_filesystem_file_read(file, 1, 3, nullptr, &read_size) == NK_ERROR_BUFFER_TOO_SMALL &&
                     read_size == 3,
                 "file read size query returned an invalid length"))
        return 1;
    std::vector<uint8_t> read_bytes(read_size);
    if (!require(nk_filesystem_file_read(file, 1, 3, read_bytes.data(), &read_size) == NK_OK &&
                     read_size == 3 && std::memcmp(read_bytes.data(), "ell", 3) == 0,
                 "bounded positional read returned incorrect bytes"))
        return 1;
    nk_filesystem_handle rejected_file = 0;
    read_size = 0;
    if (!require(nk_filesystem_file_read(file, 5, 3, nullptr, &read_size) == NK_OK && read_size == 0,
                 "read at EOF did not return an empty result") ||
        !require(nk_filesystem_file_open(root, "pipe", &rejected_file) == NK_ERROR_INVALID_ARGUMENT,
                 "FIFO was accepted for file contents") ||
        !require(nk_filesystem_file_open(root, "outside-link/secret.txt", &rejected_file) == NK_ERROR_PERMISSION_DENIED,
                 "file open followed an external symlink"))
        return 1;
    if (!require(nk_filesystem_file_close(file) == NK_OK,
                 "opened file handle could not be closed"))
        return 1;

    if (!require(nk_filesystem_file_open(root, "safe.txt", &file) == NK_OK,
                 "could not open file for change-time validation"))
        return 1;
    entry = {};
    entry.struct_size = sizeof(entry);
    if (!require(nk_filesystem_file_info(file, &entry) == NK_OK,
                 "could not read initial file metadata"))
        return 1;
    const int64_t initial_mtime = entry.modified_unix_ns;
    const int64_t initial_ctime = entry.changed_unix_ns;
    struct stat original_info {};
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    if (stat((root_path / "safe.txt").c_str(), &original_info) != 0 ||
        !write_file(root_path / "safe.txt", "xx"))
        return 1;
    struct timespec preserved_times[2] = {original_info.st_atim, original_info.st_mtim};
    if (utimensat(AT_FDCWD, (root_path / "safe.txt").c_str(), preserved_times, 0) != 0)
        return 1;
    entry = {};
    entry.struct_size = sizeof(entry);
    const auto changed_status = nk_filesystem_file_info(file, &entry);
    if (!require(changed_status == NK_OK && entry.size == 2 &&
                     entry.modified_unix_ns == initial_mtime && entry.changed_unix_ns != initial_ctime,
                 "same-size preserved-mtime write was not visible through change time") ||
        !require(nk_filesystem_file_close(file) == NK_OK,
                 "change-time test file handle could not be closed"))
        return 1;

    const std::array<const char *, 7> invalid_paths{
        "../outside/secret.txt", "/etc/passwd", "./safe.txt", "safe.txt/..",
        "nested//hello.txt", "nested/", "nested\\hello.txt"};
    for (const auto *path : invalid_paths) {
        if (!require(stat_path(root, path, entry) == NK_ERROR_INVALID_ARGUMENT,
                     "non-canonical relative path was accepted"))
            return 1;
    }
    const char malformed_path[] = {static_cast<char>(0xc0), static_cast<char>(0x80), '\0'};
    if (!require(stat_path(root, malformed_path, entry) == NK_ERROR_INVALID_ARGUMENT,
                 "invalid UTF-8 path was accepted"))
        return 1;

    nk_filesystem_handle cursor = 0;
    if (!require(nk_filesystem_directory_open(root, "inside-link", &cursor) == NK_OK,
                 "contained directory symlink could not be opened"))
        return 1;
    if (!require(nk_filesystem_directory_close(cursor) == NK_OK,
                 "contained symlink cursor close failed") ||
        !require(nk_filesystem_directory_open(root, "outside-link", &cursor) == NK_ERROR_PERMISSION_DENIED,
                 "external directory symlink was opened"))
        return 1;
    if (!require(nk_filesystem_directory_open(root, "nested", &cursor) == NK_OK,
                 "ordinary directory cursor open failed"))
        return 1;

    std::array<char, 1> short_name{};
    uint32_t name_size = short_name.size();
    nk_bool at_end = 0;
    entry = {};
    entry.struct_size = sizeof(entry);
    auto status = nk_filesystem_directory_next(cursor, &entry, short_name.data(), &name_size, &at_end);
    if (status != NK_ERROR_BUFFER_TOO_SMALL || name_size == 0 || at_end)
        return require(false, "short directory name buffer consumed or truncated an entry") ? 0 : 1;
    std::vector<char> name(name_size);
    const uint32_t name_capacity = name_size;
    status = nk_filesystem_directory_next(cursor, &entry, name.data(), &name_size, &at_end);
    if (!require(status == NK_OK && name_size == name_capacity && !at_end,
                 "retry after a short buffer did not return the same entry") ||
        !require(nk_filesystem_directory_close(cursor) == NK_OK,
                 "directory cursor close failed"))
        return 1;

    if (!require(nk_filesystem_directory_open(root, "", &cursor) == NK_OK,
                 "root directory cursor open failed"))
        return 1;
    bool saw_unsupported_name = false;
    bool saw_pipe = false;
    std::array<char, NK_FILESYSTEM_MAX_NAME_BYTES> name_buffer{};
    for (;;) {
        entry = {};
        entry.struct_size = sizeof(entry);
        name_size = name_buffer.size();
        status = nk_filesystem_directory_next(cursor, &entry, name_buffer.data(), &name_size, &at_end);
        if (!require(status == NK_OK, "directory iteration failed"))
            return 1;
        if (at_end)
            break;
        saw_unsupported_name = saw_unsupported_name || entry.name_unsupported != 0;
        if (name_size == 4 && std::memcmp(name_buffer.data(), "pipe", 4) == 0)
            saw_pipe = true;
    }
    if (!require(saw_unsupported_name, "invalid native filename bytes were silently accepted") ||
        !require(saw_pipe, "directory enumeration omitted the FIFO entry") ||
        !require(nk_filesystem_directory_close(cursor) == NK_OK,
                 "root directory cursor close failed"))
        return 1;

    // A same-path replacement must not redirect an already granted root handle.
    const fs::path moved_path = base / "workspace-moved";
    fs::rename(root_path, moved_path);
    fs::create_directories(root_path);
    fs::create_directories(root_path / "nested");
    write_file(root_path / "nested" / "hello.txt", "replacement");
    if (!require(stat_path(root, "nested/hello.txt", entry) == NK_OK && entry.size == 5,
                 "replacing the root path redirected a pinned root handle"))
        return 1;

    // Repeated symlink replacement may resolve inside or fail, but must never
    // return metadata from the external target.
    fs::remove_all(root_path);
    fs::rename(moved_path, root_path);
    const fs::path swap_link = root_path / "swap";
    fs::create_directory_symlink("nested", swap_link);
    std::thread mutator([&] {
        for (int i = 0; i < 1000; ++i) {
            std::error_code ec;
            fs::remove(swap_link, ec);
            fs::create_directory_symlink(i % 2 == 0 ? outside_path : fs::path("nested"), swap_link, ec);
        }
    });
    bool escaped = false;
    for (int i = 0; i < 1000; ++i) {
        const auto race_status = stat_path(root, "swap/secret.txt", entry);
        if (race_status == NK_OK && entry.size != 6)
            escaped = true;
        else if (race_status != NK_OK && race_status != NK_ERROR_NOT_FOUND &&
                 race_status != NK_ERROR_PERMISSION_DENIED)
            escaped = true;
    }
    mutator.join();
    if (!require(!escaped, "symlink replacement escaped the authorized root"))
        return 1;

    if (!require(nk_filesystem_directory_open(root, "", &cursor) == NK_OK,
                 "could not open cursor for root revocation check") ||
        !require(nk_filesystem_file_open(root, "nested/hello.txt", &file) == NK_OK,
                 "could not open file for root revocation check") ||
        !require(nk_filesystem_root_close(root) == NK_OK,
                 "root close failed") ||
        !require(stat_path(root, "safe.txt", entry) == NK_ERROR_INVALID_HANDLE,
                 "closed root handle remained usable") ||
        !require((entry = {}, entry.struct_size = sizeof(entry),
                  nk_filesystem_directory_next(cursor, &entry, name_buffer.data(), &name_size,
                                               &at_end)) == NK_ERROR_INVALID_REQUEST,
                 "root close did not invalidate an open directory cursor") ||
        !require((entry = {}, entry.struct_size = sizeof(entry),
                  nk_filesystem_file_info(file, &entry)) == NK_ERROR_INVALID_REQUEST,
                 "root close did not invalidate an open file handle") ||
        !require(nk_filesystem_file_close(file) == NK_OK,
                 "file close after root revocation failed") ||
        !require(nk_filesystem_directory_close(cursor) == NK_OK,
                 "cursor close after root revocation failed") ||
        !require(nk_filesystem_root_close(root) == NK_ERROR_INVALID_HANDLE,
                 "stale root handle was accepted"))
        return 1;
    fs::remove_all(base);
    return 0;
}
