#include "nativekit_pty.h"
#include "nativekit_time.h"

#include <cassert>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
std::string collect(nk_pty pty, const char *needle) {
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
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
} // namespace

int main() {
    nk_init_options options{};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    options.event_queue_capacity = 128;
    assert(nk_init(&options) == NK_OK);

    const char *args[] = {"-i"};
    nk_pty pty = NK_INVALID_HANDLE;
    assert(nk_pty_spawn("/bin/sh", args, 1, "/tmp", nullptr, 0, 80, 24, &pty) == NK_OK);
    send(pty, "printf 'ready\\n'; pwd; stty size\n");
    const auto first = collect(pty, "24 80");
    assert(first.find("ready") != std::string::npos);
    assert(first.find("/tmp") != std::string::npos);

    assert(nk_pty_resize(pty, 100, 40) == NK_OK);
    send(pty, "stty size\n");
    collect(pty, "40 100");

    // A large producer must not make one UI read block or return unbounded data.
    send(pty, "yes x | head -c 1048576\n");
    char buffer[131072];
    uint64_t count = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto result = nk_pty_read(pty, buffer, sizeof(buffer), &count);
    assert(result == NK_OK || result == NK_PENDING);
    assert(count <= 65536);
    assert(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100));

    assert(nk_pty_kill(pty) == NK_OK);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    int32_t code = 0, signal = 0;
    while (nk_pty_exit_status(pty, &code, &signal) == NK_PENDING &&
           std::chrono::steady_clock::now() < deadline)
        assert(nk_wait_events_timeout(0.05) == NK_OK);
    assert(signal == SIGKILL);
    assert(nk_pty_close(pty) == NK_OK);
    assert(nk_pty_read(pty, buffer, sizeof(buffer), &count) == NK_ERROR_INVALID_HANDLE);

    const char *command[] = {"-c", "printf '%s:%s\\n' \"$PT_MARKER\" \"$(pwd)\""};
    const char *environment[] = {"PT_MARKER=environment-ok", "PATH=/usr/bin:/bin"};
    assert(nk_pty_spawn("/bin/sh", command, 2, "/tmp", environment, 2,
                        80, 24, &pty) == NK_OK);
    collect(pty, "environment-ok:/tmp");
    while (nk_pty_exit_status(pty, &code, &signal) == NK_PENDING &&
           std::chrono::steady_clock::now() < deadline + std::chrono::seconds(5))
        assert(nk_wait_events_timeout(0.05) == NK_OK);
    assert(code == 0 && signal == 0);
    assert(nk_pty_close(pty) == NK_OK);

    // Deliberately inheritable host descriptors must not reach the PTY child.
    const int sentinel = ::open("/dev/null", O_RDONLY);
    assert(sentinel >= 3);
    assert(::fcntl(sentinel, F_SETFD, 0) == 0);
    const auto probe = "if (: <&" + std::to_string(sentinel) + ") 2>/dev/null; then printf leaked; else printf isolated; fi";
    const char *isolation[] = {"-c", probe.c_str()};
    assert(nk_pty_spawn("/bin/sh", isolation, 2, nullptr, nullptr, 0, 80, 24, &pty) == NK_OK);
    collect(pty, "isolated");
    assert(::fcntl(sentinel, F_GETFD) >= 0); // Parent ownership is unchanged.
    assert(nk_pty_close(pty) == NK_OK);
    ::close(sentinel);

    // Closing a live child reaps it.
    assert(nk_pty_spawn("/bin/sh", args, 1, nullptr, nullptr, 0, 80, 24, &pty) == NK_OK);
    assert(nk_pty_close(pty) == NK_OK);
    const char *long_command[] = {"-c", "sleep 100 & wait"};
    assert(nk_pty_spawn("/bin/sh", long_command, 2, nullptr, nullptr, 0,
                        80, 24, &pty) == NK_OK);
    const auto close_start = std::chrono::steady_clock::now();
    assert(nk_pty_close(pty) == NK_OK);
    assert(std::chrono::steady_clock::now() - close_start < std::chrono::seconds(2));
    nk_shutdown();
}
