#ifndef NATIVEKIT_PLUGIN_H
#define NATIVEKIT_PLUGIN_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Plugin ABI identifiers                                                    */
/* ------------------------------------------------------------------------- */

enum {
    /** Version of the plugin descriptor, host, and service ABI in this header. */
    NK_PLUGIN_ABI_VERSION = 1,
    /**
     * Largest inline payload accepted by a plugin call or emitted event.
     *
     * The plugin ABI is a bounded control plane, not a bulk data channel.
     * Operations that move high-bandwidth data return a NativeKit handle such
     * as nk_graphics_image instead of a payload of this or any larger size.
     */
    NK_PLUGIN_PAYLOAD_MAX = 65536,
    /** Result returned by an invoke callback that will complete later. */
    NK_PLUGIN_PENDING = NK_PENDING
};

/** Generation-checked identifier of one plugin instance owned by a runtime. */
typedef uint32_t nk_plugin_instance NK_HANDLE NK_HANDLE_DESTROY(nk_plugin_unregister);

/** Numeric identifier of a registered service; zero is never valid. */
typedef uint32_t nk_service_id;

/** Numeric identifier of a method or notification inside a service; zero is never valid. */
typedef uint32_t nk_method_id;

/** Flags describing the content of a plugin event produced by NativeKit. */
typedef uint32_t nk_plugin_event_flags;
enum NK_FLAGS(nk_plugin_event_flags) {
    /** The event carries a payload of one or more bytes. */
    NK_PLUGIN_EVENT_HAS_PAYLOAD = 1u << 0,
    /** The event carries a live NativeKit handle that the receiver now owns. */
    NK_PLUGIN_EVENT_HAS_HANDLE = 1u << 1
};

/** Flags describing how the bytes of a borrowed span are owned. */
typedef uint32_t nk_payload_flags;
enum NK_FLAGS(nk_payload_flags) {
    /** The bytes stay valid only until the object that returned the span is released. */
    NK_PAYLOAD_BORROWED = 1u << 0
};

/* ------------------------------------------------------------------------- */
/* Generic payloads                                                          */
/* ------------------------------------------------------------------------- */

/**
 * Borrowed, length-delimited byte span.
 *
 * NativeKit never interprets payload bytes; it copies or forwards them
 * unchanged between the application and plugins. NativeKit has no JSON, object,
 * or variant model, so generated bindings are free to lay typed, fixed-layout
 * records over the bytes. `data` is NULL exactly when `size` is zero, and for
 * every span NativeKit returns `flags` is NK_PAYLOAD_BORROWED and the bytes are
 * valid only until the owning nk_event is released.
 */
typedef struct nk_binary_span {
    /** Size of this structure in bytes. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Ownership flags for this span; always NK_PAYLOAD_BORROWED today. */
    uint32_t flags;
    /** Borrowed payload bytes, or NULL when `size` is zero. */
    const void *data NK_BORROWED_BUFFER(size);
    /** Payload size in bytes. */
    uint64_t size;
} nk_binary_span;

/** Frozen v1 prefix size; future fields must be appended after this boundary. */
#define NK_PLUGIN_BINARY_SPAN_V1_SIZE \
    ((uint32_t)(offsetof(nk_binary_span, size) + sizeof(((nk_binary_span *)0)->size)))

/* ------------------------------------------------------------------------- */
/* Plugin descriptors                                                        */
/* ------------------------------------------------------------------------- */

/** A service contract a plugin publishes to the runtime; defined below. */
typedef struct nk_plugin_service nk_plugin_service;

/**
 * Function table NativeKit passes to a plugin when it is created.
 *
 * The table is the versioned surface a plugin uses instead of linking directly
 * against every NativeKit symbol. It stays valid for the lifetime of the
 * runtime generation that created the plugin instance.
 */
typedef struct nk_plugin_host {
    /** Size of this structure in bytes. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Host ABI version, always NK_PLUGIN_ABI_VERSION. */
    uint32_t abi_version;
    /** Registers a service; equivalent to nk_plugin_service_register(). */
    nk_result(NK_CALL *register_service)(nk_plugin_instance instance,
                                         const nk_plugin_service *service);
    /** Removes a service; equivalent to nk_plugin_service_unregister(). */
    nk_result(NK_CALL *unregister_service)(nk_plugin_instance instance, nk_service_id service_id);
    /** Emits a notification; equivalent to nk_plugin_emit(). */
    nk_result(NK_CALL *emit_event)(nk_plugin_instance instance, nk_service_id service_id,
                                   nk_method_id method_id, const void *payload,
                                   uint64_t payload_size);
    /** Queues work on the application executor; equivalent to nk_dispatch_to_app(). */
    nk_result(NK_CALL *dispatch_to_app)(nk_task_fn fn, void *NK_NULLABLE user_data);
    /** Returns the executor of the calling thread; equivalent to nk_executor_current(). */
    nk_executor(NK_CALL *current_executor)(void);
    /** Returns the runtime generation; equivalent to nk_runtime_generation(). */
    uint64_t(NK_CALL *runtime_generation)(void);
    /** Completes a pending request; safe from an OS callback or worker thread. */
    nk_result(NK_CALL *complete_request)(nk_plugin_instance instance, nk_request_id request_id,
                                         nk_result result, nk_handle handle,
                                         const void *NK_NULLABLE payload, uint64_t payload_size);
} nk_plugin_host;

/** Frozen v1 prefix size; future fields must be appended after this boundary. */
#define NK_PLUGIN_HOST_V1_SIZE \
    ((uint32_t)(offsetof(nk_plugin_host, complete_request) + \
                sizeof(((nk_plugin_host *)0)->complete_request)))

/**
 * Called by NativeKit to instantiate a plugin.
 *
 * NativeKit passes the new, live instance handle by value. A plugin typically registers its
 * services and stores its own state with nk_plugin_set_state() here. When
 * create returns anything other than NK_OK the instance is dropped immediately,
 * no service stays registered, and destroy is not called, so create must clean
 * up any partial initialization itself.
 */
typedef nk_result(NK_CALL *nk_plugin_create_fn)(const nk_plugin_host *host,
                                                nk_plugin_instance instance);

/**
 * Called by NativeKit exactly once per successful create, on the application
 * executor, while the runtime generation is still alive. The instance has no
 * registered services at this point and its handle must not be reused.
 */
typedef void(NK_CALL *nk_plugin_destroy_fn)(nk_plugin_instance instance);

/**
 * Static description of a plugin. NativeKit copies the descriptor and its `id`
 * string, so the plugin may release its own storage after nk_plugin_register()
 * returns.
 */
typedef struct nk_plugin_descriptor {
    /** Size of this structure in bytes. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Plugin ABI version the plugin was built against; must be NK_PLUGIN_ABI_VERSION. */
    uint32_t abi_version;
    /** Stable reverse-DNS or package-style plugin identifier; never dispatched on. */
    const char *id NK_UTF8;
    /** Creates one instance; required. */
    nk_plugin_create_fn create;
    /** Destroys one instance; required. */
    nk_plugin_destroy_fn destroy;
} nk_plugin_descriptor;

/** Frozen v1 prefix size; future fields must be appended after this boundary. */
#define NK_PLUGIN_DESCRIPTOR_V1_SIZE \
    ((uint32_t)(offsetof(nk_plugin_descriptor, destroy) + \
                sizeof(((nk_plugin_descriptor *)0)->destroy)))

/* ------------------------------------------------------------------------- */
/* Plugin services                                                           */
/* ------------------------------------------------------------------------- */

/**
 * Optional response produced by a service invocation.
 *
 * NativeKit sets `struct_size` before invoking and copies the borrowed payload
 * bytes before the invocation returns, so the plugin keeps ownership of its own
 * buffer. Returned handles are transferred to the receiver, which becomes
 * responsible for releasing them; keep `handle` as NK_INVALID_HANDLE when the
 * operation returns no resource.
 */
typedef struct nk_plugin_reply {
    /** Size of this structure in bytes; set by NativeKit before the invocation. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Reply flags; must be zero today. */
    uint32_t flags;
    /** NativeKit handle transferred to the caller, or NK_INVALID_HANDLE. */
    nk_handle handle;
    /** Reserved for compatible extensions; must be zero. */
    uint32_t reserved;
    /** Borrowed reply bytes; NativeKit copies them before the invocation returns. */
    const void *payload NK_BORROWED_BUFFER(payload_size);
    /** Reply payload size in bytes; must not exceed NK_PLUGIN_PAYLOAD_MAX. */
    uint64_t payload_size;
} nk_plugin_reply;

/** Frozen v1 prefix size; future fields must be appended after this boundary. */
#define NK_PLUGIN_REPLY_V1_SIZE \
    ((uint32_t)(offsetof(nk_plugin_reply, payload_size) + \
                sizeof(((nk_plugin_reply *)0)->payload_size)))

/**
 * Handles one call on a registered service.
 *
 * The invocation runs on the executor declared by the service, never on the
 * caller's thread, and the borrowed request payload stays valid only for the
 * duration of the call. `request_id` is the identifier of the originating
 * nk_plugin_call() and is echoed in the completion event. Return
 * NK_PLUGIN_PENDING to retain the request for a later nk_plugin_complete() call;
 * every other result completes it immediately.
 */
typedef nk_result(NK_CALL *nk_plugin_invoke_fn)(nk_plugin_instance instance, nk_method_id method,
                                                const void *payload, uint64_t payload_size,
                                                nk_request_id request_id,
                                                nk_plugin_reply *NK_NULLABLE out_reply);

/**
 * Declares one service and the numeric methods it accepts.
 *
 * Service and method identifiers are small integers so dispatch never hashes
 * strings. `name` is diagnostics only. NativeKit copies this structure,
 * including the `name` string, so the plugin may release its own storage after
 * nk_plugin_service_register() returns.
 */
struct nk_plugin_service {
    /** Size of this structure in bytes. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Stable, instance-local service identifier; never zero. */
    nk_service_id service_id;
    /** Version of this service's method contract, chosen by the plugin. */
    uint32_t abi_version;
    /** Executor that runs invoke; only the aliased platform/app/render executors are supported. */
    nk_executor executor;
    /** Reserved for compatible extensions; must be zero. */
    uint32_t reserved;
    /** Optional UTF-8 diagnostics name; may be NULL. */
    const char *name NK_NULLABLE_UTF8;
    /** Handles calls routed to this service; required. */
    nk_plugin_invoke_fn invoke;
};

/** Frozen v1 prefix size; future fields must be appended after this boundary. */
#define NK_PLUGIN_SERVICE_V1_SIZE \
    ((uint32_t)(offsetof(nk_plugin_service, invoke) + sizeof(((nk_plugin_service *)0)->invoke)))

/* ------------------------------------------------------------------------- */
/* Plugin lifecycle                                                          */
/* ------------------------------------------------------------------------- */

/**
 * Registers a plugin with the active runtime generation.
 *
 * The descriptor is validated and copied, a generation-bound instance handle is
 * allocated, and create() is invoked immediately on the application executor.
 * The instance belongs to the runtime generation that created it and is always
 * destroyed by nk_plugin_unregister() or, at the latest, while nk_shutdown()
 * tears the runtime down.
 *
 * @param descriptor Non-null plugin description with create and destroy set.
 * @param out_instance Receives the new instance handle on NK_OK.
 * @return NK_OK, NK_ERROR_WRONG_THREAD off the application executor,
 *         NK_ERROR_INVALID_ARGUMENT for a malformed descriptor or a duplicate
 *         plugin id, NK_ERROR_UNSUPPORTED for an unknown abi_version, or
 *         NK_ERROR_NOT_INITIALIZED without an active runtime.
 */
NK_API nk_result NK_CALL nk_plugin_register(const nk_plugin_descriptor *descriptor,
                                            nk_plugin_instance *out_instance NK_OUT NK_OWNED);

/**
 * Destroys one plugin instance and removes every service it registered. Must be
 * called on the application executor. The handle is invalid afterwards, and
 * outstanding calls complete exactly once with NK_ERROR_INVALID_REQUEST before
 * the instance is destroyed.
 */
NK_API nk_result NK_CALL nk_plugin_unregister(nk_plugin_instance instance);

/* ------------------------------------------------------------------------- */
/* Service registration                                                      */
/* ------------------------------------------------------------------------- */

/**
 * Publishes one service on behalf of a plugin instance.
 *
 * Service identifiers are scoped to the owning plugin instance, so two plugin
 * instances may register the same numeric identifier independently.
 *
 * @param instance Live instance returned by nk_plugin_register().
 * @param service Non-null service description with a non-zero service_id.
 * @return NK_OK, NK_ERROR_WRONG_THREAD off the application executor,
 *         NK_ERROR_INVALID_HANDLE for a stale instance, NK_ERROR_INVALID_ARGUMENT
 *         for a malformed or duplicate service, or NK_ERROR_UNSUPPORTED for an
 *         executor that cannot host service invocations yet.
 */
NK_API nk_result NK_CALL nk_plugin_service_register(nk_plugin_instance instance,
                                                    const nk_plugin_service *service);

/**
 * Removes one service from the instance-local service table. Must be called on
 * the application executor by the instance that registered the service.
 */
NK_API nk_result NK_CALL nk_plugin_service_unregister(nk_plugin_instance instance,
                                                      nk_service_id service_id);

/* ------------------------------------------------------------------------- */
/* Plugin calls and events                                                   */
/* ------------------------------------------------------------------------- */

/**
 * Routes one call to a registered service.
 *
 * The call is asynchronous and never invokes plugin code on the calling thread,
 * so an arbitrary native thread may use it. The borrowed request payload is
 * copied before the call returns. Every successful call produces exactly one
 * NK_EVENT_PLUGIN_COMPLETE event carrying `request_id`, `service_id`, the
 * method identifier, the invocation result, and an optional borrowed payload or
 * transferred handle.
 *
 * @param instance Live plugin instance that owns the service.
 * @param service Non-zero service identifier scoped to `instance`.
 * @param method Non-zero method identifier inside the service.
 * @param payload Borrowed request bytes, or NULL when payload_size is zero.
 * @param payload_size Request size; may not exceed NK_PLUGIN_PAYLOAD_MAX.
 * @param out_request_id Receives the completion identifier on NK_OK.
 * @return NK_OK, NK_ERROR_INVALID_ARGUMENT for malformed arguments,
 *         NK_ERROR_PAYLOAD_TOO_LARGE above the control-plane limit,
 *         NK_ERROR_NOT_INITIALIZED without an active runtime, or an error when
 *         the routed task cannot be queued.
 */
NK_API nk_result NK_CALL nk_plugin_call(nk_plugin_instance instance, nk_service_id service,
                                        nk_method_id method,
                                        const void *NK_NULLABLE payload, uint64_t payload_size,
                                        nk_request_id *out_request_id NK_OUT);

/**
 * Completes one request previously returned as NK_PLUGIN_PENDING.
 *
 * This is safe from any native thread, including an OS callback. NativeKit
 * resolves the service and method from the request record, queues exactly one
 * completion event, and rejects duplicate, late, stale-generation, or
 * instance-mismatched completions with NK_ERROR_INVALID_REQUEST.
 */
NK_API nk_result NK_CALL nk_plugin_complete(nk_plugin_instance instance,
                                            nk_request_id request_id, nk_result result,
                                            nk_handle handle, const void *NK_NULLABLE payload,
                                            uint64_t payload_size);

/**
 * Emits an unsolicited notification from a plugin to the application.
 *
 * Safe to call from any native thread, including worker threads that never
 * poll events. The payload is copied and delivered as NK_EVENT_PLUGIN_EVENT with
 * `instance`, `service_id`, the notification identifier, and no request id.
 * Notifications are control traffic: high-bandwidth results belong in a handle
 * transferred by a completion event.
 *
 * @return NK_OK, NK_ERROR_INVALID_HANDLE for a stale instance,
 *         NK_ERROR_NOT_FOUND when service_id is not registered for that
 *         instance, NK_ERROR_PAYLOAD_TOO_LARGE above the control-plane limit,
 *         or NK_ERROR_NOT_INITIALIZED without an active runtime.
 */
NK_API nk_result NK_CALL nk_plugin_emit(nk_plugin_instance instance, nk_service_id service_id,
                                        nk_method_id method_id, const void *NK_NULLABLE payload,
                                        uint64_t payload_size);

/**
 * Attaches an opaque plugin-owned state pointer to an instance. Must be called
 * on the application executor. The pointer is never dereferenced or freed by
 * NativeKit and is cleared when the instance is unregistered.
 */
NK_API nk_result NK_CALL nk_plugin_set_state(nk_plugin_instance instance, void *NK_NULLABLE state);

/**
 * Returns the state pointer attached by nk_plugin_set_state(), or NULL when none
 * is set or the handle is invalid. Safe from a service invocation callback on
 * any declared executor; the pointer remains plugin-owned.
 */
NK_API void *NK_CALL nk_plugin_get_state(nk_plugin_instance instance);

/**
 * Returns the runtime generation the instance belongs to. An instance never
 * observes a different generation: rather, its handle becomes invalid when the
 * runtime that owns it shuts down.
 */
NK_API nk_result NK_CALL nk_plugin_instance_generation(nk_plugin_instance instance,
                                                       uint64_t *out_generation NK_OUT);

/* ------------------------------------------------------------------------- */
/* Plugin event decoding                                                     */
/* ------------------------------------------------------------------------- */

/**
 * Fixed header of every NK_EVENT_PLUGIN_COMPLETE and NK_EVENT_PLUGIN_EVENT
 * payload. The event's own nk_event fields carry the instance (`source`), the
 * request identifier, and the result; this header carries the numeric routing
 * identifiers plus the optional transferred handle, and is followed
 * immediately by `payload_size` payload bytes.
 */
typedef struct nk_plugin_event_data {
    /** Size of this header in bytes; the payload starts at this offset. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** NK_PLUGIN_EVENT_* flags describing the payload and handle. */
    uint32_t flags;
    /** Service that produced the event. */
    nk_service_id service_id;
    /** Method identifier of a completion, or notification identifier of an event. */
    nk_method_id method_id;
    /** NativeKit handle transferred to the receiver, or NK_INVALID_HANDLE. */
    nk_handle handle;
    /** Reserved for compatible extensions; always zero. */
    uint32_t reserved;
    /** Number of payload bytes that follow this header. */
    uint64_t payload_size;
} nk_plugin_event_data;

/** Frozen v1 prefix size; future fields must be appended after this boundary. */
#define NK_PLUGIN_EVENT_DATA_V1_SIZE \
    ((uint32_t)(offsetof(nk_plugin_event_data, payload_size) + \
                sizeof(((nk_plugin_event_data *)0)->payload_size)))

/**
 * Decoded view of a plugin event. All borrowed fields stay valid only until the
 * nk_event is released with nk_event_release().
 */
typedef struct nk_plugin_event_view {
    /** Size of this structure in bytes; must be set by the caller. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** NK_PLUGIN_EVENT_* flags reported by the event. */
    uint32_t flags;
    /** Plugin instance that produced the event. */
    nk_plugin_instance instance;
    /** Service that produced the event. */
    nk_service_id service_id;
    /** Method or notification identifier. */
    nk_method_id method_id;
    /** Reserved; always zero. */
    uint32_t reserved;
    /** NativeKit handle transferred to the receiver, or NK_INVALID_HANDLE. */
    nk_handle handle;
    /** Originating request, or NK_INVALID_REQUEST_ID for notifications. */
    nk_request_id request_id;
    /** Invocation result; always NK_OK for notifications. */
    nk_result result;
    /** Borrowed payload bytes, or NULL when empty. */
    const void *payload NK_BORROWED_BUFFER(payload_size);
    /** Borrowed payload size in bytes. */
    uint64_t payload_size;
} nk_plugin_event_view;

/** Frozen v1 prefix size; future fields must be appended after this boundary. */
#define NK_PLUGIN_EVENT_VIEW_V1_SIZE \
    ((uint32_t)(offsetof(nk_plugin_event_view, payload_size) + \
                sizeof(((nk_plugin_event_view *)0)->payload_size)))

/**
 * Decodes an NK_EVENT_PLUGIN_COMPLETE or NK_EVENT_PLUGIN_EVENT payload.
 *
 * @param event Event returned by nk_poll_event(); not released by this call.
 * @param out_view Zero-initialized view with struct_size set.
 * @return NK_OK, or NK_ERROR_INVALID_ARGUMENT when the event is not a
 *         well-formed plugin event.
 */
NK_API nk_result NK_CALL nk_plugin_event_query(const nk_event *event,
                                               nk_plugin_event_view *out_view NK_OUT);

#ifdef __cplusplus
}
#endif

#endif
