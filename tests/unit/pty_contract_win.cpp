#include "nativekit_pty.h"
#include "nativekit_time.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {
std::string collect(nk_pty pty, const char *needle) {
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (output.find(needle) == std::string::npos &&
           std::chrono::steady_clock::now() < deadline) {
        assert(nk_wait_events_timeout(0.05) == NK_OK);
        for (;;) {
            nk_event event{};
            event.struct_size = sizeof(event);
            assert(nk_poll_event(&event) == NK_OK);
            if (event.kind == NK_EVENT_NONE) {
                nk_event_release(&event);
                break;
            }
            nk_event_release(&event);
        }
        for (;;) {
            char buffer[4096];
            uint64_t count = 0;
            const auto result = nk_pty_read(pty, buffer, sizeof(buffer), &count);
            if (result != NK_OK)
                break;
            output.append(buffer, static_cast<std::size_t>(count));
        }
    }
    assert(output.find(needle) != std::string::npos);
    return output;
}

void send(nk_pty pty, const char *message) {
    uint64_t count = 0;
    assert(nk_pty_write(pty, message, std::strlen(message), &count) == NK_OK);
    assert(count == std::strlen(message));
}

void wait_exit(nk_pty pty, int32_t &code, int32_t &signal) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    nk_result result = NK_PENDING;
    while ((result = nk_pty_exit_status(pty, &code, &signal)) == NK_PENDING &&
           std::chrono::steady_clock::now() < deadline)
        assert(nk_wait_events_timeout(0.05) == NK_OK);
    assert(result == NK_OK);
}

std::string environment(const char *name) {
    char value[MAX_PATH] = {};
    const DWORD size = ::GetEnvironmentVariableA(name, value, MAX_PATH);
    assert(size > 0 && size < MAX_PATH);
    return value;
}
} // namespace

int main() {
    // Report failed assertions on stderr instead of a modal dialog.
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    nk_init_options options{};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    options.event_queue_capacity = 128;
    assert(nk_init(&options) == NK_OK);

    const auto shell = environment("ComSpec");
    char temp[MAX_PATH] = {};
    assert(::GetTempPathA(MAX_PATH, temp) > 0);
    std::string directory = temp;
    if (directory.size() > 3 && directory.back() == '\\')
        directory.pop_back();

    nk_pty pty = NK_INVALID_HANDLE;
    assert(nk_pty_spawn("cmd.exe", nullptr, 0, nullptr, nullptr, 0, 80, 24, &pty) ==
           NK_ERROR_INVALID_ARGUMENT);

    const char *interactive[] = {"/d", "/q"};
    assert(nk_pty_spawn(shell.c_str(), interactive, 2, directory.c_str(), nullptr, 0, 80, 24,
                        &pty) == NK_OK);
    send(pty, "echo ready & cd & mode con\r");
    const auto first = collect(pty, "Columns:");
    assert(first.find("ready") != std::string::npos);
    assert(first.find(directory) != std::string::npos);

    assert(nk_pty_resize(pty, 100, 40) == NK_OK);
    send(pty, "mode con\r");
    collect(pty, "100");

    // Input is UTF-8; characters of every encoded length reach the shell unchanged. (Shells
    // that read key events, such as PowerShell, also need the terminal to honor ConPTY's
    // win32-input-mode request; that is the terminal's job, not the PTY's.)
    send(pty, "echo in:\xC3\xA9\xE2\x82\xAC\xE2\x9C\x93\xF0\x9F\x98\x80.\r");
    collect(pty, "in:\xC3\xA9\xE2\x82\xAC\xE2\x9C\x93\xF0\x9F\x98\x80.");

    // A large producer must not make one UI read block or return unbounded data.
    send(pty, "for /L %i in (1,1,20000) do @echo xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r");
    ::Sleep(200);
    char buffer[131072];
    uint64_t count = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto result = nk_pty_read(pty, buffer, sizeof(buffer), &count);
    assert(result == NK_OK || result == NK_PENDING);
    assert(count <= 65536);
    assert(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100));

    int32_t code = 0, signal = 0;
    assert(nk_pty_kill(pty) == NK_OK);
    wait_exit(pty, code, signal);
    assert(code == -1 && signal == 9);
    assert(nk_pty_close(pty) == NK_OK);
    assert(nk_pty_read(pty, buffer, sizeof(buffer), &count) == NK_ERROR_INVALID_HANDLE);

    // A supplied environment replaces the inherited one.
    const auto root = "SystemRoot=" + environment("SystemRoot");
    const char *command[] = {"/d", "/c", "echo %PT_MARKER%:%CD%"};
    const char *variables[] = {"PT_MARKER=environment-ok", root.c_str()};
    assert(nk_pty_spawn(shell.c_str(), command, 3, directory.c_str(), variables, 2, 80, 24, &pty) ==
           NK_OK);
    collect(pty, ("environment-ok:" + directory).c_str());
    wait_exit(pty, code, signal);
    assert(code == 0 && signal == 0);
    assert(nk_pty_close(pty) == NK_OK);

    // Exit codes pass through, and all output is readable once exit is reported.
    const char *failing[] = {"/d", "/c", "echo final-output & exit 7"};
    assert(nk_pty_spawn(shell.c_str(), failing, 3, nullptr, nullptr, 0, 80, 24, &pty) == NK_OK);
    wait_exit(pty, code, signal);
    assert(code == 7 && signal == 0);
    std::string tail;
    for (;;) {
        const auto read = nk_pty_read(pty, buffer, sizeof(buffer), &count);
        if (read != NK_OK) {
            assert(read == NK_ERROR_NOT_FOUND);
            break;
        }
        tail.append(buffer, static_cast<std::size_t>(count));
    }
    assert(tail.find("final-output") != std::string::npos);
    // Killing a child that already exited keeps its status.
    assert(nk_pty_kill(pty) == NK_OK);
    assert(nk_pty_exit_status(pty, &code, &signal) == NK_OK && code == 7 && signal == 0);
    // Resizing a console that already closed is harmless.
    assert(nk_pty_resize(pty, 90, 30) == NK_OK);
    // Input to a closed console fails instead of being queued and dropped.
    nk_result written = NK_OK;
    const auto write_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((written = nk_pty_write(pty, "x", 1, &count)) == NK_OK &&
           std::chrono::steady_clock::now() < write_deadline)
        ::Sleep(10);
    assert(written == NK_ERROR_UNKNOWN && count == 0);
    assert(nk_pty_close(pty) == NK_OK);

    // Output backed up past the buffer limit holds back console teardown;
    // resize from the reading thread must not wait on it.
    const char *flood[] = {"/d", "/c",
                           "for /L %i in (1,1,40000) do @echo xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"};
    assert(nk_pty_spawn(shell.c_str(), flood, 3, nullptr, nullptr, 0, 80, 24, &pty) == NK_OK);
    const auto flood_end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < flood_end) {
        const auto resize_start = std::chrono::steady_clock::now();
        assert(nk_pty_resize(pty, 80, 24) == NK_OK);
        assert(std::chrono::steady_clock::now() - resize_start < std::chrono::seconds(1));
        ::Sleep(100);
    }
    const auto drain_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    nk_result drained = NK_OK;
    while ((drained = nk_pty_read(pty, buffer, sizeof(buffer), &count)) != NK_ERROR_NOT_FOUND &&
           std::chrono::steady_clock::now() < drain_deadline)
        if (drained == NK_PENDING)
            assert(nk_wait_events_timeout(0.05) == NK_OK);
    assert(drained == NK_ERROR_NOT_FOUND);
    wait_exit(pty, code, signal);
    assert(code == 0 && signal == 0);
    assert(nk_pty_resize(pty, 90, 30) == NK_OK);
    assert(nk_pty_close(pty) == NK_OK);

    // Closing a live child, including a grandchild in its job, does not wait for it.
    const char *long_command[] = {"/d", "/c", "ping -n 100 127.0.0.1 >nul"};
    assert(nk_pty_spawn(shell.c_str(), long_command, 3, nullptr, nullptr, 0, 80, 24, &pty) ==
           NK_OK);
    ::Sleep(300);
    const auto close_start = std::chrono::steady_clock::now();
    assert(nk_pty_close(pty) == NK_OK);
    assert(std::chrono::steady_clock::now() - close_start < std::chrono::seconds(2));
    nk_shutdown();
}
