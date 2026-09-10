#include "nativekit.h"
#include "nativekit_graphics.h"

#include <jni.h>

#include <cmath>

namespace {
nk_handle surface = NK_INVALID_HANDLE;
bool ready = false;
using ClearColor = void (*)(float, float, float, float);
using Clear = void (*)(unsigned int);
ClearColor clear_color = nullptr;
Clear clear = nullptr;

void consume_surface_events() {
    nk_event event{};
    event.struct_size = sizeof(event);
    while (nk_poll_event(&event) == NK_OK && event.kind != NK_EVENT_NONE) {
        if (event.source == surface && event.kind == NK_EVENT_SURFACE_READY)
            ready = true;
        else if (event.source == surface && event.kind == NK_EVENT_SURFACE_LOST)
            ready = false;
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
}
} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_io_nativekit_graphics_MainActivity_nativeCreateSurface(JNIEnv *, jclass, jlong host,
                                                              jint width, jint height) {
    nk_surface_options options{};
    options.struct_size = sizeof(options);
    options.api = NK_GRAPHICS_OPENGL_ES;
    options.major_version = 2;
    options.flags = NK_SURFACE_DEPTH;
    options.width = width;
    options.height = height;
    ready = false;
    clear_color = nullptr;
    clear = nullptr;
    return nk_surface_create(static_cast<nk_handle>(host), &options, &surface) == NK_OK
               ? static_cast<jlong>(surface)
               : 0;
}

extern "C" JNIEXPORT void JNICALL
Java_io_nativekit_graphics_MainActivity_nativeResizeSurface(JNIEnv *, jclass, jlong handle,
                                                              jint width, jint height) {
    if (handle)
        nk_surface_set_bounds(static_cast<nk_handle>(handle), 0, 0, width, height);
}

extern "C" JNIEXPORT void JNICALL
Java_io_nativekit_graphics_MainActivity_nativeRenderFrame(JNIEnv *, jclass, jlong handle,
                                                           jlong frame_time_nanos) {
    if (!handle)
        return;
    consume_surface_events();
    if (!ready || nk_surface_make_current(static_cast<nk_handle>(handle)) != NK_OK)
        return;
    if (!clear_color || !clear) {
        nk_graphics_proc proc = nullptr;
        if (nk_surface_get_proc_address(static_cast<nk_handle>(handle), "glClearColor", &proc) !=
            NK_OK)
            return;
        clear_color = reinterpret_cast<ClearColor>(proc);
        if (nk_surface_get_proc_address(static_cast<nk_handle>(handle), "glClear", &proc) != NK_OK)
            return;
        clear = reinterpret_cast<Clear>(proc);
    }
    const float seconds = static_cast<float>(frame_time_nanos) / 1000000000.0f;
    clear_color(0.25f + 0.2f * std::sin(seconds),
                0.35f + 0.2f * std::sin(seconds + 2.0f),
                0.55f + 0.2f * std::sin(seconds + 4.0f), 1.0f);
    clear(0x00004000u | 0x00000100u);
    nk_surface_present(static_cast<nk_handle>(handle));
}

extern "C" JNIEXPORT void JNICALL
Java_io_nativekit_graphics_MainActivity_nativeDestroySurface(JNIEnv *, jclass, jlong handle) {
    if (handle)
        nk_surface_destroy(static_cast<nk_handle>(handle));
    surface = NK_INVALID_HANDLE;
    ready = false;
    clear_color = nullptr;
    clear = nullptr;
}
