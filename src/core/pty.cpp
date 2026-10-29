#include "nativekit_pty.h"
#include "nativekit_time.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"
#include "core/thread_name.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstring>
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
#include <sys/resource.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
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

#if defined(_WIN32)
#define NK_WIN_PTY 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <condition_variable>
#include <mutex>
#else
#define NK_WIN_PTY 0
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
            while (::waitpid(pid, &code, 0) < 0 && errno == EINTR) {
            }
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

#if NK_WIN_PTY
// ConPTY backend. The pseudoconsole API is resolved at run time so hosts older
// than Windows 10 1809 report NK_ERROR_UNSUPPORTED instead of failing to load.
using PseudoConsole = void *;
using CreatePseudoConsoleFn = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD, PseudoConsole *);
using ResizePseudoConsoleFn = HRESULT(WINAPI *)(PseudoConsole, COORD);
using ClosePseudoConsoleFn = void(WINAPI *)(PseudoConsole);
constexpr DWORD_PTR pseudoconsole_attribute = 0x00020016; // PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
constexpr std::size_t io_limit = 64u * 1024u;
constexpr std::size_t buffer_limit = 1024u * 1024u;
constexpr int32_t killed_signal = 9;

struct ConsoleApi {
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn close = nullptr;
};

const ConsoleApi &console_api() {
    static const ConsoleApi api = [] {
        ConsoleApi result;
        if (HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll")) {
            result.create = reinterpret_cast<CreatePseudoConsoleFn>(
                ::GetProcAddress(kernel, "CreatePseudoConsole"));
            result.resize = reinterpret_cast<ResizePseudoConsoleFn>(
                ::GetProcAddress(kernel, "ResizePseudoConsole"));
            result.close = reinterpret_cast<ClosePseudoConsoleFn>(
                ::GetProcAddress(kernel, "ClosePseudoConsole"));
        }
        if (!result.create || !result.resize || !result.close)
            result = ConsoleApi{};
        return result;
    }();
    return api;
}

void close_handle(HANDLE &handle) noexcept {
    if (handle && handle != INVALID_HANDLE_VALUE)
        ::CloseHandle(handle);
    handle = nullptr;
}

struct Pty final : nk::core::Resource {
    PseudoConsole console = nullptr;
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    HANDLE input = nullptr;  // Write end of the console input pipe.
    HANDLE output = nullptr; // Read end of the console output pipe.
    HANDLE stopped = nullptr;
    nk_pty handle = NK_INVALID_HANDLE;
    std::uint64_t generation = 0;
    std::atomic<bool> stop{false};
    std::atomic<bool> readable{false};
    std::atomic<bool> exited{false};
    std::atomic<bool> killed{false};
    std::atomic<DWORD> exit_code{0};
    std::mutex lock;
    std::condition_variable changed;
    std::string pending_output;
    std::string pending_input;
    bool eof = false;
    bool broken = false; // The console input pipe failed; queued input is discarded.
    std::mutex console_lock;
    std::thread reader;
    std::thread writer;
    std::thread monitor;

    ~Pty() override { close(); }

    void emit(nk_event_kind kind) noexcept {
        if (!stop.load() && nk::core::is_runtime_generation(generation)) {
            nk::core::QueuedEvent event{};
            event.kind = kind;
            event.source = handle;
            (void)nk::core::push_event(std::move(event));
            nk_wake_events();
        }
    }

    // Closing the pseudoconsole ends its host and, once drained, the output pipe.
    // ClosePseudoConsole may wait for output to be read, so the reader keeps running.
    // It runs outside console_lock: a resize must never wait on a drain that only the
    // resizing thread's own nk_pty_read could complete.
    void close_console() noexcept {
        PseudoConsole closing = nullptr;
        {
            std::lock_guard<std::mutex> guard(console_lock);
            std::swap(closing, console);
        }
        if (closing)
            console_api().close(closing);
    }

    void read_output() noexcept {
        std::vector<char> chunk(io_limit);
        for (;;) {
            DWORD count = 0;
            if (!::ReadFile(output, chunk.data(), static_cast<DWORD>(chunk.size()), &count,
                            nullptr) ||
                count == 0)
                break;
            {
                std::unique_lock<std::mutex> guard(lock);
                changed.wait(guard,
                             [&] { return stop.load() || pending_output.size() < buffer_limit; });
                // After close starts, output is drained only so the console host can exit.
                if (stop.load())
                    continue;
                pending_output.append(chunk.data(), count);
            }
            if (!readable.exchange(true))
                emit(NK_EVENT_PTY_READABLE);
        }
        {
            std::lock_guard<std::mutex> guard(lock);
            eof = true;
        }
        changed.notify_all();
        if (!readable.exchange(true))
            emit(NK_EVENT_PTY_READABLE);
    }

    void write_input() noexcept {
        for (;;) {
            std::string chunk;
            {
                std::unique_lock<std::mutex> guard(lock);
                changed.wait(guard, [&] { return stop.load() || !pending_input.empty(); });
                if (stop.load())
                    return;
                chunk = pending_input.substr(0, io_limit);
                pending_input.erase(0, chunk.size());
            }
            DWORD written = 0;
            for (std::size_t offset = 0; offset < chunk.size(); offset += written)
                if (!::WriteFile(input, chunk.data() + offset,
                                 static_cast<DWORD>(chunk.size() - offset), &written, nullptr)) {
                    std::lock_guard<std::mutex> guard(lock);
                    broken = true;
                    pending_input.clear();
                    return;
                }
        }
    }

    // Exit is published only after the output pipe ends, so every byte the
    // child wrote is readable before nk_pty_exit_status stops returning pending.
    void watch_process() noexcept {
        HANDLE waits[] = {process, stopped};
        if (::WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
            return;
        DWORD code = 0;
        if (::GetExitCodeProcess(process, &code))
            exit_code.store(code);
        close_console();
        {
            std::unique_lock<std::mutex> guard(lock);
            changed.wait(guard, [&] { return stop.load() || eof; });
            if (stop.load())
                return;
        }
        exited.store(true);
        emit(NK_EVENT_PTY_EXITED);
    }

    bool kill() noexcept {
        // A child that already exited keeps its own status even before its output
        // drains; the job is still terminated so its descendants end too.
        if (::WaitForSingleObject(process, 0) != WAIT_OBJECT_0)
            killed.store(true);
        return !job || ::TerminateJobObject(job, 1) != 0;
    }

    void close() noexcept {
        stop.store(true);
        if (stopped)
            ::SetEvent(stopped);
        if (process && !exited.load())
            (void)kill();
        changed.notify_all();
        close_console();
        if (writer.joinable()) {
            // A full input pipe would otherwise keep WriteFile blocked.
            (void)::CancelSynchronousIo(writer.native_handle());
            writer.join();
        }
        if (reader.joinable())
            reader.join();
        if (monitor.joinable())
            monitor.join();
        if (process && !exited.load()) {
            ::WaitForSingleObject(process, INFINITE);
            DWORD code = 0;
            if (::GetExitCodeProcess(process, &code))
                exit_code.store(code);
            exited.store(true);
        }
        close_handle(process);
        close_handle(job);
        close_handle(input);
        close_handle(output);
        close_handle(stopped);
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

bool absolute_program(const char *path) {
    const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    return (letter(path[0]) && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) ||
           (path[0] == '\\' && path[1] == '\\');
}

bool widen(const char *value, std::wstring &result) {
    result.clear();
    if (!value[0])
        return true;
    const int size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (size <= 0)
        return false;
    result.resize(static_cast<std::size_t>(size));
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result.data(), size) !=
        size)
        return false;
    result.pop_back();
    return true;
}

// Quote one argument for the CommandLineToArgvW / MSVC runtime convention.
void append_argument(std::wstring &line, const std::wstring &argument) {
    if (!line.empty())
        line += L' ';
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += argument;
        return;
    }
    line += L'"';
    std::size_t slashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        line.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        line += c;
    }
    line.append(slashes * 2, L'\\');
    line += L'"';
}

nk_result win_spawn(const char *program, const char *const *argv, uint32_t argc, const char *cwd,
                    const char *const *env, uint32_t envc, uint16_t columns, uint16_t rows,
                    nk_pty *out_pty) {
    if (!out_pty || !program || !bounded_string(program) || !absolute_program(program) ||
        (cwd && !bounded_string(cwd)) || !columns || !rows || columns > SHRT_MAX ||
        rows > SHRT_MAX || argc > 4096 || envc > 4096 || (argc && !argv) || (envc && !env))
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid PTY spawn arguments");
    if (nk::core::runtime_generation() == 0)
        return fail(NK_ERROR_NOT_INITIALIZED, "NativeKit is not initialized");
    const auto &api = console_api();
    if (!api.create)
        return fail(NK_ERROR_UNSUPPORTED, "PTY requires Windows 10 version 1809 or later");

    std::wstring application, directory, line, value;
    if (!widen(program, application) || (cwd && !widen(cwd, directory)))
        return fail(NK_ERROR_INVALID_ARGUMENT, "PTY paths must be UTF-8");
    append_argument(line, application);
    for (uint32_t i = 0; i < argc; ++i) {
        if (!bounded_string(argv[i]) || !widen(argv[i], value))
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid PTY argv string");
        append_argument(line, value);
    }
    std::vector<wchar_t> environment;
    if (env) {
        std::vector<std::wstring> entries;
        for (uint32_t i = 0; i < envc; ++i) {
            if (!bounded_string(env[i]) || !widen(env[i], value) || value.empty())
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid PTY environment string");
            entries.push_back(value);
        }
        // CreateProcess expects names sorted without regard to case.
        std::sort(entries.begin(), entries.end(), [](const std::wstring &a, const std::wstring &b) {
            const auto name = [](const std::wstring &entry) {
                return entry.substr(0, entry.find(L'=', 1));
            };
            return ::CompareStringOrdinal(name(a).c_str(), -1, name(b).c_str(), -1, TRUE) ==
                   CSTR_LESS_THAN;
        });
        for (const auto &entry : entries)
            environment.insert(environment.end(), entry.c_str(), entry.c_str() + entry.size() + 1);
        environment.push_back(L'\0');
        if (entries.empty())
            environment.push_back(L'\0');
    }

    auto pty = std::make_shared<Pty>();
    HANDLE console_input = nullptr, console_output = nullptr;
    const auto release = [&] {
        close_handle(console_input);
        close_handle(console_output);
    };
    if (!::CreatePipe(&console_input, &pty->input, nullptr, 0) ||
        !::CreatePipe(&pty->output, &console_output, nullptr, 0)) {
        release();
        return fail(NK_ERROR_UNKNOWN, "could not create PTY pipes");
    }
    const COORD size{static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
    if (FAILED(api.create(size, console_input, console_output, 0, &pty->console))) {
        pty->console = nullptr;
        release();
        return fail(NK_ERROR_UNKNOWN, "CreatePseudoConsole failed");
    }
    pty->job = ::CreateJobObjectW(nullptr, nullptr);
    pty->stopped = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!pty->job || !pty->stopped ||
        !::SetInformationJobObject(pty->job, JobObjectExtendedLimitInformation, &limits,
                                   sizeof(limits))) {
        release();
        return fail(NK_ERROR_UNKNOWN, "could not create PTY job");
    }

    SIZE_T attribute_size = 0;
    (void)::InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    std::vector<unsigned char> attribute_storage(attribute_size);
    auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    if (!::InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size)) {
        release();
        return fail(NK_ERROR_UNKNOWN, "could not prepare PTY process attributes");
    }
    const bool attached =
        ::UpdateProcThreadAttribute(attributes, 0, pseudoconsole_attribute, pty->console,
                                    sizeof(pty->console), nullptr, nullptr) != 0;
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    // Null standard handles keep a host with redirected stdio from leaking them
    // to the child in place of the pseudoconsole.
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION created{};
    // Handles are not inherited: the child receives only its pseudoconsole.
    const bool started =
        attached &&
        ::CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, FALSE,
                         EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT |
                             CREATE_SUSPENDED,
                         env ? environment.data() : nullptr, cwd ? directory.c_str() : nullptr,
                         &startup.StartupInfo, &created) != 0;
    ::DeleteProcThreadAttributeList(attributes);
    // The console host owns its pipe ends; ours would keep the output pipe open.
    release();
    if (!started)
        return fail(NK_ERROR_UNKNOWN, "could not start PTY program");
    pty->process = created.hProcess;
    // Descendants join the job before the child runs, so kill and close reach them.
    const bool assigned = ::AssignProcessToJobObject(pty->job, created.hProcess) != 0;
    if (!assigned) {
        ::TerminateProcess(created.hProcess, 1);
        close_handle(created.hThread);
        return fail(NK_ERROR_UNKNOWN, "could not assign PTY program to its job");
    }
    ::ResumeThread(created.hThread);
    close_handle(created.hThread);

    pty->generation = nk::core::runtime_generation();
    pty->handle = nk::core::handles().insert(nk::core::ResourceType::pty, pty);
    if (pty->handle == NK_INVALID_HANDLE)
        return fail(NK_ERROR_OUT_OF_MEMORY, "PTY handle allocation failed");
    nk::core::register_runtime_shutdown_hook(&shutdown_ptys);
#if NK_ENABLE_NO_EXCEPTIONS
    pty->reader = std::thread([pty] {
        nk::core::name_current_thread_format("nk.pty.%llu.%s",
                                             static_cast<unsigned long long>(pty->handle), "read");
        pty->read_output();
    });
    pty->writer = std::thread([pty] {
        nk::core::name_current_thread_format("nk.pty.%llu.%s",
                                             static_cast<unsigned long long>(pty->handle), "write");
        pty->write_input();
    });
    pty->monitor = std::thread([pty] {
        nk::core::name_current_thread_format(
            "nk.pty.%llu.%s", static_cast<unsigned long long>(pty->handle), "monitor");
        pty->watch_process();
    });
#else
    try {
        pty->reader = std::thread([pty] {
            nk::core::name_current_thread_format(
                "nk.pty.%llu.%s", static_cast<unsigned long long>(pty->handle), "read");
            pty->read_output();
        });
        pty->writer = std::thread([pty] {
            nk::core::name_current_thread_format(
                "nk.pty.%llu.%s", static_cast<unsigned long long>(pty->handle), "write");
            pty->write_input();
        });
        pty->monitor = std::thread([pty] {
            nk::core::name_current_thread_format(
                "nk.pty.%llu.%s", static_cast<unsigned long long>(pty->handle), "monitor");
            pty->watch_process();
        });
    } catch (...) {
        nk::core::handles().erase(pty->handle, nk::core::ResourceType::pty);
        throw;
    }
#endif
    *out_pty = pty->handle;
    return static_cast<nk_result>(NK_OK);
}

nk_result win_read(nk_pty handle, void *buffer, uint64_t capacity, uint64_t *out_read) {
    if (!out_read || !buffer || !capacity)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid PTY read buffer");
    *out_read = 0;
    auto pty = lookup(handle);
    if (!pty)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid PTY handle");
    {
        std::lock_guard<std::mutex> guard(pty->lock);
        if (pty->pending_output.empty()) {
            pty->readable.store(false);
            return pty->eof ? NK_ERROR_NOT_FOUND : NK_PENDING;
        }
        const auto amount = static_cast<std::size_t>(
            std::min<uint64_t>({capacity, io_limit, pty->pending_output.size()}));
        std::memcpy(buffer, pty->pending_output.data(), amount);
        pty->pending_output.erase(0, amount);
        *out_read = amount;
    }
    pty->changed.notify_all();
    return NK_OK;
}

nk_result win_write(nk_pty handle, const void *buffer, uint64_t size, uint64_t *out_written) {
    if (!out_written || (!buffer && size))
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid PTY write buffer");
    *out_written = 0;
    auto pty = lookup(handle);
    if (!pty)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid PTY handle");
    if (!size)
        return NK_OK;
    {
        std::lock_guard<std::mutex> guard(pty->lock);
        if (pty->broken)
            return fail(NK_ERROR_UNKNOWN, "PTY write failed");
        if (pty->pending_input.size() >= buffer_limit)
            return NK_PENDING;
        const auto amount = static_cast<std::size_t>(
            std::min<uint64_t>({size, io_limit, buffer_limit - pty->pending_input.size()}));
        pty->pending_input.append(static_cast<const char *>(buffer), amount);
        *out_written = amount;
    }
    pty->changed.notify_all();
    return NK_OK;
}

nk_result win_resize(nk_pty handle, uint16_t columns, uint16_t rows) {
    auto pty = lookup(handle);
    if (!pty)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid PTY handle");
    if (!columns || !rows || columns > SHRT_MAX || rows > SHRT_MAX)
        return fail(NK_ERROR_INVALID_ARGUMENT, "PTY size must be nonzero");
    std::lock_guard<std::mutex> guard(pty->console_lock);
    // After exit the console is closed; the size no longer matters.
    if (pty->console && FAILED(console_api().resize(pty->console, COORD{static_cast<SHORT>(columns),
                                                                        static_cast<SHORT>(rows)})))
        return fail(NK_ERROR_UNKNOWN, "PTY resize failed");
    return NK_OK;
}

nk_result win_exit_status(nk_pty handle, int32_t *out_code, int32_t *out_signal) {
    if (!out_code || !out_signal)
        return fail(NK_ERROR_INVALID_ARGUMENT, "PTY exit outputs are required");
    auto pty = lookup(handle);
    if (!pty)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid PTY handle");
    if (!pty->exited.load())
        return NK_PENDING;
    // Match POSIX SIGKILL reporting for terminations requested through nk_pty_kill.
    const bool killed = pty->killed.load();
    *out_code = killed ? -1 : static_cast<int32_t>(pty->exit_code.load());
    *out_signal = killed ? killed_signal : 0;
    return NK_OK;
}

nk_result win_kill(nk_pty handle) {
    auto pty = lookup(handle);
    if (!pty)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid PTY handle");
    if (!pty->exited.load() && !pty->kill())
        return fail(NK_ERROR_UNKNOWN, "PTY kill failed");
    return NK_OK;
}

nk_result win_close(nk_pty handle) {
    auto pty = lookup(handle);
    if (!pty)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid PTY handle");
    pty->close();
    nk::core::handles().erase(handle, nk::core::ResourceType::pty);
    return NK_OK;
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
#if NK_WIN_PTY
        return win_spawn(program, argv, argc, cwd, env, envc, columns, rows, out_pty);
#elif !NK_POSIX_PTY
        (void)program;
        (void)argv;
        (void)argc;
        (void)cwd;
        (void)env;
        (void)envc;
        (void)columns;
        (void)rows;
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
        // Resolve the fallback descriptor limit before fork: the child may only
        // use async-signal-safe operations until exec in this threaded runtime.
        rlimit descriptor_limit{};
        if (::getrlimit(RLIMIT_NOFILE, &descriptor_limit) != 0)
            return fail(NK_ERROR_UNKNOWN, "could not read descriptor limit");
        const pid_t child = ::forkpty(&master, nullptr, nullptr, &size);
        if (child < 0)
            return fail(NK_ERROR_UNKNOWN, "forkpty failed");
        if (child == 0) {
            // PTY programs inherit only stdin/stdout/stderr, never host locks,
            // listener sockets or another terminal's master descriptor.
#if defined(__APPLE__)
            ::closefrom(3);
#else
#if defined(SYS_close_range)
            if (::syscall(SYS_close_range, 3u, ~0u, 0u) != 0)
#endif
                for (rlim_t fd = 3; fd < descriptor_limit.rlim_max && fd < static_cast<rlim_t>(INT_MAX); ++fd)
                    ::close(static_cast<int>(fd));
#endif
            if (cwd && ::chdir(cwd) != 0)
                _exit(127);
            ::execve(program, args.data(), env ? environment.data() : environ);
            _exit(127);
        }
        if (::fcntl(master, F_SETFD, FD_CLOEXEC) < 0 ||
            ::fcntl(master, F_SETFL, ::fcntl(master, F_GETFL) | O_NONBLOCK) < 0) {
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
        pty->monitor = std::thread([pty] {
            nk::core::name_current_thread_format("nk.pty.%llu",
                                                 static_cast<unsigned long long>(pty->handle));
            pty->run();
        });
#else
        try {
            pty->monitor = std::thread([pty] {
                nk::core::name_current_thread_format("nk.pty.%llu",
                                                     static_cast<unsigned long long>(pty->handle));
                pty->run();
            });
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

nk_result NK_CALL nk_pty_read(nk_pty handle, void *buffer, uint64_t capacity, uint64_t *out_read) {
#if NK_WIN_PTY
    return nk::core::result_boundary("nk_pty_read failed", [&] {
        nk::core::clear_error();
        return win_read(handle, buffer, capacity, out_read);
    });
#elif !NK_POSIX_PTY
    (void)handle;
    (void)buffer;
    (void)capacity;
    (void)out_read;
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
#if NK_WIN_PTY
    return nk::core::result_boundary("nk_pty_write failed", [&] {
        nk::core::clear_error();
        return win_write(handle, buffer, size, out_written);
    });
#elif !NK_POSIX_PTY
    (void)handle;
    (void)buffer;
    (void)size;
    (void)out_written;
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
#if NK_WIN_PTY
    nk::core::clear_error();
    return win_resize(handle, columns, rows);
#elif !NK_POSIX_PTY
    (void)handle;
    (void)columns;
    (void)rows;
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

nk_result NK_CALL nk_pty_exit_status(nk_pty handle, int32_t *out_code, int32_t *out_signal) {
#if NK_WIN_PTY
    nk::core::clear_error();
    return win_exit_status(handle, out_code, out_signal);
#elif !NK_POSIX_PTY
    (void)handle;
    (void)out_code;
    (void)out_signal;
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
#if NK_WIN_PTY
    nk::core::clear_error();
    return win_kill(handle);
#elif !NK_POSIX_PTY
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
#if NK_WIN_PTY
    nk::core::clear_error();
    return win_close(handle);
#elif !NK_POSIX_PTY
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
