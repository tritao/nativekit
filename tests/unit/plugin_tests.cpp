#include "nativekit.h"
#include "nativekit_plugin.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace {

constexpr nk_service_id echo_service = 0x1001;
constexpr nk_service_id secondary_service = 0x1002;
constexpr nk_service_id unknown_service = 0x9999;
constexpr nk_method_id method_echo = 1;
constexpr nk_method_id method_fail = 2;
constexpr nk_method_id method_handle = 3;
constexpr nk_method_id method_pending = 4;
constexpr nk_method_id notification_ready = 10;
constexpr nk_handle returned_handle = static_cast<nk_handle>(0x0badf00du);

struct PluginState {
    std::atomic<int> invokes{0};
    nk_executor executor = NK_EXECUTOR_WORKER;
    nk_request_id request = NK_INVALID_REQUEST_ID;
    std::vector<std::byte> payload;
    std::vector<std::byte> reply;
};

std::atomic<int> creates{0};
std::atomic<int> destroys{0};
std::atomic<int> helper_runs{0};
std::thread::id app_thread;
nk_executor helper_executor = NK_EXECUTOR_WORKER;
std::thread::id helper_thread;
PluginState *live_state = nullptr;

nk_result NK_CALL plugin_invoke(nk_plugin_instance instance, nk_method_id method,
                                const void *payload, uint64_t payload_size,
                                nk_request_id request_id, nk_plugin_reply *out_reply) {
    auto *state = static_cast<PluginState *>(nk_plugin_get_state(instance));
    assert(state && state == live_state);
    state->invokes.fetch_add(1);
    state->executor = nk_executor_current();
    state->request = request_id;
    const auto *begin = static_cast<const std::byte *>(payload);
    state->payload.assign(begin, begin + payload_size);
    if (method == method_fail)
        return NK_ERROR_UNKNOWN;
    if (method == method_handle) {
        out_reply->handle = returned_handle;
        return NK_OK;
    }
    if (method == method_pending)
        return NK_PLUGIN_PENDING;
    /* Reply from a buffer the plugin owns and reuses: NativeKit must copy it. */
    state->reply = state->payload;
    std::reverse(state->reply.begin(), state->reply.end());
    out_reply->payload = state->reply.data();
    out_reply->payload_size = state->reply.size();
    return NK_OK;
}

nk_result NK_CALL plugin_create(const nk_plugin_host *host, nk_plugin_instance instance) {
    assert(host && instance != NK_INVALID_HANDLE);
    if (host->struct_size < NK_PLUGIN_HOST_V1_SIZE || host->abi_version != NK_PLUGIN_ABI_VERSION)
        return NK_ERROR_INVALID_ARGUMENT;
    if (host->runtime_generation() == 0 || host->current_executor() != NK_EXECUTOR_APP)
        return NK_ERROR_INVALID_REQUEST;

    auto *state = new PluginState();
    live_state = state;
    if (nk_plugin_set_state(instance, state) != NK_OK) {
        live_state = nullptr;
        delete state;
        return NK_ERROR_UNKNOWN;
    }

    nk_plugin_service service{};
    service.struct_size = NK_PLUGIN_SERVICE_V1_SIZE;
    service.service_id = echo_service;
    service.abi_version = 1;
    service.executor = NK_EXECUTOR_APP;
    service.name = "nativekit.test.echo";
    service.invoke = plugin_invoke;
    if (host->register_service(instance, &service) != NK_OK)
        return NK_ERROR_UNKNOWN;
    service.service_id = secondary_service;
    if (host->register_service(instance, &service) != NK_OK)
        return NK_ERROR_UNKNOWN;
    creates.fetch_add(1);
    return NK_OK;
}

void NK_CALL plugin_destroy(nk_plugin_instance instance) {
    destroys.fetch_add(1);
    assert(nk_executor_current() == NK_EXECUTOR_APP);
    auto *state = static_cast<PluginState *>(nk_plugin_get_state(instance));
    assert(state == live_state);
    live_state = nullptr;
    delete state;
}

int idle_plugins = 0;
int scoped_plugin_destroys = 0;

nk_result NK_CALL scoped_plugin_invoke(nk_plugin_instance, nk_method_id,
                                       const void *, uint64_t, nk_request_id,
                                       nk_plugin_reply *) {
    return NK_OK;
}

nk_result NK_CALL scoped_plugin_create(const nk_plugin_host *host, nk_plugin_instance instance) {
    assert(host && instance != NK_INVALID_HANDLE);
    nk_plugin_service service{};
    service.struct_size = NK_PLUGIN_SERVICE_V1_SIZE;
    service.service_id = echo_service;
    service.abi_version = 1;
    service.executor = NK_EXECUTOR_APP;
    service.invoke = scoped_plugin_invoke;
    return host->register_service(instance, &service);
}

void NK_CALL scoped_plugin_destroy(nk_plugin_instance) {
    ++scoped_plugin_destroys;
}

nk_result NK_CALL idle_create(const nk_plugin_host *, nk_plugin_instance) {
    idle_plugins++;
    return NK_OK;
}

void NK_CALL idle_destroy(nk_plugin_instance) {
    idle_plugins--;
}

void NK_CALL helper_task(void *user_data) {
    static_cast<std::atomic<int> *>(user_data)->fetch_add(1);
    helper_executor = nk_executor_current();
    helper_thread = std::this_thread::get_id();
}

nk_plugin_descriptor make_descriptor(const char *id, nk_plugin_create_fn create,
                                     nk_plugin_destroy_fn destroy) {
    nk_plugin_descriptor descriptor{};
    descriptor.struct_size = NK_PLUGIN_DESCRIPTOR_V1_SIZE;
    descriptor.abi_version = NK_PLUGIN_ABI_VERSION;
    descriptor.id = id;
    descriptor.create = create;
    descriptor.destroy = destroy;
    return descriptor;
}

nk_plugin_service make_service(nk_service_id service_id) {
    nk_plugin_service service{};
    service.struct_size = NK_PLUGIN_SERVICE_V1_SIZE;
    service.service_id = service_id;
    service.abi_version = 1;
    service.executor = NK_EXECUTOR_APP;
    service.name = "nativekit.test.service";
    service.invoke = plugin_invoke;
    return service;
}

bool poll_plugin_event(nk_event_kind kind, nk_event *out_event, int attempts = 64) {
    for (int attempt = 0; attempt < attempts; ++attempt) {
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == kind) {
            *out_event = event;
            return true;
        }
        nk_event_release(&event);
    }
    return false;
}

nk_plugin_event_view view_of(const nk_event &event) {
    nk_plugin_event_view view{};
    view.struct_size = sizeof(view);
    assert(nk_plugin_event_query(&event, &view) == NK_OK);
    return view;
}

nk_plugin_event_view complete(nk_request_id expected_request) {
    nk_event event{};
    assert(poll_plugin_event(NK_EVENT_PLUGIN_COMPLETE, &event));
    const nk_plugin_event_view view = view_of(event);
    assert(view.request_id == expected_request);
    nk_event_release(&event);
    return view;
}

} // namespace

int main() {
    nk_init_options options{};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    options.application_id = "dev.nativekit.plugin-tests";
    assert(nk_init(&options) == NK_OK);

    const uint64_t generation = nk_runtime_generation();
    assert(generation != 0);
    app_thread = std::this_thread::get_id();
    assert(nk_executor_current() == NK_EXECUTOR_APP);
    assert(nk_executor_is_current(NK_EXECUTOR_APP) == 1);
    assert(nk_executor_is_current(NK_EXECUTOR_WORKER) == 0);
    assert(nk_dispatch_to_app(nullptr, nullptr) == NK_ERROR_INVALID_ARGUMENT);

    nk_plugin_descriptor descriptor =
        make_descriptor("dev.nativekit.test", plugin_create, plugin_destroy);
    nk_plugin_instance instance = NK_INVALID_HANDLE;
    assert(nk_plugin_register(&descriptor, &instance) == NK_OK);
    assert(instance != NK_INVALID_HANDLE);
    assert(creates.load() == 1 && live_state != nullptr);

    /* The same numeric service id is valid on a different plugin instance. */
    nk_plugin_descriptor scoped_descriptor =
        make_descriptor("dev.nativekit.test.scoped", scoped_plugin_create,
                        scoped_plugin_destroy);
    nk_plugin_instance scoped_instance = NK_INVALID_HANDLE;
    assert(nk_plugin_register(&scoped_descriptor, &scoped_instance) == NK_OK);
    nk_plugin_service scoped_duplicate = make_service(echo_service);
    assert(nk_plugin_service_register(scoped_instance, &scoped_duplicate) ==
           NK_ERROR_INVALID_ARGUMENT);
    nk_request_id scoped_request = NK_INVALID_REQUEST_ID;
    assert(nk_plugin_call(scoped_instance, echo_service, method_echo, nullptr, 0,
                          &scoped_request) == NK_OK);
    const nk_plugin_event_view scoped_completion = complete(scoped_request);
    assert(scoped_completion.instance == scoped_instance);
    assert(scoped_completion.service_id == echo_service);
    assert(scoped_plugin_destroys == 0);
    assert(nk_plugin_unregister(scoped_instance) == NK_OK);
    assert(scoped_plugin_destroys == 1);

    uint64_t instance_generation = 0;
    assert(nk_plugin_instance_generation(instance, &instance_generation) == NK_OK);
    assert(instance_generation == generation);

    /* Plugin ids are runtime-wide; service ids are scoped to each instance. */
    nk_plugin_instance duplicate_instance = NK_INVALID_HANDLE;
    assert(nk_plugin_register(&descriptor, &duplicate_instance) == NK_ERROR_INVALID_ARGUMENT);
    assert(duplicate_instance == NK_INVALID_HANDLE);
    nk_plugin_service duplicate_service = make_service(echo_service);
    assert(nk_plugin_service_register(instance, &duplicate_service) == NK_ERROR_INVALID_ARGUMENT);
    nk_plugin_service worker_service = make_service(0x2001);
    worker_service.executor = NK_EXECUTOR_WORKER;
    assert(nk_plugin_service_register(instance, &worker_service) == NK_ERROR_UNSUPPORTED);
    nk_plugin_service nameless_service = make_service(0x2002);
    nameless_service.name = nullptr;
    assert(nk_plugin_service_register(instance, &nameless_service) == NK_OK);
    assert(nk_plugin_service_unregister(instance, 0x2002) == NK_OK);
    assert(nk_plugin_service_unregister(instance, 0x2002) == NK_ERROR_NOT_FOUND);

    /* Calls are routed asynchronously and never run on the caller's thread. */
    const std::uint32_t request_bytes[] = {0x11223344u, 0x55667788u, 0x99aabbccu};
    nk_request_id request = NK_INVALID_REQUEST_ID;
    assert(nk_plugin_call(instance, echo_service, method_echo, request_bytes, sizeof(request_bytes),
                          &request) == NK_OK);
    assert(request != NK_INVALID_REQUEST_ID);
    assert(live_state->invokes.load() == 0);

    /* The plugin replies with reversed bytes, so the payload is a real copy. */
    std::vector<std::byte> reversed(sizeof(request_bytes));
    const auto *bytes = reinterpret_cast<const std::byte *>(request_bytes);
    std::reverse_copy(bytes, bytes + sizeof(request_bytes), reversed.begin());

    const nk_plugin_event_view echo = complete(request);
    assert(echo.instance == instance);
    assert(echo.service_id == echo_service);
    assert(echo.method_id == method_echo);
    assert(echo.result == NK_OK);
    assert(echo.handle == NK_INVALID_HANDLE);
    assert((echo.flags & NK_PLUGIN_EVENT_HAS_PAYLOAD) != 0);
    assert((echo.flags & NK_PLUGIN_EVENT_HAS_HANDLE) == 0);
    assert(echo.payload_size == sizeof(request_bytes));
    assert(std::memcmp(echo.payload, reversed.data(), reversed.size()) == 0);
    assert(live_state->invokes.load() == 1);
    assert(live_state->executor == NK_EXECUTOR_APP);
    assert(live_state->request == request);
    assert(live_state->payload.size() == sizeof(request_bytes));
    assert(std::memcmp(live_state->payload.data(), request_bytes, sizeof(request_bytes)) == 0);

    /* Failures and transferred handles travel through the same completion. */
    request = NK_INVALID_REQUEST_ID;
    assert(nk_plugin_call(instance, echo_service, method_fail, nullptr, 0, &request) == NK_OK);
    const nk_plugin_event_view failure = complete(request);
    assert(failure.result == NK_ERROR_UNKNOWN);
    assert(failure.payload == nullptr && failure.payload_size == 0);
    assert(failure.flags == 0);

    request = NK_INVALID_REQUEST_ID;
    assert(nk_plugin_call(instance, echo_service, method_handle, nullptr, 0, &request) == NK_OK);
    const nk_plugin_event_view handle_result = complete(request);
    assert(handle_result.result == NK_OK);
    assert(handle_result.handle == returned_handle);
    assert((handle_result.flags & NK_PLUGIN_EVENT_HAS_HANDLE) != 0);
    assert(handle_result.payload_size == 0);

    /* Pending requests stay live until an OS/worker callback completes them. */
    request = NK_INVALID_REQUEST_ID;
    assert(nk_plugin_call(instance, echo_service, method_pending, nullptr, 0, &request) == NK_OK);
    nk_event pending_probe{};
    pending_probe.struct_size = sizeof(pending_probe);
    assert(nk_poll_event(&pending_probe) == NK_OK);
    assert(pending_probe.kind == NK_EVENT_NONE);
    nk_event_release(&pending_probe);
    const std::uint8_t deferred[] = {4, 5, 6};
    nk_result callback_result = NK_ERROR_UNKNOWN;
    std::thread callback([&] {
        callback_result = nk_plugin_complete(instance, request, NK_OK, NK_INVALID_HANDLE,
                                             deferred, sizeof(deferred));
    });
    callback.join();
    assert(callback_result == NK_OK);
    assert(nk_plugin_complete(instance, request, NK_OK, NK_INVALID_HANDLE, nullptr, 0) ==
           NK_ERROR_INVALID_REQUEST);
    nk_event deferred_event{};
    deferred_event.struct_size = sizeof(deferred_event);
    assert(nk_poll_event(&deferred_event) == NK_OK);
    assert(deferred_event.kind == NK_EVENT_PLUGIN_COMPLETE);
    const nk_plugin_event_view deferred_result = view_of(deferred_event);
    nk_event_release(&deferred_event);
    assert(deferred_result.method_id == method_pending);
    assert(deferred_result.payload_size == sizeof(deferred));
    assert(std::memcmp(deferred_result.payload, deferred, sizeof(deferred)) == 0);

    request = NK_INVALID_REQUEST_ID;
    assert(nk_plugin_call(instance, unknown_service, method_echo, nullptr, 0, &request) == NK_OK);
    const nk_plugin_event_view missing = complete(request);
    assert(missing.result == NK_ERROR_NOT_FOUND);
    assert(missing.instance == instance);
    assert(missing.service_id == unknown_service);

    /* The control plane is bounded; bulk data must travel as a handle. */
    std::vector<std::byte> oversize(NK_PLUGIN_PAYLOAD_MAX + 1);
    request = NK_INVALID_REQUEST_ID;
    assert(nk_plugin_call(instance, echo_service, method_echo, oversize.data(), oversize.size(), &request) ==
           NK_ERROR_PAYLOAD_TOO_LARGE);
    assert(request == NK_INVALID_REQUEST_ID);
    assert(nk_plugin_emit(instance, echo_service, notification_ready, oversize.data(),
                          oversize.size()) == NK_ERROR_PAYLOAD_TOO_LARGE);
    assert(nk_plugin_call(instance, 0, method_echo, nullptr, 0, &request) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_plugin_call(instance, echo_service, method_echo, nullptr, 0, nullptr) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_plugin_emit(instance, secondary_service, 0, nullptr, 0) == NK_ERROR_INVALID_ARGUMENT);

    /* Worker threads may enqueue work and notifications without polling. */
    const std::uint8_t notification[] = {7, 8, 9};
    const int invokes_before_worker = live_state->invokes.load();
    std::thread worker([&] {
        assert(nk_executor_current() == NK_EXECUTOR_WORKER);
        assert(nk_plugin_emit(instance, echo_service, notification_ready, notification,
                              sizeof(notification)) == NK_OK);
        assert(nk_dispatch_to_app(&helper_task, &helper_runs) == NK_OK);
        assert(nk_dispatch_to_app(&helper_task, &helper_runs) == NK_OK);
        assert(nk_plugin_call(instance, secondary_service, method_echo, notification, sizeof(notification),
                              &request) == NK_OK);
        assert(live_state->invokes.load() == invokes_before_worker);
    });
    worker.join();

    nk_event notification_event{};
    assert(poll_plugin_event(NK_EVENT_PLUGIN_EVENT, &notification_event));
    /* Queued tasks were drained on the application thread before polling returned. */
    assert(helper_runs.load() == 2);
    assert(helper_executor == NK_EXECUTOR_APP);
    assert(helper_thread == app_thread);

    const nk_plugin_event_view notification_view = view_of(notification_event);
    assert(notification_view.instance == instance);
    assert(notification_view.service_id == echo_service);
    assert(notification_view.method_id == notification_ready);
    assert(notification_view.request_id == NK_INVALID_REQUEST_ID);
    assert(notification_view.result == NK_OK);
    assert(notification_view.payload_size == sizeof(notification));
    assert(std::memcmp(notification_view.payload, notification, sizeof(notification)) == 0);
    nk_event_release(&notification_event);

    const nk_plugin_event_view routed = complete(request);
    assert(routed.result == NK_OK);
    assert(routed.service_id == secondary_service);
    assert(live_state->invokes.load() == invokes_before_worker + 1);
    assert(live_state->executor == NK_EXECUTOR_APP);

    nk_event empty{};
    empty.struct_size = sizeof(empty);
    nk_plugin_event_view invalid_view{};
    invalid_view.struct_size = sizeof(invalid_view);
    assert(nk_plugin_event_query(&empty, &invalid_view) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_plugin_event_query(nullptr, &invalid_view) == NK_ERROR_INVALID_ARGUMENT);

    nk_plugin_instance idle = NK_INVALID_HANDLE;
    nk_plugin_descriptor idle_descriptor =
        make_descriptor("dev.nativekit.test.idle", idle_create, idle_destroy);
    assert(nk_plugin_register(&idle_descriptor, &idle) == NK_OK);
    assert(idle_plugins == 1);

    /* Unregistering cancels queued and deferred requests exactly once. */
    nk_request_id canceled_request = NK_INVALID_REQUEST_ID;
    assert(nk_plugin_call(instance, echo_service, method_pending, nullptr, 0, &canceled_request) ==
           NK_OK);
    assert(nk_plugin_unregister(instance) == NK_OK);
    assert(destroys.load() == 1);
    const nk_plugin_event_view canceled = complete(canceled_request);
    assert(canceled.result == NK_ERROR_INVALID_REQUEST);
    assert(nk_plugin_get_state(instance) == nullptr);
    assert(nk_plugin_instance_generation(instance, &instance_generation) ==
           NK_ERROR_INVALID_HANDLE);
    assert(nk_plugin_unregister(instance) == NK_ERROR_INVALID_HANDLE);
    assert(nk_plugin_service_unregister(instance, echo_service) == NK_ERROR_INVALID_HANDLE);
    assert(nk_plugin_call(instance, echo_service, method_echo, nullptr, 0, &request) ==
           NK_ERROR_INVALID_HANDLE);

    /* Queued work is discarded at shutdown, after every plugin is destroyed. */
    nk_shutdown();
    assert(destroys.load() == 1 && idle_plugins == 0);
    assert(nk_runtime_generation() == 0);
    assert(nk_executor_is_current(NK_EXECUTOR_WORKER) == 0);
    assert(nk_plugin_register(&idle_descriptor, &idle) == NK_ERROR_NOT_INITIALIZED);
    return 0;
}
