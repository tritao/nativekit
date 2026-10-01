#include "nativekit_pty.h"
#include "nativekit_time.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__ANDROID__)
#define NK_POSIX_PTY 1
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
extern "C" char **environ;
#else
#define NK_POSIX_PTY 0
#endif

namespace {

nk_result fail(nk_result code, const char *message) noexcept {
    nk::core::set_error(message);
    return code;
}

#if NK_POSIX_PTY
constexpr std::size_t io_limit = 64u * 1024u;

int kill_pty_group(pid_t pid) noexcept {
    if (::kill(-pid, SIGKILL) == 0)
        return 0;
    // forkpty returns before the child necessarily enters its new session.
    if (errno == ESRCH)
        return ::kill(pid, SIGKILL);
    return -1;
}

struct Pty final : nk::core::Resource {
    int fd = -1;
    pid_t pid = -1;
    nk_pty handle = NK_INVALID_HANDLE;
    std::uint64_t generation = 0;
    std::atomic<bool> stop{false};
    std::atomic<bool> readable{false};
    std::atomic<bool> eof{false};
    std::atomic<bool> exited{false};
    std::atomic<int> status{0};
    std::thread monitor;

    ~Pty() override { close(); }

    void close() noexcept {
        stop.store(true);
        if (pid > 0)
            (void)kill_pty_group(pid);
        if (monitor.joinable())
            monitor.join();
        if (pid > 0 && !exited.load()) {
            int code = 0;
            while (::waitpid(pid, &code, 0) < 0 && errno == EINTR) {}
            status.store(code);
            exited.store(true);
        }
        pid = -1;
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    void emit(nk_event_kind kind) noexcept {
        if (!stop.load() && nk::core::is_runtime_generation(generation)) {
            nk::core::QueuedEvent event{};
            event.kind = kind;
            event.source = handle;
            (void)nk::core::push_event(std::move(event));
            nk_wake_events();
        }
    }

    void run() noexcept {
        while (!stop.load()) {
            pollfd item{fd, static_cast<short>(POLLIN | POLLHUP | POLLERR), 0};
            const int ready = ::poll(&item, 1, 20);
            if (ready > 0 && !eof.load() && (item.revents & (POLLIN | POLLHUP | POLLERR)) &&
                !readable.exchange(true))
                emit(NK_EVENT_PTY_READABLE);
            if (!exited.load()) {
                int code = 0;
                const pid_t result = ::waitpid(pid, &code, WNOHANG);
                if (result == pid) {
                    status.store(code);
                    exited.store(true);
                    emit(NK_EVENT_PTY_EXITED);
                }
            }
            if (exited.load() && ready > 0 && (item.revents & POLLNVAL))
                break;
            if (exited.load() && eof.load())
                break;
            if (ready > 0 && (item.revents & (POLLHUP | POLLERR)) && !(item.revents & POLLIN))
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
};

std::shared_ptr<Pty> lookup(nk_pty handle) {
    return std::static_pointer_cast<Pty>(
        nk::core::handles().get(handle, nk::core::ResourceType::pty));
}

void shutdown_ptys() noexcept {
    for (const auto handle : nk::core::handles().handles_of_type(nk::core::ResourceType::pty)) {
        auto pty = lookup(handle);
        if (pty)
            pty->close();
        nk::core::handles().erase(handle, nk::core::ResourceType::pty);
    }
}

bool bounded_string(const char *value) {
    if (!value)
        return false;
    for (std::size_t i = 0; i < 32768; ++i)
        if (value[i] == '\0')
            return true;
    return false;
}
#endif
} // namespace

extern "C" {

nk_result NK_CALL nk_pty_spawn(const char *program, const char *const *argv, uint32_t argc,
                              const char *cwd, const char *const *env, uint32_t envc,
                              uint16_t columns, uint16_t rows, nk_pty *out_pty) {
    return nk::core::result_boundary("nk_pty_spawn failed", [&] {
        nk::core::clear_error();
        if (out_pty)
            *out_pty = NK_INVALID_HANDLE;
#if !NK_POSIX_PTY
        (void)program; (void)argv; (void)argc; (void)cwd; (void)env; (void)envc;
        (void)columns; (void)rows;
        return fail(NK_ERROR_UNSUPPORTED, "PTY is unavailable on this platform");
#else
        if (!out_pty || !program || program[0] != '/' || !bounded_string(program) ||
            (cwd && !bounded_string(cwd)) || !columns || !rows || argc > 4096 || envc > 4096 ||
            (argc && !argv) || (envc && !env))
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid PTY spawn arguments");
        if (nk::core::runtime_generation() == 0)
            return fail(NK_ERROR_NOT_INITIALIZED, "NativeKit is not initialized");
        std::vector<char *> args;
        args.reserve(static_cast<std::size_t>(argc) + 2);
        args.push_back(const_cast<char *>(program));
        for (uint32_t i = 0; i < argc; ++i) {
            if (!bounded_string(argv[i]))
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid PTY argv string");
            args.push_back(const_cast<char *>(argv[i]));
        }
        args.push_back(nullptr);
        std::vector<char *> environment;
        if (env) {
            environment.reserve(static_cast<std::size_t>(envc) + 1);
            for (uint32_t i = 0; i < envc; ++i) {
                if (!bounded_string(env[i]))
                    return fail(NK_ERROR_INVALID_ARGUMENT, "invalid PTY environment string");
                environment.push_back(const_cast<char *>(env[i]));
            }
            environment.push_back(nullptr);
        }
        winsize size{};
        size.ws_col = columns;
        size.ws_row = rows;
        int master = -1;
        const pid_t child = ::forkpty(&master, nullptr, nullptr, &size);
        if (child < 0)
            return fail(NK_ERROR_UNKNOWN, "forkpty failed");
        if (child == 0) {
            if (cwd && ::chdir(cwd) != 0)
                _exit(127);
            ::execve(program, args.data(), env ? environment.data() : environ);
            _exit(127);
        }
        if (::fcntl(master, F_SETFL, ::fcntl(master, F_GETFL) | O_NONBLOCK) < 0) {
            ::kill(child, SIGKILL);
            int code = 0;
            (void)::waitpid(child, &code, 0);
            ::close(master);
            return fail(NK_ERROR_UNKNOWN, "could not make PTY nonblocking");
        }
        auto pty = std::make_shared<Pty>();
        pty->fd = master;
        pty->pid = child;
        pty->generation = nk::core::runtime_generation();
        pty->handle = nk::core::handles().insert(nk::core::ResourceType::pty, pty);
        if (pty->handle == NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "PTY handle allocation failed");
        nk::core::register_runtime_shutdown_hook(&shutdown_ptys);
#if NK_ENABLE_NO_EXCEPTIONS
        pty->monitor = std::thread([pty] { pty->run(); });
#else
        try {
            pty->monitor = std::thread([pty] { pty->run(); });
        } catch (...) {
            nk::core::handles().erase(pty->handle, nk::core::ResourceType::pty);
            throw;
        }
#endif
        *out_pty = pty->handle;
        return static_cast<nk_result>(NK_OK);
#endif
    });
}

nk_result NK_CALL nk_pty_read(nk_pty handle, void *buffer, uint64_t capacity,
                              uint64_t *out_read) {
#if !NK_POSIX_PTY
    (void)handle; (void)buffer; (void)capacity; (void)out_read;
    return fail(NK_ERROR_UNSUPPORTED, "PTY is unavailable on this platform");
#else
    nk::core::clear_error();
    if (!out_read || !buffer || !capacity)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid PTY read buffer");
    *out_read = 0;
    auto pty = lookup(handle);
    if (!pty)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid PTY handle");
    const ssize_t amount = ::read(pty->fd, buffer, std::min<uint64_t>(capacity, io_limit));
    if (amount > 0) {
        *out_read = static_cast<uint64_t>(amount);
        return NK_OK;
    }
    if (amount == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
        pty->readable.store(false);
        if (amount == 0)
            pty->eof.store(true);
        return amount == 0 ? NK_ERROR_NOT_FOUND : NK_PENDING;
    }
    if (errno == EIO) {
        pty->readable.store(false);
        pty->eof.store(true);
        return NK_ERROR_NOT_FOUND;
    }
    return fail(NK_ERROR_UNKNOWN, "PTY read failed");
#endif
}

nk_result NK_CALL nk_pty_write(nk_pty handle, const void *buffer, uint64_t size,
                               uint64_t *out_written) {
#if !NK_POSIX_PTY
    (void)handle; (void)buffer; (void)size; (void)out_written;
    return fail(NK_ERROR_UNSUPPORTED, "PTY is unavailable on this platform");
#else
    nk::core::clear_error();
    if (!out_written || (!buffer && size))
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid PTY write buffer");
    *out_written = 0;
    auto pty = lookup(handle);
    if (!pty)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid PTY handle");
    if (!size)
        return NK_OK;
    const ssize_t amount = ::write(pty->fd, buffer, std::min<uint64_t>(size, io_limit));
    if (amount >= 0) {
        *out_written = static_cast<uint64_t>(amount);
        return NK_OK;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return NK_PENDING;
    return fail(NK_ERROR_UNKNOWN, "PTY write failed");
#endif
}

nk_result NK_CALL nk_pty_resize(nk_pty handle, uint16_t columns, uint16_t rows) {
#if !NK_POSIX_PTY
    (void)handle; (void)columns; (void)rows;
    return fail(NK_ERROR_UNSUPPORTED, "PTY is unavailable on this platform");
#else
    nk::core::clear_error();
    auto pty = lookup(handle);
    if (!pty)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid PTY handle");
    if (!columns || !rows)
        return fail(NK_ERROR_INVALID_ARGUMENT, "PTY size must be nonzero");
    winsize size{};
    size.ws_col = columns;
    size.ws_row = rows;
    if (::ioctl(pty->fd, TIOCSWINSZ, &size) != 0)
        return fail(NK_ERROR_UNKNOWN, "PTY resize failed");
    return NK_OK;
#endif
}

nk_result NK_CALL nk_pty_exit_status(nk_pty handle, int32_t *out_code,
                                     int32_t *out_signal) {
#if !NK_POSIX_PTY
    (void)handle; (void)out_code; (void)out_signal;
    return fail(NK_ERROR_UNSUPPORTED, "PTY is unavailable on this platform");
#else
    nk::core::clear_error();
    if (!out_code || !out_signal)
        return fail(NK_ERROR_INVALID_ARGUMENT, "PTY exit outputs are required");
    auto pty = lookup(handle);
    if (!pty)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid PTY handle");
    if (!pty->exited.load())
        return NK_PENDING;
    const int status = pty->status.load();
    *out_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    *out_signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    return NK_OK;
#endif
}

nk_result NK_CALL nk_pty_kill(nk_pty handle) {
#if !NK_POSIX_PTY
    (void)handle;
    return fail(NK_ERROR_UNSUPPORTED, "PTY is unavailable on this platform");
#else
    nk::core::clear_error();
    auto pty = lookup(handle);
    if (!pty)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid PTY handle");
    if (kill_pty_group(pty->pid) != 0 && errno != ESRCH)
        return fail(NK_ERROR_UNKNOWN, "PTY kill failed");
    return NK_OK;
#endif
}

nk_result NK_CALL nk_pty_close(nk_pty handle) {
#if !NK_POSIX_PTY
    (void)handle;
    return fail(NK_ERROR_UNSUPPORTED, "PTY is unavailable on this platform");
#else
    nk::core::clear_error();
    auto pty = lookup(handle);
    if (!pty)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid PTY handle");
    pty->close();
    nk::core::handles().erase(handle, nk::core::ResourceType::pty);
    return NK_OK;
#endif
}
}
